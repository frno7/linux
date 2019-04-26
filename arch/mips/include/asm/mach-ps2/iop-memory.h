// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) memory
 *
 * Copyright (C) 2018 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IOP_MEMORY_H
#define __ASM_MACH_PS2_IOP_MEMORY_H

#include <linux/types.h>

#include <asm/mach-ps2/iop.h>

iop_addr_t iop_phys_to_bus(phys_addr_t paddr);

phys_addr_t iop_bus_to_phys(iop_addr_t baddr);

void *iop_bus_to_virt(iop_addr_t baddr);

#endif /* __ASM_MACH_PS2_IOP_MEMORY_H */
