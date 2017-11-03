/*
 *  PlayStation 2 timer functions
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

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/mipsregs.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/ps2.h>

#define CPU_FREQ  294912000		/* CPU clock frequency (Hz) */
#define BUS_CLOCK (CPU_FREQ/2)		/* bus clock frequency (Hz) */
#define TM_COMPARE_VALUE  (BUS_CLOCK/256/HZ)	/* to generate 100Hz */
#define USECS_PER_JIFFY (1000000/HZ)

#define T0_BASE  0x10000000
#define T0_COUNT (T0_BASE + 0x00)
#define T0_MODE  (T0_BASE + 0x10)
#define T0_COMP  (T0_BASE + 0x20)

/**
 * 	ps2_timer_interrupt - Timer Interrupt Routine
 *
 * 	@regs:   registers as they appear on the stack
 *	         during a syscall/exception.
 *
 * 	Timer interrupt routine, wraps the generic timer_interrupt() but
 * 	sets the timer interrupt delay and clears interrupts first.
 */
static irqreturn_t ps2_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *cd = dev_id;

	/* Clear the interrupt */
	outl(inl(T0_MODE), T0_MODE);

	cd->event_handler(cd);

	return IRQ_HANDLED;
}

static int timer0_periodic(struct clock_event_device *evt)
{
	/* setup 100Hz interval timer */
	outl(0, T0_COUNT);
	outl(TM_COMPARE_VALUE, T0_COMP);

	/* busclk / 256, zret, cue, cmpe, equf */
	outl(2 | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 10), T0_MODE);

	return 0;
}

static int timer0_shutdown(struct clock_event_device *evt)
{
	/* Stop timer. */
	outl(0, T0_MODE);

	return 0;
}

static struct irqaction timer0_irqaction = {
	.handler	= ps2_timer_interrupt,
	.flags		= IRQF_PERCPU | IRQF_TIMER,
	.name		= "intc-timer0",
};

static struct clock_event_device timer0_clockevent_device = {
	.name		= "timer0",
	.features	= CLOCK_EVT_FEAT_PERIODIC, /* TBD: Timer is also able to provide CLOCK_EVT_FEAT_ONESHOT. */

	/* .mult, .shift, .max_delta_ns and .min_delta_ns left uninitialized */

	.rating		= 300, /* TBD: Check value. */
	.irq		= IRQ_INTC_TIMER0,
	.set_state_periodic	= timer0_periodic,
	.set_state_shutdown	= timer0_shutdown,
};

void __init plat_time_init(void)
{
	/* Setup interrupt */
	struct clock_event_device *cd = &timer0_clockevent_device;
	struct irqaction *action = &timer0_irqaction;
	unsigned int cpu = smp_processor_id();

	/* Add timer 0 as clock event source. */
	cd->cpumask = cpumask_of(cpu);
	clockevents_register_device(cd);
	action->dev_id = cd;
	setup_irq(IRQ_INTC_TIMER0, &timer0_irqaction);

	/* Timer 1 is free and can also be configured as clock event source. */

	/* Setup frequency for IP7 timer interrupt. */
	mips_hpt_frequency = CPU_FREQ;
}
