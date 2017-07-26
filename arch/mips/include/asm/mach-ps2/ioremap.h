/*
 *  PlayStation 2 I/O remap
 *
 *  Copyright (C) 2010-2013 Juergen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_MACH_PS2_IOREMAP_H
#define __ASM_MACH_PS2_IOREMAP_H

#include <linux/types.h>

#include <asm/mach-ps2/ps2.h>

/* There are no 64 bit addresses. No fixup is needed. */
static inline phys_t fixup_bigphys_addr(phys_t phys_addr, phys_t size)
{
	return phys_addr;
}

static inline void __iomem *plat_ioremap(phys_t offset, unsigned long size,
	unsigned long flags)
{
	if ((offset >= 0) && (offset < CKSEG0)) {
		/* Memory is already mapped. */
		if (flags & _CACHE_UNCACHED) {
			return (void __iomem *)
				(unsigned long)CKSEG1ADDR(offset);
		} else {
			return (void __iomem *)
				(unsigned long)CKSEG0ADDR(offset);
		}
	}
	/* Memory will be page mapped by kernel. */
	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	unsigned long kseg_addr;

	kseg_addr = (unsigned long) addr;
	if ((kseg_addr >= CKSEG0) && (kseg_addr < CKSEG2)) {
		/* Memory is always mapped in kernel mode. No unmap possible. */
		return 1;
	}
	return 0;
}

#endif /* __ASM_MACH_PS2_IOREMAP_H */
