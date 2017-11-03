/*
 *  PlayStation 2 system setup functions.
 *
 *  Copyright (C) 2010-2013 Jürgen Urban
 *  Copyright (C)      2017 Fredrik Noring
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/dma-mapping.h>

#include <asm/bootinfo.h>

#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/iopmodules.h>

#include "reset.h"

const char *get_system_type(void)
{
	return "Sony PlayStation 2";
}

void __init plat_mem_setup(void)
{
	ps2_reset_init();

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

	/*
	 * FIXME: Why doesn't dd work on /dev/kmem for SPRAM, IOP, etc.?
	 *
	 * # dd if=/dev/kmem bs=1 skip=$(( 0x70000000 )) count=256 | hexdump
	 * dd: /dev/kmem: No such device or address
	 * # dd if=/dev/kmem bs=1 skip=$(( 0x1fc00000 )) count=256 | hexdump
	 * dd: /dev/kmem: No such device or address
	 */

	/* Scratch pad RAM (SPRAM). */
	add_memory_region(0x70000000, 0x70003fff, BOOT_MEM_RESERVED);
	/* IOP RAM. */
	add_memory_region(0x1c000000, 0x1c1fffff, BOOT_MEM_RESERVED);
	/* Boot ROM. */
	add_memory_region(0x1fc00000, 0x1fffffff, BOOT_MEM_ROM_DATA);

	/* This memory region is uncached. */
	set_io_port_base(CKSEG1);
}

static int __init ps2_board_setup(void)
{
	ps2dma_init();
	ps2sif_init();

	return 0;
}
arch_initcall(ps2_board_setup);

static int __init ps2_device_setup(void)
{
	ps2rtc_init();

	return 0;
}
device_initcall(ps2_device_setup);
