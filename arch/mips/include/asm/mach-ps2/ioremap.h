/*
 *  PlayStation 2 I/O remap
 *
 *  Copyright (C) 2010-2013 Jürgen Urban
 *  Copyright (C) 2017-2018 Fredrik Noring
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#ifndef __ASM_MACH_PS2_IOREMAP_H
#define __ASM_MACH_PS2_IOREMAP_H

#include <linux/types.h>

#include <asm/mach-ps2/ps2.h>

static inline phys_addr_t fixup_bigphys_addr(
	phys_addr_t phys_addr, phys_addr_t size)
{
	return phys_addr; /* There are no 64 bit addresses, no fixup needed. */
}

static inline void __iomem *plat_ioremap(phys_addr_t offset,
	unsigned long size, unsigned long flags)
{
	if (offset >= 0 && offset < CKSEG0) {
		/* Memory is already mapped. */
		if (flags & _CACHE_UNCACHED) {
			return (void __iomem *)
				(unsigned long)CKSEG1ADDR(offset);
		} else {
			return (void __iomem *)
				(unsigned long)CKSEG0ADDR(offset);
		}
	}

	return NULL; /* Memory will be page mapped by kernel. */
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	const unsigned long kseg_addr = (unsigned long)addr;

	/* Check if memory is mapped in kernel mode with no unmap possible. */
	return CKSEG0 <= kseg_addr && kseg_addr < CKSEG2;
}

#endif /* __ASM_MACH_PS2_IOREMAP_H */
