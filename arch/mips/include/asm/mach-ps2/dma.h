/*
 * PlayStation 2 DMA
 *
 * Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_DMA_H
#define __ASM_PS2_DMA_H

#define DMA_TRUNIT	16
#define DMA_ALIGN(x)	((__typeof__(x))(((unsigned long)(x) + (DMA_TRUNIT - 1)) & ~(DMA_TRUNIT - 1)))

#endif /* __ASM_PS2_DMA_H */
