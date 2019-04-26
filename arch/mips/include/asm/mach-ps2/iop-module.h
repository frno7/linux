// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) module linker
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IOP_MODULE_H
#define __ASM_MACH_PS2_IOP_MODULE_H

int iop_module_request(const char *name, int version, const char *arg);

#endif /* __ASM_MACH_PS2_IOP_MODULE_H */
