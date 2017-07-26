/*
 *  PlayStation 2
 *
 *  Copyright (C) 2010-2013 Jürgen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#ifndef __ASM_PS2_PS2_H
#define __ASM_PS2_PS2_H

#include <linux/kernel.h>

/* Base address for hardware. */
#define PS2_HW_BASE 0x10000000

/* Base address for IOP memory. */
#define PS2_IOP_HEAP_BASE 0x1c000000
/* IOP has 2 MiB (the TEST device has more). */
#define PS2_IOP_HEAP_SIZE 0x200000

extern int ps2_pccard_present;
extern int ps2_pcic_type;
extern struct ps2_sysconf *ps2_sysconf;

extern int ps2sif_initiopheap(void);
extern int ps2rtc_init(void);

#endif /* __ASM_PS2_PS2_H */
