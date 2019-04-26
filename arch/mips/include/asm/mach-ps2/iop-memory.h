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

/**
 * iop_phys_to_bus - kernel physical to I/O processor (IOP) bus address
 * @paddr: kernel physical address
 *
 * Context: any
 * Return: I/O processor (IOP) bus address
 */
iop_addr_t iop_phys_to_bus(phys_addr_t paddr);

/**
 * iop_bus_to_phys - I/O processor (IOP) bus address to kernel physical
 * @baddr: I/O processor (IOP) bus address
 *
 * Context: any
 * Return: kernel physical address
 */
phys_addr_t iop_bus_to_phys(iop_addr_t baddr);

/**
 * iop_bus_to_virt - I/O processor (IOP) bus address to kernel virtual
 * @baddr: I/O processor (IOP) bus address
 *
 * Context: any
 * Return: kernel virtual address
 */
void *iop_bus_to_virt(iop_addr_t baddr);

/**
 * iop_read_memory - FIXME
 * @dst: FIXME
 * @src: FIXME
 * @nbyte: FIXME
 *
 * Context: FIXME
 * Return: FIXME
 */
int iop_read_memory(void *dst, const iop_addr_t src, size_t nbyte);

/**
 * iop_write_memory - FIXME
 * @dst: FIXME
 * @src: FIXME
 * @nbyte: FIXME
 *
 * Context: FIXME
 * Return: FIXME
 */
int iop_write_memory(iop_addr_t dst, const void *src, size_t nbyte);

#endif /* __ASM_MACH_PS2_IOP_MEMORY_H */
