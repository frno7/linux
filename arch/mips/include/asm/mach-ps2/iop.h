// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IOP_H
#define __ASM_MACH_PS2_IOP_H

#define IOP_RAM_BASE	0x1c000000
#define IOP_RAM_SIZE	0x200000

#define IOP_OHCI_BASE	0x1f801600

/**
 * iop_addr_t - I/O processor (IOP) bus address
 */
typedef u32 iop_addr_t;

#endif /* __ASM_MACH_PS2_IOP_H */
