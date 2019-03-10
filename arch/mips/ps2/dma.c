// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 DMA
 *
 * Copyright (C) 2018 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/irq_cpu.h>

#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/speed.h>
#include <asm/mach-ps2/ps2.h>

/*
 * The lower 16 bits are status bits and the upper 16 bits are mask bits.
 * Status bit cleared by writing 1. Mask bits are reversed by writing 1.
 */
#define DMAC_STAT_MASK	0x1000e010

/* DMAC */

static inline void dmac_reverse_mask(struct irq_data *data)
{
	outl(BIT(16 + data->irq - IRQ_DMAC), DMAC_STAT_MASK);
}

static void dmac_mask_ack(struct irq_data *data)
{
	const unsigned int bit = BIT(data->irq - IRQ_DMAC);

	outl((bit << 16) | bit, DMAC_STAT_MASK);
}

#define DMAC_IRQ_TYPE(irq_, name_) { \
	.irq = irq_, \
	.irq_chip = { \
		.name = name_, \
		.irq_unmask = dmac_reverse_mask, \
		.irq_mask = dmac_reverse_mask, \
		.irq_mask_ack = dmac_mask_ack \
	} \
}

static struct {
	unsigned int irq;
	struct irq_chip irq_chip;
} dmac_irqs[] = {
	DMAC_IRQ_TYPE(IRQ_DMAC_0,  "DMAC 0 VIF0"),
	DMAC_IRQ_TYPE(IRQ_DMAC_1,  "DMAC 1 VIF1"),
	DMAC_IRQ_TYPE(IRQ_DMAC_2,  "DMAC 2 GIF"),
	DMAC_IRQ_TYPE(IRQ_DMAC_3,  "DMAC 3 fromIPU"),
	DMAC_IRQ_TYPE(IRQ_DMAC_4,  "DMAC 4 toIPU"),
	DMAC_IRQ_TYPE(IRQ_DMAC_5,  "DMAC 5 SIF0"),
	DMAC_IRQ_TYPE(IRQ_DMAC_6,  "DMAC 6 SIF1"),
	DMAC_IRQ_TYPE(IRQ_DMAC_7,  "DMAC 7 SIF2"),
	DMAC_IRQ_TYPE(IRQ_DMAC_8,  "DMAC 8 fromSPR"),
	DMAC_IRQ_TYPE(IRQ_DMAC_9,  "DMAC 9 toSPR"),
	DMAC_IRQ_TYPE(IRQ_DMAC_S,  "DMAC stall"),
	DMAC_IRQ_TYPE(IRQ_DMAC_ME, "DMAC MFIFO empty"),
	DMAC_IRQ_TYPE(IRQ_DMAC_BE, "DMAC bus error"),
};

static irqreturn_t dmac_cascade(int irq, void *data)
{
	unsigned int pending = inl(DMAC_STAT_MASK) & 0xffff;

	if (!pending)
		return IRQ_NONE;

	while (pending) {
		const unsigned int irq_dmac = __fls(pending);

		if (generic_handle_irq(irq_dmac + IRQ_DMAC) < 0)
			spurious_interrupt();
		pending &= ~BIT(irq_dmac);
	}

	return IRQ_HANDLED;
}

static struct irqaction cascade_dmac_irqaction = {
	.name = "DMAC cascade",
	.handler = dmac_cascade,
};

static int __init init_dma(void)
{
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(dmac_irqs); i++)
		irq_set_chip_and_handler(dmac_irqs[i].irq,
			&dmac_irqs[i].irq_chip, handle_level_irq);

	outl(inl(DMAC_STAT_MASK), DMAC_STAT_MASK);

	/* Enable DMAC interrupt. */
	err = setup_irq(IRQ_C0_DMAC, &cascade_dmac_irqaction);
	if (err) {
		printk(KERN_ERR "irq: Failed to setup DMAC (err = %d).\n", err);
		goto out;
	}

out:
	return err;
}
arch_initcall(init_dma);
