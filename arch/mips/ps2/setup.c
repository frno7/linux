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
#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/dma.h>

const char *get_system_type(void)
{
	return "Sony PlayStation 2";
}

#define VU0_BASE	0x11000000
#define VU1_BASE	0x11008000
#define IOP_RAM_BASE	0x1c000000
#define IOP_OHCI_BASE	0x1f801600
#define GS_REG_BASE	0x12000000

static struct resource iop_resources[] = {
	[0] = {
		.name	= "IOP RAM",
		.start	= IOP_RAM_BASE,
		.end	= IOP_RAM_BASE + 0x1fffff,
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
		.start	= IRQ_DMAC_2,	/* GS interface (GIF) */
		.end	= IRQ_DMAC_2,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_GS_SIGNAL,
		.end	= IRQ_GS_EXVSYNC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource vu0_resources[] = {
	[0] = {
		.name	= "Vector unit 0 code",
		.start	= VU0_BASE,
		.end	= VU0_BASE + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "Vector unit 0 data",
		.start	= VU0_BASE + 0x4000,
		.end	= VU0_BASE + 0x4fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource vu1_resources[] = {
	[2] = {
		.name	= "Vector unit 1 code",
		.start	= VU1_BASE,
		.end	= VU1_BASE + 0x3fff,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.name	= "Vector unit 1 data",
		.start	= VU1_BASE + 0x4000,
		.end	= VU1_BASE + 0x7fff,
		.flags	= IORESOURCE_MEM,
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

static struct platform_device vu0_device = {
	.name           = "vu0",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(vu0_resources),
	.resource	= vu0_resources,
};

static struct platform_device vu1_device = {
	.name           = "vu1",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(vu1_resources),
	.resource	= vu1_resources,
};

#if 0
/*
 * PATA disk driver
 *
 * Compatible with:
 *  - driver/ide (old)
 *  - drivers/ata (new)
 */
static struct resource pata_resources[] = {
	/* IO base, 8 16bit registers */
	[0] = {
		.start	= CPHYSADDR(0xb4000040),
		.end	= CPHYSADDR(0xb4000040 + (8 * 2) - 1),
		.flags	= IORESOURCE_MEM,
	},
	/* CTRL base, 1 16bit register */
	[1] = {
		.start	= CPHYSADDR(0xb400005c),
		.end	= CPHYSADDR(0xb400005c + (1 * 2) - 1),
		.flags	= IORESOURCE_MEM,
	},
	/* IRQ */
	[2] = {
		.start	= IRQ_SBUS_PCIC,
		.end	= IRQ_SBUS_PCIC,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct pata_platform_info pata_platform_data = {
	.ioport_shift	= 1,
	.irq_flags	= IRQF_SHARED,
};

static struct platform_device pata_device = {
	.name		= "pata_platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pata_resources),
	.resource	= pata_resources,
	.dev  = {
		.platform_data	= &pata_platform_data,
	},
};
#else
/*
 * PATA disk driver
 *
 * For new Playstation 2 PATA driver
 */
static struct resource pata_resources[] = {
	/* IO base, 8 16bit registers */
	[0] = {
		.start	= CPHYSADDR(0xb4000040),
		.end	= CPHYSADDR(0xb4000040 + (8 * 2) - 1),
		.flags	= IORESOURCE_MEM,
	},
	/* CTRL base, 1 16bit register */
	[1] = {
		.start	= CPHYSADDR(0xb400005c),
		.end	= CPHYSADDR(0xb400005c + (1 * 2) - 1),
		.flags	= IORESOURCE_MEM,
	},
	/* IRQ */
	[2] = {
		.start	= IRQ_INTC_SBUS,
		.end	= IRQ_INTC_SBUS,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct platform_device pata_device = {
	.name		= "pata_ps2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(pata_resources),
	.resource	= pata_resources,
};
#endif

#if 0 /* FIXME */
static struct platform_device smap_device = {
	.name           = "smap",
	.id		= -1,
};

static struct platform_device smaprpc_device = {
	.name           = "smaprpc",
	.id		= -1,
};
#endif

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
	&iop_device,
	&ohci_device,
	&gs_device,
	&fb_device,
	&vu0_device,
	&vu1_device,
	&pata_device,
#if 0 /* FIXME */
	&smap_device,
	&smaprpc_device,
#endif
};

static int __init ps2_board_setup(void)
{
	printk("PlayStation 2 board setup\n");

	/*
	 * FIXME: As far as I remember the following enables the clock,
	 * so that ohci->regs->fminterval can count.
	 */
	// iop_set_dma_dpcr2(IOP_DMA_DPCR2_DEV9);

	/* FIXME: D_CTRL: DMA control register: Enable all DMAs. EE User's p. 64 */

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

EXPORT_SYMBOL(do_IRQ);	/* FIXME */
