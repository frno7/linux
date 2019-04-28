// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer (GS) IRQs
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/irq_cpu.h>

#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/gs-registers.h>

static void gs_reverse_mask(struct irq_data *data)
{
	gs_xorq_imr(BIT(8 + data->irq - IRQ_GS));
}

static void gs_ack(struct irq_data *data)
{
	gs_writeq_csr(BIT(data->irq - IRQ_GS));
}

#define GS_IRQ_TYPE(irq_, name_)			\
	{						\
		.irq = irq_,				\
		.irq_chip = {				\
			.name = name_,			\
			.irq_unmask = gs_reverse_mask,	\
			.irq_mask = gs_reverse_mask,	\
			.irq_ack = gs_ack		\
		}					\
	}

static struct {
	unsigned int irq;
	struct irq_chip irq_chip;
} gs_irqs[] = {
	GS_IRQ_TYPE(IRQ_GS_SIGNAL,  "GS SIGNAL"),
	GS_IRQ_TYPE(IRQ_GS_FINISH,  "GS FINISH"),
	GS_IRQ_TYPE(IRQ_GS_HSYNC,   "GS HSYNC"),
	GS_IRQ_TYPE(IRQ_GS_VSYNC,   "GS VSYNC"),
	GS_IRQ_TYPE(IRQ_GS_EDW,     "GS EDW"),
	GS_IRQ_TYPE(IRQ_GS_EXHSYNC, "GS EXHSYNC"),
	GS_IRQ_TYPE(IRQ_GS_EXVSYNC, "GS EXVSYNC"),
};

static irqreturn_t gs_cascade(int irq, void *data)
{
	unsigned int pending = gs_readq_csr() & 0x1f;

	if (!pending)
		return IRQ_NONE;

	while (pending) {
		const unsigned int irq_gs = __fls(pending);

		if (generic_handle_irq(irq_gs + IRQ_GS) < 0)
			spurious_interrupt();
		pending &= ~BIT(irq_gs);
	}

	return IRQ_HANDLED;
}

static struct irqaction cascade_gs_irqaction = {
	.name = "GS cascade",
	.handler = gs_cascade,
};

int gs_irq_init(void)
{
	size_t i;
	int err;

	gs_writeq_imr(0x7f00);	/* Disable GS interrupts */
	gs_writeq_csr(0x00ff);	/* Clear GS events */

	for (i = 0; i < ARRAY_SIZE(gs_irqs); i++)
		irq_set_chip_and_handler(gs_irqs[i].irq,
			&gs_irqs[i].irq_chip, handle_level_irq);

	err = setup_irq(IRQ_INTC_GS, &cascade_gs_irqaction);
	if (err)
		pr_err("gs-irq: Failed to setup GS IRQs (err = %d)\n", err);

	return err;
}
