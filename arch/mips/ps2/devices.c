// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 devices
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>

#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/iop.h>
#include <asm/mach-ps2/irq.h>

static struct resource iop_resources[] = {
	[0] = {
		.name	= "IOP RAM",
		.start	= IOP_RAM_BASE,
		.end	= IOP_RAM_BASE + IOP_RAM_SIZE - 1,
		.flags	= IORESOURCE_MEM,	/* 2 MiB IOP RAM */
	},
};

static struct platform_device iop_device = {
	.name		= "iop",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(iop_resources),
	.resource	= iop_resources,
};

static struct resource ohci_resources[] = {	/* FIXME: Subresource to IOP */
	[0] = {
		.name	= "USB OHCI",
		.start	= IOP_OHCI_BASE,
		.end	= IOP_OHCI_BASE + 0xff,
		.flags	= IORESOURCE_MEM, 	/* 256 byte HCCA. */
	},
	[1] = {
		.start	= IRQ_IOP_USB,
		.end	= IRQ_IOP_USB,
		.flags	= IORESOURCE_IRQ,
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

static struct platform_device gs_drm_device = {
	.name           = "gs-drm",
	.id		= -1,
};

static struct platform_device rtc_device = {
	.name		= "rtc-ps2",
	.id		= -1,
};

static struct platform_device *ps2_platform_devices[] __initdata = {
	&iop_device,
	&ohci_device,
	&gs_device,
	&gs_drm_device,	/* FIXME */
	&rtc_device,
};

static int __init ps2_device_setup(void)
{
	return platform_add_devices(ps2_platform_devices,
		ARRAY_SIZE(ps2_platform_devices));
}
device_initcall(ps2_device_setup);
