/*
 *  PlayStation 2 Graphic Synthesizer
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

#ifndef __ASM_PS2_GSFUNC_H
#define __ASM_PS2_GSFUNC_H

#include <linux/ps2/dev.h>
#include <asm/types.h>

int ps2gs_set_gssreg(int reg, u64 val);
int ps2gs_get_gssreg(int reg, u64 *val);
int ps2gs_set_gsreg(int reg, u64 val);

int ps2gs_crtmode(struct ps2_crtmode *crtmode, struct ps2_crtmode *old);
int ps2gs_display(int ch, struct ps2_display *display, struct ps2_display *old);
int ps2gs_dispfb(int ch, struct ps2_dispfb *dispfb, struct ps2_dispfb *old);
int ps2gs_pmode(struct ps2_pmode *pmode, struct ps2_pmode *old);
int ps2gs_screeninfo(struct ps2_screeninfo *info, struct ps2_screeninfo *old);
int ps2gs_setdpms(int mode);
int ps2gs_blank(int onoff);
int ps2gs_reset(int mode);

extern void (*ps2gs_screeninfo_hook)(struct ps2_screeninfo *info);

#endif /* __ASM_PS2_GSFUNC_H */
