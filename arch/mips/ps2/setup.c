/*
 * PlayStation 2 system setup functions
 *
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C)      2017 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>

#include <asm/bootinfo.h>

const char *get_system_type(void)
{
	return "Sony PlayStation 2";
}

void __init plat_mem_setup(void)
{
	ioport_resource.start = 0x10000000;
	ioport_resource.end   = 0x1fffffff;

	iomem_resource.start = 0x00000000;
	iomem_resource.end   = KSEG2 - 1;

	add_memory_region(0x00000000, 0x02000000, BOOT_MEM_RAM);
	add_memory_region(0x1fc00000, 0x02000000, BOOT_MEM_ROM_DATA);

	set_io_port_base(CKSEG1);	/* KSEG1 is uncached */
}

static int __init ps2_board_setup(void)
{
	printk("PlayStation 2 board setup\n");

	return 0;
}
arch_initcall(ps2_board_setup);

static int __init ps2_device_setup(void)
{
	return 0;
}
device_initcall(ps2_device_setup);
