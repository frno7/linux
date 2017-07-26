/*
 *  PlayStation 2 system setup functions.
 *
 *  Copyright (C) 2010-2013 Juergen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

#include "loadfile.h"
#include "reset.h"

extern int iopdebug_init(void);

void (*__wbflush)(void);

EXPORT_SYMBOL(__wbflush);

static struct resource ps2_usb_ohci_resources[] = {
	[0] = {
		.start	= 0xbf801600,
		.end	= 0xbf801700,
		/* OHCI needs IO memory. */
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_SBUS_USB,
		.end	= IRQ_SBUS_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device ps2_usb_ohci_device = {
	.name		= "ps2_ohci",
	.id		= -1,
	.dev = {
		/* DMA memory is allocated from IOP heap. */
		.dma_mask		= &ps2_usb_ohci_device.dev.coherent_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(ps2_usb_ohci_resources),
	.resource	= ps2_usb_ohci_resources,
};
/*
static struct platform_device ps2_smap_device = {
	.name           = "ps2smap",
	.id		= -1,
};
*/
static struct platform_device ps2_smaprpc_device = {
	.name           = "ps2smaprpc",
	.id		= -1,
};

static struct platform_device ps2_gs_device = {
	.name           = "ps2fb",
	.id		= -1,
};

static struct platform_device ps2_sd_device = {
	.name           = "ps2sd",
	.id		= -1,
};

static struct platform_device ps2_audio_device = {
	.name           = "ps2audio",
	.id		= -1,
};

static struct platform_device ps2_sbios_device = {
	.name		= "ps2sbios-uart",
	.id		= 0,
};

static struct platform_device ps2_uart_device = {
	.name		= "ps2-uart",
	.id		= -1,
};

#if 0
/*
 * PATA disk driver
 *
 * Compatible with:
 *  - driver/ide (old)
 *  - drivers/ata (new)
 */
static struct resource ps2_pata_resources[] = {
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

static struct pata_platform_info ps2_pata_platform_data = {
	.ioport_shift	= 1,
	.irq_flags	= IRQF_SHARED,
};

static struct platform_device ps2_pata_device = {
	.name		= "pata_platform",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ps2_pata_resources),
	.resource	= ps2_pata_resources,
	.dev  = {
		.platform_data	= &ps2_pata_platform_data,
	},
};
#else
/*
 * PATA disk driver
 *
 * For new Playstation 2 PATA driver
 */
static struct resource ps2_pata_resources[] = {
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

static struct platform_device ps2_pata_device = {
	.name		= "pata_ps2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ps2_pata_resources),
	.resource	= ps2_pata_resources,
};
#endif

static unsigned int ps2_blink_frequency = 500;
module_param_named(panicblink, ps2_blink_frequency, uint, 0600);
MODULE_PARM_DESC(panicblink, "Frequency with which the PS2 HDD should blink when kernel panics");

static void ps2_wbflush(void)
{
	__asm__ __volatile__("sync.l":::"memory");

	/* flush write buffer to bus */
	inl(ps2sif_bustophys(0));
}

static long ps2_panic_blink(int state)
{
	static int last_blink;
	static int powerbtn_enabled = 0;

	if (!powerbtn_enabled) {
		powerbtn_enabled = 1;

		/* Enable power button, because init will not handle any signals. */
		ps2_powerbutton_enable_auto_shutoff(-1);
	}

	/*
	 * We expect frequency to be about 1/2s. KDB uses about 1s.
	 * Make sure they are different.
	 */
	if (!ps2_blink_frequency)
		return 0;
	if (state - last_blink < ps2_blink_frequency)
		return 0;

	/* TBD: Blink HDD led of fat PS2. */
	return 0;
}

void __init plat_mem_setup(void)
{
	ps2_reset_init();
	panic_blink = ps2_panic_blink;

	__wbflush = ps2_wbflush;

	/* IO port (out and in functions). */
	ioport_resource.start = 0x10000000;
	ioport_resource.end   = 0x1FFFFFFF;

	/* IO memory. */
	iomem_resource.start = 0x00000000;
	iomem_resource.end   = KSEG2 - 1;

	/* Memory for exception vectors. */
	add_memory_region(0x00000000, 0x00001000, BOOT_MEM_RAM);
	/* Reserved for SBIOS. */
	add_memory_region(0x00001000, 0x0000F000, BOOT_MEM_RESERVED);
	/* Free memory. */
	add_memory_region(0x00010000, 0x01ff0000, BOOT_MEM_RAM);

	/* Set base address of outb()/outw()/outl()/outq()/
	 * inb()/inw()/inl()/inq().
	 * This memory region is uncached.
	 */
	set_io_port_base(CKSEG1);

#ifdef CONFIG_SERIAL_PS2_SBIOS_DEFAULT
	/* Use PS2 SBIOS as default console. */
	add_preferred_console(PS2_SBIOS_SERIAL_DEVICE_NAME, 0, NULL);
#endif
}

static struct platform_device *ps2_platform_devices[] __initdata = {
	&ps2_gs_device,
	&ps2_usb_ohci_device,
	&ps2_sd_device,
	&ps2_audio_device,
	&ps2_sbios_device,
	&ps2_uart_device,
};

static int __init ps2_board_setup(void)
{
	ps2dma_init();
	ps2sif_init();
	ps2rtc_init();
#ifdef CONFIG_SYSFS_IOP_MODULES
	ps2_loadfile_init();
#endif
	iopdebug_init();

	ps2_powerbutton_init();

	if (load_module_firmware("ps2/ps2dev9.irx", 0) < 0)
		pr_err("loading ps2/ps2dev9.irx failed\n");

	if (load_module_firmware("ps2/dev9_dma.irx", 0) < 0)
		pr_err("loading ps2/dev9_dma.irx failed\n");

	if (ps2_pccard_present == 0x0200) {
		pr_info("Playstation 2 SLIM\n");

		if (load_module_firmware("ps2/intrelay-dev9-rpc.irx", 0) < 0)
			pr_err("loading ps2/intrelay-dev9-rpc.irx failed\n");

		platform_device_register(&ps2_smaprpc_device);
	}
	else {
		pr_info("Playstation 2 FAT\n");

		if (load_module_firmware("ps2/intrelay-dev9.irx", 0) < 0)
			pr_err("loading ps2/intrelay-dev9.irx failed\n");

		if (ps2_pccard_present == 0x0100) {
			pr_info(" - With network adapter\n");
			platform_device_register(&ps2_pata_device);
			platform_device_register(&ps2_smaprpc_device);
		}
	}

	platform_add_devices(ps2_platform_devices, ARRAY_SIZE(ps2_platform_devices));

	return 0;
}
arch_initcall(ps2_board_setup);
