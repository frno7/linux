// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 IRQs
 *
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C)      2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IRQ_H
#define __ASM_MACH_PS2_IRQ_H

#define INTC_STAT	0x1000f000	/* Flags are cleared by writing 1 */
#define INTC_MASK	0x1000f010	/* Bits are reversed by writing 1 */

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
#define IRQ_INTC_VIF0	4	/* VPU0 interface packet expansion engine */
#define IRQ_INTC_VIF1	5	/* VPU1 interface packet expansion engine */
#define IRQ_INTC_VU0	6	/* Vector core operation unit 0 */
#define IRQ_INTC_VU1	7	/* Vector core operation unit 1 */
#define IRQ_INTC_IPU	8	/* Image processor unit (MPEG 2 video etc.) */
#define IRQ_INTC_TIMER0	9	/* Independent screen timer 0 */
#define IRQ_INTC_TIMER1	10	/* Independent screen timer 1 */
#define IRQ_INTC_TIMER2	11	/* Independent screen timer 2 */
#define IRQ_INTC_TIMER3	12	/* Independent screen timer 3 */
#define IRQ_INTC_SFIFO	13	/* Error detected during SFIFO transfers */
#define IRQ_INTC_VU0WD	14	/* VU0 watch dog for RUN (sends force break) */
#define IRQ_INTC_PGPU	15

/* DMA controller */
#define IRQ_DMAC	16
#define IRQ_DMAC_VIF0	16	/* Ch0 VPU0 interface (VIF0) */
#define IRQ_DMAC_VIF1	17	/* Ch1 VPU1 interface (VIF1) */
#define IRQ_DMAC_GIF	18	/* Ch2 Graphics Synthesizer interface (GIF) */
#define IRQ_DMAC_FIPU	19	/* Ch3 from image processor unit (IPU) */
#define IRQ_DMAC_TIPU	20	/* Ch4 to image processor unit (IPU) */
#define IRQ_DMAC_SIF0	21	/* Ch5 sub-system interface 0 (SIF0) */
#define IRQ_DMAC_SIF1	22	/* Ch6 sub-system interface 1 (SIF1) */
#define IRQ_DMAC_SIF2	23	/* Ch7 Sub-system interface 2 (SIF2) */
#define IRQ_DMAC_FSPR	24	/* Ch8 from scratch-pad RAM (SPR) */
#define IRQ_DMAC_TSPR	25	/* Ch9 to scratch-pad RAM (SPR) */
#define IRQ_DMAC_S	29	/* DMA stall */
#define IRQ_DMAC_ME	30	/* MFIFO empty */
#define IRQ_DMAC_BE	31	/* Bus error */

/* Graphics Synthesizer */
#define IRQ_GS		32
#define IRQ_GS_SIGNAL	32	/* GS signal event control */
#define IRQ_GS_FINISH	33	/* GS finish event control */
#define IRQ_GS_HSYNC	34	/* GS horizontal synch interrupt control */
#define IRQ_GS_VSYNC	35	/* GS vertical synch interrupt control */
#define IRQ_GS_EDW	36	/* GS rectangular area write termination */
#define IRQ_GS_EXHSYNC	37
#define IRQ_GS_EXVSYNC	38

/* MIPS IRQs */
#define MIPS_CPU_IRQ_BASE 48
#define IRQ_C0_INTC	50
#define IRQ_C0_DMAC	51
#define IRQ_C0_IRQ7	55

int __init intc_irq_init(void);
int __init dmac_irq_init(void);

#endif /* __ASM_MACH_PS2_IRQ_H */
