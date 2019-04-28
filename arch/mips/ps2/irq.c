// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 IRQs
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>

#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

#include <asm/mach-ps2/irq.h>

void __init arch_init_irq(void)
{
	mips_cpu_irq_init();

	intc_irq_init();
	dmac_irq_init();
}

asmlinkage void plat_irq_dispatch(void)
{
	const unsigned int pending = read_c0_status() & read_c0_cause();

	if (!(pending & (CAUSEF_IP2 | CAUSEF_IP3 | CAUSEF_IP7)))
		return spurious_interrupt();

	if (pending & CAUSEF_IP2)
		do_IRQ(IRQ_C0_INTC);	/* INTC interrupt */
	if (pending & CAUSEF_IP3)
		do_IRQ(IRQ_C0_DMAC);	/* DMAC interrupt */
	if (pending & CAUSEF_IP7)
		do_IRQ(IRQ_C0_IRQ7);	/* Timer interrupt */
}
