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

#include <asm/mach-ps2/sbios.h>

const char *get_system_type(void)
{
	return "Sony PlayStation 2";
}

void __init plat_mem_setup(void)
{
	/* IO port (out and in functions). */
	ioport_resource.start = 0x10000000;
	ioport_resource.end   = 0x1fffffff;

	/* IO memory. */
	iomem_resource.start = 0x00000000;
	iomem_resource.end   = KSEG2 - 1;

	/* Memory for exception vectors. */
	add_memory_region(0x00000000, 0x00001000, BOOT_MEM_RAM);
	/* Reserved for SBIOS. */
	add_memory_region(0x00001000, 0x0000f000, BOOT_MEM_RESERVED);
	/* Free memory. */
	add_memory_region(0x00010000, 0x01ff0000, BOOT_MEM_RAM);

	/* This memory region is uncached. */
	set_io_port_base(CKSEG1);
}

static int __init ps2_board_setup(void)
{
	sbios_init();

	return 0;
}
arch_initcall(ps2_board_setup);

static int __init ps2_device_setup(void)
{
	return 0;
}
device_initcall(ps2_device_setup);
