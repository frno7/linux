// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) heap memory
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IOP_HEAP_H
#define __ASM_MACH_PS2_IOP_HEAP_H

#include <linux/types.h>

#include <asm/mach-ps2/iop.h>

iop_addr_t iop_alloc(size_t nbyte);

int iop_free(iop_addr_t baddr);

#endif /* __ASM_MACH_PS2_IOP_HEAP_H */
