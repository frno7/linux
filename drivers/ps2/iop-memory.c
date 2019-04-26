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

/**
 * iop_phys_to_bus - kernel physical to I/O processor (IOP) bus address
 * @paddr: kernel physical address
 *
 * Context: any
 * Return: I/O processor (IOP) bus address
 */
iop_addr_t iop_phys_to_bus(phys_addr_t paddr)
{
	return (u32)paddr - IOP_RAM_BASE;
}
EXPORT_SYMBOL(iop_phys_to_bus);

/**
 * iop_bus_to_phys - I/O processor (IOP) bus address to kernel physical
 * @baddr: I/O processor (IOP) bus address
 *
 * Context: any
 * Return: kernel physical address
 */
phys_addr_t iop_bus_to_phys(iop_addr_t baddr)
{
	return (u32)baddr + IOP_RAM_BASE;
}
EXPORT_SYMBOL(iop_bus_to_phys);

/**
 * iop_bus_to_virt - I/O processor (IOP) bus address to kernel virtual
 * @baddr: I/O processor (IOP) bus address
 *
 * Context: any
 * Return: kernel virtual address
 */
void *iop_bus_to_virt(iop_addr_t baddr)
{
	return phys_to_virt(iop_bus_to_phys(baddr));
}
EXPORT_SYMBOL(iop_bus_to_virt);

MODULE_DESCRIPTION("PlayStation 2 input/output processor (IOP) memory");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
