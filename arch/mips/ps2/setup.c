/*
 *  PlayStation 2 system setup functions.
 *
 *  Copyright (C) 2010-2013 Jürgen Urban
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

	/* This memory region is uncached. */
	set_io_port_base(CKSEG1);
}

static int __init ps2_board_setup(void)
{
	ps2dma_init();
	ps2sif_init();
	ps2rtc_init();

	ps2_powerbutton_init();

	return 0;
}
arch_initcall(ps2_board_setup);
