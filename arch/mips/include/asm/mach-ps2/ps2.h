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

#define INTC_STAT	0x1000f000	/* Flags are cleared by writing 1. */
#define INTC_MASK	0x1000f010	/* Bits are reversed by writing 1. */

int ps2rtc_init(void);

#endif /* __ASM_PS2_PS2_H */
