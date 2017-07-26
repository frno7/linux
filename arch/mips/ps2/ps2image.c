/*
 *  PlayStation 2 image data transfer functions
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

#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>

#include <asm/pgtable.h>
#include <asm/atomic.h>
#include <asm/irq.h>

#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>

#include "ps2dev.h"

/*
 *  loadimage (EE->GS image data transfer)
 */

struct loadimage_request {
    struct dma_dev_request r;
    struct page_list *mem;
    volatile int *done;
    struct dma_tag tag[0] __attribute__((aligned(DMA_TRUNIT)));
} __attribute__((aligned(DMA_TRUNIT)));

static void loadimage_start(struct dma_request *req, struct dma_channel *ch)
{
    struct loadimage_request *lreq =
        container_of(
            container_of(req, struct dma_dev_request, r),
            struct loadimage_request, r);

    WRITEDMAREG(ch, PS2_Dn_TADR, virt_to_bus(lreq->tag));
    WRITEDMAREG(ch, PS2_Dn_QWC, 0);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_SENDC);
}

static unsigned long loadimage_stop(struct dma_request *req, struct dma_channel *ch)
{
    DMABREAK(ch);
    return 0;
}

static void loadimage_free(struct dma_request *req, struct dma_channel *ch)
{
    struct loadimage_request *lreq =
        container_of(
            container_of(req, struct dma_dev_request, r),
            struct loadimage_request, r);

    if (lreq->mem)
	ps2pl_free(lreq->mem);
    if (lreq->done)
	*lreq->done = 1;
    kfree(lreq);
}

static struct dma_ops loadimage_ops =
{ loadimage_start, NULL, loadimage_stop, ps2dma_dev_end };

int ps2gs_loadimage(struct ps2_image *img, struct dma_device *dev, int async)
{
    struct loadimage_request *lreq; 
    struct dma_devch *devch = &dev->devch[DMA_SENDCH];
    struct dma_channel *ch = devch->channel;
    u64 *p;
    struct dma_tag *tag, *dp;
    struct page_list *mem = NULL;
    int size, qsize;
    volatile int done = 0;
    int result;

    switch (img->psm) {
    case PS2_GS_PSMCT32:
    case PS2_GS_PSMZ32:
	size = (img->w * img->h) << 2;
	break;
    case PS2_GS_PSMCT24:
    case PS2_GS_PSMZ24:
	size = img->w * img->h * 3;
	break;
    case PS2_GS_PSMCT16:
    case PS2_GS_PSMCT16S:
    case PS2_GS_PSMZ16:
    case PS2_GS_PSMZ16S:
	size = (img->w * img->h) << 1;
	break;
    case PS2_GS_PSMT8:
    case PS2_GS_PSMT8H:
	if (img->x % 2 || img->w % 2)
	    return -EINVAL;		/* invalid alignment */
	size = img->w * img->h;
	break;
    case PS2_GS_PSMT4:
    case PS2_GS_PSMT4HL:
    case PS2_GS_PSMT4HH:
	if (img->x % 4 || img->w % 4)
	    return -EINVAL;		/* invalid alignment */
	size = (img->w * img->h) >> 1;
	break;
    default:
	return -EINVAL;
    }
    if (size == 0)
	return -EINVAL;
    size = DMA_ALIGN(size);

    if (img->fbp & ~0x3fff) {
	return -EINVAL;
    }

    if (img->fbw & ~0x3f) {
	return -EINVAL;
    }

    switch (result = ps2dma_make_tag((unsigned long)img->ptr, size, &tag, NULL, &mem)) {
    case BUFTYPE_MEM:
    case BUFTYPE_SPR:
	qsize = 0;
	break;
    case BUFTYPE_USER:
	if ((result = ps2pl_copy_from_user(mem, img->ptr, size))) {
	    ps2pl_free(mem);
	    kfree(tag);
	    return result;
	}
	qsize = size;
	break;
    default:
	return result;
    }

    lreq = kmalloc(sizeof(struct loadimage_request) +
			(6 + ((size >> PAGE_SHIFT) + 3) * 3) * DMA_TRUNIT,
			GFP_KERNEL);
    if ((lreq != NULL) && ((((unsigned long)&lreq->tag) & (DMA_TRUNIT - 1)) != 0)) {
        kfree(lreq);
        lreq = NULL;
        printk(KERN_ERR "ps2gs_loadimage: lreq->tag is not DMA aligned.\n");
    }
    if (lreq == NULL) {
	if (mem)
	    ps2pl_free(mem);
	kfree(tag);
	return -ENOMEM;
    }

    init_dma_dev_request(&lreq->r, &loadimage_ops, devch, qsize, loadimage_free);
    lreq->mem = mem;
    lreq->done = NULL;

    p = (u64 *)lreq->tag;
    *p++ = DMATAG_SET(5, DMATAG_CNT, 0);
    p++;

    *p++ = PS2_GIFTAG_SET_TOPHALF(4, 0, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
    *p++ = 0xe;		/* A+D */

    *p++ = ((u64)(img->fbp & 0x3fff) << 32) |
	((u64)(img->fbw & 0x3f) << 48) | ((u64)(img->psm & 0x3f) << 56);
    *p++ = PS2_GS_BITBLTBUF;
    *p++ = PACK64(0, PACK32(img->x & 0xfff, img->y & 0xfff));
    *p++ = PS2_GS_TRXPOS;
    *p++ = PACK64(img->w & 0xfff, img->h & 0xfff);
    *p++ = PS2_GS_TRXREG;
    *p++ = 0;
    *p++ = PS2_GS_TRXDIR;

    dp = tag;
    while (dp->qwc != 0) {
	*p++ = DMATAG_SET(1, DMATAG_CNT, 0);
	p++;
	*p++ = PS2_GIFTAG_SET_TOPHALF(dp->qwc, 0, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
	*p++ = 0;
	*((struct dma_tag *)p) = *dp++;
	p = (void *)(((struct dma_tag *)p) + 1);
    }
    *p++ = DMATAG_SET(1, DMATAG_END, 0);
    p++;
    *p++ = PS2_GIFTAG_SET_TOPHALF(0, 1, 0, 0, PS2_GIFTAG_FLG_IMAGE, 0);
    *p++ = 0;
    kfree(tag);
    dp = NULL;
    tag = NULL;

    if (!async)
	lreq->done = &done;

    result = ps2dma_check_and_add_queue(&lreq->r, 0);
    if (result < 0) {
        loadimage_free(&lreq->r.r, ch);
	return result;
    }

    if (!async && !done) {
	DECLARE_WAITQUEUE(wait, current);

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
	    lreq->done = NULL;
	spin_unlock_irq(&devch->device->lock);
    }
    return result;
}

/*
 *  storeimage (GS->EE image data transfer)
 */

struct storeimage_request {
    struct dma_request r;
    struct dma_channel *vifch, *gifch;
    struct page_list *mem;
    struct completion c;
    struct timer_list timer;
    int result;
    atomic_t count;

    void *hptr;
    int hlen, hdummy;
    void *tptr;
    int tlen, tdummy;

    /* VIFcode for image transfer */
    u32 vifcode[4] __attribute__((aligned(DMA_TRUNIT)));
    u64 gspacket[6 * 2] __attribute__((aligned(DMA_TRUNIT)));

    /* DMA tag */
    struct dma_tag tag[0] __attribute__((aligned(DMA_TRUNIT)));
} __attribute__((aligned(DMA_TRUNIT)));

struct storeimage_gif_request {
    struct dma_request r;
    struct storeimage_request *sreq;
};

static u32 mask_vifcode[] __attribute__((aligned(DMA_TRUNIT))) = {
    0x00000000,		/* NOP */
    0x06008000,		/* MSKPATH3(0x8000, 0) */
    0x13000000,		/* FLUSHA */
    0x50000006,		/* DIRECT(6, 0) */
};
static u32 unmask_vifcode[] __attribute__((aligned(DMA_TRUNIT))) = {
    0x06000000,		/* MSKPATH3(0, 0) */
    0x00000000,		/* NOP */
    0x00000000,		/* NOP */
    0x00000000,		/* NOP */
};

static struct storeimage_request *finish_sreq = NULL;

extern struct dma_channel ps2dma_channels[];

/* internal functions */
static void storeimage_gif_start(struct dma_request *req, struct dma_channel *ch);
static void storeimage_vif_start(struct dma_request *req, struct dma_channel *ch);
static void storeimage_start(struct storeimage_request *sreq);

int ps2gs_storeimage_finish(void);
static int storeimage_vif_isdone(struct dma_request *req, struct dma_channel *ch);
static void storeimage_vif_firstpio(struct storeimage_request *sreq);

static int storeimage_vif_nextdma(struct dma_request *req, struct dma_channel *ch);

static inline void storeimage_terminate(struct storeimage_request *sreq, int result);
static void storeimage_timer_handler(unsigned long ptr);

static void storeimage_vif_free(struct dma_request *req, struct dma_channel *ch);
static void storeimage_gif_free(struct dma_request *req, struct dma_channel *ch);

static inline int pio_transfer(unsigned char *ptr, int len, int dummy);

/* storeimage operations */
static struct dma_ops storeimage_gif_ops =
{ storeimage_gif_start, NULL, NULL, storeimage_gif_free };
static struct dma_ops storeimage_vif_ops =
{ storeimage_vif_start, storeimage_vif_isdone, NULL, storeimage_vif_free };
static struct dma_ops storeimage_vif_ops_dma =
{ storeimage_vif_start, storeimage_vif_nextdma, NULL, storeimage_vif_free };
static struct dma_ops storeimage_vif_ops_done =
{ storeimage_vif_start, NULL, NULL, storeimage_vif_free };

static void storeimage_gif_start(struct dma_request *req, struct dma_channel *ch)
{
    struct storeimage_gif_request *gifreq = container_of(req, struct storeimage_gif_request, r);

    DSPRINT("storeimage_gif_start:\n");
    storeimage_start(gifreq->sreq);
}

static void storeimage_vif_start(struct dma_request *req, struct dma_channel *ch)
{
    struct storeimage_request *sreq = container_of(req, struct storeimage_request, r);

    DSPRINT("storeimage_vif_start:\n");
    storeimage_start(sreq);
}

static void storeimage_start(struct storeimage_request *sreq)
{
    if (atomic_inc_return(&sreq->count) <= 1)
	return;
    DSPRINT("storeimage_start: %08X\n", sreq);

    sreq->timer.expires = jiffies + DMA_TIMEOUT;
    add_timer(&sreq->timer);
    WRITEDMAREG(sreq->vifch, PS2_Dn_MADR, virt_to_bus(sreq->vifcode));
    WRITEDMAREG(sreq->vifch, PS2_Dn_QWC, (sizeof(sreq->vifcode) + sizeof(sreq->gspacket))/ DMA_TRUNIT);
    finish_sreq = sreq;
    WRITEDMAREG(sreq->vifch, PS2_Dn_CHCR, CHCR_SENDN);
}

int ps2gs_storeimage_finish(void)
{
    struct storeimage_request *sreq = finish_sreq;
    DSPRINT("storeimage_finish:\n");
    if (finish_sreq == NULL)
	return 0;
    finish_sreq = NULL;
    storeimage_vif_firstpio(sreq);
    return 1;
}

static int storeimage_vif_isdone(struct dma_request *req, struct dma_channel *ch)
{
    struct storeimage_request *sreq = container_of(req, struct storeimage_request, r);

    DSPRINT("storeimage_vif_isdone:\n");
    sreq->r.ops = &storeimage_vif_ops_dma;
    storeimage_vif_firstpio(sreq);
    return 0;
}

static void storeimage_vif_firstpio(struct storeimage_request *sreq)
{
    if (atomic_dec_return(&sreq->count) > 0)
	return;

    del_timer(&sreq->timer);
    DSPRINT("storeimage_vif_firstpio:\n");
    /* switch bus direction (GS -> EE) */
    SET_VIF1REG(PS2_VIFREG_STAT, 0x00800000);
    outq(1ULL, GSSREG2(PS2_GSSREG_BUSDIR));
    __asm__ __volatile__("sync.l":::"memory");

    if (pio_transfer(sreq->hptr, sreq->hlen, sreq->hdummy) != 0) {
	storeimage_terminate(sreq, -1);
	return;
    }

    sreq->vifch->tagp = sreq->tag;
    storeimage_vif_nextdma(&sreq->r, sreq->vifch);
}

static int storeimage_vif_nextdma(struct dma_request *req, struct dma_channel *ch)
{
    struct storeimage_request *sreq = container_of(req, struct storeimage_request, r);

    del_timer(&sreq->timer);
    DSPRINT("storeimage_vif_nextdma: %08X\n", ch->tagp);
    if (ch->tagp != NULL && ch->tagp->qwc > 0) {
	DSPRINT("storeimage_vif_nextdma: madr=%08X qwc=%d\n", ch->tagp->addr, ch->tagp->qwc);
	sreq->timer.expires = jiffies + DMA_TIMEOUT;
	add_timer(&sreq->timer);
	WRITEDMAREG(ch, PS2_Dn_MADR, ch->tagp->addr);
	WRITEDMAREG(ch, PS2_Dn_QWC, ch->tagp->qwc);
	WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_RECVN);
	ch->tagp++;
	return 0;
    }

    if (pio_transfer(sreq->tptr, sreq->tlen, sreq->tdummy) != 0) {
	storeimage_terminate(sreq, -1);
	return 0;
    }    

    storeimage_terminate(sreq, 0);
    return 0;
}

static inline void storeimage_terminate(struct storeimage_request *sreq, int result)
{
    struct dma_channel *ch = sreq->vifch;
    DSPRINT("storeimage_terminate:\n");

    if (result != 0) {
	sreq->result = -EAGAIN;
	/* GS,VIF1 FIFO reset */
	outq(0x100ULL, GSSREG2(PS2_GSSREG_CSR));
	SET_VIF1REG(PS2_VIFREG_FBRST, 1);
    }

    /* switch bus direction (EE -> GS) */
    SET_VIF1REG(PS2_VIFREG_STAT, 0x00000000);
    outq(0ULL, GSSREG2(PS2_GSSREG_BUSDIR));
    __asm__ __volatile__("sync.l":::"memory");

    /* send PATH3 unmask VIFcode */
    sreq->r.ops = &storeimage_vif_ops_done;
    WRITEDMAREG(ch, PS2_Dn_MADR, virt_to_bus(unmask_vifcode));
    WRITEDMAREG(ch, PS2_Dn_QWC, sizeof(unmask_vifcode)/ DMA_TRUNIT);
    WRITEDMAREG(ch, PS2_Dn_CHCR, CHCR_SENDN);

    /* restart GIF DMA */
    ps2dma_intr_handler(sreq->gifch->irq, sreq->gifch);

    if (result != 0)
	printk("ps2gs: storeimage timeout\n");
}

static void storeimage_timer_handler(unsigned long ptr)
{
    struct storeimage_request *sreq = (struct storeimage_request *)ptr;
    struct dma_channel *ch = sreq->vifch;

    finish_sreq = NULL;
    DSPRINT("storeimage_timer_handler\n");

    /* DMA force break */
    DMABREAK(ch);

    storeimage_terminate(sreq, -1);
}

static void storeimage_vif_free(struct dma_request *req, struct dma_channel *ch)
{
    struct storeimage_request *sreq = container_of(req, struct storeimage_request, r);

    DSPRINT("storeimage_vif_free:\n");
    if (sreq->mem)
	ps2pl_free(sreq->mem);
    DSPRINT("storeimage_vif_free: wake_up\n");
    complete(&sreq->c);
    DSPRINT("storeimage_vif_free: wake_up end\n");
}

static void storeimage_gif_free(struct dma_request *req, struct dma_channel *ch)
{
    DSPRINT("storeimage_gif_free:\n");
    /* nothing to do */
}


#define VIF1FQC()	(VIF1REG(PS2_VIFREG_STAT) & 0x1f000000)
#define PIO_TIMEOUT	100000

extern void *ps2spr_vaddr;

/* data transfer with PIO */
static inline int pio_transfer(unsigned char *ptr, int len, int dummy)
{
    unsigned char buf0[128 + DMA_TRUNIT];
    unsigned char *p, *buf;
    int i, cnt;

    buf = DMA_ALIGN(&buf0[0]);
    p = buf;
    i = len;

    while (i > 0) {
	cnt = 0;
        /* wait for VIF1 FIFO fill */
        while (VIF1FQC() == 0) {
	    if (cnt++ > PIO_TIMEOUT) {
		DSPRINT("storeimage_pio_transfer: data left = %d\n", i);
		return -1;
	    }
        }
        move_quad((unsigned long)p, KSEG1ADDR(VIF1_FIFO));
	p += DMA_TRUNIT;
	i -= DMA_TRUNIT;
    }
    while (dummy > 0) {
	cnt = 0;
        /* wait for VIF1 FIFO fill */
        while (VIF1FQC() == 0) {
	    if (cnt++ > PIO_TIMEOUT) {
		DSPRINT("storeimage_pio_transfer: dummy left = %d\n", dummy);
		return -1;
	    }
        }
        dummy_read_quad(KSEG1ADDR(VIF1_FIFO));
	dummy--;
    }
    if (len) {
	if ((unsigned long)ptr >= 0x80000000 + SPR_SIZE) {
	    memcpy(ptr, buf, len);
	} else {		/* copy to scratchpad RAM */
	    memcpy(ps2spr_vaddr + ((unsigned long)ptr & (SPR_SIZE - 1)),
		   buf, len);
	}
    }
    return 0;
}

int ps2gs_storeimage(struct ps2_image *img, struct dma_device *dev)
{
    struct storeimage_gif_request gifreq;
    struct storeimage_request *sreq;
    struct dma_channel *gifch = dev->devch[DMA_SENDCH].channel;
    struct dma_channel *vifch = &ps2dma_channels[DMA_VIF1];

    struct dma_tag *tag, *dp, *tp;
    struct page_list *recv_mem = NULL;
    int result;

    int size;
    int mask, a, tfrlen;
    int bpl;		/* bytes per line */
    int hlen;		/* PIO tfr size before DMA */
    int hdummy;		/* dummy tfr size (qword) */
    int dlen;		/* DMA tfr size (8qword aligned) */
    int tlen;		/* PIO tfr size after DMA */
    int tdummy;		/* dummy tfr size (qword) */
    void *hptr, *tptr;
    u64 *p;

    /* get image size */
    switch (img->psm) {
    case PS2_GS_PSMCT32:
    case PS2_GS_PSMZ32:
	bpl = img->w << 2;
	break;
    case PS2_GS_PSMCT24:
    case PS2_GS_PSMZ24:
	bpl = img->w * 3;
	break;
    case PS2_GS_PSMCT16:
    case PS2_GS_PSMCT16S:
    case PS2_GS_PSMZ16:
    case PS2_GS_PSMZ16S:
	bpl = img->w << 1;
	break;
    case PS2_GS_PSMT8:
    case PS2_GS_PSMT8H:
	if (img->x % 2 || img->w % 2)
	    return -EINVAL;		/* invalid alignment */
	bpl = img->w;
	break;
    case PS2_GS_PSMT4:
    case PS2_GS_PSMT4HL:
    case PS2_GS_PSMT4HH:
	if (img->x % 4 || img->w % 4)
	    return -EINVAL;		/* invalid alignment */
	bpl = img->w >> 1;
	break;
    default:
	return -EINVAL;
    }
    size = bpl * img->h;
    if (size == 0)
	return -EINVAL;

    DSPRINT("storeimage: %d x %d  %08X (%d,%d)\n", img->w, img->h, img->ptr, img->x, img->y);

    if (img->fbp & ~0x3fff) {
	return -EINVAL;
    }

    if (img->fbw & ~0x3f) {
	return -EINVAL;
    }

    /* make DMA tags (including PIO transferred area) */
    switch (result = ps2dma_make_tag((unsigned long)img->ptr, DMA_ALIGN(size), &tag, NULL, &recv_mem)) {
    case BUFTYPE_MEM:
    case BUFTYPE_SPR:
    case BUFTYPE_USER:
	hptr = bus_to_virt(tag[0].addr);
	break;
    default:
	return result;
    }
    DSPRINT("storeimage: hptr = %08X\n", hptr);
    if (recv_mem) { DSPRINT("storeimage: USER: %08X\n", recv_mem->page[0]); }

    /* get the size of PIO transferred area before/after DMA */
    hlen = DMA_ALIGN_IMG(hptr) - hptr;
    hlen = size > hlen ? hlen : size;
    tlen = (size - hlen) & (DMA_TRUNIT_IMG - 1);
    dlen = (size - hlen) & ~(DMA_TRUNIT_IMG - 1);
    hdummy = tdummy = 0;
    if (tlen != 0 || dlen == 0) {
	mask = 15;
	for (a = bpl; (a % 2) == 0; a >>= 1)
	    if ((mask >>= 1) == 0)
		break;
	img->h = (img->h + mask) & ~mask;
	tfrlen = bpl * img->h;
	if (tlen == 0)
	    hdummy = (tfrlen - hlen) >> 4;
	else
	    tdummy = (tfrlen - dlen - hlen - tlen) >> 4;
    }

    DSPRINT("hlen=%d hdummy=%d dlen=%d tlen=%d tdummy=%d\n", hlen, hdummy, dlen, tlen, tdummy);
    DSPRINT("%d x %d\n", img->w, img->h);

    sreq = kmalloc(sizeof(struct storeimage_request) +
			((size >> PAGE_SHIFT) + 3) * DMA_TRUNIT, GFP_KERNEL);
    if ((sreq != NULL) && ((((unsigned long)&sreq->tag) & (DMA_TRUNIT - 1)) != 0)) {
        kfree(sreq);
        sreq = NULL;
        printk(KERN_ERR "ps2gs_storeimage: sreq->tag is not DMA aligned.\n");
    }
    if (sreq == NULL) {
	if (recv_mem)
	    ps2pl_free(recv_mem);
	kfree(tag);
	return -ENOMEM;
    }
    memset(sreq, 0, sizeof(struct storeimage_request));

    /* exclude PIO area from the tags */
    dp = tag;
    tp = sreq->tag;

    dp->addr += hlen;		/* exclude PIO before DMA */
    dp->qwc -= hlen >> 4;
    if (dp->qwc == 0)
	dp++;			/* all of the first tag is processed by PIO */

    if (dp->qwc == 0) {
	tptr = hptr + hlen;	/* no DMA transfer */
    } else {
	while (dp->qwc != 0)	/* copy tags */
	    *tp++ = *dp++;
	tp--;
	tp->qwc -= (tlen + 15) >> 4;	/* exclude PIO after DMA */
	tptr = bus_to_virt(tp->addr + (tp->qwc << 4));
	if (tp->qwc)
	    tp++;
    }

    tp->id = DMATAG_END;
    tp->qwc = 0;		/* end of the tags */
    tp++;
    kfree(tag);
    tag = NULL;
    dp = NULL;

    init_dma_request(&gifreq.r, &storeimage_gif_ops);
    gifreq.sreq = sreq;

    init_dma_request(&sreq->r, &storeimage_vif_ops);
    init_completion(&sreq->c);
    sreq->result = 0;
    sreq->vifch = vifch;
    sreq->gifch = gifch;
    sreq->hptr = hptr;
    sreq->hlen = hlen;
    sreq->hdummy = hdummy;
    sreq->tptr = tptr;
    sreq->tlen = tlen;
    sreq->tdummy = tdummy;
    atomic_set(&sreq->count, 0);
    
    init_timer(&sreq->timer);
    sreq->timer.function = storeimage_timer_handler;
    sreq->timer.data = (unsigned long)sreq;

    move_quad((unsigned long)sreq->vifcode, (unsigned long)mask_vifcode);
    p = sreq->gspacket;
    *p++ = PS2_GIFTAG_SET_TOPHALF(5, 1, 0, 0, PS2_GIFTAG_FLG_PACKED, 1);
    *p++ = 0xe;		/* A+D */
    
    *p++ = ((u64)(img->fbp & 0x3fff) << 0) |
	((u64)(img->fbw & 0x3f) << 16) | ((u64)(img->psm & 0x3f) << 24);
    *p++ = PS2_GS_BITBLTBUF;
    *p++ = PACK64(PACK32(img->x & 0xfff, img->y & 0xfff), 0);
    *p++ = PS2_GS_TRXPOS;
    *p++ = PACK64(img->w & 0xfff, img->h & 0xfff);
    *p++ = PS2_GS_TRXREG;
    *p++ = 0;
    *p++ = PS2_GS_FINISH;
    *p++ = 1;
    *p++ = PS2_GS_TRXDIR;

    ps2dma_add_queue(&gifreq.r, gifch, 1);
    ps2dma_add_queue(&sreq->r, vifch, 1);
    DSPRINT("storeimage: sleep_on\n");
    wait_for_completion(&sreq->c);
    DSPRINT("storeimage: sleep_on end\n");

    result = sreq->result;
    kfree(sreq);
    if (recv_mem) {
	if (result == 0)
	    result = ps2pl_copy_to_user(img->ptr, recv_mem, size);
	ps2pl_free(recv_mem);
    }
    return result;
}
