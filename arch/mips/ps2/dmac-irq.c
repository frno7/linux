// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 DMA controller (DMAC) IRQs
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/irq.h>

static void dmac_reverse_mask(struct irq_data *data)
{
	outl(BIT(16 + data->irq - IRQ_DMAC), DMAC_STAT_MASK);
}

static void dmac_mask_ack(struct irq_data *data)
{
	const unsigned int bit = BIT(data->irq - IRQ_DMAC);

	outl((bit << 16) | bit, DMAC_STAT_MASK);
}

#define DMAC_IRQ_TYPE(irq_, name_)				\
	{							\
		.irq = irq_,					\
		.irq_chip = {					\
			.name = name_,				\
			.irq_unmask = dmac_reverse_mask,	\
			.irq_mask = dmac_reverse_mask,		\
			.irq_mask_ack = dmac_mask_ack		\
		}						\
	}

static struct {
	unsigned int irq;
	struct irq_chip irq_chip;
} dmac_irqs[] = {
	DMAC_IRQ_TYPE(IRQ_DMAC_VIF0, "DMAC VIF0"),
	DMAC_IRQ_TYPE(IRQ_DMAC_VIF1, "DMAC VIF1"),
	DMAC_IRQ_TYPE(IRQ_DMAC_GIF,  "DMAC GIF"),
	DMAC_IRQ_TYPE(IRQ_DMAC_FIPU, "DMAC fromIPU"),
	DMAC_IRQ_TYPE(IRQ_DMAC_TIPU, "DMAC toIPU"),
	DMAC_IRQ_TYPE(IRQ_DMAC_SIF0, "DMAC SIF0"),
	DMAC_IRQ_TYPE(IRQ_DMAC_SIF1, "DMAC SIF1"),
	DMAC_IRQ_TYPE(IRQ_DMAC_SIF2, "DMAC SIF2"),
	DMAC_IRQ_TYPE(IRQ_DMAC_FSPR, "DMAC fromSPR"),
	DMAC_IRQ_TYPE(IRQ_DMAC_TSPR, "DMAC toSPR"),
	DMAC_IRQ_TYPE(IRQ_DMAC_S,    "DMAC stall"),
	DMAC_IRQ_TYPE(IRQ_DMAC_ME,   "DMAC MFIFO empty"),
	DMAC_IRQ_TYPE(IRQ_DMAC_BE,   "DMAC bus error"),
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

int __init dmac_irq_init(void)
{
	size_t i;
	int err;

	outl(inl(DMAC_STAT_MASK), DMAC_STAT_MASK); /* Clear status register */

	for (i = 0; i < ARRAY_SIZE(dmac_irqs); i++)
		irq_set_chip_and_handler(dmac_irqs[i].irq,
			&dmac_irqs[i].irq_chip, handle_level_irq);

	err = setup_irq(IRQ_C0_DMAC, &cascade_dmac_irqaction);
	if (err)
		pr_err("irq: Failed to setup DMAC IRQs (err = %d)\n", err);

	return err;
}
