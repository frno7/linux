// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 memory
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/types.h>

#include <asm/bootinfo.h>

void __init plat_mem_setup(void)
{
	ioport_resource.start = 0x10000000;
	ioport_resource.end   = 0x1fffffff;

	iomem_resource.start = 0x00000000;
	iomem_resource.end   = KSEG2 - 1;

	add_memory_region(0x00000000, 0x02000000, BOOT_MEM_RAM);
	add_memory_region(ROM0_BASE, ROM0_SIZE, BOOT_MEM_ROM_DATA);
	add_memory_region(ROM1_BASE, ROM1_SIZE, BOOT_MEM_ROM_DATA);

	set_io_port_base(CKSEG1);	/* KSEG1 is uncached */
}
