// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Interrupt controller (INTC) IRQs
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

#include <asm/mach-ps2/irq.h>

static void intc_reverse_mask(struct irq_data *data)
{
	outl(BIT(data->irq - IRQ_INTC), INTC_MASK);
}

static void intc_mask_ack(struct irq_data *data)
{
	const unsigned int bit = BIT(data->irq - IRQ_INTC);

	outl(bit, INTC_MASK);
	outl(bit, INTC_STAT);
}

#define INTC_IRQ_TYPE(irq_, name_)				\
	{							\
		.irq = irq_,					\
		.irq_chip = {					\
			.name = name_,				\
			.irq_unmask = intc_reverse_mask,	\
			.irq_mask = intc_reverse_mask,		\
			.irq_mask_ack = intc_mask_ack,		\
		}						\
	}

static struct {
	unsigned int irq;
	struct irq_chip irq_chip;
} intc_irqs[] = {
	INTC_IRQ_TYPE(IRQ_INTC_GS,     "INTC GS"),
	INTC_IRQ_TYPE(IRQ_INTC_SBUS,   "INTC SBUS"),
	INTC_IRQ_TYPE(IRQ_INTC_VB_ON,  "INTC VB on"),
	INTC_IRQ_TYPE(IRQ_INTC_VB_OFF, "INTC VB off"),
	INTC_IRQ_TYPE(IRQ_INTC_VIF0,   "INTC VIF0"),
	INTC_IRQ_TYPE(IRQ_INTC_VIF1,   "INTC VIF1"),
	INTC_IRQ_TYPE(IRQ_INTC_VU0,    "INTC VU0"),
	INTC_IRQ_TYPE(IRQ_INTC_VU1,    "INTC VU1"),
	INTC_IRQ_TYPE(IRQ_INTC_IPU,    "INTC IPU"),
	INTC_IRQ_TYPE(IRQ_INTC_TIMER0, "INTC timer0"),
	INTC_IRQ_TYPE(IRQ_INTC_TIMER1, "INTC timer1"),
	INTC_IRQ_TYPE(IRQ_INTC_TIMER2, "INTC timer2"),
	INTC_IRQ_TYPE(IRQ_INTC_TIMER3, "INTC timer3"),
	INTC_IRQ_TYPE(IRQ_INTC_SFIFO,  "INTC SFIFO"),
	INTC_IRQ_TYPE(IRQ_INTC_VU0WD,  "INTC VU0WD"),
	INTC_IRQ_TYPE(IRQ_INTC_PGPU,   "INTC PGPU"),
};

static irqreturn_t intc_cascade(int irq, void *data)
{
	unsigned int pending, irq_intc;
	irqreturn_t status = IRQ_NONE;

	for (pending = inl(INTC_STAT); pending; pending &= ~BIT(irq_intc)) {
		irq_intc = __fls(pending);

		if (generic_handle_irq(irq_intc + IRQ_INTC) < 0)
			spurious_interrupt();
		else
			status = IRQ_HANDLED;
	}

	return status;
}

static struct irqaction cascade_intc_irqaction = {
	.name = "INTC cascade",
	.handler = intc_cascade,
};

void intc_sif_irq(unsigned int irq)
{
	do_IRQ(irq);
}
EXPORT_SYMBOL_GPL(intc_sif_irq);

int __init intc_irq_init(void)
{
	size_t i;
	int err;

	/* Clear mask and status registers */
	outl(inl(INTC_MASK), INTC_MASK);
	outl(inl(INTC_STAT), INTC_STAT);

	for (i = 0; i < ARRAY_SIZE(intc_irqs); i++)
		irq_set_chip_and_handler(intc_irqs[i].irq,
			&intc_irqs[i].irq_chip, handle_level_irq);

	/* FIXME: Is HARDIRQS_SW_RESEND needed? Are these edge types needed? */
	irq_set_irq_type(IRQ_INTC_GS, IRQ_TYPE_EDGE_FALLING);
	irq_set_irq_type(IRQ_INTC_SBUS, IRQ_TYPE_EDGE_FALLING);
	irq_set_irq_type(IRQ_INTC_VB_ON, IRQ_TYPE_EDGE_RISING);
	irq_set_irq_type(IRQ_INTC_VB_OFF, IRQ_TYPE_EDGE_FALLING);

	err = setup_irq(IRQ_C0_INTC, &cascade_intc_irqaction);
	if (err)
		pr_err("irq: Failed to setup INTC IRQs (err = %d)\n", err);

	return err;
}
