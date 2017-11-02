/*
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2011-2012 Jürgen Urban
 * Copyright (C) 2017      Fredrik Noring <noring@nocrew.org>
 *
 * This source code is licensed under the GNU General Public License,
 * see the file COPYING for more details.
 */

#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/freezer.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/kd.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/vt_buffer.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/mach-ps2/gsfunc.h>
#include <asm/mach-ps2/ps2.h>

#define DEVICE_NAME "ps2fb"

/** Bigger 1 bit color images will be devided into smaller images with the following maximum width. */
#define PATTERN_MAX_X 16
/** Bigger 1 bit color images will be devided into smaller images with the following maximum height. */
#define PATTERN_MAX_Y 8

/** Alignment for width in Bytes. */
#define PS2_FBMEM_ALIGN 8

/** Number of colors in palette. */
#define PAL_COLORS 256

/** Usable memory in GS. */
#define MAXVIDEOMEMSIZE (4 * 1024 * 1024)

/* ps2fb module parameters. */
#define VESA "VESA"
#define DTV "dtv"
#define NTSC "NTSC"
#define PAL "pal"
#define VIDEOMEMEMORYSIZE "videomemsize="
#define DEFAULTVIDEOMEMSIZE (2 * 1024 * 1024)

#if 0
#define DPRINTK(args...) printk(args)
#else
#define DPRINTK(args...)
#endif

static int ps2fb_init(void);

struct ps2fb_priv {
	int is_blanked;
	int is_kicked;
	int mapped;
	struct task_struct *task;
};
static struct ps2fb_priv ps2fb;

struct ps2fb_par
{
	u32 pseudo_palette[PAL_COLORS];
	u32 opencnt;
	struct ps2_screeninfo screeninfo;
	int redraw_xres;
	int redraw_yres;
};

static char *mode_option;
static int crtmode = -1;
static int videomemsize = DEFAULTVIDEOMEMSIZE;

#ifdef CONFIG_T10000
static int defaultmode = PS2_GS_VESA, defaultres = PS2_GS_640x480;
static int default_w = 640, default_h = 480;
#else
static int defaultmode = PS2_GS_NTSC, defaultres = PS2_GS_NOINTERLACE;
static int default_w = 640, default_h = 448;
#endif

#ifdef CONFIG_PS2_CONSOLE_LARGEBUF
#define BUF_SIZE	(4096 * 8)
#else
#define BUF_SIZE	1024
#endif
static unsigned char buf1[BUF_SIZE] __attribute__ ((aligned(16)));

static unsigned char buf2[BUF_SIZE] __attribute__ ((aligned(16)));

struct kdma_buffer;

struct kdma_request {
	struct dma_request r;
	void *next;
	struct kdma_buffer *kdb;
	unsigned long qwc;
} __attribute__ ((aligned(DMA_TRUNIT)));

struct kdma_buffer {
	struct dma_channel *channel;
	void *start, *end;
	void *top, *volatile bottom;
	int size, allocmax;
	struct semaphore sem;
	spinlock_t lock;
	struct dma_completion c;
	struct kdma_request *kreq;
	int error;
};

static void kdma_send_start(struct dma_request *req, struct dma_channel *ch)
{
	struct kdma_request *kreq = (struct kdma_request *) req;

	int count = DMA_POLLING_TIMEOUT;

	/*
	 * If PATH3 is active and no data exists in GIF FIFO,
	 * previous GS packet may not be terminated.
	 */
	while ((GIFREG(PS2_GIFREG_STAT) & 0x1f000c00) == 0x00000c00) {
		if (--count <= 0) {
			SET_GIFREG(PS2_GIFREG_CTRL, 1);	/* reset GIF */
			printk("ps2dma: GS packet is not terminated\n");
			break;
		}
	}

	WRITEDMAREG(ch, PS2_Dn_MADR,
		virt_to_bus((void *) kreq + sizeof(struct kdma_request)));
	WRITEDMAREG(ch, PS2_Dn_QWC, kreq->qwc);
	WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_SENDN);
}

static void kdma_free(struct dma_request *req, struct dma_channel *ch)
{
	struct kdma_request *kreq = (struct kdma_request *) req;

	unsigned long flags;

	spin_lock_irqsave(&kreq->kdb->lock, flags);
	kreq->kdb->bottom = kreq->next;
	spin_unlock_irqrestore(&kreq->kdb->lock, flags);
	ps2dma_complete(&kreq->kdb->c);
}

static struct dma_ops kdma_send_ops =
	{ kdma_send_start, NULL, NULL, kdma_free };

static void ps2kdma_init(struct kdma_buffer *kdb, int ch, void *buf, int len)
{
	len = kdb->size = len & ~(DMA_TRUNIT - 1);
	kdb->channel = &ps2dma_channels[ch];
	kdb->start = buf;
	kdb->end = buf + len;
	kdb->top = kdb->bottom = NULL;
	kdb->allocmax = DMA_ALIGN(len - (len >> 2));
	sema_init(&kdb->sem, 1);
	spin_lock_init(&kdb->lock);
	ps2dma_init_completion(&kdb->c);
	kdb->error = 0;
}

static void *ps2kdma_alloc(struct kdma_buffer *kdb, int min, int max, int *size)
{
	unsigned long flags;

	int free, amin;

	int poll;

	/* polling wait is used when
	 *  - called from interrupt handler
	 *  - interrupt is already disabled (in printk())
	 */
	poll = in_interrupt() || !irqs_disabled();

	if (down_trylock(&kdb->sem) != 0) {
		if (poll)
			return NULL;		/* cannot sleep */
		else
			down(&kdb->sem);
	}

	amin = DMA_ALIGN(min) + sizeof(struct kdma_request);
	if (amin > kdb->size) {
		up(&kdb->sem);
		return NULL;			/* requested size is too large */
	}

	spin_lock_irqsave(&kdb->lock, flags);

	while (1) {
		if (kdb->top == kdb->bottom) {	/* whole buffer is free */
			kdb->top = kdb->bottom = kdb->start;
			free = kdb->size - DMA_TRUNIT;
			break;
		}
		if (kdb->top > kdb->bottom) {	/* [...#####...] */
			free = kdb->end - kdb->top;
			if (amin <= free)
				break;
			if (kdb->bottom > kdb->start) {
				kdb->top = kdb->start;	/* wrap around */
				continue;
			}
		} else if (kdb->top < kdb->bottom) {	/* [###.....###] */
			free = kdb->bottom - kdb->top - DMA_TRUNIT;
			if (amin <= free)
				break;
		}

		spin_unlock_irqrestore(&kdb->lock, flags);
		kdb->error |=
			ps2dma_intr_safe_wait_for_completion(kdb->channel, poll, &kdb->c);
		spin_lock_irqsave(&kdb->lock, flags);
	}

	if (amin < kdb->allocmax && free > kdb->allocmax)
		free = kdb->allocmax;
	free -= sizeof(struct kdma_request);
	if (size)
		*size = free > max ? max : free;
	kdb->kreq = (struct kdma_request *) kdb->top;
	spin_unlock_irqrestore(&kdb->lock, flags);

	return (void *) kdb->kreq + sizeof(struct kdma_request);
}

static void ps2kdma_send(struct kdma_buffer *kdb, int len, int flushall)
{
	unsigned long flags;

	int alen;

	struct kdma_request *kreq = kdb->kreq;

	spin_lock_irqsave(&kdb->lock, flags);
	alen = sizeof(struct kdma_request) + DMA_ALIGN(len);
	kdb->top = (void *) kreq + alen;
	spin_unlock_irqrestore(&kdb->lock, flags);

	up(&kdb->sem);

	init_dma_request(&kreq->r, &kdma_send_ops);
	kreq->next = (void *) kreq + alen;
	kreq->kdb = kdb;
	kreq->qwc = len >> 4;

	ps2dma_add_queue((struct dma_request *) kreq, kdb->channel, flushall);

	if (kdb->error) {
		kdb->error = 0;
		printk("ps2dma: %s timeout\n", kdb->channel->device);
	}
}

static struct kdma_buffer kdb, kkdb;

void ps2con_gsp_init(void)
{
	ps2kdma_init(&kdb, DMA_GIF, buf1, BUF_SIZE);
	ps2kdma_init(&kkdb, DMA_GIF, buf2, BUF_SIZE);
}

u64 *ps2con_gsp_alloc(int request, int *avail)
{
	return ps2kdma_alloc(in_interrupt()? &kkdb : &kdb, request, BUF_SIZE,
		avail);
}

void ps2con_gsp_send(int len, int flushall)
{
	struct kdma_buffer *buf;

	buf = in_interrupt()? &kkdb : &kdb;
	if (!flushall) {
		dma_cache_wback((unsigned long) buf->kreq + sizeof(struct kdma_request),
			len);
	}
	ps2kdma_send(buf, len, flushall);
}

void ps2con_initinfo(struct ps2_screeninfo *info)
{
	info->fbp = 0;
	info->psm = PS2_GS_PSMCT32;
	info->mode = defaultmode;
	info->res = defaultres;
	info->w = default_w;
	info->h = default_h;

	if (info->w * info->h > 1024 * 1024)
		info->psm = PS2_GS_PSMCT16;
}

const static struct {
	int w, h;
} reslist[4][4] = {
	{
		{640, 480},
		{800, 600},
		{1024, 768},
		{1280, 1024},
	}, {
		{720, 480},
		{1920, 1080},
		{1280, 720},
		{-1, -1},
	},
	{
		{640, 224},
		{640, 448},
		{-1, -1},
		{-1, -1},
	},
	{
		{640, 240},
		{640, 480},
		{-1, -1},
		{-1, -1},
	},
};

static int __init crtmode_setup(char *options)
{
	int maxres;

	int rrate, w, h;

	if (!options || !*options)
		return 0;

	if (strncasecmp(options, "vesa", 4) == 0) {
		options += 4;
		defaultmode = PS2_GS_VESA;
		maxres = 4;
	} else if (strncasecmp(options, "dtv", 3) == 0) {
		options += 3;
		defaultmode = PS2_GS_DTV;
		maxres = 3;
	} else if (strncasecmp(options, "ntsc", 4) == 0) {
		options += 4;
		defaultmode = PS2_GS_NTSC;
		maxres = 2;
	} else if (strncasecmp(options, "pal", 3) == 0) {
		options += 3;
		defaultmode = PS2_GS_PAL;
		maxres = 2;
	} else
		return 0;

	defaultres = 0;
	if (*options >= '0' && *options <= '9') {
		defaultres = *options - '0';
		options++;
		if (defaultres >= maxres)
			defaultres = 0;
	}

	if (defaultmode == PS2_GS_VESA && *options == ',') {
		rrate = simple_strtoul(options + 1, &options, 0);
		if (rrate == 60)
			defaultres |= PS2_GS_60Hz;
		else if (rrate == 75)
			defaultres |= PS2_GS_75Hz;
	}

	w = default_w = reslist[defaultmode][defaultres & 0xff].w;
	h = default_h = reslist[defaultmode][defaultres & 0xff].h;

	if (*options == ':') {
		w = simple_strtoul(options + 1, &options, 0);
		if (*options == ',' || tolower(*options) == 'x') {
			h = simple_strtoul(options + 1, &options, 0);
		}
		if (w > 0)
			default_w = w;
		if (h > 0)
			default_h = h;
	}

	return 1;
}

/* Convert resolution to ps2 mode format. */
int ps2con_get_resolution(int mode, int w, int h, int rate)
{
	int res;

	for (res = 0; res < 4; res++) {
		if (reslist[mode][res & 0xff].w == w) {
			if (reslist[mode][res & 0xff].h == h) {
				if (mode == PS2_GS_VESA) {
					if (rate >= 75) {
						res |= PS2_GS_75Hz;
					} else if (rate >= 60) {
						res |= PS2_GS_60Hz;
					}
				}
				return res;
			}
		}
	}
	return -1;
}

__setup("crtmode=", crtmode_setup);

#define param_get_crtmode NULL
static int param_set_crtmode(const char *val, struct kernel_param *kp)
{
	if (strncasecmp(val, VESA, sizeof(VESA) - 1) == 0) {
		crtmode = PS2_GS_VESA;
	} else if (strncasecmp(val, DTV, sizeof(DTV) - 1) == 0) {
		crtmode = PS2_GS_DTV;
	} else if (strncasecmp(val, NTSC, sizeof(NTSC) - 1) == 0) {
		crtmode = PS2_GS_NTSC;
	} else if (strncasecmp(val, PAL, sizeof(PAL) - 1) == 0) {
		crtmode = PS2_GS_PAL;
	}

	/* TBD: Add code to support old parameter style. */

	return 0;
}

#define param_check_crtmode(name, p) __param_check(name, p, void)

module_param_named(crtmode, crtmode, int, 0);
MODULE_PARM_DESC(crtmode,
	"Crtmode mode, set to '" VESA "', '" DTV "', '" NTSC "' or '" PAL "'");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option,
	"Specify initial video mode as \"<xres>x<yres>[-<bpp>][@<refresh>]\"");
module_param(videomemsize, int, 0);
MODULE_PARM_DESC(videomemsize,
	"Maximum memory for frame buffer mmap");

/* TBD: Calculate correct timing values. */
static const struct fb_videomode pal_modes[] = {
	{
		/* 640x240 @ 50 Hz, 15.625 kHz hsync (PAL RGB) */
		NULL, 50, 640, 240, 74074, 64, 16, 39, 5, 64, 5,
		0, FB_VMODE_NONINTERLACED
	},
	{
		/* 640x480i @ 50 Hz, 15.625 kHz hsync (PAL RGB) */
		NULL, 50, 640, 480, 74074, 64, 16, 39, 5, 64, 5,
		0, FB_VMODE_INTERLACED
	},
};

/* TBD: Calculate correct timing values. */
static const struct fb_videomode ntsc_modes[] = {
	{
		/* 640x224 @ 60 Hz, 15.625 kHz hsync (NTSC RGB) */
		NULL, 60, 640, 224, 74074, 64, 16, 39, 5, 64, 5,
		0, FB_VMODE_NONINTERLACED
	},
	{
		/* 640x448i @ 60 Hz, 15.625 kHz hsync (NTSC RGB) */
		NULL, 60, 640, 448, 74074, 64, 16, 39, 5, 64, 5,
		0, FB_VMODE_INTERLACED
	},
};

/* TBD: Calculate correct timing values. */
static const struct fb_videomode dtv_modes[] = {
	{
		/* 720x480p @ 60 Hz, 15.625 kHz hsync (DTV RGB) */
		NULL, 60, 720, 480, 74074, 64, 16, 39, 5, 64, 5,
		0, FB_VMODE_NONINTERLACED
	},
	{
		/* 1280x720p @ 60 Hz, 15.625 kHz hsync (DTV RGB) */
		NULL, 60, 1280, 720, 74074, 64, 16, 39, 5, 64, 5,
		0, FB_VMODE_NONINTERLACED
	},
	{
		/* 1920x1080i @ 30 Hz, 15.625 kHz hsync (DTV RGB) */
		NULL, 30, 1920, 1080, 74074, 64, 16, 39, 5, 64, 5,
		0, FB_VMODE_INTERLACED
	},
};

static void *rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

static void rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(mem);
}

u32 colto32(struct fb_var_screeninfo *var, u32 col)
{
	u32 rv;
	u32 r;
	u32 g;
	u32 b;
	u32 t;

	r = (col >> var->red.offset) & (0xFFFFFFFF >> (32 - var->red.length));
	r <<= 8 - var->red.length;
	g = (col >> var->green.offset) & (0xFFFFFFFF >> (32 - var->green.length));
	g <<= 8 - var->green.length;
	b = (col >> var->blue.offset) & (0xFFFFFFFF >> (32 - var->blue.length));
	b <<= 8 - var->blue.length;
	t = (col >> var->transp.offset) & (0xFFFFFFFF >> (32 - var->transp.length));
	t <<= 8 - var->transp.length;

	rv = (r << 0) | (g << 8) | (b << 16) | (t << 24);

	return rv;
}

/**
 *	ps2fb_open - Optional function. Called when the framebuffer is
 *		     first accessed.
 *	@info: frame buffer structure that represents a single frame buffer
 *	@user: tell us if the userland (value=1) or the console is accessing
 *	       the framebuffer.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int ps2fb_open(struct fb_info *info, int user)
{
    struct ps2fb_par *par = info->par;

	DPRINTK("ps2fb_open: user %d\n", user);

	if (user) {
		par->opencnt++;
	}
    return 0;
}

/**
 *	ps2fb_release - Optional function. Called when the framebuffer
 *			device is closed.
 *	@info: frame buffer structure that represents a single frame buffer
 *	@user: tell us if the userland (value=1) or the console is accessing
 *	       the framebuffer.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int ps2fb_release(struct fb_info *info, int user)
{
    struct ps2fb_par *par = info->par;

	DPRINTK("ps2fb_release: user %d\n", user);

	if (user) {
		par->opencnt--;
		if (par->opencnt == 0) {
			/* Redrawing shouldn't be needed after closing by application. */
			ps2fb.mapped = 0;
		}
	}
    return 0;
}

/**
 * Paints a filled rectangle.
 *
 * Coordinate system:
 * 0-----> x++
 * |
 * |
 * |
 * \/
 * y++
 * @color Color format ABGR
 *
 */
static void ps2_paintrect(int sx, int sy, int width, int height, uint32_t color)
{
    u64 *gsp;
    int ctx = 0;

    if ((gsp = ps2con_gsp_alloc(ALIGN16(6 * 8), NULL)) == NULL) {
		return;
	}

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(1, 1, 0, 0, PS2_GIFTAG_FLG_REGLIST, 4);
    *gsp++ = 0x5510;
	/* PRIM */
    *gsp++ = 0x006 + (ctx << 9);
	/* RGBAQ */
    *gsp++ = color;
	/* XYZ2 */
    *gsp++ = PACK32(sx * 16, sy * 16);
	/* XYZ2 */
    *gsp++ = PACK32((sx + width) * 16, (sy + height) * 16);

    ps2con_gsp_send(ALIGN16(6 * 8), 0);
}

/* Convert 1bpp to 32bpp */
static void *ps2_addpattern1_32(void *gsp, const unsigned char *data, int width, int height, uint32_t bgcolor, uint32_t fgcolor, int lineoffset)
{
	int y;
	int x;
	int offset;
    u32 *p32;

	offset = 0;
	p32 = (u32 *) gsp;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			u32 v;

			v = data[(offset + x) / 8];
			v >>= 7 - ((offset + x) & 7);
			v &= 1;
			if (v) {
				*p32++ = fgcolor;
			} else {
				*p32++ = bgcolor;
			}
		}
		offset += lineoffset;
	}
	return (void *)p32;
}

/* Convert 1bpp to 16bpp */
static void *ps2_addpattern1_16(void *gsp, const unsigned char *data, int width, int height, uint16_t bgcolor, uint16_t fgcolor, int lineoffset)
{
	int y;
	int x;
	int offset;
    u16 *p16;

	offset = 0;
	p16 = (u16 *) gsp;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			u32 v;

			v = data[(offset + x) / 8];
			v >>= 7 - ((offset + x) & 7);
			v &= 1;
			if (v) {
				*p16++ = fgcolor;
			} else {
				*p16++ = bgcolor;
			}
		}
		offset += lineoffset;
	}
	return (void *)p16;
}

/* Paint image from data with 1 bit per pixel in 32bpp framebuffer. */
static void ps2_paintsimg1_32(struct ps2_screeninfo *info, int sx, int sy, int width, int height, uint32_t bgcolor, uint32_t fgcolor, const unsigned char *data, int lineoffset)
{
    u64 *gsp;
    void *gsp_h;
    int gspsz; /* Available DMA packet size. */
	int fbw = (info->w + 63) / 64;
	unsigned int packetlen;

	if ((gsp = ps2con_gsp_alloc(ALIGN16(12 * 8 + sizeof(fgcolor) * width * height), &gspsz)) == NULL) {
		DPRINTK("Failed ps2con_gsp_alloc with w %d h %d size %lu\n", width, height, ALIGN16(12 * 8 + sizeof(fgcolor) * width * height));
	    return;
	}
	gsp_h = gsp;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	*gsp++ = 0x0e;		/* A+D */
	*gsp++ = (u64)0 | ((u64)info->fbp << 32) |
	    ((u64)fbw << 48) | ((u64)info->psm << 56);
	*gsp++ = PS2_GS_BITBLTBUF;
	*gsp++ = PACK64(0, PACK32(sx, sy));
	*gsp++ = PS2_GS_TRXPOS;
	*gsp++ = PACK64(width, height);
	*gsp++ = PS2_GS_TRXREG;
	*gsp++ = 0;		/* host to local */
	*gsp++ = PS2_GS_TRXDIR;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16(sizeof(fgcolor) * width * height) / 16, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	*gsp++ = 0;

	gsp = ps2_addpattern1_32(gsp, data, width, height, bgcolor, fgcolor, lineoffset);
	packetlen = ((void *) ALIGN16(gsp)) - gsp_h;
	ps2con_gsp_send(packetlen, 0);
}

/* Paint image from data with 1 bit per pixel in 16bpp framebuffer. */
static void ps2_paintsimg1_16(struct ps2_screeninfo *info, int sx, int sy, int width, int height, uint16_t bgcolor, uint16_t fgcolor, const unsigned char *data, int lineoffset)
{
    u64 *gsp;
    void *gsp_h;
    int gspsz; /* Available DMA packet size. */
	int fbw = (info->w + 63) / 64;
	unsigned int packetlen;

	if ((gsp = ps2con_gsp_alloc(ALIGN16(12 * 8 + sizeof(fgcolor) * width * height), &gspsz)) == NULL) {
		DPRINTK("Failed ps2con_gsp_alloc with w %d h %d size %lu\n", width, height, ALIGN16(12 * 8 + sizeof(fgcolor) * width * height));
	    return;
	}
	gsp_h = gsp;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	*gsp++ = 0x0e;		/* A+D */
	*gsp++ = ((u64)0) | ((u64)info->fbp << 32) |
	    ((u64)fbw << 48) | ((u64)info->psm << 56);
	*gsp++ = PS2_GS_BITBLTBUF;
	*gsp++ = PACK64(0, PACK32(sx, sy));
	*gsp++ = PS2_GS_TRXPOS;
	*gsp++ = PACK64(width, height);
	*gsp++ = PS2_GS_TRXREG;
	*gsp++ = 0;		/* host to local */
	*gsp++ = PS2_GS_TRXDIR;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16(sizeof(fgcolor) * width * height) / 16, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	*gsp++ = 0;

	gsp = ps2_addpattern1_16(gsp, data, width, height, bgcolor, fgcolor, lineoffset);
	packetlen = ((void *) ALIGN16(gsp)) - gsp_h;
	ps2con_gsp_send(packetlen, 0);
}

/* Paint image from data with 8 bit per pixel. */
static void *ps2_addpattern8_32(void *gsp, const unsigned char *data, int width, int height, uint32_t *palette, int lineoffset)
{
	int y;
	int x;
	int offset;
    u32 *p32;

	offset = 0;
	p32 = (u32 *) gsp;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			u32 v;

			v = data[offset + x];
			*p32++ = palette[v];
		}
		offset += lineoffset;
	}
	return (void *)p32;
}

/* Paint image from data with 8 bit per pixel. */
static void *ps2_addpattern8_16(void *gsp, const unsigned char *data, int width, int height, uint32_t *palette, int lineoffset)
{
	int y;
	int x;
	int offset;
    u16 *p16;

	offset = 0;
	p16 = (u16 *) gsp;
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			u32 v;

			v = data[offset + x];
			*p16++ = palette[v];
		}
		offset += lineoffset;
	}
	return (void *)p16;
}

/* Paint 8bpp image in 32bpp screen buffer. */
static void ps2_paintsimg8_32(struct ps2_screeninfo *info, int sx, int sy, int width, int height, uint32_t *palette, const unsigned char *data, int lineoffset)
{
    u64 *gsp;
    void *gsp_h;
    int gspsz; /* Available size. */
	int fbw = (info->w + 63) / 64;
	unsigned int packetlen;

	if ((gsp = ps2con_gsp_alloc(ALIGN16(12 * 8 + sizeof(palette[0]) * width * height), &gspsz)) == NULL) {
		DPRINTK("Failed ps2con_gsp_alloc with w %d h %d size %lu\n", width, height, ALIGN16(12 * 8 + sizeof(palette[0]) * width * height));
	    return;
	}
	gsp_h = gsp;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	/* A+D */
	*gsp++ = 0x0e;
	*gsp++ = (u64)0 | ((u64)info->fbp << 32) |
	    ((u64)fbw << 48) | ((u64)info->psm << 56);
	*gsp++ = PS2_GS_BITBLTBUF;
	*gsp++ = PACK64(0, PACK32(sx, sy));
	*gsp++ = PS2_GS_TRXPOS;
	*gsp++ = PACK64(width, height);
	*gsp++ = PS2_GS_TRXREG;
	/* host to local */
	*gsp++ = 0;
	*gsp++ = PS2_GS_TRXDIR;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16(sizeof(palette[0]) * width * height) / 16, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	*gsp++ = 0;

	gsp = ps2_addpattern8_32(gsp, data, width, height, palette, lineoffset);
	packetlen = ((void *) ALIGN16(gsp)) - gsp_h;
	ps2con_gsp_send(packetlen, 0);
}

/* Paint 8bpp image in 16bpp screen buffer. */
static void ps2_paintsimg8_16(struct ps2_screeninfo *info, int sx, int sy, int width, int height, uint32_t *palette, const unsigned char *data, int lineoffset)
{
    u64 *gsp;
    void *gsp_h;
    int gspsz; /* Available size. */
	int fbw = (info->w + 63) / 64;
	unsigned int packetlen;

	if ((gsp = ps2con_gsp_alloc(ALIGN16(12 * 8 + sizeof(palette[0]) * width * height), &gspsz)) == NULL) {
		DPRINTK("Failed ps2con_gsp_alloc with w %d h %d size %lu\n", width, height, ALIGN16(12 * 8 + sizeof(palette[0]) * width * height));
	    return;
	}
	gsp_h = gsp;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	/* A+D */
	*gsp++ = 0x0e;
	*gsp++ = (u64)0 | ((u64)info->fbp << 32) |
	    ((u64)fbw << 48) | ((u64)info->psm << 56);
	*gsp++ = PS2_GS_BITBLTBUF;
	*gsp++ = PACK64(0, PACK32(sx, sy));
	*gsp++ = PS2_GS_TRXPOS;
	*gsp++ = PACK64(width, height);
	*gsp++ = PS2_GS_TRXREG;
	/* host to local */
	*gsp++ = 0;
	*gsp++ = PS2_GS_TRXDIR;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16((sizeof(palette[0]) / 2) * width * height) / 16, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	*gsp++ = 0;

	gsp = ps2_addpattern8_16(gsp, data, width, height, palette, lineoffset);
	packetlen = ((void *) ALIGN16(gsp)) - gsp_h;
	ps2con_gsp_send(packetlen, 0);
}

void ps2fb_dma_send(const void *data, unsigned long len)
{
	unsigned long page;
	unsigned long start;
	unsigned long end;
	unsigned long s;
	unsigned long offset;
	void *cur_start;
	unsigned long cur_size;

	/* TBD: Cache is flushed by ps2con_gsp_send, but this may change. */

	start = (unsigned long) data;
	cur_start = 0;
	cur_size = 0;

	s = start & (PAGE_SIZE - 1);
	s = PAGE_SIZE - s;
	if (s < PAGE_SIZE) {
		if (s > len) {
			s = len;
		}
		offset = start & (PAGE_SIZE - 1);
		/* vmalloc_to_pfn is only working with redraw handler. */
		cur_start = phys_to_virt(PFN_PHYS(vmalloc_to_pfn((void *) start))) + offset;
		cur_size = ALIGN16(s);
		start += s;
		len -= s;
	}

	end = ALIGN16(start + len);

	for (page = start; page < end; page += PAGE_SIZE) {
		void *addr;
		unsigned long size;

		addr = phys_to_virt(PFN_PHYS(vmalloc_to_pfn((void *) page)));
		size = end - page;
		if (size > PAGE_SIZE) {
			size = PAGE_SIZE;
		}

		if (cur_size > 0) {
			if (addr == (cur_start + cur_size)) {
				/* contigous physical memory. */
				cur_size += size;
			} else {
				/* Start DMA transfer for current data. */
				ps2sdma_send(DMA_GIF, cur_start, ALIGN16(cur_size));

				cur_size = size;
				cur_start = addr;
			}
		} else {
			cur_start = addr;
			cur_size = size;
		}
	}
	if (cur_size > 0) {
		/* Start DMA transfer for current data. */
		ps2sdma_send(DMA_GIF, cur_start, ALIGN16(cur_size));

		cur_size = 0;
		cur_start = 0;
	}
}

/**
 *		Copy data to framebuffer on GS side.
 *
 *		@param info PS2 screeninfo (16 bit and 32 bit PSM is supported)
 *		@param sx Destination X coordinate in framebuffer
 *		@param sy Destination Y coordinate in framebuffer
 *		@param width Width of frame
 *		@param height Height of frame
 *		@param data Source pointer which was allocated with rvmalloc()
 */
static void ps2fb_copyframe(struct ps2_screeninfo *info, int sx, int sy, int width, int height, const uint32_t *data)
{
    u64 *gsp;
    void *gsp_h;
    int gspsz; /* Available size. */
	int fbw = (info->w + 63) / 64;
	int bpp;

	if ((gsp = ps2con_gsp_alloc(ALIGN16(12 * 8), &gspsz)) == NULL) {
		DPRINTK("Failed ps2con_gsp_alloc\n");
	    return;
	}
	/* Calculate number of bytes per pixel. */
	switch (info->psm) {
	case PS2_GS_PSMCT32:
	case PS2_GS_PSMZ32:
	case PS2_GS_PSMCT24:
	case PS2_GS_PSMZ24:
		bpp = 4;
		break;

	case PS2_GS_PSMCT16:
	case PS2_GS_PSMCT16S:
	case PS2_GS_PSMZ16:
	case PS2_GS_PSMZ16S:
		bpp = 2;
		break;

	case PS2_GS_PSMT8:
	case PS2_GS_PSMT8H:
		bpp = 1;
		break;

	default:
		bpp = 1;
		break;
	}
	gsp_h = gsp;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	/* A+D */
	*gsp++ = 0x0e;
	*gsp++ = (u64)0 | ((u64)info->fbp << 32) |
	    ((u64)fbw << 48) | ((u64)info->psm << 56);
	*gsp++ = PS2_GS_BITBLTBUF;
	*gsp++ = PACK64(0, PACK32(sx, sy));
	*gsp++ = PS2_GS_TRXPOS;
	*gsp++ = PACK64(width, height);
	*gsp++ = PS2_GS_TRXREG;
	/* host to local */
	*gsp++ = 0;
	*gsp++ = PS2_GS_TRXDIR;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16(bpp * width * height) / 16, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	*gsp++ = 0;
	ps2con_gsp_send(((unsigned long) gsp) - ((unsigned long) gsp_h), 1);

	ps2fb_dma_send(data, ALIGN16(bpp * width * height));
}

static void ps2fb_redraw(struct fb_info *info)
{
	int offset;
	int y;
	int maxheight;
    struct ps2fb_par *par = info->par;

	switch (par->redraw_xres) {
		case 640:
			maxheight = 64;
			break;
		case 720:
			maxheight = 56;
			break;
		case 800:
			maxheight = 50;
			break;
		case 1024:
			maxheight = 40;
			break;
		case 1280:
			maxheight = 32;
			break;
		case 1920:
			maxheight = 20;
			break;
		default:
			maxheight = 20;
			break;
	}

	offset = ((info->var.bits_per_pixel/8) * par->redraw_xres + PS2_FBMEM_ALIGN - 1) & ~(PS2_FBMEM_ALIGN - 1);
	for (y = 0; y < par->redraw_yres; y += maxheight) {
		int h;

		h = par->redraw_yres - y;
		if (h > maxheight) {
			h = maxheight;
		}
		ps2fb_copyframe(&par->screeninfo, 0, y, par->screeninfo.w, h, (void *) (info->fix.smem_start + y * offset));
	}
}

/**
 *      ps2fb_check_var - Optional function. Validates a var passed in.
 *      @var: frame buffer variable screen structure
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *	Checks to see if the hardware supports the state requested by
 *	var passed in. This function does not alter the hardware state!!!
 *	This means the data stored in struct fb_info and struct ps2fb_par do
 *      not change. This includes the var inside of struct fb_info.
 *	Do NOT change these. This function can be called on its own if we
 *	intent to only test a mode and not actually set it. The stuff in
 *	modedb.c is a example of this. If the var passed in is slightly
 *	off by what the hardware can support then we alter the var PASSED in
 *	to what we can do.
 *
 *      For values that are off, this function must round them _up_ to the
 *      next value that is supported by the hardware.  If the value is
 *      greater than the highest value supported by the hardware, then this
 *      function must return -EINVAL.
 *
 *      Exception to the above rule:  Some drivers have a fixed mode, ie,
 *      the hardware is already set at boot up, and cannot be changed.  In
 *      this case, it is more acceptable that this function just return
 *      a copy of the currently working var (info->var). Better is to not
 *      implement this function, as the upper layer will do the copying
 *      of the current var for you.
 *
 *      Note:  This is the only function where the contents of var can be
 *      freely adjusted after the driver has been registered. If you find
 *      that you have code outside of this function that alters the content
 *      of var, then you are doing something wrong.  Note also that the
 *      contents of info->var must be left untouched at all times after
 *      driver registration.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int ps2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	int res;

	if (var->bits_per_pixel <= 16) {
		var->bits_per_pixel = 16;
	} else if (var->bits_per_pixel <= 32) {
		var->bits_per_pixel = 32;
	} else {
		printk("ps2fb: %d bits per pixel are not supported.\n", var->bits_per_pixel);
		return -EINVAL;
	}
	if ((var->bits_per_pixel/8 * var->xres * var->yres) > MAXVIDEOMEMSIZE) {
		if (var->bits_per_pixel > 16) {
			printk("ps2fb: %d bits per pixel are not supported at %dx%d.\n",
				var->bits_per_pixel, var->xres, var->yres);
			var->bits_per_pixel = 16;
		}
	}
	if (var->bits_per_pixel == 32) {
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 16;
		var->blue.length = 8;
		var->transp.offset = 24; /* TBD: Check, seems to be disabled. Not needed? */
		var->transp.length = 8;
	} else if (var->bits_per_pixel == 16) {
		var->red.offset = 0;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 10;
		var->blue.length = 5;
		var->transp.offset = 15; /* TBD: Check, seems to be disabled. Not needed? */
		var->transp.length = 1;
	} else {
		/* TBD: Add support for 8bpp, because directfb doesn't support BGR, but 8bpp. */
		/* TBD: Simulate 8bpp, convert on GS side. */
		printk("ps2fb: %d bits per pixel are not supported.\n", var->bits_per_pixel);
		return -EINVAL;
	}

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;

	if (var->xres_virtual != var->xres) {
		/* Resolution is not supported. */
		printk("ps2fb: xres_virtual %d not support with xres %d\n", var->xres_virtual, var->xres);
		var->xres_virtual = var->xres;
	}

	if (var->yres_virtual != var->yres) {
		/* Support second screen. */
		printk("ps2fb: yres_virtual %d not support with yres %d\n", var->yres_virtual, var->yres);
		var->yres_virtual = var->yres;
	}

	if (var->xoffset != 0) {
		/* Panning not supported. */
		printk("ps2fb: xoffset %d is not supported\n", var->xoffset);
		var->xoffset = 0;
	}

	if (var->yoffset != 0) {
		/* TBD: Panning not supported. */
		printk("ps2fb: yoffset %d is not supported\n", var->yoffset);
		var->yoffset = 0;
	}

	if (var->nonstd > 3) {
		printk("ps2fb: unsupported crtmode %d (nonstd)\n", var->nonstd);
		return -EINVAL;
	}

	/* TBD: Should use crtmode set by set crtmode application via /dev/ps2gs */
	res = ps2con_get_resolution(var->nonstd, var->xres, var->yres, 60 /* TBD: calculate rate. */);
	if (res < 0) {
		/* Resolution is not supported in this crtmode. */
		printk("ps2fb: %dx%d is not supported in crtmode %d\n", var->xres, var->yres, var->nonstd);
		return -EINVAL;
	}

	if (var->rotate) {
		/* No support for rotating. */
		printk("ps2fb: rotate is not supported.\n");
		return -EINVAL;
	}

	/* TBD: check more parameters? */

    return 0;
}

static void ps2fb_switch_mode(struct fb_info *info)
{
    struct ps2fb_par *par = info->par;
	int maxredrawline;

	DPRINTK("ps2fb: %dx%d\n", info->var.xres, info->var.yres);

	/* TDB:	Lock redraw task */

	/* Activate screen mode. */
	par->screeninfo.psm = PS2_GS_PSMCT32;
	par->screeninfo.mode = info->var.nonstd;
	par->screeninfo.w = info->var.xres;
	par->redraw_xres = info->var.xres;
	par->screeninfo.h = info->var.yres;
	par->redraw_yres = info->var.yres;
	par->screeninfo.res = ps2con_get_resolution(par->screeninfo.mode,
		info->var.xres, info->var.yres, 60 /* TBD: calculate rate. */);
	if (info->var.bits_per_pixel == 16) {
		par->screeninfo.psm = PS2_GS_PSMCT16;
	}

	DPRINTK("ps2fb: %d %s() mode %dx%d %dbpp crtmode %d res %d psm %d\n", __LINE__, __FUNCTION__, info->var.xres, info->var.yres, info->var.bits_per_pixel, par->screeninfo.mode, par->screeninfo.res, par->screeninfo.psm);
    ps2gs_screeninfo(&par->screeninfo, NULL);

	/* Clear screen (black). */
	ps2_paintrect(0, 0, info->var.xres, info->var.yres, 0x80000000);

	info->fix.line_length = (info->var.bits_per_pixel/8 * info->var.xres + PS2_FBMEM_ALIGN - 1) & ~(PS2_FBMEM_ALIGN - 1);
	maxredrawline = videomemsize / info->fix.line_length;
	if (maxredrawline < par->redraw_yres) {
		/* Reserved framebuffer memory is too small. */
		par->redraw_yres = maxredrawline;
	}
	DPRINTK("ps2fb: smem_start 0x%08lx\n", info->fix.smem_start);
	DPRINTK("ps2fb: smem_len 0x%08lx\n", info->fix.smem_len);
	DPRINTK("ps2fb: line_length 0x%08lx\n", info->fix.line_length);

	if (ps2fb.mapped != 0) {
		/* Make screen black when mapped the first time. */
		memset((void *) info->fix.smem_start, 0, info->fix.smem_len);

		/* TBD: Copy current frame buffer to memory. */
	}

	/* TDB:	Unlock redraw task */
}

/**
 *      ps2fb_set_par - Optional function. Alters the hardware state.
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *	Using the fb_var_screeninfo in fb_info we set the resolution of the
 *	this particular framebuffer. This function alters the par AND the
 *	fb_fix_screeninfo stored in fb_info. It doesn't not alter var in
 *	fb_info since we are using that data. This means we depend on the
 *	data in var inside fb_info to be supported by the hardware.
 *
 *      This function is also used to recover/restore the hardware to a
 *      known working state.
 *
 *	ps2fb_check_var is always called before ps2fb_set_par to ensure that
 *      the contents of var is always valid.
 *
 *	Again if you can't change the resolution you don't need this function.
 *
 *      However, even if your hardware does not support mode changing,
 *      a set_par might be needed to at least initialize the hardware to
 *      a known working state, especially if it came back from another
 *      process that also modifies the same hardware, such as X.
 *
 *      If this is the case, a combination such as the following should work:
 *
 *      static int ps2fb_check_var(struct fb_var_screeninfo *var,
 *                                struct fb_info *info)
 *      {
 *              *var = info->var;
 *              return 0;
 *      }
 *
 *      static int ps2fb_set_par(struct fb_info *info)
 *      {
 *              init your hardware here
 *      }
 *
 *	Returns negative errno on error, or zero on success.
 */
static int ps2fb_set_par(struct fb_info *info)
{
	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);

	ps2fb_switch_mode(info);

    return 0;
}

/**
 *  	ps2fb_setcolreg - Optional function. Sets a color register.
 *      @regno: Which register in the CLUT we are programming
 *      @red: The red value which can be up to 16 bits wide
 *      @green: The green value which can be up to 16 bits wide
 *      @blue:  The blue value which can be up to 16 bits wide.
 *      @transp: If supported, the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 *
 *  	Set a single color register. The values supplied have a 16 bit
 *  	magnitude which needs to be scaled in this function for the hardware.
 *	Things to take into consideration are how many color registers, if
 *	any, are supported with the current color visual. With truecolor mode
 *	no color palettes are supported. Here a pseudo palette is created
 *	which we store the value in pseudo_palette in struct fb_info. For
 *	pseudocolor mode we have a limited color palette. To deal with this
 *	we can program what color is displayed for a particular pixel value.
 *	DirectColor is similar in that we can program each color field. If
 *	we have a static colormap we don't need to implement this function.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int ps2fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct fb_var_screeninfo *var;
	u32 *reg;

    if (regno >= PAL_COLORS)  /* no. of hw registers */
       return -EINVAL;

	var = &info->var;
	reg = info->pseudo_palette;

	red >>= 16 - var->red.length;
	green >>= 16 - var->green.length;
	blue >>= 16 - var->blue.length;
	transp >>= 16 - var->transp.length;

	reg[regno] =
		red << var->red.offset |
		green << var->green.offset |
		blue << var->blue.offset |
		transp << var->transp.offset;

	DPRINTK("ps2fb: %d %s() reg %d = 0x%08x r 0x%02x g 0x%02x b 0x%02x\n", __LINE__, __FUNCTION__, regno, ((u32 *) (info->pseudo_palette))[regno], red, green, blue);
    return 0;
}

#if 0 /* TBD: Implement functions. */
ssize_t ps2fb_read(struct fb_info *info, char __user *buf,
			   size_t count, loff_t *ppos)
{
	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);
	/* TBD: implement reading of framebuffer. */
	return count;
}

ssize_t ps2fb_write(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);
	/* TBD: implement writing of framebuffer. */
	return count;
}
#endif

/**
 *      ps2fb_blank - NOT a required function. Blanks the display.
 *      @blank_mode: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *      Blank the screen if blank_mode != FB_BLANK_UNBLANK, else unblank.
 *      Return 0 if blanking succeeded, != 0 if un-/blanking failed due to
 *      e.g. a video mode which doesn't support it.
 *
 *      Implements VESA suspend and powerdown modes on hardware that supports
 *      disabling hsync/vsync:
 *
 *      FB_BLANK_NORMAL = display is blanked, syncs are on.
 *      FB_BLANK_HSYNC_SUSPEND = hsync off
 *      FB_BLANK_VSYNC_SUSPEND = vsync off
 *      FB_BLANK_POWERDOWN =  hsync and vsync off
 *
 *      If implementing this function, at least support FB_BLANK_UNBLANK.
 *      Return !0 for any modes that are unimplemented.
 *
 */
static int ps2fb_blank(int blank_mode, struct fb_info *info)
{
	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);

	switch (blank_mode) {
	case FB_BLANK_POWERDOWN:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_NORMAL:
		ps2gs_blank(1);
		ps2fb.is_blanked = 1;
		break;

	default:
		ps2gs_blank(0);
		ps2fb.is_blanked = 0;
		break;
	}
	return 0;
}

#if 0 /* TBD: Implement functions. */
/**
 *	ps2fb_sync - NOT a required function. Normally the accel engine
 *		     for a graphics card take a specific amount of time.
 *		     Often we have to wait for the accelerator to finish
 *		     its operation before we can write to the framebuffer
 *		     so we can have consistent display output.
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *      If the driver has implemented its own hardware-based drawing function,
 *      implementing this function is highly recommended.
 */
int ps2fb_sync(struct fb_info *info)
{
	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);
	return 0;
}
#endif

/**
 *      ps2fb_fillrect - REQUIRED function. Can use generic routines if
 *		 	 non acclerated hardware and packed pixel based.
 *			 Draws a rectangle on the screen.
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *    @region: The structure representing the rectangular region we
 *		 wish to draw to.
 *
 *	This drawing operation places/removes a retangle on the screen
 *	depending on the rastering operation with the value of color which
 *	is in the current color depth format.
 */
void ps2fb_fillrect(struct fb_info *p, const struct fb_fillrect *region)
{
	uint32_t color;

	if (region->rop != ROP_COPY) {
		printk("ps2fb: %d %s() dx %d dy %d w %d h %d col 0x%08x unsupported rop\n", __LINE__, __FUNCTION__,
			region->dx,
			region->dy,
			region->width,
			region->height,
			region->color);
		/* TBD: handle region->rop ROP_COPY or ROP_XOR */
		return;
	}

	if (p->fix.visual == FB_VISUAL_TRUECOLOR ||
	    p->fix.visual == FB_VISUAL_DIRECTCOLOR ) {
		color = ((u32*)(p->pseudo_palette))[region->color];
	} else {
		color = region->color;
	}
	ps2_paintrect(region->dx, region->dy,
		region->width, region->height, colto32(&p->var, color));
}

/**
 *      ps2fb_copyarea - REQUIRED function.
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *      @area: Structure providing the data to copy the framebuffer contents
 *	       from one region to another.
 *
 *      This drawing operation copies a rectangular area from one area of the
 *		screen to another area.
 */
void ps2fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct ps2fb_par *par = info->par;
	u64 *gsp;
	int fbw = (par->screeninfo.w + 63) / 64;

	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);

	if ((gsp = ps2con_gsp_alloc(ALIGN16(10 * 8), NULL)) == NULL) {
		printk("ps2fb: ps2con_gsp_alloc() failed in ps2con_bmove().\n");
		return;
	}

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 1, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	/* A+D */
	*gsp++ = 0x0e;
	*gsp++ = (u64)par->screeninfo.fbp |
	((u64)fbw << 16) | ((u64)par->screeninfo.psm << 24) |
	((u64)par->screeninfo.fbp << 32) |
	((u64)fbw << 48) | ((u64)par->screeninfo.psm << 56);
	*gsp++ = PS2_GS_BITBLTBUF;

	if (area->sy > area->dy || (area->sy == area->dy && area->sx > area->dx)) {
		/* copy region LT -> RB */
		*gsp++ = PACK64(PACK32(area->sx, area->sy),
		PACK32(area->dx, area->dy) + (0 << 27));
	} else {
		/* copy region RB -> LT */
		*gsp++ = PACK64(PACK32(area->sx, area->sy),
			PACK32(area->dx, area->dy) + (3 << 27));
	}

	*gsp++ = PS2_GS_TRXPOS;
	*gsp++ = PACK64(area->width, area->height);
	*gsp++ = PS2_GS_TRXREG;
	/* local to local */
	*gsp++ = 2;
	*gsp++ = PS2_GS_TRXDIR;

	ps2con_gsp_send(ALIGN16(10 * 8), 0);
}

/**
 *      ps2fb_imageblit - REQUIRED function.
 *
 *		@info: frame buffer structure that represents a single frame buffer
 *		@image:	structure defining the image.
 *
 *      This drawing operation draws a image on the screen. It can be a
 *		mono image (needed for font handling) or a color image (needed for
 *		tux).
 */
void ps2fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
    struct ps2fb_par *par = info->par;

	if (image->depth == 1) {
		int x;
		int y;
		int offset;
		uint32_t fgcolor;
		uint32_t bgcolor;

		fgcolor = ((u32*)(info->pseudo_palette))[image->fg_color];
		bgcolor = ((u32*)(info->pseudo_palette))[image->bg_color];
		offset = (image->width + 7) & ~7;
		for (x = 0; x < image->width; x += PATTERN_MAX_X)
		{
			int w;

			w = image->width - x;
			if (w > PATTERN_MAX_X) {
				w = PATTERN_MAX_X;
			}
			for (y = 0; y < image->height; y += PATTERN_MAX_Y)
			{
				int h;

				h = image->height - y;
				if (h > PATTERN_MAX_Y) {
					h = PATTERN_MAX_Y;
				}
				if (par->screeninfo.psm == PS2_GS_PSMCT32) {
					ps2_paintsimg1_32(&par->screeninfo, image->dx + x, image->dy + y, w, h,
						bgcolor, fgcolor, image->data + (x + y * offset) / 8,
						offset);
				} else if (par->screeninfo.psm == PS2_GS_PSMCT16) {
					ps2_paintsimg1_16(&par->screeninfo, image->dx + x, image->dy + y, w, h,
						bgcolor, fgcolor, image->data + (x + y * offset) / 8,
						offset);
				} else {
					printk("ps2fb: PSM %d is not supported.\n", par->screeninfo.psm);
				}
			}
		}
	} else if (image->depth == 8) {
		int x;
		int y;
		int offset;

		offset = image->width;
		for (x = 0; x < image->width; x += PATTERN_MAX_X)
		{
			int w;

			w = image->width - x;
			if (w > PATTERN_MAX_X) {
				w = PATTERN_MAX_X;
			}
			for (y = 0; y < image->height; y += PATTERN_MAX_Y)
			{
				int h;

				h = image->height - y;
				if (h > PATTERN_MAX_Y) {
					h = PATTERN_MAX_Y;
				}
				if (par->screeninfo.psm == PS2_GS_PSMCT32) {
					ps2_paintsimg8_32(&par->screeninfo, image->dx + x, image->dy + y, w, h,
						info->pseudo_palette, image->data + x + y * offset,
						offset);
				} else if (par->screeninfo.psm == PS2_GS_PSMCT16) {
					ps2_paintsimg8_16(&par->screeninfo, image->dx + x, image->dy + y, w, h,
						info->pseudo_palette, image->data + x + y * offset,
						offset);
				} else {
					printk("ps2fb: PSM %d is not supported.\n", par->screeninfo.psm);
				}
			}
		}
	} else {
		printk("ps2fb: blit depth %d dx %d dy %d w %d h %d 0x%08x\n",
		image->depth,
		image->dx,
		image->dy,
		image->width,
		image->height,
		(unsigned int)image->data);
		/* TBD: Implement other color depths in ps2fb_imageblit()? */
	}
}

static int ps2fb_mmap(struct fb_info *info,
		    struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;

	if (offset + size > info->fix.smem_len) {
		return -EINVAL;
	}

	if ((info->fix.smem_start == 0) && (info->fix.smem_len > 0)) {
		/* Memory is allocated on first call to mmap. */
		/* This will save memory until framebuffer is used. */
		info->fix.smem_start = (unsigned long) rvmalloc(info->fix.smem_len);
	}

	if (info->fix.smem_start == 0) {
		printk("ps2fb: Failed to allocate frame buffer (%d Bytes).\n", info->fix.smem_len);
		return -ENOMEM;
	}

	pos = (unsigned long)info->fix.smem_start + offset;
	/* Framebuffer can't be mapped. Map normal memory instead
	 * and copy every 20ms the data from memory to the
	 * framebuffer. Currently there is no other way beside
	 * mapping to access the framebuffer from applications
	 * like xorg.
	 */

	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;	/* avoid to swap out this VMA */

	if (ps2fb.mapped == 0) {
		ps2fb.mapped = 1;

		/* Make screen black when mapped the first time. */
		memset((void *) info->fix.smem_start, 0, info->fix.smem_len);

		/* TBD: Copy current frame buffer to memory. */
	}
	return 0;
}

static int ps2fbd(void *arg)
{
	struct fb_info *info = arg;

	set_freezable();
	while (!kthread_should_stop()) {
		try_to_freeze();
		set_current_state(TASK_INTERRUPTIBLE);
		if (ps2fb.is_kicked) {
			ps2fb.is_kicked = 0;
			console_lock();
			ps2fb_redraw(info);
			console_unlock();
		}
		schedule();
	}
	return 0;
}

static irqreturn_t ps2fb_vsync_interrupt(int irq, void *ptr)
{
	if (irq == IRQ_GS_VSYNC){
		if (ps2fb.task && !ps2fb.is_blanked && ps2fb.mapped){
			ps2fb.is_kicked = 1;
			wake_up_process(ps2fb.task);
		}
	}

	return IRQ_HANDLED;
}

static struct fb_ops ps2fb_ops = {
	.owner		= THIS_MODULE,
	.fb_open = ps2fb_open,
	.fb_release = ps2fb_release,
#if 0 /* TBD: Implement functions. */
	.fb_read = ps2fb_read,
	.fb_write = ps2fb_write,
	.fb_sync = ps2fb_sync,
	/** TBD: .fb_ioctl = ps2fb_ioctl, */
#endif
	.fb_check_var = ps2fb_check_var,
	.fb_set_par = ps2fb_set_par,
	.fb_setcolreg = ps2fb_setcolreg,
	.fb_fillrect = ps2fb_fillrect,
	.fb_copyarea = ps2fb_copyarea,
	.fb_imageblit = ps2fb_imageblit,
	.fb_mmap = ps2fb_mmap,
	.fb_blank = ps2fb_blank,
};



static int ps2fb_probe(struct platform_device *pdev)
{
    struct fb_info *info;
    struct ps2fb_par *par;
    struct device *device = &pdev->dev; /* or &pdev->dev */
    struct task_struct *task;
    int cmap_len, retval;

	/* TBD: move to other function? */
	ps2con_gsp_init();

	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);

    /*
     * Dynamically allocate info and par
     */
    info = framebuffer_alloc(sizeof(struct ps2fb_par), device);

    if (!info) {
		return -ENOMEM;
    }

    /*
     * Here we set the screen_base to the virtual memory address
     * for the framebuffer. Usually we obtain the resource address
     * from the bus layer and then translate it to virtual memory
     * space via ioremap. Consult ioport.h.
     */
    info->screen_base = NULL; /* TBD: framebuffer_virtual_memory; */
    info->fbops = &ps2fb_ops;

	strcpy(info->fix.id, "PS2 GS");
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR; /* TBD: FB_VISUAL_PSEUDOCOLOR, */
	info->fix.xpanstep = 1;
	info->fix.ypanstep = 1; // TBD: Add suport for second screen.
	info->fix.ywrapstep = 1 ;
	info->fix.accel = FB_ACCEL_NONE; /* TBD: Check if something is possible. */


    par = info->par;
	info->pseudo_palette = par->pseudo_palette;
	par->opencnt = 0;

    ps2con_initinfo(&par->screeninfo);
	if ((crtmode < 0) || (crtmode > 3)) {
		/* Set default to old crtmode parameter. */
		info->var.nonstd = par->screeninfo.mode;
	} else {
	    info->var.nonstd = crtmode;
	}


    /*
     * Set up flags to indicate what sort of acceleration your
     * driver can provide (pan/wrap/copyarea/etc.) and whether it
     * is a module -- see FBINFO_* in include/linux/fb.h
     *
     * If your hardware can support any of the hardware accelerated functions
     * fbcon performance will improve if info->flags is set properly.
     *
     * FBINFO_HWACCEL_COPYAREA - hardware moves
     * FBINFO_HWACCEL_FILLRECT - hardware fills
     * FBINFO_HWACCEL_IMAGEBLIT - hardware mono->color expansion
     * FBINFO_HWACCEL_YPAN - hardware can pan display in y-axis
     * FBINFO_HWACCEL_YWRAP - hardware can wrap display in y-axis
     * FBINFO_HWACCEL_DISABLED - supports hardware accels, but disabled
     * FBINFO_READS_FAST - if set, prefer moves over mono->color expansion
     * FBINFO_MISC_TILEBLITTING - hardware can do tile blits
     *
     * NOTE: These are for fbcon use only.
     */
    info->flags = FBINFO_DEFAULT
		| FBINFO_HWACCEL_COPYAREA
		| FBINFO_HWACCEL_FILLRECT
		| FBINFO_HWACCEL_IMAGEBLIT;
	/* TBD: Support FBINFO_HWACCEL_YPAN or FBINFO_HWACCEL_YWRAP some applications need this. */

    /*
     * This should give a reasonable default video mode. The following is
     * done when we can set a video mode.
     */
	switch (info->var.nonstd) {
	case PS2_GS_PAL:
		retval = fb_find_mode(&info->var, info, mode_option, pal_modes, ARRAY_SIZE(pal_modes), NULL, 32);
		break;
	case PS2_GS_NTSC:
		retval = fb_find_mode(&info->var, info, mode_option, ntsc_modes, ARRAY_SIZE(ntsc_modes), NULL, 32);
		break;
	case PS2_GS_DTV:
		retval = fb_find_mode(&info->var, info, mode_option, dtv_modes, ARRAY_SIZE(dtv_modes), NULL, 32);
		break;
	case PS2_GS_VESA:
		retval = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 32);
		break;
	default:
		printk("ps2fb: unknown crtmode %d\n", info->var.nonstd);
		retval = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 32);
		break;
	}
	DPRINTK("ps2fb: fb_find_mode retval = %d\n", retval);
	DPRINTK("ps2fb: mode_option %s %dx%d\n", mode_option, info->var.xres, info->var.yres);

	if (!retval)
		return -EINVAL;

	if (videomemsize > 0) {
		info->fix.smem_len = videomemsize;
	} else {
		info->fix.smem_len = 0;
	}
	info->fix.smem_start = 0;
	ps2fb_switch_mode(info);

    /* This has to be done! */
	cmap_len = PAL_COLORS;
    if (fb_alloc_cmap(&info->cmap, cmap_len, 0))
	return -ENOMEM;

	retval = devm_request_irq(device, IRQ_GS_VSYNC, ps2fb_vsync_interrupt,
				  IRQF_SHARED, DEVICE_NAME, device);
	if (retval) {
		DPRINTK("ps2fb: devm_request_irq failed %d\n", retval);
		return -EINVAL;
	}

	task = kthread_run(ps2fbd, info, DEVICE_NAME);
	if (IS_ERR(task)) {
		DPRINTK("ps2fb: kthread_run failed\n");
		return -EINVAL;
	}

	ps2fb.is_blanked = 0;
	ps2fb.is_kicked = 0;
	ps2fb.task = task;

	if (register_framebuffer(info) < 0) {
		fb_dealloc_cmap(&info->cmap);
		return -EINVAL;
	}
	DPRINTK(KERN_INFO "fb%d: %s frame buffer device\n", info->node,
		info->fix.id);
	platform_set_drvdata(pdev, info);

    return 0;
}

static int ps2fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);

	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);
	if (info) {
		if (info->fix.smem_start != 0) {
			rvfree((void *) info->fix.smem_start, info->fix.smem_len);
			info->fix.smem_start = 0;
		}
		info->fix.smem_len = 0;
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver ps2fb_driver = {
	.probe = ps2fb_probe,
	.remove = ps2fb_remove,
	.driver = {
		.name = DEVICE_NAME,
	},
};

#ifndef MODULE
/*
 * Only necessary if your driver takes special options,
 * otherwise we fall back on the generic fb_setup().
 */
int __init ps2fb_setup(char *options)
{
	char *this_opt;

	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);

	if (!options || !*options)
		return 0;

    /* Parse user speficied options (`video=ps2fb:') */
	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;

		if (strncasecmp(this_opt, VESA, sizeof(VESA) - 1) == 0) {
			crtmode = PS2_GS_VESA;
		} else if (strncasecmp(this_opt, DTV, sizeof(DTV) - 1) == 0) {
			crtmode = PS2_GS_DTV;
		} else if (strncasecmp(this_opt, NTSC, sizeof(NTSC) - 1) == 0) {
			crtmode = PS2_GS_NTSC;
		} else if (strncasecmp(this_opt, PAL, sizeof(PAL) - 1) == 0) {
			crtmode = PS2_GS_PAL;
		} else if (strncasecmp(this_opt, VIDEOMEMEMORYSIZE, sizeof(VIDEOMEMEMORYSIZE) - 1) == 0) {
			videomemsize = simple_strtoul(this_opt + sizeof(VIDEOMEMEMORYSIZE) - 1, NULL, 10);
		} else if (this_opt[0] >= '0' && this_opt[0] <= '9') {
			mode_option = this_opt;
		} else {
			printk(KERN_WARNING
				"ps2fb: unrecognized option %s\n", this_opt);
		}
	}
	return 0;
}
#endif /* MODULE */

static int __init ps2fb_init(void)
{
	int ret;
	/*
	 *  For kernel boot options (in 'video=ps2fb:<options>' format)
	 */
#ifndef MODULE
	char *option = NULL;

	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);

	if (fb_get_options(DEVICE_NAME, &option))
		return -ENODEV;
	ps2fb_setup(option);
#else
	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);
#endif

	ret = platform_driver_register(&ps2fb_driver);
	DPRINTK("ps2fb: %d %s() ret = %d\n", __LINE__, __FUNCTION__, ret);

	return ret;
}

static void __exit ps2fb_exit(void)
{
	DPRINTK("ps2fb: %d %s()\n", __LINE__, __FUNCTION__);
	platform_driver_unregister(&ps2fb_driver);
}

/* ------------------------------------------------------------------------- */

module_init(ps2fb_init);
module_exit(ps2fb_exit);

MODULE_LICENSE("GPL");
