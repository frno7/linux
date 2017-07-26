/*
 *  Playstation 2 IRQ handling.
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

#define INTC_STAT	0x1000f000
#define INTC_MASK	0x1000f010
#define DMAC_STAT	0x1000e010
#define DMAC_MASK	0x1000e010
#define GS_CSR		0x12001000
#define GS_IMR		0x12001010

#define SBUS_SMFLG	0x1000f230
#define SBUS_AIF_INTSR	0x18000004
#define SBUS_AIF_INTEN	0x18000006
#define SBUS_PCIC_EXC1	0x1f801476
#define SBUS_PCIC_CSC1	0x1f801464
#define SBUS_PCIC_IMR1	0x1f801468
#define SBUS_PCIC_TIMR	0x1f80147e
#define SBUS_PCIC3_TIMR	0x1f801466

/*
 * INTC
 */
static volatile unsigned long intc_mask = 0;

static inline void intc_enable_irq(struct irq_data *data)
{
	if (!(intc_mask & (1 << data->irq))) {
		intc_mask |= (1 << data->irq);
		outl(1 << data->irq, INTC_MASK);
	}
}

static inline void intc_disable_irq(struct irq_data *data)
{
	if ((intc_mask & (1 << data->irq))) {
		intc_mask &= ~(1 << data->irq);
		outl(1 << data->irq, INTC_MASK);
	}
}

static unsigned int intc_startup_irq(struct irq_data *data)
{
	intc_enable_irq(data);
	return 0;
}

static void intc_shutdown_irq(struct irq_data *data)
{
	intc_disable_irq(data);
}

static void intc_ack_irq(struct irq_data *data)
{
	intc_disable_irq(data);
	outl(1 << data->irq, INTC_STAT);
}

static void intc_end_irq(struct irq_data *data)
{
	intc_enable_irq(data);
}

static struct irq_chip intc_irq_type = {
	.name		= "EE INTC",
	.irq_startup	= intc_startup_irq,
	.irq_shutdown	= intc_shutdown_irq,
	.irq_unmask	= intc_enable_irq,
	.irq_mask	= intc_disable_irq,
	.irq_mask_ack	= intc_ack_irq,
	.irq_eoi	= intc_end_irq,
};

/*
 * DMAC
 */
static volatile unsigned long dmac_mask = 0;

static inline void dmac_enable_irq(struct irq_data *data)
{
	unsigned int dmac_irq_nr = data->irq - IRQ_DMAC;

	if (!(dmac_mask & (1 << dmac_irq_nr))) {
		dmac_mask |= (1 << dmac_irq_nr);
		outl(1 << (dmac_irq_nr + 16), DMAC_MASK);
	}
}

static inline void dmac_disable_irq(struct irq_data *data)
{
	unsigned int dmac_irq_nr = data->irq - IRQ_DMAC;

	if ((dmac_mask & (1 << dmac_irq_nr))) {
		dmac_mask &= ~(1 << dmac_irq_nr);
		outl(1 << (dmac_irq_nr + 16), DMAC_MASK);
	}
}

static unsigned int dmac_startup_irq(struct irq_data *data)
{
	dmac_enable_irq(data);
	return 0;
}

static void dmac_shutdown_irq(struct irq_data *data)
{
	dmac_disable_irq(data);
}

static void dmac_ack_irq(struct irq_data *data)
{
	unsigned int dmac_irq_nr = data->irq - IRQ_DMAC;

	dmac_disable_irq(data);
	outl(1 << dmac_irq_nr, DMAC_STAT);
}

static void dmac_end_irq(struct irq_data *data)
{
	dmac_enable_irq(data);
}

static struct irq_chip dmac_irq_type = {
	.name		= "EE DMAC",
	.irq_startup	= dmac_startup_irq,
	.irq_shutdown	= dmac_shutdown_irq,
	.irq_unmask	= dmac_enable_irq,
	.irq_mask	= dmac_disable_irq,
	.irq_mask_ack	= dmac_ack_irq,
	.irq_eoi	= dmac_end_irq,
};

/*
 * GS
 */
static volatile unsigned long gs_mask = 0;

void ps2_setup_gs_imr(void)
{
	outl(0xff00, GS_IMR);
	outl((~gs_mask & 0x7f) << 8, GS_IMR);
}

static inline void gs_enable_irq(struct irq_data *data)
{
	unsigned int gs_irq_nr = data->irq - IRQ_GS;

	gs_mask |= (1 << gs_irq_nr);
	ps2_setup_gs_imr();
}

static inline void gs_disable_irq(struct irq_data *data)
{
	unsigned int gs_irq_nr = data->irq - IRQ_GS;

	gs_mask &= ~(1 << gs_irq_nr);
	ps2_setup_gs_imr();
}

static unsigned int gs_startup_irq(struct irq_data *data)
{
	gs_enable_irq(data);
	return 0;
}

static void gs_shutdown_irq(struct irq_data *data)
{
	gs_disable_irq(data);
}

static void gs_ack_irq(struct irq_data *data)
{
	unsigned int gs_irq_nr = data->irq - IRQ_GS;

	outl(0xff00, GS_IMR);
	outl(1 << gs_irq_nr, GS_CSR);
}

static void gs_end_irq(struct irq_data *data)
{
	outl((~gs_mask & 0x7f) << 8, GS_IMR);
	gs_enable_irq(data);
}

static struct irq_chip gs_irq_type = {
	.name		= "GS",
	.irq_startup	= gs_startup_irq,
	.irq_shutdown	= gs_shutdown_irq,
	.irq_unmask	= gs_enable_irq,
	.irq_mask	= gs_disable_irq,
	.irq_mask_ack	= gs_ack_irq,
	.irq_eoi	= gs_end_irq,
};

/*
 * SBUS
 */
static volatile unsigned long sbus_mask = 0;

static inline unsigned long sbus_enter_irq(void)
{
	unsigned long istat = 0;

	if (inl(SBUS_SMFLG) & (1 << 8)) {
		outl(1 << 8, SBUS_SMFLG);
		switch (ps2_pcic_type) {
		case 1:
			if (inw(SBUS_PCIC_CSC1) & 0x0080) {
				outw(0xffff, SBUS_PCIC_CSC1);
				istat |= 1 << (IRQ_SBUS_PCIC - IRQ_SBUS);
			}
			break;
		case 2:
			if (inw(SBUS_PCIC_CSC1) & 0x0080) {
				outw(0xffff, SBUS_PCIC_CSC1);
				istat |= 1 << (IRQ_SBUS_PCIC - IRQ_SBUS);
			}
			break;
		case 3:
			istat |= 1 << (IRQ_SBUS_PCIC - IRQ_SBUS);
			break;
		}
	}

	if (inl(SBUS_SMFLG) & (1 << 10)) {
		outl(1 << 10, SBUS_SMFLG);
		istat |= 1 << (IRQ_SBUS_USB - IRQ_SBUS);
	}
	return istat;
}

static inline void sbus_leave_irq(void)
{
	unsigned short mask;

	if (ps2_pccard_present == 0x0100) {
		mask = inw(SPD_R_INTR_ENA);
		outw(0, SPD_R_INTR_ENA);
		outw(mask, SPD_R_INTR_ENA);
	}

	switch (ps2_pcic_type) {
	case 1: case 2:
		mask = inw(SBUS_PCIC_TIMR);
		outw(1, SBUS_PCIC_TIMR);
		outw(mask, SBUS_PCIC_TIMR);
		break;
	case 3:
		mask = inw(SBUS_PCIC3_TIMR);
		outw(1, SBUS_PCIC3_TIMR);
		outw(mask, SBUS_PCIC3_TIMR);
		break;
	}
}

static inline void sbus_enable_irq(struct irq_data *data)
{
	unsigned int sbus_irq_nr = data->irq - IRQ_SBUS;

	sbus_mask |= (1 << sbus_irq_nr);

	switch (data->irq) {
	case IRQ_SBUS_PCIC:
		switch (ps2_pcic_type) {
		case 1:
			outw(0xff7f, SBUS_PCIC_IMR1);
			break;
		case 2:
			outw(0, SBUS_PCIC_TIMR);
			break;
		case 3:
			outw(0, SBUS_PCIC3_TIMR);
			break;
		}
		break;
	case IRQ_SBUS_USB:
		break;
	}
}

static inline void sbus_disable_irq(struct irq_data *data)
{
	unsigned int sbus_irq_nr = data->irq - IRQ_SBUS;

	sbus_mask &= ~(1 << sbus_irq_nr);

	switch (data->irq) {
	case IRQ_SBUS_PCIC:
		switch (ps2_pcic_type) {
		case 1:
			outw(0xffff, SBUS_PCIC_IMR1);
			break;
		case 2:
			outw(1, SBUS_PCIC_TIMR);
			break;
		case 3:
			outw(1, SBUS_PCIC3_TIMR);
			break;
		}
		break;
	case IRQ_SBUS_USB:
		break;
	}
}

static unsigned int sbus_startup_irq(struct irq_data *data)
{
	sbus_enable_irq(data);
	return 0;
}

static void sbus_shutdown_irq(struct irq_data *data)
{
	sbus_disable_irq(data);
}

static void sbus_ack_irq(struct irq_data *data)
{
}

static void sbus_end_irq(struct irq_data *data)
{
}

static struct irq_chip sbus_irq_type = {
	.name		= "IOP",
	.irq_startup	= sbus_startup_irq,
	.irq_shutdown	= sbus_shutdown_irq,
	.irq_unmask	= sbus_enable_irq,
	.irq_mask	= sbus_disable_irq,
	.irq_mask_ack	= sbus_ack_irq,
	.irq_eoi	= sbus_end_irq,
};

static irqreturn_t gs_cascade(int irq, void *data)
{
	uint32_t irq_reg;

	//irq_reg = readl(intc_base + GS_CSR);
	irq_reg = inl(GS_CSR) & gs_mask;

	if (irq_reg)
		generic_handle_irq(__fls(irq_reg) + IRQ_GS);

	return IRQ_HANDLED;
}

static struct irqaction cascade_gs_irqaction = {
	.handler = gs_cascade,
	.name = "GS cascade",
};

static irqreturn_t sbus_cascade(int irq, void *data)
{
	uint32_t irq_reg;

	preempt_disable();
	irq_reg = sbus_enter_irq() & sbus_mask;
	if (irq_reg)
		generic_handle_irq(__fls(irq_reg) + IRQ_SBUS);
	sbus_leave_irq();
	preempt_enable_no_resched();

	return IRQ_HANDLED;
}

static struct irqaction cascade_sbus_irqaction = {
	.handler = sbus_cascade,
	.name = "SBUS cascade",
};

static irqreturn_t intc_cascade(int irq, void *data)
{
	uint32_t irq_reg;

	//irq_reg = readl(intc_base + INTC_STAT);
	irq_reg = inl(INTC_STAT) & intc_mask;

	if (irq_reg)
		generic_handle_irq(__fls(irq_reg) + IRQ_INTC);

	return IRQ_HANDLED;
}

static struct irqaction cascade_intc_irqaction = {
	.handler = intc_cascade,
	.name = "INTC cascade",
};

static irqreturn_t dmac_cascade(int irq, void *data)
{
	uint32_t irq_reg;

	//irq_reg = readl(dmac_base + DMAC_STAT);
	irq_reg = inl(DMAC_STAT) & dmac_mask;

	if (irq_reg)
		generic_handle_irq(__fls(irq_reg) + IRQ_DMAC);

	return IRQ_HANDLED;
}

static struct irqaction cascade_dmac_irqaction = {
	.handler = dmac_cascade,
	.name = "DMAC cascade",
};

void __init arch_init_irq(void)
{
	int i;
	int rv;

	/* init CPU irqs */
	mips_cpu_irq_init();

	for (i = 0; i < MIPS_CPU_IRQ_BASE; i++) {
		struct irq_chip *handler;

		if (i < IRQ_DMAC) {
			handler = &intc_irq_type;
		} else if (i < IRQ_GS) {
			handler = &dmac_irq_type;
		} else if (i < IRQ_SBUS) {
			handler = &gs_irq_type;
		} else {
			handler = &sbus_irq_type;
		}
		irq_set_chip_and_handler(i, handler, handle_level_irq);
	}

	/* initialize interrupt mask */
	intc_mask = 0;
	outl(inl(INTC_MASK), INTC_MASK);
	outl(inl(INTC_STAT), INTC_STAT);
	dmac_mask = 0;
	outl(inl(DMAC_MASK), DMAC_MASK);
	gs_mask = 0;
	outl(0xff00, GS_IMR);
	outl(0x00ff, GS_CSR);
	sbus_mask = 0;
	outl((1 << 8) | (1 << 10), SBUS_SMFLG);

	/* Enable cascaded GS IRQ. */
	rv = setup_irq(IRQ_INTC_GS, &cascade_gs_irqaction);
	if (rv) {
		printk("Failed to setup GS IRQ (rv = %d).\n", rv);
	}

	/* Enable cascaded SBUS IRQ. */
	rv = setup_irq(IRQ_INTC_SBUS, &cascade_sbus_irqaction);
	if (rv) {
		printk("Failed to setup SBUS IRQ (rv = %d).\n", rv);
	}

	/* Enable INTC interrupt. */
	rv = setup_irq(IRQ_C0_INTC, &cascade_intc_irqaction);
	if (rv) {
		printk("Failed to setup INTC (rv = %d).\n", rv);
	}

	/* Enable DMAC interrupt. */
	rv = setup_irq(IRQ_C0_DMAC, &cascade_dmac_irqaction);
	if (rv) {
		printk("Failed to setup DMAC (rv = %d).\n", rv);
	}
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	/*
	 * First we check for r4k counter/timer IRQ.
	 */
	if (pending & CAUSEF_IP2) {
		/* INTC interrupt. */
		do_IRQ(IRQ_C0_INTC);
	} else if (pending & CAUSEF_IP3) {
		/* DMAC interrupt. */
		do_IRQ(IRQ_C0_DMAC);
	} else if (pending & CAUSEF_IP7) {
		/* Timer interrupt. */
		do_IRQ(IRQ_C0_IRQ7);
	} else {
		spurious_interrupt();
	}
}
