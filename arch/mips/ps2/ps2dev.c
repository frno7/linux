/*
 *  PlayStation 2 integrated device driver
 *
 *  Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
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

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/vmalloc.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>

#include <asm/types.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/tlbmisc.h>
#include <asm/cop2.h>

#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/mach-ps2/gsfunc.h>

#include "ps2dev.h"

static void ps2gif_reset(void)
{
    int apath;

    apath = (GIFREG(PS2_GIFREG_STAT) >> 10) & 3;
    SET_GIFREG(PS2_GIFREG_CTRL, 0x00000001);	/* reset GIF */
    if (apath == 3)
	outq(0x0100ULL, GSSREG2(PS2_GSSREG_CSR));	/* reset GS */
}

static int ps2dev_initialized;

int __init ps2dev_init(void)
{
    u64 gs_revision;

    spin_lock_irq(&ps2dma_channels[DMA_GIF].lock);
    ps2dma_channels[DMA_GIF].reset = ps2gif_reset;
    spin_unlock_irq(&ps2dma_channels[DMA_GIF].lock);
    ps2gs_get_gssreg(PS2_GSSREG_CSR, &gs_revision);

    printk("PlayStation 2 device support: GIF\n");
    printk("Graphics Synthesizer revision: %08x\n",
	   ((u32)gs_revision >> 16) & 0xffff);

    ps2dev_initialized = 1;

    return 0;
}

void ps2dev_cleanup(void)
{
    if (!ps2dev_initialized)
	return;

    spin_lock_irq(&ps2dma_channels[DMA_GIF].lock);
    ps2dma_channels[DMA_GIF].reset = NULL;
    ps2dma_channels[DMA_IPU_to].reset = NULL;
    spin_unlock_irq(&ps2dma_channels[DMA_GIF].lock);
}

module_init(ps2dev_init);
module_exit(ps2dev_cleanup);

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 integrated device driver");
MODULE_LICENSE("GPL");
