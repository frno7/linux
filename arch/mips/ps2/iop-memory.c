/*
 * PlayStation 2 I/O processor (IOP) memory
 *
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mutex.h>

#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/dma.h>

#define SBIOS_RPC_IOP_ALLOC	65
#define SBIOS_RPC_IOP_FREE	66

/* Base address for IOP memory. */
#define PS2_IOP_HEAP_BASE 0x1c000000
/* IOP has 2 MiB (the TEST device has more). */
#define PS2_IOP_HEAP_SIZE 0x200000

static DEFINE_MUTEX(iop_memory_mutex);

dma_addr_t iop_phys_to_bus(phys_addr_t paddr)
{
	return (uint32_t)paddr - PS2_IOP_HEAP_BASE;
}
EXPORT_SYMBOL(iop_phys_to_bus);

phys_addr_t iop_bus_to_phys(dma_addr_t baddr)
{
	return (uint32_t)baddr + PS2_IOP_HEAP_BASE;
}
EXPORT_SYMBOL(iop_bus_to_phys);

dma_addr_t iop_alloc(size_t size)
{
	struct {
		uint32_t size;
	} arg = {
		/*
		 * Pad to DMA transfer unit size to ensure that allocations
		 * that follow will not be overwritten.
		 */
		.size = DMA_ALIGN(size)
	};
	int result;
	int err;

	if (arg.size < size)
		return 0;

	mutex_lock(&iop_memory_mutex);
	err = sbios_rpc(SBIOS_RPC_IOP_ALLOC, &arg, &result);
	mutex_unlock(&iop_memory_mutex);

	if (err < 0)
		return 0;

	WARN_ON((result & (DMA_TRUNIT - 1)) != 0);

	return result;
}
EXPORT_SYMBOL(iop_alloc);

int iop_free(dma_addr_t baddr)
{
	struct {
		uint32_t addr;
	} arg = {
		.addr = baddr
	};
	int result;
	int err;

	mutex_lock(&iop_memory_mutex);
	err = sbios_rpc(SBIOS_RPC_IOP_FREE, &arg, &result);
	mutex_unlock(&iop_memory_mutex);

	return err < 0 ? -1 : result;
}
EXPORT_SYMBOL(iop_free);
