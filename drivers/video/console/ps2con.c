/*
 *  linux/drivers/video/ps2con.c
 *  PlayStation 2 Graphics Synthesizer console driver
 *
 *	Copyright (C) 2000-2002  Sony Computer Entertainment Inc.
 *	Copyright (C) 2010       Mega Man
 *
 * 
 *  This file is based on the low level frame buffer based console driver
 *  (fbcon.c):
 *
 *	Copyright (C) 1995 Geert Uytterhoeven
 *
 *  and on the original Amiga console driver (amicon.c):
 *
 *	Copyright (C) 1993 Hamish Macdonald
 *			   Greg Harp
 *	Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *	      with work by William Rucklidge (wjr@cs.cornell.edu)
 *			   Geert Uytterhoeven
 *			   Jes Sorensen (jds@kom.auc.dk)
 *			   Martin Apel
 *
 *  and on the original Atari console driver (atacon.c):
 *
 *	Copyright (C) 1993 Bjoern Brauel
 *			   Roman Hodek
 *
 *	      with work by Guenther Kelleter
 *			   Martin Schaller
 *			   Andreas Schwab
 *
 *  Hardware cursor support added by Emmanuel Marty (core@ggi-project.org)
 *  Smart redraw scrolling, arbitrary font width support, 512char font support
 *  added by 
 *                         Jakub Jelinek (jj@ultra.linux.cz)
 *
 *  Random hacking by Martin Mares <mj@ucw.cz>
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */
/* TBD: Unfinished state. Rework code. */

#define PS2CONDEBUG

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/font.h>
#define INCLUDE_LINUX_LOGO_DATA
#include <linux/linux_logo.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>
#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/gsfunc.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/mach-ps2/ps2con.h>

//#define LOGO_SUPPORT /* TBD: Add support for logo. */

#ifdef PS2CONDEBUG
#  define DPRINTK(fmt, args...) ps2_printf(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

#define LOGO_H			80
#define LOGO_W			80
#define LOGO_LINE	(LOGO_W/8)

static unsigned int ps2_cmap[] = {
    0x00000000, 0x80aa0000, 0x8000aa00, 0x80aaaa00,
    0x800000aa, 0x80aa00aa, 0x800055aa, 0x80aaaaaa,
    0x80555555, 0x80ff5555, 0x8055ff55, 0x80ffff55,
    0x805555ff, 0x80ff55ff, 0x8055ffff, 0x80ffffff,
};

static struct ps2_screeninfo defaultinfo;
static struct ps2dpy ps2dpy[MAX_NR_CONSOLES];
static int currcon = 0;

static int logo_lines;
static int logo_shown = -1;
/* Software scrollback */
#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
int ps2con_softback_size = 32768;
static unsigned long softback_buf, softback_curr;
static unsigned long softback_in;
static unsigned long softback_top, softback_end;
static int softback_lines;
#endif

#define REFCOUNT(fd)	(((int *)(fd))[-1])
#define FNTSIZE(fd)	(((int *)(fd))[-2])
#define FNTCHARCNT(fd)	(((int *)(fd))[-3])
#define FNTSUM(fd)	(((int *)(fd))[-4])
#define FONT_EXTRA_WORDS 4

#define CM_SOFTBACK	(8)

#define advance_row(p, delta) (unsigned short *)((unsigned long)(p) + (delta) * conp->vc_size_row)

static void ps2con_free_font(struct ps2dpy *);
static int ps2con_set_origin(struct vc_data *);

#define CURSOR_DRAW_DELAY		1
#define DEFAULT_CURSOR_BLINK_RATE	20

static int cursor_drawn = 0;
static int vbl_cursor_cnt = 0;
static int cursor_on = 0;
static int cursor_blink_rate = DEFAULT_CURSOR_BLINK_RATE;

static inline void cursor_undrawn(void)
{
    vbl_cursor_cnt = 0;
    cursor_drawn = 0;
}


/*
 *  Interface used by the world
 */

static const char *ps2con_startup(void);
static void ps2con_init(struct vc_data *conp, int init);
static void ps2con_deinit(struct vc_data *conp);
static void ps2con_clear(struct vc_data *conp, int sy, int sx, int height,
			 int width);
static void ps2con_putc(struct vc_data *conp, int ch, int ypos, int xpos);
static void ps2con_putcs(struct vc_data *conp, const unsigned short *s, int count,
			 int ypos, int xpos);
static void ps2con_cursor(struct vc_data *conp, int mode);
static int ps2con_scroll(struct vc_data *conp, int t, int b, int dir,
			 int count);
static void ps2con_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
			 int height, int width);
static int ps2con_switch(struct vc_data *conp);
static int ps2con_blank(struct vc_data *conp, int blank, int mode_switch);
static int ps2con_set_palette(struct vc_data *conp, unsigned char *table);
static int ps2con_scrolldelta(struct vc_data *conp, int lines);


/*
 *  Internal routines
 */

static void ps2con_setup(int con, int init, int logo);
static void ps2con_vbl_handler(int irq, void *dummy, struct pt_regs *fp);
static void ps2con_revc(struct vc_data *conp, int sy, int sx,
			int height, int width);
static void ps2con_clear_margins(struct vc_data *conp, struct ps2dpy *p);
#ifdef LOGO_SUPPORT
static int ps2con_show_logo(void);
#endif

static void cursor_timer_handler(unsigned long dev_addr);

static struct timer_list cursor_timer = {
    function: cursor_timer_handler
};

static void cursor_timer_handler(unsigned long dev_addr)
{
      ps2con_vbl_handler(0, NULL, NULL);
      cursor_timer.expires = jiffies+HZ/50;
      add_timer(&cursor_timer);
}


/*
 *  Low Level Operations
 */

static void ps2con_get_screeninfo(struct ps2_screeninfo *info)
{
    int con = fg_console;
    struct ps2dpy *p = &ps2dpy[con];

    p->info = *info;

    acquire_console_sem();
    ps2con_setup(con, 0, 0);
    release_console_sem();
}

static const char *ps2con_startup(void)
{
    const char *display_desc = "PlayStation 2 Graphics Synthesizer";
    static int done = 0;

    if (done)
	return display_desc;
    done = 1;

    ps2con_initinfo(&defaultinfo);
    ps2gs_screeninfo(&defaultinfo, NULL);
    ps2con_gsp_init();
    ps2gs_screeninfo_hook = ps2con_get_screeninfo;

    cursor_blink_rate = DEFAULT_CURSOR_BLINK_RATE;
    init_timer(&cursor_timer);
    cursor_timer.expires = jiffies+HZ/50;
    add_timer(&cursor_timer);

    return display_desc;
}


static void ps2con_init(struct vc_data *conp, int init)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];

    p->conp = conp;
    p->info = defaultinfo;

    ps2con_setup(unit, init, !init);
}


static void ps2con_deinit(struct vc_data *conp)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];

    ps2con_free_font(p);
    p->conp = 0;
}


static void ps2con_setup(int con, int init, int logo)
{
    struct ps2dpy *p = &ps2dpy[con];
    struct vc_data *conp = p->conp;
    int nr_rows, nr_cols;
    int old_rows, old_cols;
    unsigned short *save = NULL, *r, *q;
    int charcnt = 256;
    const struct font_desc *font;

#if 0 /* TODO */
    if (con != fg_console || graphics_boot)
	logo = 0;
#else
    if (con != fg_console)
	logo = 0;
#endif

#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
    if (con == fg_console) {   
	if (ps2con_softback_size) {
	    if (!softback_buf) {
		softback_buf = (unsigned long)kmalloc(ps2con_softback_size, GFP_KERNEL);
		if (!softback_buf) {
    	            ps2con_softback_size = 0;
    	            softback_top = 0;
    		}
    	    }
	} else {
	    if (softback_buf) {
		kfree((void *)softback_buf);
		softback_buf = 0;
		softback_top = 0;
	    }
	}
	if (softback_buf)
	    softback_in = softback_top = softback_curr = softback_buf;
	softback_lines = 0;
    }
#endif

    p->fbw = (p->info.w + 63) / 64;
    switch (p->info.psm) {
    case PS2_GS_PSMCT32:
	p->pixel_size = 4;
	break;
    case PS2_GS_PSMCT24:
	p->pixel_size = 3;
	break;
    case PS2_GS_PSMCT16:
    case PS2_GS_PSMCT16S:
	p->pixel_size = 2;
	break;
    default:
	p->pixel_size = -1;
    }	
    p->can_soft_blank = 1;

    if (!p->fontdata) {
	font = get_default_font(p->info.w, p->info.h,
		(1 << (8 - 1)),
		(1 << (8 - 1)) | (1 << (16 - 1)));
	if (font->width > 0) { 
	    p->_fontwidth = font->width;
	    p->grayfont = 0;
	} else {
	    p->_fontwidth = -font->width;
	    p->grayfont = 1;
	}
	p->_fontheight = font->height;
	p->fontdata = font->data;
    }

    old_cols = conp->vc_cols;
    old_rows = conp->vc_rows;

    nr_cols = p->info.w / fontwidth(p);
    nr_rows = p->info.h / fontheight(p);

    if (logo) {
    	/* Need to make room for the logo */
	int cnt;
	int step;

	if (is_nointer(p))
	    logo_lines = ((LOGO_H / 2) + fontheight(p) - 1) / fontheight(p);
	else
	    logo_lines = (LOGO_H + fontheight(p) - 1) / fontheight(p);
	q = (unsigned short *)(conp->vc_origin + conp->vc_size_row * old_rows);
	step = logo_lines * old_cols;
	for (r = q - logo_lines * old_cols; r < q; r++)
	    if (scr_readw(r) != conp->vc_video_erase_char)
    	    	break;
	if (r != q && nr_rows >= old_rows + logo_lines) {
	    save = kmalloc(logo_lines * nr_cols * 2, GFP_KERNEL);
	    if (save) {
		int i = old_cols < nr_cols ? old_cols : nr_cols;
		scr_memsetw(save, conp->vc_video_erase_char, logo_lines * nr_cols * 2);
		r = q - step;
		for (cnt = 0; cnt < logo_lines; cnt++, r += i)
		    scr_memcpyw(save + cnt * nr_cols, r, 2 * i);
		r = q;
	    }
	}
	if (r == q) {
	    /* We can scroll screen down */
	    r = q - step - old_cols;
	    for (cnt = old_rows - logo_lines; cnt > 0; cnt--) {
		scr_memcpyw(r + step, r, conp->vc_size_row);
		r -= old_cols;
	    }
	    if (!save) {
		conp->vc_y += logo_lines;
		conp->vc_pos += logo_lines * conp->vc_size_row;
	    }
	}
	scr_memsetw((unsigned short *)conp->vc_origin,
		    conp->vc_video_erase_char, 
		    conp->vc_size_row * logo_lines);
    }

    if (init) {
	conp->vc_cols = nr_cols;
	conp->vc_rows = nr_rows;
    }

    conp->vc_can_do_color = 1;
    conp->vc_complement_mask = 0x7700;
    if (charcnt == 256) {
	conp->vc_hi_font_mask = 0;
	p->fgshift = 8;
	p->bgshift = 12;
	p->charmask = 0xff;
    } else {
	conp->vc_hi_font_mask = 0x100;
	conp->vc_complement_mask <<= 1;
	p->fgshift = 9;
	p->bgshift = 13;
	p->charmask = 0x1ff;
    }

    p->fgcol = 7;
    p->bgcol = 0;

    if (!init) {
	if (conp->vc_cols != nr_cols || conp->vc_rows != nr_rows)
	    vc_resize(conp, nr_cols, nr_rows);
	else if (CON_IS_VISIBLE(conp) &&
		 conp->vc_mode == KD_TEXT) {
	    ps2con_clear_margins(conp, p);
	    update_screen(conp);
	}
	if (save) {
    	    q = (unsigned short *)(conp->vc_origin + conp->vc_size_row * old_rows);
	    scr_memcpyw(q, save, logo_lines * nr_cols * 2);
	    conp->vc_y += logo_lines;
    	    conp->vc_pos += logo_lines * conp->vc_size_row;
    	    kfree(save);
	}
    }

    if (logo) {
	logo_shown = -2;
	conp->vc_top = logo_lines;
    }

#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
    if (con == fg_console && softback_buf) {
    	int l = ps2con_softback_size / conp->vc_size_row;
    	if (l > 5)
    	    softback_end = softback_buf + l * conp->vc_size_row;
    	else {
    	    /* Smaller scrollback makes no sense, and 0 would screw
    	       the operation totally */
    	    softback_top = 0;
    	}
    }
#endif
}


/* ====================================================================== */

/*
 *  ps2con_XXX routines - interface used by the world
 */

static void ps2con_clear(struct vc_data *conp, int sy, int sx, int height,
			 int width)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    int redraw_cursor = 0;
    u64 *gsp;
    int ctx = 0;

    if (logo_shown == fg_console &&
	sy == 0 && sx == 0 &&
	height == conp->vc_rows && width == conp->vc_cols) {
	ps2con_clear_margins(conp, p);
	logo_shown = -1;
    }

    if (!p->can_soft_blank && console_blanked)
	return;

    if (height == 0 || width == 0)
	return;

    if ((sy <= p->cursor_y) && (p->cursor_y < sy + height) &&
	(sx <= p->cursor_x) && (p->cursor_x < sx + width)) {
	cursor_undrawn();
	redraw_cursor = 1;
    }

    if ((gsp = ps2con_gsp_alloc(ALIGN16(6 * 8), NULL)) == NULL)
	return;

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(1, 1, 0, 0, PS2_GIFTAG_FLG_REGLIST, 4);
    *gsp++ = 0x5510;
    *gsp++ = 0x006 + (ctx << 9);				/* PRIM */
    *gsp++ = ps2_cmap[attr_bgcol_ec(p, conp)];			/* RGBAQ */
    *gsp++ = PACK32(sx * fontwidth(p) * 16, sy * fontheight(p) * 16);
								/* XYZ2 */
    *gsp++ = PACK32((sx + width) * fontwidth(p) * 16,
		    (sy + height) * fontheight(p) * 16);	/* XYZ2 */

    ps2con_gsp_send(ALIGN16(6 * 8), 0);

    if (redraw_cursor)
	vbl_cursor_cnt = CURSOR_DRAW_DELAY;
}

#define GRAYSCALE

static void *ps2con_put_pattern(struct ps2dpy *p, void *ptr, 
				const unsigned short *s, int count)
{
    int i, j;
    const unsigned char *cp;
    unsigned char mask = 0, pattern = 0;
    int fgcol, bgcol;
    unsigned short ch;
    int x;
    u32 *p32, *q32, *b32;
    u16 *p16, *q16, *b16;
    u8 *p8, *q8, *b8;
    int fontsize;

#ifdef GRAYSCALE
    if (p->grayfont)
	goto gray;
#endif

    fontsize = (fontwidth(p) + 7) / 8 * fontheight(p);
    switch (p->pixel_size) {
    case 4:		/* RGBA32 */
	b32 = p32 = (u32 *)ptr;
	for (x = 0; x < count; x++) {
	    ch = *s++;
	    cp = p->fontdata + fontsize * (ch & p->charmask);
	    fgcol = ps2_cmap[attr_fgcol(p, ch)];
	    bgcol = ps2_cmap[attr_bgcol(p, ch)];

	    q32 = b32;
	    for (i = 0; i < fontheight(p); i++) {
		p32 = q32;
		for (j = 0; j < fontwidth(p); j++) {
		    if ((j & 7) == 0) {
			mask = 0x80;
			pattern = *cp++;
		    }
		    if (mask & pattern)
			*p32++ = fgcol;
		    else
			*p32++ = bgcol;
		    mask >>= 1;
		}
		q32 += fontwidth(p) * count;
	    }
	    b32 += fontwidth(p);
	}
	return (void *)p32;

    case 2:		/* RGBA16 */
	b16 = p16 = (u16 *)ptr;
	for (x = 0; x < count; x++) {
	    ch = *s++;
	    cp = p->fontdata + fontsize * (ch & p->charmask);
	    fgcol = bpp32to16(ps2_cmap[attr_fgcol(p, ch)]);
	    bgcol = bpp32to16(ps2_cmap[attr_bgcol(p, ch)]);

	    q16 = b16;
	    for (i = 0; i < fontheight(p); i++) {
		p16 = q16;
		for (j = 0; j < fontwidth(p); j++) {
		    if ((j & 7) == 0) {
			mask = 0x80;
			pattern = *cp++;
		    }
		    if (mask & pattern)
			*p16++ = fgcol;
		    else
			*p16++ = bgcol;
		    mask >>= 1;
		}
		q16 += fontwidth(p) * count;
	    }
	    b16 += fontwidth(p);
	}
	return (void *)p16;

    case 3:		/* RGB24 */
	b8 = p8 = (u8 *)ptr;
	for (x = 0; x < count; x++) {
	    ch = *s++;
	    cp = p->fontdata + fontsize * (ch & p->charmask);
	    fgcol = ps2_cmap[attr_fgcol(p, ch)];
	    bgcol = ps2_cmap[attr_bgcol(p, ch)];

	    q8 = b8;
	    for (i = 0; i < fontheight(p); i++) {
		p8 = q8;
		for (j = 0; j < fontwidth(p); j++) {
		    if ((j & 7) == 0) {
			mask = 0x80;
			pattern = *cp++;
		    }
		    if (mask & pattern) {
			*p8++ = ((unsigned char *)&fgcol)[0];
			*p8++ = ((unsigned char *)&fgcol)[1];
			*p8++ = ((unsigned char *)&fgcol)[2];
		    } else {
			*p8++ = ((unsigned char *)&bgcol)[0];
			*p8++ = ((unsigned char *)&bgcol)[1];
			*p8++ = ((unsigned char *)&bgcol)[2];
		    }
		    mask >>= 1;
		}
		q8 += fontwidth(p) * count * 3;
	    }
	    b8 += fontwidth(p) * 3;
	}
	return (void *)p8;
    }

#ifdef GRAYSCALE
gray:
    fontsize = (fontwidth(p) + 1) / 2 * fontheight(p);
    switch (p->pixel_size) {
    case 4:		/* RGBA32 */
	b32 = p32 = (u32 *)ptr;
	for (x = 0; x < count; x++) {
	    ch = *s++;
	    cp = p->fontdata + fontsize * (ch & p->charmask);
	    fgcol = ps2_cmap[attr_fgcol(p, ch)];
	    bgcol = ps2_cmap[attr_bgcol(p, ch)];

	    q32 = b32;
	    for (i = 0; i < fontheight(p); i++) {
		p32 = q32;
		for (j = 0; j < fontwidth(p); j++) {
		    if ((j & 1) == 0)
			pattern = *cp++;
		    mask = pattern & 0x0f;
		    pattern >>= 4;
		    *p32++ = 
			((fgcol & 0xff) * mask / 15 +
			 (bgcol & 0xff) * (15 - mask) / 15) |
			((((fgcol >> 8) & 0xff) * mask / 15 +
			  ((bgcol >> 8) & 0xff) * (15 - mask) / 15) << 8) |
			((((fgcol >> 16) & 0xff) * mask / 15 +
			  ((bgcol >> 16) & 0xff) * (15 - mask) / 15) << 16);
		}
		q32 += fontwidth(p) * count;
	    }
	    b32 += fontwidth(p);
	}
	return (void *)p32;

    case 2:		/* RGBA16 */
	b16 = p16 = (u16 *)ptr;
	for (x = 0; x < count; x++) {
	    ch = *s++;
	    cp = p->fontdata + fontsize * (ch & p->charmask);
	    fgcol = bpp32to16(ps2_cmap[attr_fgcol(p, ch)]);
	    bgcol = bpp32to16(ps2_cmap[attr_bgcol(p, ch)]);

	    q16 = b16;
	    for (i = 0; i < fontheight(p); i++) {
		p16 = q16;
		for (j = 0; j < fontwidth(p); j++) {
		    if ((j & 1) == 0)
			pattern = *cp++;
		    mask = pattern & 0x0f;
		    pattern >>= 4;
		    *p16++ = 
			((fgcol & 0x1f) * mask / 15 +
			 (bgcol & 0x1f) * (15 - mask) / 15) |
			((((fgcol >> 5) & 0x1f) * mask / 15 +
			  ((bgcol >> 5) & 0x1f) * (15 - mask) / 15) << 5) |
			((((fgcol >> 10) & 0x1f) * mask / 15 +
			  ((bgcol >> 10) & 0x1f) * (15 - mask) / 15) << 10);
		}
		q16 += fontwidth(p) * count;
	    }
	    b16 += fontwidth(p);
	}
	return (void *)p16;

    case 3:		/* RGB24 */
	b8 = p8 = (u8 *)ptr;
	for (x = 0; x < count; x++) {
	    ch = *s++;
	    cp = p->fontdata + fontsize * (ch & p->charmask);
	    fgcol = ps2_cmap[attr_fgcol(p, ch)];
	    bgcol = ps2_cmap[attr_bgcol(p, ch)];

	    q8 = b8;
	    for (i = 0; i < fontheight(p); i++) {
		p8 = q8;
		for (j = 0; j < fontwidth(p); j++) {
		    if ((j & 1) == 0)
			pattern = *cp++;
		    mask = pattern & 0x0f;
		    pattern >>= 4;
		    *p8++ = ((fgcol & 0xff) * mask / 15 +
			     (bgcol & 0xff) * (15 - mask) / 15);
		    *p8++ = (((fgcol >> 8) & 0xff) * mask / 15 +
			     ((bgcol >> 8) & 0xff) * (15 - mask) / 15);
		    *p8++ = (((fgcol >> 16) & 0xff) * mask / 15 +
			     ((bgcol >> 16) & 0xff) * (15 - mask) / 15);
		}
		q8 += fontwidth(p) * count * 3;
	    }
	    b8 += fontwidth(p) * 3;
	}
	return (void *)p8;
    }
#endif

    return NULL;
}


static void ps2con_putc(struct vc_data *conp, int ch, int ypos, int xpos)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    int redraw_cursor = 0;
    u64 *gsp;
    void *gsp_h;
    unsigned short sch = ch;

    if (!p->can_soft_blank && console_blanked)
	return;
    if (conp->vc_mode != KD_TEXT)
	return;
    if (p->pixel_size <= 0)
	return;

    if ((p->cursor_x == xpos) && (p->cursor_y == ypos)) {
	cursor_undrawn();
	redraw_cursor = 1;
    }

    if ((gsp = ps2con_gsp_alloc(ALIGN16(12 * 8 + fontwidth(p) * fontheight(p) * p->pixel_size), NULL)) == NULL)
	return;
    gsp_h = gsp;

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
    *gsp++ = 0x0e;	/* A+D */
    *gsp++ = (u64)0 | ((u64)p->info.fbp << 32) |
	((u64)p->fbw << 48) | ((u64)p->info.psm << 56);
    *gsp++ = PS2_GS_BITBLTBUF;
    *gsp++ = PACK64(0, PACK32(xpos * fontwidth(p), ypos * fontheight(p)));
    *gsp++ = PS2_GS_TRXPOS;
    *gsp++ = PACK64(fontwidth(p), fontheight(p));
    *gsp++ = PS2_GS_TRXREG;
    *gsp++ = 0;		/* host to local */
    *gsp++ = PS2_GS_TRXDIR;

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16(fontwidth(p) * fontheight(p) * p->pixel_size) / 16, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
    *gsp++ = 0;

    ps2con_gsp_send(ALIGN16(ps2con_put_pattern(p, gsp, &sch, 1) - gsp_h), 0);

    if (redraw_cursor)
	vbl_cursor_cnt = CURSOR_DRAW_DELAY;
}


static void ps2con_putcs(struct vc_data *conp, const unsigned short *s, int count,
			 int ypos, int xpos)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    int redraw_cursor = 0;
    int gspsz, maxdc, dc;
    u64 *gsp;
    void *gsp_h;

    if (!p->can_soft_blank && console_blanked)
	return;
    if (conp->vc_mode != KD_TEXT)
	return;
    if (p->pixel_size <= 0)
	return;

    if ((p->cursor_y == ypos) && (xpos <= p->cursor_x) &&
	(p->cursor_x < (xpos + count))) {
	cursor_undrawn();
	redraw_cursor = 1;
    }

    while (count > 0) {
	if ((gsp = ps2con_gsp_alloc(ALIGN16(12 * 8 + fontwidth(p) * fontheight(p) * p->pixel_size), &gspsz)) == NULL)
	    return;
	gsp_h = gsp;
	maxdc = (gspsz - ALIGN16(12 * 8)) / (fontwidth(p) * fontheight(p) * p->pixel_size);
	dc = count > maxdc ? maxdc : count;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	*gsp++ = 0x0e;		/* A+D */
	*gsp++ = (u64)0 | ((u64)p->info.fbp << 32) |
	    ((u64)p->fbw << 48) | ((u64)p->info.psm << 56);
	*gsp++ = PS2_GS_BITBLTBUF;
	*gsp++ = PACK64(0, PACK32(xpos * fontwidth(p), ypos * fontheight(p)));
	*gsp++ = PS2_GS_TRXPOS;
	*gsp++ = PACK64(fontwidth(p) * dc, fontheight(p));
	*gsp++ = PS2_GS_TRXREG;
	*gsp++ = 0;		/* host to local */
	*gsp++ = PS2_GS_TRXDIR;

	*gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16(dc * fontwidth(p) * fontheight(p) * p->pixel_size) / 16, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	*gsp++ = 0;

	ps2con_gsp_send(ALIGN16(ps2con_put_pattern(p, gsp, s, dc) - gsp_h), 0);

	s += dc;
	xpos += dc;
	count -= dc;
    }

    if (redraw_cursor)
	vbl_cursor_cnt = CURSOR_DRAW_DELAY;
}


static void ps2con_cursor(struct vc_data *conp, int mode)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    int y = conp->vc_y;

#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
    if (mode & CM_SOFTBACK) {
    	mode &= ~CM_SOFTBACK;
    	if (softback_lines) {
    	    if (y + softback_lines >= conp->vc_rows)
    		mode = CM_ERASE;
    	    else
    	        y += softback_lines;
	}
    } else if (softback_lines)
	ps2con_set_origin(conp);
#endif

    /* Avoid flickering if there's no real change. */
    if (p->cursor_x == conp->vc_x && p->cursor_y == y &&
	(mode == CM_ERASE) == !cursor_on)
	return;

    cursor_on = 0;
    if (cursor_drawn)
	ps2con_revc(p->conp, p->cursor_y, p->cursor_x, 1, 1);

    p->cursor_x = conp->vc_x;
    p->cursor_y = y;

    switch (mode) {
    case CM_ERASE:
	cursor_drawn = 0;
	break;
    case CM_MOVE:
    case CM_DRAW:
	if (cursor_drawn)
	    ps2con_revc(p->conp, p->cursor_y, p->cursor_x, 1, 1);
	vbl_cursor_cnt = CURSOR_DRAW_DELAY;
	cursor_on = 1;
	break;
    }
}


static void ps2con_vbl_handler(int irq, void *dummy, struct pt_regs *fp)
{
    struct ps2dpy *p;

    if (!cursor_on)
	return;

    if (vbl_cursor_cnt && --vbl_cursor_cnt == 0) {
	p = &ps2dpy[fg_console];
	ps2con_revc(p->conp, p->cursor_y, p->cursor_x, 1, 1);
	cursor_drawn ^= 1;
	vbl_cursor_cnt = cursor_blink_rate;
    }
}


#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
static void ps2con_redraw_softback(struct vc_data *conp, struct ps2dpy *p, long delta)
{
    unsigned short *d, *s;
    unsigned long n;
    int line = 0;
    int count = conp->vc_rows;
    
    d = (u16 *)softback_curr;
    if (d == (u16 *)softback_in)
	d = (u16 *)conp->vc_origin;
    n = softback_curr + delta * conp->vc_size_row;
    softback_lines -= delta;
    if (delta < 0) {
        if (softback_curr < softback_top && n < softback_buf) {
            n += softback_end - softback_buf;
	    if (n < softback_top) {
		softback_lines -= (softback_top - n) / conp->vc_size_row;
		n = softback_top;
	    }
        } else if (softback_curr >= softback_top && n < softback_top) {
	    softback_lines -= (softback_top - n) / conp->vc_size_row;
	    n = softback_top;
        }
    } else {
    	if (softback_curr > softback_in && n >= softback_end) {
    	    n += softback_buf - softback_end;
	    if (n > softback_in) {
		n = softback_in;
		softback_lines = 0;
	    }
	} else if (softback_curr <= softback_in && n > softback_in) {
	    n = softback_in;
	    softback_lines = 0;
	}
    }
    if (n == softback_curr)
    	return;
    softback_curr = n;
    s = (u16 *)softback_curr;
    if (s == (u16 *)softback_in)
	s = (u16 *)conp->vc_origin;
    while (count--) {
	unsigned short *start;
	unsigned short *le;
	unsigned short c;
	int x = 0;
	unsigned short attr = 1;

	start = s;
	le = advance_row(s, 1);
	do {
	    c = scr_readw(s);
	    if (attr != (c & 0xff00)) {
		attr = c & 0xff00;
		if (s > start) {
		    ps2con_putcs(conp, start, s - start, line, x);
		    x += s - start;
		    start = s;
		}
	    }
	    if (c == scr_readw(d)) {
	    	if (s > start) {
	    	    ps2con_putcs(conp, start, s - start, line, x);
		    x += s - start + 1;
		    start = s + 1;
	    	} else {
	    	    x++;
	    	    start++;
	    	}
	    }
	    s++;
	    d++;
	} while (s < le);
	if (s > start)
	    ps2con_putcs(conp, start, s - start, line, x);
	line++;
	if (d == (u16 *)softback_end)
	    d = (u16 *)softback_buf;
	if (d == (u16 *)softback_in)
	    d = (u16 *)conp->vc_origin;
	if (s == (u16 *)softback_end)
	    s = (u16 *)softback_buf;
	if (s == (u16 *)softback_in)
	    s = (u16 *)conp->vc_origin;
    }
}

static inline void ps2con_softback_note(struct vc_data *conp, int t, int count)
{
    unsigned short *p;

    if (conp->vc_num != fg_console)
	return;
    p = (unsigned short *)(conp->vc_origin + t * conp->vc_size_row);

    while (count) {
    	scr_memcpyw((u16 *)softback_in, p, conp->vc_size_row);
    	count--;
    	p = advance_row(p, 1);
    	softback_in += conp->vc_size_row;
    	if (softback_in == softback_end)
    	    softback_in = softback_buf;
    	if (softback_in == softback_top) {
    	    softback_top += conp->vc_size_row;
    	    if (softback_top == softback_end)
    	    	softback_top = softback_buf;
    	}
    }
    softback_curr = softback_in;
}
#endif

static int ps2con_scroll(struct vc_data *conp, int t, int b, int dir,
			 int count)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];

    if (!p->can_soft_blank && console_blanked)
	return 0;

    if (!count || conp->vc_mode != KD_TEXT)
	return 0;

    ps2con_cursor(conp, CM_ERASE);

    switch (dir) {
    case SM_UP:
	if (count > conp->vc_rows)
	    count = conp->vc_rows;
#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
	if (softback_top)
	    ps2con_softback_note(conp, t, count);
#endif
	ps2con_bmove(conp, t + count, 0, t, 0, b - t - count, conp->vc_cols);
	ps2con_clear(conp, b - count, 0, count, conp->vc_cols);
	break;

    case SM_DOWN:
	if (count > conp->vc_rows)	/* Maximum realistic size */
	    count = conp->vc_rows;
	ps2con_bmove(conp, t, 0, t + count, 0, b - t - count, conp->vc_cols);
	ps2con_clear(conp, t, 0, count, conp->vc_cols);
	break;
    }

    return 0;
}


static void ps2con_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
			 int height, int width)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    u64 *gsp;

    if (!p->can_soft_blank && console_blanked)
	return;

    if (height == 0 || width == 0)
	return;

    if (((sy <= p->cursor_y) && (p->cursor_y < sy + height) &&
	 (sx <= p->cursor_x) && (p->cursor_x < sx + width)) ||
	((dy <= p->cursor_y) && (p->cursor_y < dy + height) &&
	 (dx <= p->cursor_x) && (p->cursor_x < dx + width)))
	ps2con_cursor(conp, CM_ERASE|CM_SOFTBACK);

    if ((gsp = ps2con_gsp_alloc(ALIGN16(10 * 8), NULL)) == NULL)
	return;

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 1, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
    *gsp++ = 0x0e;	/* A+D */
    *gsp++ = (u64)p->info.fbp |
	((u64)p->fbw << 16) | ((u64)p->info.psm << 24) |
	((u64)p->info.fbp << 32) |
	((u64)p->fbw << 48) | ((u64)p->info.psm << 56);
    *gsp++ = PS2_GS_BITBLTBUF;

    if (sy > dy || (sy == dy && sx > dx)) {
	/* copy region LT -> RB */
	*gsp++ = PACK64(PACK32(sx * fontwidth(p),
			       sy * fontheight(p)),
			PACK32(dx * fontwidth(p),
			       dy * fontheight(p)) + (0 << 27));
    } else {
	/* copy region RB -> LT */
	*gsp++ = PACK64(PACK32(sx * fontwidth(p),
			       sy * fontheight(p)),
			PACK32(dx * fontwidth(p),
			       dy * fontheight(p)) + (3 << 27));
    }

    *gsp++ = PS2_GS_TRXPOS;
    *gsp++ = PACK64(width * fontwidth(p), height * fontheight(p));
    *gsp++ = PS2_GS_TRXREG;
    *gsp++ = 2;	/* local to local */
    *gsp++ = PS2_GS_TRXDIR;

    ps2con_gsp_send(ALIGN16(10 * 8), 0);
}


static int ps2con_switch(struct vc_data *conp)
{
    int oldcon = currcon;
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    struct ps2dpy *oldp = &ps2dpy[oldcon];

#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
    if (softback_top) {
    	int l = ps2con_softback_size / conp->vc_size_row;
	if (softback_lines)
	    ps2con_set_origin(conp);
        softback_top = softback_curr = softback_in = softback_buf;
        softback_lines = 0;

	if (l > 5)
	    softback_end = softback_buf + l * conp->vc_size_row;
	else {
	    /* Smaller scrollback makes no sense, and 0 would screw
	       the operation totally */
	    softback_top = 0;
	}
    }
#endif
    if (logo_shown >= 0) {
    	struct vc_data *conp2 = vc_cons[logo_shown].d;

    	if (conp2->vc_top == logo_lines && conp2->vc_bottom == conp2->vc_rows)
	    conp2->vc_top = 0;
    	logo_shown = -1;
    }

    currcon = unit;

    if ((unit != oldcon) &&
	!(conp->vc_mode == KD_TEXT &&
	  conp->vc_mode == KD_TEXT &&
	  memcmp(&p->info, &oldp->info, sizeof(struct ps2_screeninfo)) == 0))
	ps2gs_screeninfo(&p->info, NULL);

    if (conp->vc_mode == KD_TEXT)
	ps2con_clear_margins(conp, p);

    if (logo_shown == -2) {
	logo_shown = fg_console;
#ifdef LOGO_SUPPORT
	ps2con_show_logo(); /* This is protected above by initmem_freed */
	update_region(conp,
		      conp->vc_origin + conp->vc_size_row * conp->vc_top,
		      conp->vc_size_row * (conp->vc_bottom - conp->vc_top) / 2);
#endif
	return 0;
    }
    return 1;
}


static int ps2con_blank(struct vc_data *conp, int blank, int mode_switch)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];

    /* TBD: Parameter mode_switch was added, check. */
    if (blank < 0)	/* Entering graphics mode */
	return 0;

    ps2con_cursor(p->conp, blank ? CM_ERASE : CM_DRAW);

    if (!p->can_soft_blank) {
	if (blank) {
	    ps2con_clear(conp, 0, 0, conp->vc_rows, conp->vc_cols);
	    return 0;
	} else {
	    /* Tell console.c that it has to restore the screen itself */
	    return 1;
	}
    }

    if (blank) {
	ps2gs_blank(1);
	ps2gs_setdpms(blank - 1);
    } else {
	ps2gs_blank(0);
	ps2gs_setdpms(0);
    }
    return 0;
}


static void ps2con_free_font(struct ps2dpy *p)
{
    if (p->userfont && p->fontdata &&
        (--REFCOUNT(p->fontdata) == 0))
	kfree(p->fontdata - FONT_EXTRA_WORDS*sizeof(int));
    p->fontdata = NULL;
    p->userfont = 0;
}

static int ps2con_get_font(struct vc_data *vc, struct console_font *font)
{
    int unit = vc->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    u8 *data = font->data;
    const u8 *fontdata = p->fontdata;
    int i, j;

#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
    if (fontwidth(p) != 8) return -EINVAL;
#endif
    font->width = fontwidth(p);
    font->height = fontheight(p);
    font->charcount = (p->charmask == 0x1ff) ? 512 : 256;
    if (!font->data) return 0;
    
    if (font->width <= 8) {
	j = fontheight(p);
    	for (i = 0; i < font->charcount; i++) {
	    memcpy(data, fontdata, j);
	    memset(data+j, 0, 32-j);
	    data += 32;
	    fontdata += j;
	}
    }
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
    else if (font->width <= 16) {
	j = fontheight(p) * 2;
	for (i = 0; i < font->charcount; i++) {
	    memcpy(data, fontdata, j);
	    memset(data+j, 0, 64-j);
	    data += 64;
	    fontdata += j;
	}
    } else if (font->width <= 24) {
	for (i = 0; i < font->charcount; i++) {
	    for (j = 0; j < fontheight(p); j++) {
		*data++ = fontdata[0];
		*data++ = fontdata[1];
		*data++ = fontdata[2];
		fontdata += sizeof(u32);
	    }
	    memset(data, 0, 3*(32-j));
	    data += 3 * (32 - j);
	}
    } else {
	j = fontheight(p) * 4;
	for (i = 0; i < font->charcount; i++) {
	    memcpy(data, fontdata, j);
	    memset(data+j, 0, 128-j);
	    data += 128;
	    fontdata += j;
	}
    }
#endif
    return 0;
}

static int ps2con_do_set_font(struct vc_data *vc, int unit, struct console_font *font, const u8 *data, int userfont)
{
    struct ps2dpy *p = &ps2dpy[unit];
    int resize;
    int w = font->width;
    int h = font->height;
    int cnt;
    const char *old_data = NULL;

#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
    if (CON_IS_VISIBLE(p->conp) && softback_lines)
	ps2con_set_origin(p->conp);
#endif
	
    resize = (w != fontwidth(p)) || (h != fontheight(p));
    if (p->userfont)
        old_data = p->fontdata;
    if (userfont)
        cnt = FNTCHARCNT(data);
    else
    	cnt = 256;
    p->fontdata = data;
    if ((p->userfont = userfont))
        REFCOUNT(data)++;
    p->_fontwidth = w;
    p->_fontheight = h;
    p->grayfont = 0;
    if (p->conp->vc_hi_font_mask && cnt == 256) {
    	p->conp->vc_hi_font_mask = 0;
    	if (p->conp->vc_can_do_color)
	    p->conp->vc_complement_mask >>= 1;
    	p->fgshift--;
    	p->bgshift--;
    	p->charmask = 0xff;

	/* ++Edmund: reorder the attribute bits */
	if (p->conp->vc_can_do_color) {
	    struct vc_data *conp = p->conp;
	    unsigned short *cp = (unsigned short *) conp->vc_origin;
	    int count = conp->vc_screenbuf_size/2;
	    unsigned short c;
	    for (; count > 0; count--, cp++) {
	        c = scr_readw(cp);
		scr_writew(((c & 0xfe00) >> 1) | (c & 0xff), cp);
	    }
	    c = conp->vc_video_erase_char;
	    conp->vc_video_erase_char = ((c & 0xfe00) >> 1) | (c & 0xff);
	    conp->vc_attr >>= 1;
	}

    } else if (!p->conp->vc_hi_font_mask && cnt == 512) {
    	p->conp->vc_hi_font_mask = 0x100;
    	if (p->conp->vc_can_do_color)
	    p->conp->vc_complement_mask <<= 1;
    	p->fgshift++;
    	p->bgshift++;
    	p->charmask = 0x1ff;

	/* ++Edmund: reorder the attribute bits */
	{
	    struct vc_data *conp = p->conp;
	    unsigned short *cp = (unsigned short *) conp->vc_origin;
	    int count = conp->vc_screenbuf_size/2;
	    unsigned short c;
	    for (; count > 0; count--, cp++) {
	        unsigned short newc;
	        c = scr_readw(cp);
		if (conp->vc_can_do_color)
		    newc = ((c & 0xff00) << 1) | (c & 0xff);
		else
		    newc = c & ~0x100;
		scr_writew(newc, cp);
	    }
	    c = conp->vc_video_erase_char;
	    if (conp->vc_can_do_color) {
		conp->vc_video_erase_char = ((c & 0xff00) << 1) | (c & 0xff);
		conp->vc_attr <<= 1;
	    } else
	        conp->vc_video_erase_char = c & ~0x100;
	}

    }
    if (resize) {
    	struct vc_data *conp = p->conp;
	vc_resize(vc, p->info.w / w, p->info.h / h);
#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
        if (CON_IS_VISIBLE(conp) && softback_buf) {
	    int l = ps2con_softback_size / conp->vc_size_row;
	    if (l > 5)
		softback_end = softback_buf + l * conp->vc_size_row;
	    else {
		/* Smaller scrollback makes no sense, and 0 would screw
		   the operation totally */
		softback_top = 0;
    	    }
    	}
#endif
    } else if (CON_IS_VISIBLE(p->conp) && vc->vc_mode == KD_TEXT) {
	ps2con_clear_margins(p->conp, p);
	update_screen(p->conp);
    }

    if (old_data && (--REFCOUNT(old_data) == 0))
	kfree(old_data - FONT_EXTRA_WORDS*sizeof(int));

    return 0;
}

static int ps2con_copy_font(struct vc_data *vc, int con)
{
    int unit = vc->vc_num;
    struct ps2dpy *od, *p = &ps2dpy[unit];
    struct console_font *font = &vc->vc_font;
    int h = font->height;

    if (h < 0 || !vc_cons_allocated( h ))
        return -ENOTTY;
    if (h == unit)
        return 0; /* nothing to do */
    od = &ps2dpy[h];
    if (od->fontdata == p->fontdata)
        return 0; /* already the same font... */
    font->width = fontwidth(od);
    font->height = fontheight(od);
    return ps2con_do_set_font(vc, unit, font, od->fontdata, od->userfont);
}

static int ps2con_set_font(struct vc_data *vc, struct console_font *font, unsigned flags)
{
    int unit = vc->vc_num;
    int w = font->width;
    int h = font->height;
    int size = h;
    int i, k;
    u8 *new_data;
    u8 *data = font->data;
	u8 *p;

	data = font->data;

    /* TBD: Check additonal parameter flags. */
#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
    if (w != 8)
    	return -EINVAL;
#endif
    if ((w <= 0) || (w > 32) || (font->charcount != 256 && font->charcount != 512))
        return -EINVAL;

    if (w > 8) { 
    	if (w <= 16)
    		size *= 2;
    	else
    		size *= 4;
    }
    size *= font->charcount;
       
    if (!(new_data = kmalloc(FONT_EXTRA_WORDS*sizeof(int)+size, GFP_USER)))
        return -ENOMEM;
    new_data += FONT_EXTRA_WORDS*sizeof(int);
    FNTSIZE(new_data) = size;
    FNTCHARCNT(new_data) = font->charcount;
    REFCOUNT(new_data) = 0; /* usage counter */
    p = new_data;
    if (w <= 8) {
	for (i = 0; i < font->charcount; i++) {
	    memcpy(p, data, h);
	    data += 32;
	    p += h;
	}
    }
#ifndef CONFIG_FBCON_FONTWIDTH8_ONLY
    else if (w <= 16) {
	h *= 2;
	for (i = 0; i < font->charcount; i++) {
	    memcpy(p, data, h);
	    data += 64;
	    p += h;
	}
    } else if (w <= 24) {
	for (i = 0; i < font->charcount; i++) {
	    int j;
	    for (j = 0; j < h; j++) {
	        memcpy(p, data, 3);
		p[3] = 0;
		data += 3;
		p += sizeof(u32);
	    }
	    data += 3*(32 - h);
	}
    } else {
	h *= 4;
	for (i = 0; i < font->charcount; i++) {
	    memcpy(p, data, h);
	    data += 128;
	    p += h;
	}
    }
#endif
    /* we can do it in u32 chunks because of charcount is 256 or 512, so
       font length must be multiple of 256, at least. And 256 is multiple
       of 4 */
    k = 0;
    while (p > new_data) {
	p = (u8 *) ((u32 *)p - 1);
	k += *(u32 *)p;
    }
    FNTSUM(new_data) = k;
    /* Check if the same font is on some other console already */
    for (i = 0; i < MAX_NR_CONSOLES; i++) {
    	if (ps2dpy[i].userfont &&
    	    ps2dpy[i].fontdata &&
    	    FNTSUM(ps2dpy[i].fontdata) == k &&
    	    FNTSIZE(ps2dpy[i].fontdata) == size &&
	    !memcmp(ps2dpy[i].fontdata, new_data, size)) {
	    kfree(new_data - FONT_EXTRA_WORDS*sizeof(int));
    	return ps2con_do_set_font(vc, unit, font, ps2dpy[i].fontdata, 1);
    	}
    }
    return ps2con_do_set_font(vc, unit, font, new_data, 1);
}

static int ps2con_set_def_font(struct vc_data *vc, struct console_font *font, char *name)
{
    int unit = vc->vc_num;
    const struct font_desc *f;
    struct ps2dpy *p = &ps2dpy[unit];

    if (!name)
	f = get_default_font(p->info.w, p->info.h,
		(1 << (8 - 1)),
		(1 << (8 - 1)) | (1 << (16 - 1)));
    else if (!(f = find_font(name)))
	return -ENOENT;

    font->width = f->width;
    font->height = f->height;
    return ps2con_do_set_font(vc, unit, font, f->data, 0);
}

static int ps2con_set_palette(struct vc_data *conp, unsigned char *table)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    int i, j, k;
    unsigned int palette;
    
    if (!conp->vc_can_do_color || (!p->can_soft_blank && console_blanked))
	return -EINVAL;
    for (i = j = 0; i < 16; i++) {
	k = table[i];
	palette = conp->vc_palette[j++];			/* R */
	palette = palette | (conp->vc_palette[j++] << 8);	/* G */
	palette = palette | (conp->vc_palette[j++] << 16);	/* B */
	ps2_cmap[k] = palette;
    }

    return 0;
}


/* As we might be inside of softback, we may work with non-contiguous buffer,
   that's why we have to use a separate routine. */
static void ps2con_invert_region(struct vc_data *conp, u16 *p, int cnt)
{
    while (cnt--) {
	u16 a = scr_readw(p);
	if (!conp->vc_can_do_color)
	    a ^= 0x0800;
	else if (conp->vc_hi_font_mask == 0x100)
	    a = ((a) & 0x11ff) | (((a) & 0xe000) >> 4) | (((a) & 0x0e00) << 4);
	else
	    a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
	scr_writew(a, p++);
#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
	if (p == (u16 *)softback_end)
	    p = (u16 *)softback_buf;
	if (p == (u16 *)softback_in)
	    p = (u16 *)conp->vc_origin;
#endif
    }
}

static int ps2con_scrolldelta(struct vc_data *conp, int lines)
{
    int unit;
    struct ps2dpy *p;
    
    unit = fg_console;
    p = &ps2dpy[unit];
#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
    if (softback_top) {
    	if (conp->vc_num != unit)
    	    return 0;
    	if (conp->vc_mode != KD_TEXT || !lines)
    	    return 0;
    	if (logo_shown >= 0) {
		struct vc_data *conp2 = vc_cons[logo_shown].d;
    	
		if (conp2->vc_top == logo_lines && conp2->vc_bottom == conp2->vc_rows)
    		    conp2->vc_top = 0;
    		if (logo_shown == unit) {
    		    unsigned long p, q;
    		    int i;
    		    
    		    p = softback_in;
    		    q = conp->vc_origin + logo_lines * conp->vc_size_row;
    		    for (i = 0; i < logo_lines; i++) {
    		    	if (p == softback_top) break;
    		    	if (p == softback_buf) p = softback_end;
    		    	p -= conp->vc_size_row;
    		    	q -= conp->vc_size_row;
    		    	scr_memcpyw((u16 *)q, (u16 *)p, conp->vc_size_row);
    		    }
    		    softback_in = p;
    		    update_region(conp, conp->vc_origin, logo_lines * conp->vc_cols);
    		}
		logo_shown = -1;
	}
    	ps2con_cursor(conp, CM_ERASE|CM_SOFTBACK);
    	ps2con_redraw_softback(conp, p, lines);
    	ps2con_cursor(conp, CM_DRAW|CM_SOFTBACK);
    	return 0;
    }
#endif

    return -ENOSYS;
}

static int ps2con_set_origin(struct vc_data *conp)
{
#ifdef CONFIG_PS2_CONSOLE_SCROLLBACK
    if (softback_lines && !console_blanked)
        ps2con_scrolldelta(conp, softback_lines);
#endif
    return 0;
}

static void ps2con_revc(struct vc_data *conp, int sy, int sx,
			 int height, int width)
{
    int unit = conp->vc_num;
    struct ps2dpy *p = &ps2dpy[unit];
    u64 *gsp;
    int ctx = 0;

    if (height == 0 || width == 0)
	return;

    if ((gsp = ps2con_gsp_alloc(ALIGN16(9 * 8), NULL)) == NULL)
	return;

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(1, 0, 1, 0x046 + (ctx << 9), PS2_GIFTAG_FLG_PACKED, 1);
    *gsp++ = 0x0e;	/* A+D */
    *gsp++ = (0 << 0) | (1 << 2) | (2 << 4) | (3 << 6) | ((u64)0x80 << 32);
					    /* (Cs - Cd) * 0x80 + 0 */
    *gsp++ = PS2_GS_ALPHA_1 + ctx;

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(1, 1, 0, 0, PS2_GIFTAG_FLG_REGLIST, 3);
    *gsp++ = 0x551;
    *gsp++ = 0xffffff;						/* RGBAQ */
    *gsp++ = PACK32(sx * fontwidth(p) * 16, sy * fontheight(p) * 16);
								/* XYZ2 */
    *gsp++ = PACK32((sx + width) * fontwidth(p) * 16,
		    (sy + height) * fontheight(p) * 16);	/* XYZ2 */

    ps2con_gsp_send(ALIGN16(9 * 8), 0);
}

static void ps2con_clear_margins(struct vc_data *conp, struct ps2dpy *p)
{
    unsigned int right_start = conp->vc_cols * fontwidth(p);
    unsigned int bottom_start = conp->vc_rows * fontheight(p);
    u64 *gsp;
    int ctx = 0;

    if ((gsp = ps2con_gsp_alloc(ALIGN16(8 * 8), NULL)) == NULL)
	return;

    *gsp++ = PS2_GIFTAG_SET_TOPHALF(1, 1, 0, 0, PS2_GIFTAG_FLG_REGLIST, 6);
    *gsp++ = 0x555510;
    *gsp++ = 0x006 + (ctx << 9);				/* PRIM */
    *gsp++ = ps2_cmap[attr_bgcol_ec(p, conp)];			/* RGBAQ */
    *gsp++ = PACK32(right_start * 16, 0);			/* XYZ2 */
    *gsp++ = PACK32(p->info.w * 16, p->info.h * 16);		/* XYZ2 */
    *gsp++ = PACK32(0, bottom_start * 16);			/* XYZ2 */
    *gsp++ = PACK32(p->info.w * 16, p->info.h * 16);		/* XYZ2 */

    ps2con_gsp_send(ALIGN16(8 * 8), 0);
}

#ifdef LOGO_SUPPORT
static int __init ps2con_show_logo( void )
{
    struct ps2dpy *p = &ps2dpy[fg_console]; /* draw to vt in foreground */
    u64 *gsp, *gsp_h;
    u32 *ptr, val;
    unsigned char *src;
    int i, x, y;
    int lines_left, lines, size;
    int logo_h = LOGO_H;

    if (is_nointer(p))
	logo_h /= 2;

    for (x = 0; x < smp_num_cpus * (LOGO_W + 8) &&
	     x < p->info.w - (LOGO_W + 8); x += (LOGO_W + 8)) {

	lines_left = logo_h;
	src = linux_logo;
	y = 0;

	while (lines_left > 0) {
	    gsp_h = gsp = ps2con_gsp_alloc(ALIGN16(12 * 8 + LOGO_W * p->pixel_size), &size);
	    if (gsp == NULL)
		return 0;
	    lines = (size - ALIGN16(12 * 8)) / (LOGO_W * p->pixel_size);
	    lines = lines > lines_left ? lines_left : lines;

	    *gsp++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
	    *gsp++ = 0x0e;		/* A+D */
	    *gsp++ = (u64)0 | ((u64)p->info.fbp << 32) |
		((u64)p->fbw << 48) | ((u64)p->info.psm << 56);
	    *gsp++ = PS2_GS_BITBLTBUF;
	    *gsp++ = PACK64(0, PACK32(x, y));
	    *gsp++ = PS2_GS_TRXPOS;
	    *gsp++ = PACK64(LOGO_W, lines);
	    *gsp++ = PS2_GS_TRXREG;
	    *gsp++ = 0;			/* host to local */
	    *gsp++ = PS2_GS_TRXDIR;

	    *gsp++ = PS2_GIFTAG_SET_TOPHALF(ALIGN16(LOGO_W * lines * p->pixel_size) / 16,
				     1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	    *gsp++ = 0;
	    ptr = (u32 *)gsp;

	    while (lines-- > 0) {
		lines_left--;
		y++;

		for(i = 0; i < LOGO_W; i++, src++) {
		    switch (p->pixel_size) {
		    case 4:		/* RGBA32 */
			val = (linux_logo_red[*src - 32] << 0) |
			    (linux_logo_green[*src - 32] << 8) |
			    (linux_logo_blue[*src - 32] << 16);
			*ptr++ = val;
			break;
		    case 2:		/* RGBA16 */
			val = ((linux_logo_red[*src - 32] & 0xf8) >> 3) |
			    ((linux_logo_green[*src - 32] & 0xf8) << (5 - 3)) |
			    ((linux_logo_blue[*src - 32] & 0xf8) << (10 - 3));
			*((u16 *)ptr) = val;
			ptr = (void *) ((u16 *)ptr) + 1;
			break;
		    case 3:		/* RGB24 */
			val = (linux_logo_red[*src - 32] << 0) |
			    (linux_logo_green[*src - 32] << 8) |
			    (linux_logo_blue[*src - 32] << 16);
			*((u8 *)ptr) = ((unsigned char *)&val)[0];
			ptr = (void *) ((u8 *)ptr) + 1;
			*((u8 *)ptr) = ((unsigned char *)&val)[1];
			ptr = (void *) ((u8 *)ptr) + 1;
			*((u8 *)ptr) = ((unsigned char *)&val)[2];
			ptr = (void *) ((u8 *)ptr) + 1;
			break;
		    }
		}
		if (is_nointer(p))
		    src += LOGO_W;
	    }

	    ps2con_gsp_send(ALIGN16((unsigned char *)ptr - (unsigned char *)gsp_h), 0);
	}
   }
    
    return (logo_h + fontheight(p) - 1) / fontheight(p);
}
#endif

/*
 *  The console `switch' structure for the PS2 Graphics Synthesizer console
 */

const struct consw ps2_con = {
    owner:		THIS_MODULE,
    con_startup:	ps2con_startup,
    con_init:		ps2con_init,
    con_deinit:		ps2con_deinit,
    con_clear:		ps2con_clear,
    con_putc:		ps2con_putc,
    con_putcs:		ps2con_putcs,
    con_cursor:		ps2con_cursor,
    con_scroll:		ps2con_scroll,
    con_bmove:		ps2con_bmove,
    con_switch:		ps2con_switch,
    con_blank:		ps2con_blank,
    con_font_set:	ps2con_set_font,
    con_font_get:	ps2con_get_font,
    con_font_default:	ps2con_set_def_font,
    con_font_copy:	ps2con_copy_font,
    con_set_palette:	ps2con_set_palette,
    con_scrolldelta:	ps2con_scrolldelta,
    con_set_origin:	ps2con_set_origin,
    con_save_screen:	NULL,
    con_build_attr:	NULL,
    con_invert_region:	ps2con_invert_region,
#if 0 /* TBD: Check missing calls. */
	.con_screen_pos 	= fbcon_screen_pos,
	.con_getxy 		= fbcon_getxy,
	.con_resize             = fbcon_resize,
#endif
};

static int graphics_boot = 0;

static int graphics_setup(char *options)
{
    graphics_boot = 1;
    return 0;
}

__setup("graphics", graphics_setup);

int __init ps2_console_init(void)
{
    if (!graphics_boot) {
	take_over_console(&ps2_con, 0, MAX_NR_CONSOLES - 1, 1);
    } else {
	/* get current crtmode */
	struct ps2_crtmode crtmode;

	ps2con_initinfo(&defaultinfo);
	if (defaultinfo.mode == PS2_GS_NTSC ||
	    defaultinfo.mode == PS2_GS_PAL) {
	    crtmode.mode = defaultinfo.mode;
	    crtmode.res = -1;			/* set current mode */
	    ps2gs_crtmode(&crtmode, NULL);
	}
    }

    return 0;
}

module_init(ps2_console_init)
