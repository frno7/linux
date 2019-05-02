// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 DMA controller (DMAC)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

/**
 * DOC:
 *
 * The DMA controller handles transfers between main memory and peripheral
 * devices or the scratch-pad RAM (SPR).
 *
 * The DMAC arbitrates the main bus at the same time, and supports chain
 * mode which switches transfer addresses according to DMA tags attached to
 * the transfer. The stall control synchronises two-channel transfers with
 * priority control.
 *
 * Data is transferred in 128-bit words that must be aligned. Bus snooping
 * is not performed.
 */

#ifndef __ASM_MACH_PS2_DMAC_H
#define __ASM_MACH_PS2_DMAC_H

#include <asm/types.h>

/* Channel 0: Vector core operation unit 0 (VU0) interface (VIF0) */
#define DMAC_VIF0_CHCR		0x10008000	/* VIF0 channel control */
#define DMAC_VIF0_MADR		0x10008010	/* VIF0 memory address */
#define DMAC_VIF0_QWC		0x10008020	/* VIF0 quadword count */
#define DMAC_VIF0_TADR		0x10008030	/* VIF0 tag address */
#define DMAC_VIF0_ASR0		0x10008040	/* VIF0 address stack 0 */
#define DMAC_VIF0_ASR1		0x10008050	/* VIF0 address stack 1 */

/* Channel 1: Vector core operation unit 1 (VU1) interface (VIF1) */
#define DMAC_VIF1_CHCR		0x10009000	/* VIF1 channel control */
#define DMAC_VIF1_MADR		0x10009010	/* VIF1 memory address */
#define DMAC_VIF1_QWC		0x10009020	/* VIF1 quadword count */
#define DMAC_VIF1_TADR		0x10009030	/* VIF1 tag address */
#define DMAC_VIF1_ASR0		0x10009040	/* VIF1 address stack 0 */
#define DMAC_VIF1_ASR1		0x10009050	/* VIF1 address stack 1 */

/* Channel 2: Graphics Synthesizer interface (GIF) */
#define DMAC_GIF_CHCR		0x1000a000	/* GIF channel control */
#define DMAC_GIF_MADR		0x1000a010	/* GIF memory address */
#define DMAC_GIF_QWC		0x1000a020	/* GIF quadword count */
#define DMAC_GIF_TADR		0x1000a030	/* GIF tag address */
#define DMAC_GIF_ASR0		0x1000a040	/* GIF address stack 0 */
#define DMAC_GIF_ASR1		0x1000a050	/* GIF address stack 1 */

/* Channel 3: From image processor unit (IPU) */
#define DMAC_FIPU_CHCR		0x1000b000	/* From IPU channel control */
#define DMAC_FIPU_MADR		0x1000b010	/* From IPU memory address */
#define DMAC_FIPU_QWC		0x1000b020	/* From IPU quadword count */

/* Channel 4: To image processor unit (IPU) */
#define DMAC_TIPU_CHCR		0x1000b400	/* To IPU channel control */
#define DMAC_TIPU_MADR		0x1000b410	/* To IPU memory address */
#define DMAC_TIPU_QWC		0x1000b420	/* To IPU quadword count */
#define DMAC_TIPU_TADR		0x1000b430	/* To IPU tag address */

/* Channel 5: Sub-system interface 0 (SIF0) */
#define DMAC_SIF0_CHCR		0x1000c000	/* SIF0 channel control */
#define DMAC_SIF0_MADR		0x1000c010	/* SIF0 memory address */
#define DMAC_SIF0_QWC		0x1000c020	/* SIF0 quadword count */

/* Channel 6: Sub-system interface 1 (SIF1) */
#define DMAC_SIF1_CHCR		0x1000c400	/* SIF1 channel control */
#define DMAC_SIF1_MADR		0x1000c410	/* SIF1 memory address */
#define DMAC_SIF1_QWC		0x1000c420	/* SIF1 quadword count */
#define DMAC_SIF1_TADR		0x1000c430	/* SIF1 tag address */

/* Channel 7: Sub-system interface 2 (SIF2) */
#define DMAC_SIF2_CHCR		0x1000c800	/* SIF2 channel control */
#define DMAC_SIF2_MADR		0x1000c810	/* SIF2 memory address */
#define DMAC_SIF2_QWC		0x1000c820	/* SIF2 quadword count */

/* Channel 8: From scratch-pad RAM (SPR) */
#define DMAC_FSPR_CHCR		0x1000d000	/* From SPR channel control */
#define DMAC_FSPR_MADR		0x1000d010	/* From SPR memory address */
#define DMAC_FSPR_QWC		0x1000d020	/* From SPR quadword count */
#define DMAC_FSPR_SADR		0x1000d080	/* From SPR address */

/* Channel 9: To scratch-pad RAM (SPR) */
#define DMAC_TSPR_CHCR		0x1000d400	/* To SPR channel control */
#define DMAC_TSPR_MADR		0x1000d410	/* To SPR memory address */
#define DMAC_TSPR_QWC		0x1000d420	/* To SPR quadword count */
#define DMAC_TSPR_TADR		0x1000d430	/* To SPR tag address */
#define DMAC_TSPR_SADR		0x1000d480	/* To SPR address */

#define DMAC_CHCR_DIR_TOMEM	(0 << 0)	/* Direction to memory */
#define DMAC_CHCR_DIR_FROMMEM	(1 << 0)	/* Direction from memory */
#define DMAC_CHCR_MOD_NORMAL	(0 << 2)	/* Mode normal */
#define DMAC_CHCR_MOD_CHAIN	(1 << 2)	/* Mode chain */
#define DMAC_CHCR_MOD_ILEAVE	(2 << 2)	/* Mode interleave */
#define DMAC_CHCR_ASP_NONE	(0 << 4)	/* 0 address stack pointer */
#define DMAC_CHCR_ASP_1ADDR	(1 << 4)	/* 1 address stack pointer */
#define DMAC_CHCR_ASP_2ADDR	(2 << 4)	/* 2 address stack pointer */
#define DMAC_CHCR_TTE_OFF	(0 << 6)	/* Tag transfer enable off */
#define DMAC_CHCR_TTE_ON	(1 << 6)	/* Tag transfer enable on */
#define DMAC_CHCR_TIE_OFF	(0 << 7)	/* Tag interrupt enable off */
#define DMAC_CHCR_TIE_ON	(1 << 7)	/* Tag interrupt enable on */
#define DMAC_CHCR_STR_STOP	(0 << 8)	/* Stop DMA */
#define DMAC_CHCR_STR_START	(1 << 8)	/* Start DMA */

#define DMAC_CHCR_STOP		DMAC_CHCR_STR_STOP
#define DMAC_CHCR_BUSY		DMAC_CHCR_STR_START
#define DMAC_CHCR_SENDN		(DMAC_CHCR_DIR_FROMMEM	| \
				 DMAC_CHCR_MOD_NORMAL	| \
				 DMAC_CHCR_ASP_NONE	| \
				 DMAC_CHCR_TTE_OFF	| \
				 DMAC_CHCR_TIE_OFF	| \
				 DMAC_CHCR_STR_START)
#define DMAC_CHCR_SENDN_TIE	(DMAC_CHCR_DIR_FROMMEM	| \
				 DMAC_CHCR_MOD_NORMAL	| \
				 DMAC_CHCR_ASP_NONE	| \
				 DMAC_CHCR_TTE_OFF	| \
				 DMAC_CHCR_TIE_ON	| \
				 DMAC_CHCR_STR_START)
#define DMAC_CHCR_SENDC		(DMAC_CHCR_DIR_FROMMEM	| \
				 DMAC_CHCR_MOD_CHAIN	| \
				 DMAC_CHCR_ASP_NONE	| \
				 DMAC_CHCR_TTE_OFF	| \
				 DMAC_CHCR_TIE_OFF	| \
				 DMAC_CHCR_STR_START)
#define DMAC_CHCR_SENDC_TTE	(DMAC_CHCR_DIR_FROMMEM	| \
				 DMAC_CHCR_MOD_CHAIN	| \
				 DMAC_CHCR_ASP_NONE	| \
				 DMAC_CHCR_TTE_ON	| \
				 DMAC_CHCR_TIE_OFF	| \
				 DMAC_CHCR_STR_START)
#define DMAC_CHCR_RECVN		(DMAC_CHCR_DIR_TOMEM	| \
				 DMAC_CHCR_MOD_NORMAL	| \
				 DMAC_CHCR_ASP_NONE	| \
				 DMAC_CHCR_TTE_OFF	| \
				 DMAC_CHCR_TIE_OFF	| \
				 DMAC_CHCR_STR_START)
#define DMAC_CHCR_RECVC_TIE	(DMAC_CHCR_DIR_TOMEM	| \
				 DMAC_CHCR_MOD_CHAIN	| \
				 DMAC_CHCR_ASP_NONE	| \
				 DMAC_CHCR_TTE_OFF	| \
				 DMAC_CHCR_TIE_ON	| \
				 DMAC_CHCR_STR_START)

#define DMAC_CTRL	0x1000e000	/* DMAC control */
#define DMAC_STAT	0x1000e010	/* DMAC status */
#define DMAC_PCR	0x1000e020	/* DMAC priority control */
#define DMAC_SQWC	0x1000e030	/* DMAC skip quadword */
#define DMAC_RBSR	0x1000e040	/* DMAC ring buffer size */
#define DMAC_RBOR	0x1000e050	/* DMAC ring buffer offset */
#define DMAC_STADR	0x1000e060	/* DMAC stall address */

/*
 * The lower 16 bits are status bits and the upper 16 bits are mask bits.
 * Status bit cleared by writing 1. Mask bits are reversed by writing 1.
 */
#define DMAC_STAT_MASK	0x1000e010

#define DMAC_STAT_VIF0S	(1 << 0)	/* Ch0 interrupt status VIF0 */
#define DMAC_STAT_VIF1S	(1 << 1)	/* Ch1 interrupt status VIF1 */
#define DMAC_STAT_GIFS	(1 << 2)	/* Ch2 interrupt status GIF */
#define DMAC_STAT_FIPUS	(1 << 3)	/* Ch3 interrupt status from IPU */
#define DMAC_STAT_TIPUS	(1 << 4)	/* Ch4 interrupt status to IPU */
#define DMAC_STAT_SIF0S	(1 << 5)	/* Ch5 interrupt status SIF0 */
#define DMAC_STAT_SIF1S	(1 << 6)	/* Ch6 interrupt status SIF1 */
#define DMAC_STAT_SIF2S	(1 << 7)	/* Ch7 interrupt status SIF2 */
#define DMAC_STAT_FSPRS	(1 << 8)	/* Ch8 interrupt status from SPR */
#define DMAC_STAT_TSPRS	(1 << 9)	/* Ch9 interrupt status to SPR */
#define DMAC_STAT_SIS	(1 << 13)	/* DMA stall interrupt status */
#define DMAC_STAT_MEIS	(1 << 14)	/* MFIFO empty interrupt status */
#define DMAC_STAT_BEIS	(1 << 15)	/* BUSERR interrupt status */
#define DMAC_STAT_VIF0M	(1 << 16)	/* Ch0 interrupt mask VIF0 */
#define DMAC_STAT_VIF1M	(1 << 17)	/* Ch1 interrupt mask VIF1 */
#define DMAC_STAT_GIFM	(1 << 18)	/* Ch2 interrupt mask GIF */
#define DMAC_STAT_FIPUM	(1 << 19)	/* Ch3 interrupt mask from IPU */
#define DMAC_STAT_TIPUM	(1 << 20)	/* Ch4 interrupt mask to IPU */
#define DMAC_STAT_SIF0M	(1 << 21)	/* Ch5 interrupt mask SIF0 */
#define DMAC_STAT_SIF1M	(1 << 22)	/* Ch6 interrupt mask SIF1 */
#define DMAC_STAT_SIF2M	(1 << 23)	/* Ch7 interrupt mask SIF2 */
#define DMAC_STAT_FSPRM	(1 << 24)	/* Ch8 interrupt mask from SPR */
#define DMAC_STAT_TSPRM	(1 << 25)	/* Ch9 interrupt mask to SPR */
#define DMAC_STAT_SIM	(1 << 29)	/* DMA stall interrupt mask */
#define DMAC_STAT_MEIM	(1 << 30)	/* MFIFO empty interrupt mask */

#define DMAC_ENABLER	0x1000f520	/* Acquisition of DMA suspend status */
#define DMAC_ENABLEW	0x1000f590	/* DMA suspend control */

enum dma_tag_reg {		/* Data start address:	Next tag address: */
	dma_tag_id_refe = 0,	/* ADDR			(none) */
	dma_tag_id_cnts = 0,	/* ADDR			(none) */
	dma_tag_id_cnt,		/* next to tag		next to transfer data */
	dma_tag_id_next,	/* next to tag		ADDR */
	dma_tag_id_ref,		/* ADDR			next to tag */
	dma_tag_id_refs,	/* ADDR			next to tag */
	dma_tag_id_call,	/* next to tag		ADDR */
	dma_tag_id_ret,		/* next to tag		Dn_ASR */
	dma_tag_id_end		/* next to tag		(none) */
};

/**
 * enum dma_tag_spr - memory or scratch-pad RAM
 * @dma_tag_spr_memory: select memory
 * @dma_tag_spr_scratchpad: select scratch-pad RAM
 */
enum dma_tag_spr {
	dma_tag_spr_memory,
	dma_tag_spr_scratchpad
};

/**
 * struct dma_tag - DMA tag
 * @qwc: 128-bit quadword count
 * @pce: priority control enable
 * @id: &enum dma_tag_reg tag identifier
 * @irq: interrupt request
 * @addr: address with lower 4 bits zero
 * @spr: &enum dma_tag_spr memory or scratch-pad RAM
 *
 * The DMA tag must be aligned with 16 byte boundaries.
 */
struct dma_tag {
	u64 qwc : 16;
	u64 : 10;
	u64 pce : 2;
	u64 id : 3;
	u64 irq : 1;
	u64 addr : 31;
	u64 spr : 1;

	u64 : 64;
} __attribute__((aligned(16)));

/**
 * struct iop_dma_tag - I/O processor (IOP) DMA tag
 * @addr: IOP address
 * @int_0: assert IOP interupt on completion
 * @ert: FIXME
 * @wc: 32-bit word count
 *
 * The IOP DMA tag must be aligned with 16 byte boundaries.
 */
struct iop_dma_tag {
	u32 addr : 24;
	u32 : 6;
	u32 int_0 : 1;
	u32 ert : 1;

	u32 wc;

	u64 : 64;
} __attribute__((aligned(16)));

#endif /* __ASM_MACH_PS2_DMAC_H */
