/*
 *  PlayStation 2
 *
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
#ifndef __ASM_PS2_PS2_H
#define __ASM_PS2_PS2_H

#include <linux/kernel.h>

/* Device name of PS2 SBIOS serial device. */
#define PS2_SBIOS_SERIAL_DEVICE_NAME "ttyS"

/* Base address for hardware. */
#define PS2_HW_BASE 0x10000000

/* Base address for IOP memory. */
#define PS2_IOP_HEAP_BASE 0x1c000000

extern int ps2_pccard_present;
extern int ps2_pcic_type;
extern struct ps2_sysconf *ps2_sysconf;

extern void prom_putchar(char);
extern int ps2_printf(const char *fmt, ...);
extern int ps2sif_initiopheap(void);
extern int ps2rtc_init(void);
int ps2_powerbutton_init(void);
int ps2_powerbutton_enable_auto_shutoff(int enable_auto_shutoff);

#endif
