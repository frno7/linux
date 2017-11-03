/*
 *  PlayStation 2 Ethernet
 *
 *  Copyright (C) 2001      Sony Computer Entertainment Inc.
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

#ifndef __ASM_PS2_SPEED_H
#define __ASM_PS2_SPEED_H

#define DEV9M_BASE		0x14000000

#define SPD_R_REV		(DEV9M_BASE + 0x00)
#define SPD_R_REV_1		(DEV9M_BASE + 0x00)
#define SPD_R_REV_3		(DEV9M_BASE + 0x04)

#define SPD_R_INTR_STAT		(DEV9M_BASE + 0x28)
#define SPD_R_INTR_ENA		(DEV9M_BASE + 0x2a)
#define SPD_R_XFR_CTRL		(DEV9M_BASE + 0x32)
#define SPD_R_IF_CTRL		(DEV9M_BASE + 0x64)

#endif /* __ASM_PS2_SPEED_H */

