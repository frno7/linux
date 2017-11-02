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

#define IOP_REG_BASE	0x1f801460
#define IOP_USB_BASE	(IOP_REG_BASE + 0x1a0)

static struct resource ps2_usb_ohci_resources[] = {
	[0] = {
		.start	= IOP_USB_BASE,
		.end	= IOP_USB_BASE + 0xff,
		.flags	= IORESOURCE_MEM, /* 256 byte HCCA */
	},
	[1] = {
		.start	= 0x1f801570,
		.end	= 0x1f801573,
		/* PS2 specific register (DPCR2). */
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= IRQ_SBUS_USB,
		.end	= IRQ_SBUS_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ps2_usb_ohci_device = {
	.name		= "ohci-ps2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ps2_usb_ohci_resources),
	.resource	= ps2_usb_ohci_resources,
};

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

static struct platform_device *ps2_platform_devices[] __initdata = {
	&ps2_usb_ohci_device,
};

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

	platform_add_devices(ps2_platform_devices, ARRAY_SIZE(ps2_platform_devices));

	return 0;
}
device_initcall(ps2_device_setup);
