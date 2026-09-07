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

static struct resource pata_resources[] = {	/* FIXME: Subresource to IOP */
	[0] = {
		.name	= "PATA",
		.start	= IOP_PATA_BASE,
		.end	= IOP_PATA_BASE + 0x1f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_IOP_SPD_ATA0,
		.end	= IRQ_IOP_SPD_ATA0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct platform_device pata_device = {
	.name		= "pata-ps2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pata_resources),
	.resource	= pata_resources,
};

/*
 * The SMAP part of the SPEED chip in the expansion bay.  The chip window
 * starts at physical 0x14000000 and holds the ATA registers as well, at
 * IOP_PATA_BASE, so SMAP is described as the two ranges it actually uses
 * rather than as one block: the revision and interrupt registers below the
 * ATA window, and everything from the buffer descriptor mode register up,
 * which covers both FIFO data ports, the EMAC3 registers and the two
 * descriptor rings.  Declaring the whole window instead would collide with
 * the ATA device above and leave SMAP unregistered.
 *
 * The hardware only answers once iop-dev9 has powered the expansion bay,
 * which the driver verifies through the IOP before it touches the window.
 */
#define SPEED_BASE	0x14000000

static struct resource smap_resources[] = {
	[0] = {
		.name	= "speed",
		.start	= SPEED_BASE,
		.end	= SPEED_BASE + 0x3f,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "smap",
		.start	= SPEED_BASE + 0x100,
		.end	= SPEED_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.name	= "rx",
		.start	= IRQ_IOP_SPD_RXEND,
		.end	= IRQ_IOP_SPD_RXEND,
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.name	= "tx",
		.start	= IRQ_IOP_SPD_TXEND,
		.end	= IRQ_IOP_SPD_TXEND,
		.flags	= IORESOURCE_IRQ,
	},
	[4] = {
		.name	= "emac3",
		.start	= IRQ_IOP_SPD_EMAC3,
		.end	= IRQ_IOP_SPD_EMAC3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smap_device = {
	.name		= "ps2-smap",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smap_resources),
	.resource	= smap_resources,
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
	&pata_device,
	&smap_device,
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
