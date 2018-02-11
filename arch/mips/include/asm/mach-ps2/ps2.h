/*
 * PlayStation 2
 *
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_PS2_H
#define __ASM_PS2_PS2_H

#include <linux/kernel.h>

/* Base address for hardware. */
#define PS2_HW_BASE 0x10000000

extern int ps2_pccard_present;
extern int ps2_pcic_type;
extern struct ps2_sysconf *ps2_sysconf;

int ps2rtc_init(void);

#endif /* __ASM_PS2_PS2_H */
