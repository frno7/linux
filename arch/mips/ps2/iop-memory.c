// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) memory operations
 *
 * Copyright (C) 2018 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/sif.h>

#define IOP_MEMORY_BASE_ADDRESS 0x1c000000

enum iop_fio_rpc_ops {
	rpo_alloc = 1, rpo_free = 2, rpo_load =  3
};

static struct t_SifRpcClientData iop_heap_rpc;

iop_addr_t iop_phys_to_bus(phys_addr_t paddr)
{
	return (u32)paddr - IOP_MEMORY_BASE_ADDRESS;
}
EXPORT_SYMBOL(iop_phys_to_bus);

phys_addr_t iop_bus_to_phys(iop_addr_t baddr)
{
	return (u32)baddr + IOP_MEMORY_BASE_ADDRESS;
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
	iop_addr_t addr;

	/* FIXME: Use some kind of memory barrier instead? */
	addr = dma_map_single(NULL, ptr, nbyte, DMA_FROM_DEVICE);
	if (dma_mapping_error(NULL, addr))
		return -EIO;

	memcpy(dst, ptr, nbyte);

	dma_unmap_single(NULL, addr, nbyte, DMA_FROM_DEVICE);

	return 0;
}
EXPORT_SYMBOL(iop_read_memory);

int iop_write_memory(iop_addr_t dst, const void *src, size_t nbyte)
{
	void *ptr = iop_bus_to_virt(dst);
	iop_addr_t addr;

	memcpy(ptr, src, nbyte);

	/* FIXME: Use some kind of memory barrier instead? */
	addr = dma_map_single(NULL, ptr, nbyte, DMA_TO_DEVICE);
	if (dma_mapping_error(NULL, addr))
		return -EIO;

	dma_unmap_single(NULL, addr, nbyte, DMA_TO_DEVICE);

	return 0;
}
EXPORT_SYMBOL(iop_write_memory);

iop_addr_t iop_alloc(size_t nbyte)
{
	const u32 size_arg = nbyte;
	u32 iop_addr;

	if (size_arg != nbyte)
		return 0;

	return sif_rpc(&iop_heap_rpc, rpo_alloc,
		&size_arg, sizeof(size_arg),
		&iop_addr, sizeof(iop_addr)) < 0 ? 0 : iop_addr;
}
EXPORT_SYMBOL(iop_alloc);

int iop_free(iop_addr_t baddr)
{
	const u32 addr_arg = baddr;
	s32 status;
	int err;

	err = sif_rpc(&iop_heap_rpc, rpo_free,
		&addr_arg, sizeof(addr_arg),
		&status, sizeof(status));

	return err < 0 ? err : status;
}
EXPORT_SYMBOL(iop_free);

static int __init iop_memory_init(void)
{
	return sif_rpc_bind(&iop_heap_rpc, SIF_SID_HEAP);
}

static void __exit iop_memory_exit(void)
{
	/* FIXME */
}

module_init(iop_memory_init);
module_exit(iop_memory_exit);

MODULE_LICENSE("GPL");
