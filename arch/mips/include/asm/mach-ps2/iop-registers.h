/*
 * PlayStation 2 I/O processor (IOP) registers
 *
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_IOP_REGISTERS_H
#define __ASM_PS2_IOP_REGISTERS_H

#include <linux/types.h>

#define IOP_DMA_DPCR2_OHCI	0x08000000	/* USB OHCI */
#define IOP_DMA_DPCR2_DEV9	0x00000080	/* DEV9 (Expansion Bay, USB) */

void iop_set_dma_dpcr2(const u32 mask);
void iop_clr_dma_dpcr2(const u32 mask);

#endif /* __ASM_PS2_IOP_REGISTERS_H */
