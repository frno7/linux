/*
 * PlayStation 2 DMA
 *
 * Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_DMA_H
#define __ASM_PS2_DMA_H

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <asm/types.h>
#include <asm/io.h>

//#define DEBUG
#ifdef DEBUG
#define DPRINT(fmt, args...) \
	printk(__FILE__ ": " fmt, ## args)
#define DSPRINT(fmt, args...) \
	prom_printf(__FILE__ ": " fmt, ## args)
#else
#define DPRINT(fmt, args...)
#define DSPRINT(fmt, args...)
#endif

#define DMA_VIF0	0
#define DMA_VIF1	1
#define DMA_GIF		2
#define DMA_IPU_from	3
#define DMA_IPU_to	4
#define DMA_SPR_from	5
#define DMA_SPR_to	6

#define DMA_SENDCH	0
#define DMA_RECVCH	1

#define DMA_TRUNIT	16
#define DMA_ALIGN(x)	((__typeof__(x))(((unsigned long)(x) + (DMA_TRUNIT - 1)) & ~(DMA_TRUNIT - 1)))

#define DMA_TRUNIT_IMG		128
#define DMA_ALIGN_IMG(x)	((__typeof__(x))(((unsigned long)(x) + (DMA_TRUNIT_IMG - 1)) & ~(DMA_TRUNIT_IMG - 1)))

#define DMA_TIMEOUT		(HZ / 2)
#define DMA_POLLING_TIMEOUT	1500000

/* DMA registers */

#define PS2_D_STAT	0x1000e010
#define PS2_D_ENABLER	0x1000f520
#define PS2_D_ENABLEW	0x1000f590

#define PS2_Dn_CHCR	0x0000
#define PS2_Dn_MADR	0x0010
#define PS2_Dn_QWC	0x0020
#define PS2_Dn_TADR	0x0030
#define PS2_Dn_SADR	0x0080

#define CHCR_STOP	0x0000
#define CHCR_SENDN	0x0101
#define CHCR_SENDC	0x0105
#define CHCR_SENDC_TTE	0x0145
#define CHCR_RECVN	0x0100

#define READDMAREG(ch, x)		inl((ch)->base + (x))
#define WRITEDMAREG(ch, x, value)	outl((value), (ch)->base + (x))
#define DMABREAK(ch)	\
    do { \
	 outl(inl(PS2_D_ENABLER) | (1 << 16), PS2_D_ENABLEW); \
	 READDMAREG((ch), PS2_Dn_CHCR); \
	 READDMAREG((ch), PS2_Dn_CHCR); \
	 WRITEDMAREG((ch), PS2_Dn_CHCR, CHCR_STOP); \
	 outl(inl(PS2_D_ENABLER) & ~(1 << 16), PS2_D_ENABLEW); } while (0)
#define IS_DMA_RUNNING(ch)	((READDMAREG((ch), PS2_Dn_CHCR) & 0x0100) != 0)

struct dma_tag {
    u16 qwc;
    u16 id;
    u32 addr;
} __attribute__((aligned(DMA_TRUNIT)));

#define DMATAG_SET(qwc, id, addr)	\
	((u64)(qwc) | ((u64)(id) << 16) | ((u64)(addr) << 32))
#define DMATAG_REFE	0x0000
#define DMATAG_CNT	0x1000
#define DMATAG_NEXT	0x2000
#define DMATAG_REF	0x3000
#define DMATAG_REFS	0x4000
#define DMATAG_CALL	0x5000
#define DMATAG_RET	0x6000
#define DMATAG_END	0x7000

/* DMA channel structures */

struct dma_request;

struct dma_channel {
    int irq;				/* DMA interrupt IRQ # */
    unsigned long base;			/* DMA register base addr */
    int direction;			/* data direction */
    int isspr;				/* true if DMA for scratchpad RAM */
    char *device;			/* request_irq() device name */
    void (*reset)(void);		/* FIFO reset function */
    spinlock_t lock;

    struct dma_request *head, *tail;	/* DMA request queue */
    struct dma_tag *tagp;		/* tag pointer (for destination DMA) */
};

struct dma_ops {
    void (*start)(struct dma_request *, struct dma_channel *);
    int (*isdone)(struct dma_request *, struct dma_channel *);
    unsigned long (*stop)(struct dma_request *, struct dma_channel *);
    void (*free)(struct dma_request *, struct dma_channel *);
};

struct dma_request {
    struct dma_request *next;		/* next request */
    struct dma_ops *ops;		/* DMA operation functions */
};

#define init_dma_request(_req, _ops)	\
    do { (_req)->next = NULL; (_req)->ops = (_ops); } while (0)

struct dma_completion {
    int done;
    spinlock_t lock;
    wait_queue_head_t wait;
};

/* function prototypes */

extern struct dma_channel ps2dma_channels[];
void __init ps2dma_init(void);
irqreturn_t ps2dma_intr_handler(int irq, void *dev_id);
void ps2dma_add_queue(struct dma_request *req, struct dma_channel *ch, int flushall);
void ps2dma_complete(struct dma_completion *x);
void ps2dma_init_completion(struct dma_completion *x);
int ps2dma_intr_safe_wait_for_completion(struct dma_channel *ch, int polling, struct dma_completion *x);
int ps2sdma_send(int chno, const void *ptr, size_t size);

#endif /* __ASM_PS2_DMA_H */
