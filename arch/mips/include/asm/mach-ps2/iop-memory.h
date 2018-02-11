/*
 * PlayStation 2 I/O processor (IOP) memory
 *
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_IOP_MEMORY_H
#define __ASM_PS2_IOP_MEMORY_H

#include <linux/types.h>

dma_addr_t iop_phys_to_bus(phys_addr_t paddr);
phys_addr_t iop_bus_to_phys(dma_addr_t baddr);

dma_addr_t iop_alloc(size_t size);
int iop_free(dma_addr_t baddr);

#endif /* __ASM_PS2_IOP_MEMORY_H */
