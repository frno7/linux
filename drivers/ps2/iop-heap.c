// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) heap memory
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>

#include <asm/mach-ps2/iop-error.h>
#include <asm/mach-ps2/iop-heap.h>
#include <asm/mach-ps2/sif.h>

/**
 * enum iop_heap_rpc_ops - I/O processor (IOP) heap RPC operations
 * @rpo_alloc: allocate IOP memory
 * @rpo_free: free IOP memory
 * @rpo_load: FIXME
 */
enum iop_heap_rpc_ops {
	rpo_alloc = 1,
	rpo_free  = 2,
	rpo_load  = 3,
};

static struct sif_rpc_client iop_heap_rpc;

/**
 * iop_alloc - allocate IOP memory
 * @nbyte: number of bytes to allocate
 *
 * Context: sleep
 * Return: IOP address, or zero if the allocation failed
 */
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

/**
 * iop_free - free previously allocated IOP memory
 * @baddr: IOP address, or zero
 *
 * Context: sleep
 * Return: 0 on success, otherwise a negative error number
 */
int iop_free(iop_addr_t baddr)
{
	const u32 addr_arg = baddr;
	s32 status;
	int err;

	if (!baddr)
		return 0;

	err = sif_rpc(&iop_heap_rpc, rpo_free,
		&addr_arg, sizeof(addr_arg),
		&status, sizeof(status));

	return err < 0 ? err : errno_for_iop_error(status);
}
EXPORT_SYMBOL(iop_free);

static int __init iop_heap_init(void)
{
	return sif_rpc_bind(&iop_heap_rpc, SIF_SID_HEAP);
}

static void __exit iop_heap_exit(void)
{
	sif_rpc_unbind(&iop_heap_rpc);
}

module_init(iop_heap_init);
module_exit(iop_heap_exit);

MODULE_DESCRIPTION("PlayStation 2 input/output processor (IOP) heap memory");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
