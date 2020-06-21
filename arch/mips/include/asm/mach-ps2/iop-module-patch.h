// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) patch
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IOP_MODULE_PATCH_H
#define __ASM_MACH_PS2_IOP_MODULE_PATCH_H

#include <linux/init.h>
#include <linux/types.h>

int __init iop_module_patch(void);

#endif /* __ASM_MACH_PS2_IOP_MODULE_PATCH_H */
