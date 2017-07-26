/*
 *  PlayStation 2 integrated device driver
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
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/sched/signal.h>
#include <linux/ps2/dev.h>

#include <asm/types.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/page.h>

#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/dma.h>

#include "ps2dev.h"

/*
 *  memory page list management functions
 */

struct page_list *ps2pl_alloc(int pages)
{
    int i;
    struct page_list *list;

    list = kmalloc(sizeof(*list) +
        ((unsigned long) &list->page[pages]) - ((unsigned long) &list->page[0]), GFP_KERNEL);
    if (list == NULL)
	return NULL;
    list->pages = pages;

    for (i = 0; i < list->pages; i++) {
	list->page[i] = alloc_page(GFP_KERNEL);
	if (list->page[i] == NULL) {
	    /* out of memory */
	    while (--i >= 0)
		put_page(list->page[i]);
	    kfree(list);
	    return NULL;
	}
	DPRINT("ps2pl_alloc: %p\n", page_address(list->page[i]));
    }
    return list;
}

struct page_list *ps2pl_realloc(struct page_list *list, int newpages)
{
    int i;
    struct page_list *newlist;

    if (list->pages >= newpages)
	return list;
    newlist = kmalloc(sizeof(*newlist) +
        ((unsigned long) &list->page[list->pages]) - ((unsigned long) &list->page[0]), GFP_KERNEL);
    if (newlist == NULL)
	return NULL;

    memcpy(&newlist->page[0], &list->page[0],
        ((unsigned long) &list->page[list->pages]) - ((unsigned long) &list->page[0]));
    newlist->pages = newpages;
    for (i = list->pages; i < newpages; i++) {
	newlist->page[i] = alloc_page(GFP_KERNEL);
	if (newlist->page[i] == NULL) {
	    /* out of memory */
	    while (--i >= list->pages)
		put_page(newlist->page[i]);
	    kfree(newlist);
	    return NULL;
	}
    }
    kfree(list);
    return newlist;
}

void ps2pl_free(struct page_list *list)
{
    int i;

    for (i = 0; i < list->pages; i++) {
	put_page(list->page[i]);
	DPRINT("ps2pl_free: %p\n", page_address(list->page[i]));
    }
    kfree(list);
}

int ps2pl_copy_from_user(struct page_list *list, void *from, long len)
{
    int size;
    int index = 0;

    if (list->pages < ((len + ~PAGE_MASK) >> PAGE_SHIFT))
	return -EINVAL;

    while (len) {
	size = len > PAGE_SIZE ? PAGE_SIZE : len;
	DPRINT("ps2pl_copy_from_user: %p<-%p 0x%08x\n", page_address(list->page[index]), from, size);
	if (copy_from_user(page_address(list->page[index++]), from, size))
	    return -EFAULT;
	from = (void *)((unsigned long)from + size);
	len -= size;
    }
    return 0;
}

int ps2pl_copy_to_user(void *to, struct page_list *list, long len)
{
    int size;
    int index = 0;

    if (list->pages < ((len + ~PAGE_MASK) >> PAGE_SHIFT))
	return -EINVAL;

    while (len) {
	size = len > PAGE_SIZE ? PAGE_SIZE : len;
	DPRINT("ps2pl_copy_to_user: %p->%p %08x\n", page_address(list->page[index]), to, size);
	if (copy_to_user(to, page_address(list->page[index++]), size))
	    return -EFAULT;
	to = (void *)((unsigned long)to + size);
	len -= size;
    }
    return 0;
}

/*
 *  make DMA tag
 */

static int ps2dma_make_tag_mem(unsigned long offset, int len, struct dma_tag **tagp, struct dma_tag **lastp, struct page_list *mem)
{
    struct dma_tag *tag;
    int sindex, eindex;
    unsigned long vaddr, next, end;

    DPRINT("ps2dma_make_tag_mem: %08X %08X\n", offset, len);
    end = offset + len;
    sindex = offset >> PAGE_SHIFT;
    eindex = (end - 1) >> PAGE_SHIFT;
    tag = kmalloc(sizeof(struct dma_tag) * (eindex - sindex + 2), GFP_KERNEL);
    if (tag == NULL)
	return -ENOMEM;
    if ((((unsigned long)tag) & (DMA_TRUNIT - 1)) != 0)  {
	printk(KERN_ERR "ps2dma_make_tag_mem: tag is not DMA aligned.\n");
	return -ENOMEM;
    }
    *tagp = tag;

    while (sindex <= eindex) {
	vaddr = ((unsigned long) page_address(mem->page[sindex])) + (offset & ~PAGE_MASK);
	next = (offset + PAGE_SIZE) & PAGE_MASK;
	tag->id = DMATAG_REF;
	tag->qwc = (next < end ? next - offset : end - offset) >> 4;
	tag->addr = virt_to_bus((void *)vaddr);
	DPRINT("ps2dma_make_tag_mem: tag %08X %08X\n", tag->addr, tag->qwc);
	tag++;
	offset = next;
	sindex++;
    }

    if (lastp)
	*lastp = tag;

    tag->id = DMATAG_END;
    tag->qwc = 0;

    return BUFTYPE_MEM;
}

static int ps2dma_make_tag_user(unsigned long start, int len, struct dma_tag **tagp, struct dma_tag **lastp, struct page_list **memp)
{
    struct page_list *mem;

    DPRINT("ps2dma_make_tag_user: %08X %08X\n", start, len);
    if (memp == NULL)
	return -EINVAL;
    if ((mem = ps2pl_alloc((len + PAGE_SIZE - 1) >> PAGE_SHIFT)) == NULL)
	return -ENOMEM;
    if (ps2dma_make_tag_mem(0, len, tagp, lastp, mem) < 0) {
	ps2pl_free(mem);
	return -ENOMEM;
    }
    *memp = mem;
    return BUFTYPE_USER;
}

int ps2dma_make_tag(unsigned long start, int len, struct dma_tag **tagp, struct dma_tag **lastp, struct page_list **memp)
{
    struct vm_area_struct *vma;

    DPRINT("ps2dma_make_tag: %08X %08X\n", start, len);

    /* alignment check */
    if ((start & (DMA_TRUNIT - 1)) != 0 ||
	(len & (DMA_TRUNIT - 1)) != 0 || len <= 0)
	return -EINVAL;

    if ((vma = find_vma(current->mm, start)) == NULL)
	return -EINVAL;
    if (vma->vm_start > start)
        return -EINVAL;
    DPRINT("ps2dma_make_tag: vma %08X-%08X\n", vma->vm_start, vma->vm_end);

    return ps2dma_make_tag_user(start, len, tagp, lastp, memp);
}


void ps2dma_dev_end(struct dma_request *req, struct dma_channel *ch)
{
    struct dma_dev_request *dreq = container_of(req, struct dma_dev_request, r);
    unsigned long flags;
    struct dma_devch *devch = dreq->devch;
    struct dma_device *dev = dreq->devch->device;

    spin_lock_irqsave(&dev->lock, flags);

    DPRINT("ps2dma_dev_end: qct=%d\n", devch->qct);
    devch->qsize -= dreq->qsize;
    if (--devch->qct <= 0) {        /* request queue empty */
	wake_up(&dev->empty);
	if (dev->intr_mask & (1 << ch->direction)) {
	    dev->intr_flag |= 1 << ch->direction;
	    if (dev->sig)
		send_sig(dev->sig, dev->ts, 1);
	}
    }
    wake_up(&devch->done);

    dreq->free(req, ch);

    spin_unlock_irqrestore(&dev->lock, flags);
}

/*
 * DMA operations for send
 */

static void dma_send_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = container_of(container_of(req, struct dma_dev_request, r), struct udma_request, r);

    DPRINT("dma_send_start: %08X %08X\n", ureq->tag->addr, ureq->tag->qwc);
    WRITEDMAREG(ch, PS2_Dn_TADR, virt_to_bus(ureq->tag));
    WRITEDMAREG(ch, PS2_Dn_QWC, 0);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_SENDC);
}

static void dma_send_spr_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = container_of(container_of(req, struct dma_dev_request, r), struct udma_request, r);

    DPRINT("dma_send_spr_start: %08X %08X %08X\n", ureq->tag->addr, ureq->tag->qwc, ureq->saddr);
    WRITEDMAREG(ch, PS2_Dn_SADR, ureq->saddr);
    WRITEDMAREG(ch, PS2_Dn_TADR, virt_to_bus(ureq->tag));
    WRITEDMAREG(ch, PS2_Dn_QWC, 0);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_SENDC);
}

static unsigned long dma_stop(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = container_of(container_of(req, struct dma_dev_request, r), struct udma_request, r);
    unsigned long vaddr = ureq->vaddr;
    struct dma_tag *tag = ureq->tag;
    unsigned long eaddr;
    unsigned long maddr, qwc;
    struct dma_tag *taddr;


    /* DMA force break */
    DMABREAK(ch);
    maddr = READDMAREG(ch, PS2_Dn_MADR);
    qwc = READDMAREG(ch, PS2_Dn_QWC);
    taddr = (struct dma_tag *)bus_to_virt(READDMAREG(ch, PS2_Dn_TADR));
    DPRINT("dma_stop :%08X %08X %08X\n", maddr, qwc, (long)taddr);

    while (tag->qwc) {
	if (taddr == tag && qwc == 0)
	    return vaddr;

	eaddr = tag->addr + (tag->qwc << 4);
	if ((qwc != 0 && maddr == tag->addr) ||
	    (maddr > tag->addr && maddr < eaddr) ||
	    (qwc == 0 && maddr == eaddr)) {
	    /* if maddr points the last address of DMA request,
	       the request is already finished */
	    if (maddr == eaddr && tag[1].qwc == 0)
		return 0;
	    return vaddr + (maddr - tag->addr);
	}
	vaddr += tag->qwc << 4;
	tag++;
    }
    return 0;		/* cannot get virtual address */
}

static void dma_free(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = container_of(container_of(req, struct dma_dev_request, r), struct udma_request, r);

    DPRINT("dma_free %08X\n", ureq->mem);
    if (ureq->mem)
	ps2pl_free(ureq->mem);
    if (ureq->done)
	*ureq->done = 1;
    kfree(ureq->tag);
    kfree(ureq);
}

static struct dma_ops dma_send_ops =
{ dma_send_start, NULL, dma_stop, ps2dma_dev_end };
static struct dma_ops dma_send_spr_ops =
{ dma_send_spr_start, NULL, dma_stop, ps2dma_dev_end };

/*
 * DMA operations for receive
 */

static void dma_recv_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = container_of(container_of(req, struct dma_dev_request, r), struct udma_request, r);

    ch->tagp = ureq->tag;
    DPRINT("dma_recv_start: %08X %08X\n", ch->tagp->addr, ch->tagp->qwc);
    WRITEDMAREG(ch, PS2_Dn_MADR, ch->tagp->addr);
    WRITEDMAREG(ch, PS2_Dn_QWC, ch->tagp->qwc);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_RECVN);
    ch->tagp++;
}

static void dma_recv_spr_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request *ureq = container_of(container_of(req, struct dma_dev_request, r), struct udma_request, r);

    ch->tagp = ureq->tag;
    DPRINT("dma_recv_spr_start: %08X %08X %08X\n", ch->tagp->addr, ch->tagp->qwc, ureq->saddr);
    WRITEDMAREG(ch, PS2_Dn_SADR, ureq->saddr);
    WRITEDMAREG(ch, PS2_Dn_MADR, ch->tagp->addr);
    WRITEDMAREG(ch, PS2_Dn_QWC, ch->tagp->qwc);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_RECVN);
    ch->tagp++;
}

static int dma_recv_isdone(struct dma_request *req, struct dma_channel *ch)
{
    DPRINT("dma_recv_isdone: %08X %08X\n", ch->tagp->addr, ch->tagp->qwc);
    if (ch->tagp->qwc <= 0)
	return 1;		/* chain DMA is finished */

    WRITEDMAREG(ch, PS2_Dn_MADR, ch->tagp->addr);
    WRITEDMAREG(ch, PS2_Dn_QWC, ch->tagp->qwc);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_RECVN);
    ch->tagp++;
    return 0;			/* chain DMA is not finished */
}

static struct dma_ops dma_recv_ops =
{ dma_recv_start, dma_recv_isdone, dma_stop, ps2dma_dev_end };
static struct dma_ops dma_recv_spr_ops =
{ dma_recv_spr_start, dma_recv_isdone, dma_stop, ps2dma_dev_end };

/*
 * DMA operations for send request list
 */

static void dma_sendl_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_sendl_request *usreq = container_of(container_of(req, struct dma_dev_request, r), struct udma_sendl_request, r);

    DPRINT("dma_sendl_start: %08X\n", usreq->tag);

    WRITEDMAREG(ch, PS2_Dn_TADR, virt_to_bus(usreq->tag));
    WRITEDMAREG(ch, PS2_Dn_QWC, 0);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_SENDC);
}

static unsigned long dma_sendl_stop(struct dma_request *req, struct dma_channel *ch)
{
    DMABREAK(ch);
    return 0;
}

static void dma_sendl_free(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_sendl_request *usreq = container_of(container_of(req, struct dma_dev_request, r), struct udma_sendl_request, r);
    void *p, *q;

    DPRINT("dma_sendl_free\n");
    p = usreq->mem_tail;
    while (p) {
	q = *(void **)p;
	free_page((unsigned long)p);
	p = q;
    }
    p = usreq->tag_tail;
    while (p) {
	q = *(void **)p;
	free_page((unsigned long)p);
	p = q;
    }
    kfree(usreq);
}

static struct dma_ops dma_sendl_ops =
{ dma_sendl_start, NULL, dma_sendl_stop, ps2dma_dev_end };

/*
 * DMA operations for receive request list
 */

static void dma_list_start(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request_list *ureql = container_of(container_of(req, struct dma_dev_request, r), struct udma_request_list, r);

    ureql->index = 0;
    ureql->ureq[ureql->index]->r.r.ops->start((struct dma_request *)ureql->ureq[ureql->index], ch);
}

static int dma_list_isdone(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request_list *ureql = container_of(container_of(req, struct dma_dev_request, r), struct udma_request_list, r);
    struct udma_request *ureq = ureql->ureq[ureql->index];

    if (ureq->r.r.ops->isdone)
	if (!ureq->r.r.ops->isdone((struct dma_request *)ureq, ch))
	    return 0;		/* not finished */

    if (++ureql->index < ureql->reqs) {
	ureql->ureq[ureql->index]->r.r.ops->start(&ureql->ureq[ureql->index]->r.r, ch);
	return 0;		/* not finished */
    }
    return 1;			/* finished */
}

static unsigned long dma_list_stop(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request_list *ureql = container_of(container_of(req, struct dma_dev_request, r), struct udma_request_list, r);
    struct udma_request *ureq = ureql->ureq[ureql->index];

    return ureq->r.r.ops->stop(&ureq->r.r, ch);
}

static void dma_list_free(struct dma_request *req, struct dma_channel *ch)
{
    struct udma_request_list *ureql = container_of(container_of(req, struct dma_dev_request, r), struct udma_request_list, r);
    int i;

    for (i = 0; i < ureql->reqs; i++)
	ureql->ureq[i]->r.free(&ureql->ureq[i]->r.r, ch);
    kfree(ureql);
}

static struct dma_ops dma_recv_list_ops =
{ dma_list_start, dma_list_isdone, dma_list_stop, ps2dma_dev_end };

/*
 *  User mode DMA functions
 */

int ps2dma_check_and_add_queue(struct dma_dev_request *req, int nonblock)
{
    unsigned long flags;
    struct dma_devch *devch = req->devch;

#define QUEUEABLE(_devch, _qsize)	\
	((_devch)->qct < (_devch)->qlimit && \
	 ((_devch)->qsize + (_qsize) <= DMA_USER_LIMIT || \
	  (_devch)->qsize == 0))

    spin_lock_irqsave(&devch->device->lock, flags);

    if (!QUEUEABLE(devch, req->qsize)) {
	if (nonblock) {
	    spin_unlock_irqrestore(&devch->device->lock, flags);
	    return -EAGAIN;
	} else {
	    DECLARE_WAITQUEUE(wait, current);

	    add_wait_queue(&devch->done, &wait);
	    while (!QUEUEABLE(devch, req->qsize) & !signal_pending(current)) {
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irq(&devch->device->lock);
		schedule();
		spin_lock_irq(&devch->device->lock);
	    }
	    remove_wait_queue(&devch->done, &wait);

	    if (signal_pending(current)) {
		spin_unlock_irqrestore(&devch->device->lock, flags);
		return -ERESTARTSYS;		/* signal arrived */
	    }
	}
    }

    devch->qct++;
    devch->qsize += req->qsize;

    ps2dma_add_queue(&req->r, devch->channel, 1);

    spin_unlock_irqrestore(&devch->device->lock, flags);

    return 0;
}

int ps2dma_write(struct dma_device *dev, struct ps2_packet *pkt, int nonblock)
{
    struct udma_request *ureq;
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    int result;

    DPRINT("dma_write %08X %08X %d\n", pkt->ptr, pkt->len, nonblock);

    /* alignment check */
    if (((unsigned long)pkt->ptr & (DMA_TRUNIT - 1)) != 0 ||
	(pkt->len & (DMA_TRUNIT - 1)) != 0 || pkt->len <= 0)
	return -EINVAL;

    if ((ureq = kmalloc(sizeof(struct udma_request), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_dev_request(&ureq->r, &dma_send_ops, devch, pkt->len, dma_free);
    ureq->vaddr = (unsigned long)pkt->ptr;
    ureq->done = NULL;

    if ((result = ps2dma_make_tag_user((unsigned long)pkt->ptr, pkt->len, &ureq->tag, NULL, &ureq->mem)) < 0) {
	kfree(ureq);
	return result;
    }
    if ((result = ps2pl_copy_from_user(ureq->mem, pkt->ptr, pkt->len))) {
	kfree(ureq);
	return result;
    }
    result = ps2dma_check_and_add_queue(&ureq->r, nonblock);
    if (result < 0) {
	ps2pl_free(ureq->mem);
	kfree(ureq->tag);
	kfree(ureq);
	return result;
    }
    return pkt->len;
}

static int make_send_request(struct udma_request **ureqp,
			     struct dma_device *dev, struct dma_channel *ch,
			     struct ps2_packet *pkt, struct dma_tag **lastp)
{
    struct udma_request *ureq;
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    int result, len;

    if ((ureq = kmalloc(sizeof(struct udma_request), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_dev_request(&ureq->r, &dma_send_ops, devch, 0, dma_free);
    ureq->vaddr = (unsigned long)pkt->ptr;
    ureq->mem = NULL;
    ureq->done = NULL;

    if (!ch->isspr) {
	len = pkt->len;
    } else {
	DPRINT("make_send_request : SPR\n");
	ureq->r.r.ops = &dma_send_spr_ops;
	ureq->saddr = ((struct ps2_packet_spr *)pkt)->offset;
	len = ((struct ps2_packet_spr *)pkt)->len;
    }
    switch (result = ps2dma_make_tag((unsigned long)pkt->ptr, len, &ureq->tag, lastp, &ureq->mem)) {
    case BUFTYPE_MEM:
	DPRINT("make_send_request : BUFTYPE_MEM\n");
	break;
    case BUFTYPE_SPR:
	DPRINT("make_send_request : BUFTYPE_SPR\n");
	if (!ch->isspr)
	    break;
	/* both src and dest are SPR */
	kfree(ureq->tag);
	kfree(ureq);
	return -EINVAL;
    case BUFTYPE_USER:
	DPRINT("make_send_request : BUFTYPE_USER\n");
	if ((result = ps2pl_copy_from_user(ureq->mem, pkt->ptr, len))) {
	    ps2pl_free(ureq->mem);
	    kfree(ureq->tag);
	    kfree(ureq);
	    return result;
	}
	ureq->r.qsize = len;
	break;
    default:
	kfree(ureq);
	return result;
    }

    *ureqp = ureq;
    return 0;
}

int ps2dma_send(struct dma_device *dev, struct ps2_packet *pkt, int async)
{
    struct udma_request *ureq = NULL;
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    struct dma_channel *ch = devch->channel;
    volatile int done = 0;
    int result;

    DPRINT("dma_send %08X %08X %d\n", pkt->ptr, pkt->len, async);
    if ((result = make_send_request(&ureq, dev, ch, pkt, NULL)))
	return result;

    if (!async)
	ureq->done = &done;

    result = ps2dma_check_and_add_queue(&ureq->r, 0);
    if (result < 0) {
	dma_free(&ureq->r.r, ch);
	return result;
    }

    if (!async && !done) {
	DECLARE_WAITQUEUE(wait, current);
	DPRINT("dma_send: sleep\n");

	add_wait_queue(&devch->done, &wait);
	while (1) {
	    set_current_state(TASK_INTERRUPTIBLE);
	    if (done || signal_pending(current))
		break;
	    schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&devch->done, &wait);

	if (signal_pending(current))
	    result = -ERESTARTNOHAND; /* already queued - don't restart */

	spin_lock_irq(&devch->device->lock);
	if (!done)
	    ureq->done = NULL;
	spin_unlock_irq(&devch->device->lock);
	DPRINT("dma_send: done\n");
    }
    return result;
}

int ps2dma_send_list(struct dma_device *dev, int num, struct ps2_packet *pkts)
{
    struct udma_sendl_request *usreq;
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    struct dma_channel *ch = devch->channel;
    struct dma_tag *tag, *tag_bottom;
    int i;
    int result;

    DPRINT("dma_send_list\n");
    if (num <= 0)
	return -EINVAL;
    if ((usreq = kmalloc(sizeof(struct udma_sendl_request), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_dev_request(&usreq->r, &dma_sendl_ops, devch, 0, dma_sendl_free);

    if ((usreq->tag_head = (struct dma_tag *)get_zeroed_page(GFP_KERNEL)) /* TBD: check page reference counter */
	== NULL) {
	kfree(usreq);
	return -ENOMEM;
    }
    usreq->tag_tail = usreq->tag_head;
    *(void **)usreq->tag_head = NULL;
    tag = usreq->tag = &(usreq->tag_head[1]);
    tag_bottom = &(usreq->tag_head[(PAGE_SIZE / DMA_TRUNIT) - 1]);
    usreq->mem_head = usreq->mem_tail = NULL;
    
    for (i = 0; i < num; i++) {
	unsigned long start;
	int len;

	start = (unsigned long)pkts[i].ptr;
	len = pkts[i].len;
	DPRINT(" request %d %08X %08X\n", i, start, len);

	/* alignment check */
	if ((start & (DMA_TRUNIT - 1)) != 0 ||
	    (len & (DMA_TRUNIT - 1)) != 0 || len <= 0) {
	    dma_sendl_free(&usreq->r.r, ch);
	    return -EINVAL;
	}

	    while (len > 0) {
		int size = (len > PAGE_SIZE - DMA_TRUNIT) ? PAGE_SIZE - DMA_TRUNIT : len;
		void *nextmem;
		
		if ((nextmem = (void *)get_zeroed_page(GFP_KERNEL)) == NULL) { /* TBD: check page reference counter */
		    dma_sendl_free(&usreq->r.r, ch);
		    return -ENOMEM;
		}
		if (usreq->mem_head != NULL)
		    *(void **)usreq->mem_head = nextmem;
		*(void **)nextmem = NULL;
		usreq->mem_head = nextmem;
		if (usreq->mem_tail == NULL)
		    usreq->mem_tail = usreq->mem_head;
		nextmem += DMA_TRUNIT;

		DPRINT(" copy_from_user: %08X <- %08X %08X\n", nextmem, start, size);
		if (copy_from_user(nextmem, (void *)start, size)) {
		    dma_sendl_free(&usreq->r.r, ch);
		    return -EFAULT;
		}
		tag->id = DMATAG_REF;
		tag->qwc = size >> 4;
		tag->addr = virt_to_bus(nextmem);
		DPRINT(" tag %08X %08X %08X\n", tag, tag->addr, tag->qwc);
		start += size;
		len -= size;
		tag++;

		if (tag >= tag_bottom) {
		    struct dma_tag *nexthead, *nexttag;

		    if ((nexthead = (struct dma_tag *)get_zeroed_page(GFP_KERNEL)) == NULL) { /* TBD: check page reference counter */
			dma_sendl_free(&usreq->r.r, ch);
			return -ENOMEM;
		    }
		    DPRINT(" alloc tag %08X\n", nexthead);
		    *(void **)usreq->tag_head = nexthead;
		    *(void **)nexthead = NULL;
		    usreq->tag_head = nexthead;
		    nexttag = &(usreq->tag_head[1]);
		    tag_bottom = &(usreq->tag_head[(PAGE_SIZE / DMA_TRUNIT) - 1]);
		    DPRINT(" tag next %08X -> %08X\n", tag, nexttag);
		    tag->id = DMATAG_NEXT;
		    tag->qwc = 0;
		    tag->addr = virt_to_bus((void *)nexttag);
		    tag = nexttag;
		}
	    }
    }

    tag->id = DMATAG_END;
    tag->qwc = 0;
    DPRINT(" tag finish %08X\n", tag);

    result = ps2dma_check_and_add_queue(&usreq->r, 0);
    if (result < 0) {
	dma_sendl_free(&usreq->r.r, ch);
	return result;
    }
    return 0;
}

static int make_recv_request(struct udma_request **ureqp,
			     struct dma_device *dev, struct dma_channel *ch,
			     struct ps2_packet *pkt, 
			     struct page_list **memp, int async)
{
    struct udma_request *ureq;
    int result, len;
    struct dma_devch *devch = &dev->devch[DMA_RECVCH];

    if ((ureq = kmalloc(sizeof(struct udma_request), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_dev_request(&ureq->r, &dma_recv_ops, devch, 0, dma_free);
    ureq->vaddr = (unsigned long)pkt->ptr;
    ureq->mem = NULL;
    ureq->done = NULL;

    if (!ch->isspr) {
	len = pkt->len;
    } else {
	ureq->r.r.ops = &dma_recv_spr_ops;
	ureq->saddr = ((struct ps2_packet_spr *)pkt)->offset;
	len = ((struct ps2_packet_spr *)pkt)->len;
	DPRINT("make_recv_request : SPR\n");
    }
    switch (result = ps2dma_make_tag((unsigned long)pkt->ptr, len, &ureq->tag, NULL, memp)) {
    case BUFTYPE_MEM:
	DPRINT("make_recv_request : BUFTYPE_MEM\n");
	break;
    case BUFTYPE_SPR:
	DPRINT("make_recv_request : BUFTYPE_SPR\n");
	if (!ch->isspr)
	    break;
	/* both src and dest are SPR */
	kfree(ureq->tag);
	kfree(ureq);
	return -EINVAL;
    case BUFTYPE_USER:
	DPRINT("make_recv_request : BUFTYPE_USER\n");
	if (async) {
	    /* no asynchronous copy_to_user function */
	    kfree(ureq->tag);
	    kfree(ureq);
	    ps2pl_free(*memp);
	    return -EINVAL;
	}
	ureq->r.qsize = len;
	break;
    default:
	kfree(ureq);
	return result;
    }

    *ureqp = ureq;
    return 0;
}

int ps2dma_recv(struct dma_device *dev, struct ps2_packet *pkt, int async)
{
    struct udma_request *ureq;
    struct dma_devch *devch = &dev->devch[DMA_RECVCH];
    struct dma_channel *ch = devch->channel;
    struct page_list *recv_mem = NULL;
    volatile int done = 0;
    int result;

    DPRINT("dma_recv %08X %08X %d\n", pkt->ptr, pkt->len, async);
    if ((result = make_recv_request(&ureq, dev, ch, pkt, &recv_mem, async)))
	return result;

    if (!async)
	ureq->done = &done;

    result = ps2dma_check_and_add_queue(&ureq->r, 0);
    if (result < 0) {
	dma_free(&ureq->r.r, ch);
	return result;
    }

    if (!async && !done) {
	DECLARE_WAITQUEUE(wait, current);
	DPRINT("dma_recv: sleep\n");

	add_wait_queue(&devch->done, &wait);
	while (1) {
	    set_current_state(TASK_INTERRUPTIBLE);
	    if (done || signal_pending(current))
		break;
	    schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&devch->done, &wait);

	if (signal_pending(current))
	    result = -ERESTARTNOHAND; /* already queued - don't restart */

	spin_lock_irq(&devch->device->lock);
	if (!done)
	    ureq->done = NULL;
	spin_unlock_irq(&devch->device->lock);
	DPRINT("dma_recv: done\n");

	if (recv_mem != NULL && result == 0) {
	    if (!ch->isspr)
		result = ps2pl_copy_to_user(pkt->ptr, recv_mem, pkt->len);
	    else
		result = ps2pl_copy_to_user(pkt->ptr, recv_mem, ((struct ps2_packet_spr *)pkt)->len);
	    ps2pl_free(recv_mem);
	}
    }
    return result;
}

int ps2dma_recv_list(struct dma_device *dev, int num, struct ps2_packet *pkts)
{
    struct udma_request_list *ureql;
    struct dma_devch *devch = &dev->devch[DMA_RECVCH];
    struct dma_channel *ch = devch->channel;
    int result, i;

    if (num <= 0)
	return -EINVAL;
    if ((ureql = kmalloc(sizeof(struct udma_request_list) + sizeof(struct udma_request *) * num, GFP_KERNEL)) == NULL)
	return -ENOMEM;
    init_dma_dev_request(&ureql->r, &dma_recv_list_ops, devch, 0, dma_list_free);
    ureql->reqs = num;
    ureql->index = 0;

    for (i = 0; i < num; i++) {
	if ((result = make_recv_request(&ureql->ureq[i], dev, ch, &pkts[i], NULL, 1))) {
	    while (--i >= 0)
		dma_free(&ureql->ureq[i]->r.r, ch);
	    kfree(ureql);
	    return result;
	}
    }

    result = ps2dma_check_and_add_queue(&ureql->r, 0);
    if (result < 0) {
	dma_list_free(&ureql->r.r, ch);
	return result;
    }
    return 0;
}

int ps2dma_stop(struct dma_device *dev, int dir, struct ps2_pstop *pstop)
{
    unsigned long flags;
    struct dma_channel *ch = dev->devch[dir].channel;
    struct dma_request *hreq, **reqp, *next;
    int stop = 0;

    if (ch == NULL)
	return -1;

    pstop->ptr = NULL;
    pstop->qct = 0;

    spin_lock_irq(&dev->lock);
    spin_lock_irqsave(&ch->lock, flags);

    /* delete all DMA requests from the queue */
    reqp = &ch->tail;
    hreq = NULL;
    while (*reqp != NULL) {
    	struct dma_dev_request *dreq = container_of(*reqp, struct dma_dev_request, r);
	if ((*reqp)->ops->free == ps2dma_dev_end &&
	    dreq->devch->device == dev) {
	    if (!stop && reqp == &ch->tail) {
		/* the request is processing now - stop DMA */
		if ((pstop->ptr = (void *)(*reqp)->ops->stop(*reqp, ch)))
		    stop = 1;
	    } else {
		if (pstop->ptr == NULL &&
		    (*reqp)->ops->stop == dma_stop) {
		    struct udma_request *ureq = container_of(dreq, struct udma_request, r);
		    pstop->ptr = (void *)ureq->vaddr;
		}
	    }
	    pstop->qct++;

	    next = (*reqp)->next;
	    dreq->free(*reqp, ch);
	    *reqp = next;
	} else {
	    hreq = *reqp;
	    reqp = &(*reqp)->next;
	}
    }
    ch->head = hreq;
    dev->devch[dir].qct = 0;
    dev->devch[dir].qsize = 0;

    spin_unlock_irqrestore(&ch->lock, flags);

    if (stop)
	ps2dma_intr_handler(ch->irq, ch);

    spin_unlock_irq(&dev->lock);

    return 0;
}

int ps2dma_get_qct(struct dma_device *dev, int dir, int param)
{
    int qct;
    struct dma_devch *devch = &dev->devch[dir];

    spin_lock_irq(&devch->device->lock);

    if (param <= 0) {
	qct = devch->qct;
    } else {
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&devch->done, &wait);
	while ((qct = devch->qct) >= param) {
	    set_current_state(TASK_INTERRUPTIBLE);
	    spin_unlock_irq(&devch->device->lock);
	    schedule();
	    spin_lock_irq(&devch->device->lock);
	}
	remove_wait_queue(&devch->done, &wait);

	if (signal_pending(current)) {
	    qct = -ERESTARTSYS;			/* signal arrived */
	}
    }

    spin_unlock_irq(&devch->device->lock);

    return qct;
}

int ps2dma_set_qlimit(struct dma_device *dev, int dir, int param)
{
    int oldlimit;
    struct dma_devch *devch = &dev->devch[dir];

    if (param <= 0 || param > DMA_QUEUE_LIMIT_MAX)
	return -EINVAL;

    oldlimit = devch->qlimit;
    devch->qlimit = param;
    return oldlimit;
}

struct dma_device *ps2dma_dev_init(int send, int recv)
{
    struct dma_device *dev;

    if ((dev = kmalloc(sizeof(struct dma_device), GFP_KERNEL)) == NULL)
	return NULL;
    memset(dev, 0, sizeof(struct dma_device));
    dev->ts = current;
    spin_lock_init(&dev->lock);
    init_waitqueue_head(&dev->empty);

    if (send >= 0) {
	dev->devch[DMA_SENDCH].channel = &ps2dma_channels[send];
	dev->devch[DMA_SENDCH].qlimit = DMA_QUEUE_LIMIT_MAX;
	dev->devch[DMA_SENDCH].device = dev;
	init_waitqueue_head(&dev->devch[DMA_SENDCH].done);
    }
    if (recv >= 0) {
	dev->devch[DMA_RECVCH].channel = &ps2dma_channels[recv];
	dev->devch[DMA_RECVCH].qlimit = DMA_QUEUE_LIMIT_MAX;
	dev->devch[DMA_RECVCH].device = dev;
	init_waitqueue_head(&dev->devch[DMA_RECVCH].done);
    }

    return dev;
}

int ps2dma_finish(struct dma_device *dev)
{
    struct ps2_pstop pstop;
    long timeout;

    DPRINT("finish\n");

    if (dev->devch[DMA_SENDCH].qct != 0 ||
	dev->devch[DMA_RECVCH].qct != 0) {
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&dev->empty, &wait);
	while (1) {
	    set_current_state(TASK_INTERRUPTIBLE);

	    if (current->flags & PF_SIGNALED) {
		/* closed by a signal */
		DPRINT("closed by a signal\n");
		flush_signals(current);
	    }

	    if (dev->devch[DMA_SENDCH].qct == 0 &&
		dev->devch[DMA_RECVCH].qct == 0)
		break;

	    timeout = schedule_timeout(DMA_TIMEOUT);

	    if (signal_pending(current) ||
		(current->flags & PF_SIGNALED && timeout == 0)) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&dev->empty, &wait);

		/* reset device FIFO */
		if (dev->devch[DMA_SENDCH].channel->reset != NULL)
		    dev->devch[DMA_SENDCH].channel->reset();
		/* force break by a signal */
		ps2dma_stop(dev, DMA_SENDCH, &pstop);
		ps2dma_stop(dev, DMA_RECVCH, &pstop);

		return -1;
	    }
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dev->empty, &wait);
    }
    return 0;
}
