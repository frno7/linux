// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 read-only memory (ROM)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_ROM_H
#define __ASM_MACH_PS2_ROM_H

#define ROM0_BASE	0x1fc00000	/* ROM0 base address (boot) */
#define ROM0_SIZE	0x400000	/* ROM0 maximum size */

#define ROM1_BASE	0x1e000000	/* ROM1 base address (DVD) */
#define ROM1_SIZE	0x100000	/* ROM1 maximum size */

#endif /* __ASM_MACH_PS2_ROM_H */
