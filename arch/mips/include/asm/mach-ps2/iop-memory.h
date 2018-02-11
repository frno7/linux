// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) memory operations
 *
 * Copyright (C) 2018 Fredrik Noring
 */

#ifndef PS2_IOP_MEMORY_H
#define PS2_IOP_MEMORY_H

#include <linux/types.h>

typedef u32 iop_addr_t;

iop_addr_t iop_phys_to_bus(phys_addr_t paddr);
phys_addr_t iop_bus_to_phys(iop_addr_t baddr);
void *iop_bus_to_virt(iop_addr_t baddr);

int iop_read_memory(void *dst, const iop_addr_t src, size_t nbyte);
int iop_write_memory(iop_addr_t dst, const void *src, size_t nbyte);

iop_addr_t iop_alloc(size_t nbyte);
int iop_free(iop_addr_t baddr);

#endif /* PS2_IOP_MEMORY_H */
