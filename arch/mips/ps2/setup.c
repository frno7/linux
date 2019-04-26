// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 system setup functions
 *
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C)      2017 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>

#include <asm/bootinfo.h>
#include <asm/prom.h>

#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/scmd.h>

const char *get_system_type(void)
{
	return "Sony PlayStation 2";
}

#define IOP_OHCI_BASE	0x1f801600
#define GS_REG_BASE	0x12000000

static struct resource ohci_resources[] = {	/* FIXME: Subresource to IOP */
	[0] = {
		.name	= "USB OHCI",
		.start	= IOP_OHCI_BASE,
		.end	= IOP_OHCI_BASE + 0xff,
		.flags	= IORESOURCE_MEM, 	/* 256 byte HCCA. */
	},
	[1] = {
		.start	= IRQ_INTC_SBUS,
		.end	= IRQ_INTC_SBUS,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct platform_device ohci_device = {
	.name		= "ohci-ps2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ohci_resources),
	.resource	= ohci_resources,
};

static struct resource gs_resources[] = {
	[0] = {
		.name	= "Graphics Synthesizer",
		.start	= GS_REG_BASE,
		.end	= GS_REG_BASE + 0x1ffffff,
		.flags	= IORESOURCE_MEM,	/* FIXME: IORESOURCE_REG? */
	},
	[1] = {
		.start	= IRQ_DMAC_GIF,
		.end	= IRQ_DMAC_GIF,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_GS_SIGNAL,
		.end	= IRQ_GS_EXVSYNC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device gs_device = {
	.name           = "gs",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(gs_resources),
	.resource	= gs_resources,
};

static struct platform_device fb_device = {
	.name           = "ps2fb",	/* FIXME: Remove from platform? */
	.id		= -1,
};

static struct platform_device rtc_device = {
	.name		= "rtc-ps2",
	.id		= -1,
};

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

static struct platform_device *ps2_platform_devices[] __initdata = {
	&ohci_device,
	&gs_device,
	&fb_device,
	&rtc_device,
};

static int __init set_machine_name(void)
{
	const struct scmd_machine_name machine = scmd_read_machine_name();

	/*
	 * FIXME: There are issues reading the machine name for
	 * SCPH-10000 and SCPH-15000. Late SCPH-10000 and all
	 * SCPH-15000 have the name in rom0:OSDSYS.
	 */

	if (!strlen(machine.name)) {
		pr_err("%s: scmd_read_machine_name failed\n", __func__);
		return -EIO;
	}

	mips_set_machine_name(machine.name);

	return 0;
}

static int __init ps2_board_setup(void)
{
	pr_info("PlayStation 2 board setup\n");

	set_machine_name();

	return 0;
}
arch_initcall(ps2_board_setup);

static int __init ps2_device_setup(void)
{
	return platform_add_devices(ps2_platform_devices,
		ARRAY_SIZE(ps2_platform_devices));
}
device_initcall(ps2_device_setup);
