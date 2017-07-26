/*
 *  PlayStation 2 Graphic console
 *
 *  Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 *  Copyright (C) 2010-2013 Juergen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _VIDEO_PS2CON_H
#define _VIDEO_PS2CON_H

/* TBD: Please use framebuffer driver instead! */
#include <linux/types.h>
#include <linux/kd.h>
#include <linux/tty.h>
#include <linux/console_struct.h>
#include <linux/vt_buffer.h>
#include <linux/ps2/dev.h>
#include <asm/io.h>

/*
 *  PlayStation 2 display structure
 */

struct ps2dpy {
    struct ps2_screeninfo info;
    int fbw;				/* frame buffer width */
    int pixel_size;			/* # of bytes per pixel (2/3/4) */
    unsigned int cmap[16];		/* colormap */
    unsigned short can_soft_blank;	/* zero if no hardware blanking */

    struct vc_data *conp;		/* pointer to console data */
    unsigned short cursor_x;		/* current cursor position */
    unsigned short cursor_y;
    int fgcol;				/* text colors */
    int bgcol;
    const unsigned char *fontdata;		/* Font associated to this display */
    unsigned short _fontheight;
    unsigned short _fontwidth;
    int grayfont;			/* != 0 if font is 4bpp grayscale */
    int userfont;			/* != 0 if fontdata kmalloc()ed */
    unsigned char fgshift, bgshift;
    unsigned short charmask;		/* 0xff or 0x1ff */
};

#define fontwidth(p)	((p)->_fontwidth)
#define fontheight(p)	((p)->_fontheight)

#define attr_fgcol(p,s)		\
	(((s) >> ((p)->fgshift)) & 0x0f)
#define attr_bgcol(p,s)		\
	(((s) >> ((p)->bgshift)) & 0x0f)
#define attr_bgcol_ec(p,conp)	\
	((conp) ? (((conp)->vc_video_erase_char >> ((p)->bgshift)) & 0x0f) : 0)


#define is_nointer(p)	((p->info.mode == PS2_GS_NTSC || p->info.mode == PS2_GS_PAL) && !(p->info.res & PS2_GS_INTERLACE))

#define bpp32to16(col)	\
	((((col) & 0xf8) >> (8 - 5)) + (((col) & 0xf800) >> (16 - 10)) + \
	 (((col) & 0xf80000) >> (24 - 15)) + ((col & 0x80000000) >> 16))

/* function prototypes */

void ps2con_initinfo(struct ps2_screeninfo *info);
void ps2con_gsp_init(void);
u64 *ps2con_gsp_alloc(int request, int *avail);
void ps2con_gsp_send(int len, int flushall);
int ps2con_get_resolution(int mode, int w, int h, int rate);

#endif /* _VIDEO_PS2CON_H */
