/*
 * PlayStation 2 sub-system interface (SIF)
 *
 * The sub-system interface is an interface unit to the I/O processor (IOP).
 *
 * Copyright (C) 2001      Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>
#include <asm/mach-ps2/sbios.h>

#define SBIOS_SIF_INIT			16
#define SBIOS_SIF_EXIT			17

#define SBIOS_SIF_CMDINTRHDLR		35

#define SBIOS_SIF_INITRPC		48
#define SBIOS_SIF_EXITRPC		49

static irqreturn_t sif0_dma_handler(int irq, void *dev_id)
{
	sbios(SBIOS_SIF_CMDINTRHDLR, 0);

	return IRQ_HANDLED;
}

int __init sif_init(void)
{
	int err;

	err = request_irq(IRQ_DMAC_5, sif0_dma_handler, 0, "SIF0 DMA", NULL);
	if (err) {
		printk(KERN_ERR "sif: Failed to setup SIF0 handler.\n");
		goto err_irq_sif0;
	}

	if (sbios(SBIOS_SIF_INIT, 0) < 0) {
		printk(KERN_ERR "sif: SIF init failed.\n");
		err = -EINVAL;
		goto err_sif_init;
	}
	if (sbios(SBIOS_SIF_INITRPC, 0) < 0) {
		printk(KERN_ERR "sif: SIF init RPC failed.\n");
		err = -EINVAL;
		goto err_sif_initrpc;
	}

	printk(KERN_INFO "sif: SIF initialized.\n");

	return 0;

err_sif_initrpc:
	sbios(SBIOS_SIF_EXIT, 0);

err_sif_init:
	free_irq(IRQ_DMAC_5, NULL);

err_irq_sif0:
	return err;
}

void sif_exit(void)
{
	sbios(SBIOS_SIF_EXITRPC, 0);
	sbios(SBIOS_SIF_EXIT, 0);

	free_irq(IRQ_DMAC_5, NULL);
}
