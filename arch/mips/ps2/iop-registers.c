/*
 * PlayStation 2 I/O processor (IOP) registers
 *
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <asm/io.h>
#include <linux/spinlock.h>

#include <asm/mach-ps2/iop-registers.h>

#define IOP_DMA_DPCR2	0x1f801570

static DEFINE_SPINLOCK(reg_lock);

void iop_set_dma_dpcr2(const u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&reg_lock, flags);
	outl(inl(IOP_DMA_DPCR2) | mask, IOP_DMA_DPCR2);
	spin_unlock_irqrestore(&reg_lock, flags);
}
EXPORT_SYMBOL(iop_set_dma_dpcr2);

void iop_clr_dma_dpcr2(const u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&reg_lock, flags);
	outl(inl(IOP_DMA_DPCR2) & ~mask, IOP_DMA_DPCR2);
	spin_unlock_irqrestore(&reg_lock, flags);
}
EXPORT_SYMBOL(iop_clr_dma_dpcr2);
