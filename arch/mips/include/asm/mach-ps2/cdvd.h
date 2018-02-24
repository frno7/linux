/*
 * PlayStation 2 CD/DVD driver
 *
 * Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_CDVD_H
#define __ASM_PS2_CDVD_H

#include <linux/types.h>

int cdvd_read_rtc(unsigned long *t);
int cdvd_write_rtc(unsigned long t);

int __init cdvd_init(void);

#endif /* __ASM_PS2_CDVD_H */
