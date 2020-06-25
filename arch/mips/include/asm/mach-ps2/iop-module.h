// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) module linker
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IOP_MODULE_H
#define __ASM_MACH_PS2_IOP_MODULE_H

#include <linux/types.h>

#include "iop-memory.h"

/* FIXME: What does these fields mean? */
struct iop_module_info {
	iop_addr_t next;	/* IOP address to the next module, or zero */
	iop_addr_t name;	/* IOP address to the module name */
	u16 version;
	u16 newflags;
	u16 id;
	u16 flags;
	iop_addr_t entry;
	iop_addr_t gp;
	iop_addr_t text_start;
	u32 text_size;
	u32 data_size;
	u32 bss_size;
	u32 unused1;
	u32 unused2;
};

#define IOP_MODULE_BASE 0x800

/* FIXME: What does the empty module list look like? */
/* FIXME: Use a memory barrier */
#define iop_for_each_module(pos) \
	for ((pos) = iop_bus_to_virt(IOP_MODULE_BASE); \
	     (pos) != NULL; \
	     (pos) = (pos)->next ? iop_bus_to_virt((pos)->next) : NULL)

static inline const char *iop_module_name(const struct iop_module_info *module)
{
	return iop_bus_to_virt(module->name);
}

int iop_module_request(const char *name, int version, const char *arg);

#endif /* __ASM_MACH_PS2_IOP_MODULE_H */
