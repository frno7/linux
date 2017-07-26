
/* Copyright 2010 Mega Man */

/* TBD: Unfinished state. Rework code. */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/ps2/gs.h>
#include <linux/semaphore.h>
#include <linux/hardirq.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/mach-ps2/ps2con.h>

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
			ps2_printf("ps2dma: GS packet is not terminated\n");
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
		ps2_printf("ps2dma: %s timeout\n", kdb->channel->device);
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

	if (strnicmp(options, "vesa", 4) == 0) {
		options += 4;
		defaultmode = PS2_GS_VESA;
		maxres = 4;
	} else if (strnicmp(options, "dtv", 3) == 0) {
		options += 3;
		defaultmode = PS2_GS_DTV;
		maxres = 3;
	} else if (strnicmp(options, "ntsc", 4) == 0) {
		options += 4;
		defaultmode = PS2_GS_NTSC;
		maxres = 2;
	} else if (strnicmp(options, "pal", 3) == 0) {
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

MODULE_LICENSE("GPL");
