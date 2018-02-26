/*
 * PlayStation 2 sub-system interface (SIF)
 *
 * The sub-system interface is an interface unit to the I/O processor (IOP).
 *
 * Copyright (C) 2001      Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_SIF_H
#define __ASM_PS2_SIF_H

#include <linux/types.h>
#include "sbios.h"

int __init sif_init(void);
void sif_exit(void);

#endif /* __ASM_PS2_SIF_H */
