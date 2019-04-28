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

#define NR_IRQS		128

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

/* Input/output processor (IOP) */
#define IOP_IRQ_BASE	64
#define IRQ_IOP_VBLANK	64
#define IRQ_IOP_SBUS	65
#define IRQ_IOP_CDVD	66
#define IRQ_IOP_DMA	67
#define IRQ_IOP_RTC0	68
#define IRQ_IOP_RTC1	69
#define IRQ_IOP_RTC2	70
#define IRQ_IOP_SIO0	71
#define IRQ_IOP_SIO1	72
#define IRQ_IOP_SPU	73
#define IRQ_IOP_PIO	74
#define IRQ_IOP_EVBLANK	75
#define IRQ_IOP_DVD	76
#define IRQ_IOP_DEV9	77
#define IRQ_IOP_RTC3	78
#define IRQ_IOP_RTC4	79
#define IRQ_IOP_RTC5	80
#define IRQ_IOP_SIO2	81
#define IRQ_IOP_HTR0	82
#define IRQ_IOP_HTR1	83
#define IRQ_IOP_HTR2	84
#define IRQ_IOP_HTR3	85
#define IRQ_IOP_USB	86
#define IRQ_IOP_EXTR	87
#define IRQ_IOP_ILINK	88
#define IRQ_IOP_ILNKDMA	89

#define IRQ_IOP_DMAC_MDEC_IN	96	/* Ch 0 */
#define IRQ_IOP_DMAC_MDEC_OUT	97	/* Ch 1 */
#define IRQ_IOP_DMAC_SIF2	98	/* Ch 2 */
#define IRQ_IOP_DMAC_CDVD	99	/* Ch 3 */
#define IRQ_IOP_DMAC_SPU	100	/* Ch 4 */
#define IRQ_IOP_DMAC_PIO	101	/* Ch 5 */
#define IRQ_IOP_DMAC_GPU_OTC	102	/* Ch 6 */
#define IRQ_IOP_DMAC_BE		103	/* Bus error */
#define IRQ_IOP_DMAC_SPU2	104	/* Ch 7 */
#define IRQ_IOP_DMAC_DEV9	105	/* Ch 8 */
#define IRQ_IOP_DMAC_SIF0	106	/* Ch 9 */
#define IRQ_IOP_DMAC_SIF1	107	/* Ch 10 */
#define IRQ_IOP_DMAC_SIO2_IN	108	/* Ch 11 */
#define IRQ_IOP_DMAC_SIO2_OUT	109	/* Ch 12 */

#define IRQ_IOP_SW1	126	/* R3000A Software Interrupt 1 */
#define IRQ_IOP_SW2	127	/* R3000A Software Interrupt 2 */

int __init intc_irq_init(void);
int __init dmac_irq_init(void);
int gs_irq_init(void);

/*
 * IRQs asserted by the I/O processor (IOP) via the sub-system interface (SIF).
 */
void intc_sif_irq(unsigned int irq);

#endif /* __ASM_MACH_PS2_IRQ_H */
