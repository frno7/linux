/*
 *  PlayStation 2 Game Controller driver
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

#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/sifdefs.h>
#include <linux/ps2/pad.h>

#define PadStateDiscon		(0)
#define PadStateFindPad		(1)
#define PadStateFindCTP1	(2)
#define PadStateExecCmd		(5)
#define PadStateStable		(6)
#define PadStateError		(7)

#define PadReqStateComplete	(0)
#define PadReqStateFaild	(1)
#define PadReqStateFailed	(1)
#define PadReqStateBusy		(2)

#define PS2PAD_NPORTS            2
#define PS2PAD_NSLOTS            1 /* currently, we doesn't support multitap */
#define PS2PAD_MAXNPADS          8

struct ps2pad_libctx {
	int port, slot;
	void *dmabuf;
};

extern struct ps2pad_libctx ps2pad_pads[];
extern int ps2pad_npads;

void ps2pad_js_init(void);
void ps2pad_js_quit(void);

int ps2pad_stat_conv(int stat);
