// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) memory
 *
 * Copyright (C) 2018 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <asm/mach-ps2/iop-memory.h>

iop_addr_t iop_phys_to_bus(phys_addr_t paddr)
{
	return (u32)paddr - IOP_RAM_BASE;
}
EXPORT_SYMBOL(iop_phys_to_bus);

phys_addr_t iop_bus_to_phys(iop_addr_t baddr)
{
	return (u32)baddr + IOP_RAM_BASE;
}
EXPORT_SYMBOL(iop_bus_to_phys);

void *iop_bus_to_virt(iop_addr_t baddr)
{
	return phys_to_virt(iop_bus_to_phys(baddr));
}
EXPORT_SYMBOL(iop_bus_to_virt);

int iop_read_memory(void *dst, const iop_addr_t src, size_t nbyte)
{
	void *ptr = iop_bus_to_virt(src);

	dma_cache_inv((unsigned long)ptr, nbyte);
	memcpy(dst, ptr, nbyte);

	return 0;
}
EXPORT_SYMBOL(iop_read_memory);

int iop_write_memory(iop_addr_t dst, const void *src, size_t nbyte)
{
	void *ptr = iop_bus_to_virt(dst);

	memcpy(ptr, src, nbyte);
	dma_cache_wback((unsigned long)ptr, nbyte);

	return 0;
}
EXPORT_SYMBOL(iop_write_memory);

MODULE_DESCRIPTION("PlayStation 2 input/output processor (IOP) memory");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
