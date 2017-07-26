/*
 *  Playstation 2 DMA functions.
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

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>

#include <asm/types.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>

#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/dma.h>

struct dma_channel ps2dma_channels[] = {
    { IRQ_DMAC_0, 0x10008000, DMA_SENDCH, 0, "VIF0 DMA", },
    { IRQ_DMAC_1, 0x10009000, DMA_SENDCH, 0, "VIF1 DMA", },
    { IRQ_DMAC_2, 0x1000a000, DMA_SENDCH, 0, "GIF DMA", },
    { IRQ_DMAC_3, 0x1000b000, DMA_RECVCH, 0, "fromIPU DMA", },
    { IRQ_DMAC_4, 0x1000b400, DMA_SENDCH, 0, "toIPU DMA", },
    { IRQ_DMAC_8, 0x1000d000, DMA_RECVCH, 1, "fromSPR DMA", },
    { IRQ_DMAC_9, 0x1000d400, DMA_SENDCH, 1, "toSPR DMA", },
};

irqreturn_t ps2dma_intr_handler(int irq, void *dev_id)
{
    unsigned long flags;
    struct dma_channel *ch = (struct dma_channel *)dev_id;
    struct dma_request *cur_req, *next_req;

    spin_lock_irqsave(&ch->lock, flags);

    /* to prevent stray interrupt while DMA polling */
    outl(1 << (irq - IRQ_DMAC), PS2_D_STAT);

    cur_req = ch->tail;
    if (cur_req == NULL) {
	spin_unlock_irqrestore(&ch->lock, flags);
	return IRQ_HANDLED;
    }
    next_req = cur_req->next;

    if (cur_req->ops->isdone) {
	if (!cur_req->ops->isdone(cur_req, ch)) {
	    spin_unlock_irqrestore(&ch->lock, flags);
	    return IRQ_HANDLED;
	}
    }

    if ((ch->tail = next_req) != NULL)
	next_req->ops->start(next_req, ch);

    cur_req->ops->free(cur_req, ch);

    spin_unlock_irqrestore(&ch->lock, flags);
    return IRQ_HANDLED;
}

void ps2dma_add_queue(struct dma_request *req, struct dma_channel *ch, int flushall)
{
    unsigned long flags;

#if 0
	/* This flushes data and instruction cache. */
    __flush_cache_all();
#else
	/* This functions is only used by graphical routines,
	 * so only flushing of data cache is needed. This will heavily
	 * increase the performance.
	 * TBD: When updating kernel, check if __flush_cache_vmap()
	 * always flushes the complete data cache and never flushes
	 * the instruction cache because of the performance penalty.
	 * TBD: Function would be faster if only the required area is
	 * flushed.
	 */
	if (flushall) {
		__flush_cache_vmap();
	}
#endif

    spin_lock_irqsave(&ch->lock, flags);

    if (ch->tail == NULL) {
	ch->tail = ch->head = req;
	req->ops->start(req, ch);
    } else {
	ch->head->next = req;
	ch->head = req;
    }

    spin_unlock_irqrestore(&ch->lock, flags);
}

/*
 *  Wait-for-completion functions
 */

static inline int dma_sleeping_wait(struct dma_channel *ch, struct dma_completion *x)
{
    unsigned long flags;
    int result = 0;

    spin_lock_irqsave(&x->lock, flags);
    if (!x->done) {
	DECLARE_WAITQUEUE(wait, current);
	add_wait_queue_exclusive(&x->wait, &wait);
	do {
	    set_current_state(TASK_UNINTERRUPTIBLE);
	    spin_unlock_irq(&x->lock);
	    if (schedule_timeout(DMA_TIMEOUT) == 0)
		result = -1;
	    spin_lock_irq(&x->lock);
	} while (!x->done && result == 0);
	remove_wait_queue(&x->wait, &wait);
	if (!x->done) {
	    /* timeout - DMA force break */
	    DMABREAK(ch);
	    /* reset device FIFO */
	    if (ch->reset != NULL)
		ch->reset();
	    /* next request */
	    ps2dma_intr_handler(ch->irq, ch);
	}
    }
    x->done = 0;
    spin_unlock_irqrestore(&x->lock, flags);
    return result;
}

static inline int dma_polling_wait(struct dma_channel *ch, struct dma_completion *x)
{
    unsigned long flags;
    int result = 0;
    int count = DMA_POLLING_TIMEOUT;

    spin_lock_irqsave(&x->lock, flags);
    if (x->done) {
	x->done = 0;
	spin_unlock_irqrestore(&x->lock, flags);
	return result;
    }

    disable_irq(ch->irq);

    do {
	spin_unlock_irqrestore(&x->lock, flags);
	if (!IS_DMA_RUNNING(ch))
	    ps2dma_intr_handler(ch->irq, ch);
	if (--count <= 0)
	    result = -1;
	spin_lock_irqsave(&x->lock, flags);
    } while (!x->done && result == 0);

    if (!x->done) {
	/* timeout - DMA force break */
	DMABREAK(ch);
	/* reset device FIFO */
	if (ch->reset != NULL)
	    ch->reset();
	/* next request */
	ps2dma_intr_handler(ch->irq, ch);
    }
    x->done = 0;
    enable_irq(ch->irq);
    spin_unlock_irqrestore(&x->lock, flags);
    return result;
}

void ps2dma_complete(struct dma_completion *x)
{
    unsigned long flags;

    spin_lock_irqsave(&x->lock, flags);
    x->done = 1;
    wake_up(&x->wait);
    spin_unlock_irqrestore(&x->lock, flags);
}

void ps2dma_init_completion(struct dma_completion *x)
{
    x->done = 0;
    spin_lock_init(&x->lock);
    init_waitqueue_head(&x->wait);
}

int ps2dma_intr_safe_wait_for_completion(struct dma_channel *ch, int polling, struct dma_completion *x)
{
    if (polling)
	return dma_polling_wait(ch, x);
    else
	return dma_sleeping_wait(ch, x);
}

/*
 *  Simple DMA functions
 */

struct sdma_request {
    struct dma_request r;
    dma_addr_t madr;
    size_t qwc;
    struct dma_completion c;
};

static void sdma_send_start(struct dma_request *req, struct dma_channel *ch)
{
    struct sdma_request *sreq = (struct sdma_request *)req;

    WRITEDMAREG(ch, PS2_Dn_MADR, sreq->madr);
    WRITEDMAREG(ch, PS2_Dn_QWC, sreq->qwc);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_SENDN);
}

static void sdma_free(struct dma_request *req, struct dma_channel *ch)
{
    struct sdma_request *sreq = (struct sdma_request *)req;

    ps2dma_complete(&sreq->c);
}

static struct dma_ops sdma_send_ops =
{ sdma_send_start, NULL, NULL, sdma_free };

int ps2sdma_send(int chno, const void *ptr, size_t size)
{
	struct sdma_request sreq;
	struct dma_channel *ch = &ps2dma_channels[chno];
	int result;

	init_dma_request(&sreq.r, &sdma_send_ops);
	sreq.madr = dma_map_single(NULL, (void *)ptr, size, DMA_TO_DEVICE);
	sreq.qwc = size >> 4;
	ps2dma_init_completion(&sreq.c);

	/* Set flushall flag to 0 because we use dma_map_single to flush */
	ps2dma_add_queue((struct dma_request *)&sreq, ch, 0);
	do {
		result = ps2dma_intr_safe_wait_for_completion(ch, in_interrupt(), &sreq.c);
		if (result)
			printk("ps2dma: %s timeout\n", ch->device);
	} while (result != 0);

	/* This is a NOOP */
	dma_unmap_single(NULL, sreq.madr, size, DMA_TO_DEVICE);

	return 0;
}

/*
 * Initialize DMA handlers
 */

void __init ps2dma_init(void)
{
    int i;

    for (i = 0; i < sizeof(ps2dma_channels) / sizeof(ps2dma_channels[0]); i++) {
    	spin_lock_init(&ps2dma_channels[i].lock);

	if (request_irq(ps2dma_channels[i].irq, ps2dma_intr_handler,
			0, ps2dma_channels[i].device,
			&ps2dma_channels[i]))
	    printk("unable to get irq %d\n", i);
    }
}

EXPORT_SYMBOL(ps2dma_channels);
EXPORT_SYMBOL(ps2dma_intr_handler);
EXPORT_SYMBOL(ps2dma_add_queue);
EXPORT_SYMBOL(ps2dma_complete);
EXPORT_SYMBOL(ps2dma_init_completion);
EXPORT_SYMBOL(ps2dma_intr_safe_wait_for_completion);
EXPORT_SYMBOL(ps2sdma_send);
