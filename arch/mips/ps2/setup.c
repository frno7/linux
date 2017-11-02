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

#include <asm/mach-ps2/iop-registers.h>
#include <asm/mach-ps2/sbios.h>

const char *get_system_type(void)
{
	return "Sony PlayStation 2";
}

#define IOP_OHCI_BASE	0x1f801600

static struct resource ps2_usb_ohci_resources[] = {
	[0] = {
		.start	= IOP_OHCI_BASE,
		.end	= IOP_OHCI_BASE + 0xff,
		.flags	= IORESOURCE_MEM, /* 256 byte HCCA. */
	},
	[1] = {
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

static struct platform_device *ps2_platform_devices[] __initdata = {
	&ps2_usb_ohci_device,
};

static int __init ps2_board_setup(void)
{
	sbios_init();

	/*
	 * FIXME: As far as I remember the following enables the clock,
	 * so that ohci->regs->fminterval can count.
	 */
	iop_set_dma_dpcr2(IOP_DMA_DPCR2_DEV9);

	return 0;
}
arch_initcall(ps2_board_setup);

static int __init ps2_device_setup(void)
{
	ps2rtc_init();

	return platform_add_devices(ps2_platform_devices,
		ARRAY_SIZE(ps2_platform_devices));
}
device_initcall(ps2_device_setup);
