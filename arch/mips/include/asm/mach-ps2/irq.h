/*
 * PlayStation 2 IRQs
 *
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_IRQ_H
#define __ASM_PS2_IRQ_H

#define NR_IRQS		56

/*
 * The interrupt controller (INTC) arbitrates interrupts from peripheral
 * devices, except for the DMAC.
 */
#define IRQ_INTC	0
#define IRQ_INTC_GS	0	/* Graphics Synthesizer */
#define IRQ_INTC_SBUS	1	/* Bus connecting the Emotion Engine to the
				   I/O processor (IOP) via the sub-system
				   interface (SIF) */
#define IRQ_INTC_VB_ON	2	/* Vertical blank start */
#define IRQ_INTC_VB_OFF	3	/* Vertical blank end */
#define IRQ_INTC_VIF0	4	/* VPU0 Interface packet expansion engine */
#define IRQ_INTC_VIF1	5	/* VPU1 Interface packet expansion engine */
#define IRQ_INTC_VU0	6	/* Vector Core Operation Unit 0 */
#define IRQ_INTC_VU1	7	/* Vector Core Operation Unit 1 */
#define IRQ_INTC_IPU	8	/* Image processor unit (MPEG 2 video etc.) */
#define IRQ_INTC_TIMER0	9	/* Independent screen timer 0 */
#define IRQ_INTC_TIMER1	10	/* Independent screen timer 1 */
#define IRQ_INTC_TIMER2	11	/* Independent screen timer 2 */
#define IRQ_INTC_TIMER3	12	/* Independent screen timer 3 */
#define IRQ_INTC_SFIFO	13	/* Error detected during SFIFO transfers */
#define IRQ_INTC_VU0WD	14	/* VU0 watch dog for RUN (sends force break) */
#define IRQ_INTC_PGPU	15

/*
 * The DMA controller (DMAC) handles transfers between main memory and
 * peripheral devices or the scratch pad RAM (SPRAM).
 *
 * The DMAC arbitrates the main bus at the same time, and supports chain
 * mode which switches transfer addresses according to DMA tags attached to
 * the transfer. The stall control synchronises two-channel transfers with
 * priority control.
 *
 * Data is transferred in 128-bit words which must be aligned. Bus snooping
 * is not performed.
 */
#define IRQ_DMAC	16
#define IRQ_DMAC_0	16
#define IRQ_DMAC_1	17
#define IRQ_DMAC_2	18
#define IRQ_DMAC_3	19
#define IRQ_DMAC_4	20
#define IRQ_DMAC_5	21
#define IRQ_DMAC_6	22
#define IRQ_DMAC_7	23
#define IRQ_DMAC_8	24
#define IRQ_DMAC_9	25
#define IRQ_DMAC_S	29	/* DMA stall */
#define IRQ_DMAC_ME	30	/* MFIFO empty */
#define IRQ_DMAC_BE	31	/* Buss error */

/* Graphics Synthesizer */
#define IRQ_GS		32
#define IRQ_GS_SIGNAL	32
#define IRQ_GS_FINISH	33
#define IRQ_GS_HSYNC	34
#define IRQ_GS_VSYNC	35
#define IRQ_GS_EDW	36
#define IRQ_GS_EXHSYNC	37
#define IRQ_GS_EXVSYNC	38

/*
 * Bus connecting the Emotion Engine to the I/O processor (IOP)
 * via the sub-system interface (SIF)
 */
#define IRQ_SBUS	40
#define IRQ_SBUS_AIF	40
#define IRQ_SBUS_PCIC	41
#define IRQ_SBUS_USB	42

/* MIPS IRQs */
#define MIPS_CPU_IRQ_BASE 48
#define IRQ_C0_INTC	50
#define IRQ_C0_DMAC	51
#define IRQ_C0_IRQ7	55

#endif /* __ASM_PS2_IRQ_H */
