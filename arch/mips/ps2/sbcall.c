/*
 *  PlayStation 2 SBIOS support routines
 *
 *  Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 *  Copyright (C) 2011-2013 Juergen Urban
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
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched/signal.h>

#include <asm/io.h>

#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>

static wait_queue_head_t ps2sif_dma_waitq;
static DEFINE_SPINLOCK(ps2sif_dma_lock);

EXPORT_SYMBOL(sbios_rpc);
EXPORT_SYMBOL(ps2sif_setdma);
EXPORT_SYMBOL(ps2sif_dmastat);
EXPORT_SYMBOL(__ps2sif_setdma_wait);
EXPORT_SYMBOL(__ps2sif_dmastat_wait);
EXPORT_SYMBOL(ps2sif_writebackdcache);

EXPORT_SYMBOL(ps2sif_addcmdhandler);
EXPORT_SYMBOL(ps2sif_sendcmd);

EXPORT_SYMBOL(ps2sif_bindrpc);
EXPORT_SYMBOL(ps2sif_callrpc);
EXPORT_SYMBOL(ps2sif_checkstatrpc);
EXPORT_SYMBOL(ps2sif_setrpcqueue);
EXPORT_SYMBOL(ps2sif_getnextrequest);
EXPORT_SYMBOL(ps2sif_execrequest);
EXPORT_SYMBOL(ps2sif_registerrpc);
EXPORT_SYMBOL(ps2sif_getotherdata);
EXPORT_SYMBOL(ps2sif_removerpc);
EXPORT_SYMBOL(ps2sif_removerpcqueue);

/*
 *  SIF DMA functions
 */

unsigned int ps2sif_setdma(ps2sif_dmadata_t *sdd, int len)
{
    struct sb_sifsetdma_arg arg;
    arg.sdd = sdd;
    arg.len = len;
    return sbios(SB_SIFSETDMA, &arg);
}

int ps2sif_dmastat(unsigned int id)
{
    struct sb_sifdmastat_arg arg;
    arg.id = id;
    return sbios(SB_SIFDMASTAT, &arg);
}

#define WAIT_DMA(cond, state)						\
    do {								\
	unsigned long flags;						\
	wait_queue_entry_t wait;					\
									\
	init_waitqueue_entry(&wait, current);				\
									\
	spin_lock_irqsave(&ps2sif_dma_lock, flags);			\
	add_wait_queue(&ps2sif_dma_waitq, &wait);			\
	while (cond) {							\
	    set_current_state(state);					\
	    spin_unlock_irq(&ps2sif_dma_lock);				\
	    schedule();							\
	    spin_lock_irq(&ps2sif_dma_lock);				\
	    if(signal_pending(current) && state == TASK_INTERRUPTIBLE)	\
		break;							\
	}								\
	remove_wait_queue(&ps2sif_dma_waitq, &wait);			\
	spin_unlock_irqrestore(&ps2sif_dma_lock, flags);		\
    } while (0)

unsigned int __ps2sif_setdma_wait(ps2sif_dmadata_t *sdd, int len, long state)
{
    int res;
    struct sb_sifsetdma_arg arg;

    arg.sdd = sdd;
    arg.len = len;

    WAIT_DMA(((res = sbios(SB_SIFSETDMA, &arg)) == 0), state);

    return (res);
}

int __ps2sif_dmastat_wait(unsigned int id, long state)
{
    int res;
    struct sb_sifdmastat_arg arg;

    arg.id = id;
    WAIT_DMA((0 <= (res = sbios(SB_SIFDMASTAT, &arg))), state);

    return (res);
}

void ps2sif_writebackdcache(const void *addr, int size)
{
    dma_cache_wback_inv((unsigned long)addr, size);
}


/*
 *  SIF CMD functions
 */

int ps2sif_addcmdhandler(u_int fid, ps2_addr_t func, ps2_addr_t data)
{
    	struct sb_sifaddcmdhandler_arg arg;
    	arg.fid  = fid;
    	arg.func = func;
    	arg.data = data;
        return sbios(SB_SIFADDCMDHANDLER, &arg);
}

int ps2sif_sendcmd(u_int fid, ps2_addr_t pp, int ps, ps2_addr_t src, ps2_addr_t dest, int size)
{
    	struct sb_sifsendcmd_arg arg;
	arg.fid  = fid;
	arg.pp   = pp;
	arg.ps   = ps;
	arg.src  = src;
	arg.dest = dest;
	arg.size = size;
        return sbios(SB_SIFSENDCMD, &arg);
}

/*
 *  SIF RPC functions
 */

int ps2sif_getotherdata(ps2sif_receivedata_t *rd, void *src, void *dest, int size, unsigned int mode, ps2sif_endfunc_t func, void *para)
{
    struct sb_sifgetotherdata_arg arg;
    arg.rd = rd;
    arg.src = src;
    arg.dest = dest;
    arg.size = size;
    arg.mode = mode;
    arg.func = func;
    arg.para = para;
    return sbios(SB_SIFGETOTHERDATA, &arg);
}

int ps2sif_bindrpc(ps2sif_clientdata_t *bd, unsigned int command, unsigned int mode, ps2sif_endfunc_t func, void *para)
{
    struct sb_sifbindrpc_arg arg;
    arg.bd = bd;
    arg.command = command;
    arg.mode = mode;
    arg.func = func;
    arg.para = para;
    return sbios(SB_SIFBINDRPC, &arg);
}

int ps2sif_callrpc(ps2sif_clientdata_t *bd, unsigned int fno, unsigned int mode, void *send, int ssize, void *receive, int rsize, ps2sif_endfunc_t func, void *para)
{
    struct sb_sifcallrpc_arg arg;
    arg.bd = bd;
    arg.fno = fno;
    arg.mode = mode;
    arg.send = send;
    arg.ssize = ssize;
    arg.receive = receive;
    arg.rsize = rsize;
    arg.func = func;
    arg.para = para;
    return sbios(SB_SIFCALLRPC, &arg);
}

int ps2sif_checkstatrpc(ps2sif_rpcdata_t *cd)
{
    struct sb_sifcheckstatrpc_arg arg;
    arg.cd = cd;
    return sbios(SB_SIFCHECKSTATRPC, &arg);
}

void ps2sif_setrpcqueue(ps2sif_queuedata_t *pSrqd, void (*callback)(void*), void *aarg)
{
    struct sb_sifsetrpcqueue_arg arg;
    arg.pSrqd = pSrqd;
    arg.callback = callback;
    arg.arg = aarg;
    sbios(SB_SIFSETRPCQUEUE, &arg);
}

void ps2sif_registerrpc(ps2sif_servedata_t *pr, unsigned int command,
			ps2sif_rpcfunc_t func, void *buff,
			ps2sif_rpcfunc_t cfunc, void *cbuff,
			ps2sif_queuedata_t *pq)
{
    struct sb_sifregisterrpc_arg arg;
    arg.pr = pr;
    arg.command = command;
    arg.func = func;
    arg.buff = buff;
    arg.cfunc = cfunc;
    arg.cbuff = cbuff;
    arg.pq = pq;
    sbios(SB_SIFREGISTERRPC, &arg);
}

ps2sif_servedata_t *ps2sif_removerpc(ps2sif_servedata_t *pr, ps2sif_queuedata_t *pq)
{
    struct sb_sifremoverpc_arg arg;
    arg.pr = pr;
    arg.pq = pq;
    return (ps2sif_servedata_t *)sbios(SB_SIFREMOVERPC, &arg);
}

ps2sif_queuedata_t *ps2sif_removerpcqueue(ps2sif_queuedata_t *pSrqd)
{
    struct sb_sifremoverpcqueue_arg arg;
    arg.pSrqd = pSrqd;
    return (ps2sif_queuedata_t *)sbios(SB_SIFREMOVERPCQUEUE, &arg);
}

ps2sif_servedata_t *ps2sif_getnextrequest(ps2sif_queuedata_t *qd)
{
    struct sb_sifgetnextrequest_arg arg;
    arg.qd = qd;
    return (ps2sif_servedata_t *)sbios(SB_SIFGETNEXTREQUEST, &arg);
}

void ps2sif_execrequest(ps2sif_servedata_t *rdp)
{
    struct sb_sifexecrequest_arg arg;
    arg.rdp = rdp;
    sbios(SB_SIFEXECREQUEST, &arg);
}


/*
 *  SBIOS blocking RPC function
 */

struct rpc_wait_queue {
    wait_queue_head_t wq;
    volatile int woken;
    spinlock_t lock;
};

static void rpc_wakeup(void *p, int result)
{
    struct rpc_wait_queue *rwq = (struct rpc_wait_queue *)p;
	unsigned long flags;

    spin_lock_irqsave(&rwq->lock, flags);
    rwq->woken = 1;
    wake_up(&rwq->wq);
    spin_unlock_irqrestore(&rwq->lock, flags);
}

int sbios_rpc(int func, void *arg, int *result)
{
    int ret;
    unsigned long flags;
    struct rpc_wait_queue rwq;
    struct sbr_common_arg carg;
    DECLARE_WAITQUEUE(wait, current);

    carg.arg = arg;
    carg.func = rpc_wakeup;
    carg.para = &rwq;

    init_waitqueue_head(&rwq.wq);
    rwq.woken = 0;
    spin_lock_init(&rwq.lock);

    /*
     * invoke RPC
     */
    do {
	ret = sbios(func, &carg);
	switch (ret) {
	case 0:
	    break;
	case -SIF_RPCE_SENDP:
	    /* resouce temporarily unavailable */
	    break;
	default:
	    /* ret == -SIF_PRCE_GETP (=1) */
	    *result = ret;
	    printk("sbios_rpc: RPC failed, func=%d result=%d\n", func, ret);
	    return ret;
	}
    } while (ret < 0);

    /*
     * wait for result
     */
    spin_lock_irqsave(&rwq.lock, flags);
    add_wait_queue(&rwq.wq, &wait);
    while (!rwq.woken) {
	set_current_state(TASK_UNINTERRUPTIBLE);
	spin_unlock_irq(&rwq.lock);
	schedule();
	spin_lock_irq(&rwq.lock);
    }
    remove_wait_queue(&ps2sif_dma_waitq, &wait);
    spin_unlock_irqrestore(&rwq.lock, flags);

    *result = carg.result;

    return 0;
}


/*
 *  SIF interrupt handler
 */

static irqreturn_t sif0_dma_handler(int irq, void *dev_id)
{
    sbios(SB_SIFCMDINTRHDLR, 0);
    return IRQ_HANDLED;
}

static irqreturn_t sif1_dma_handler(int irq, void *dev_id)
{
    spin_lock(&ps2sif_dma_lock);
    wake_up(&ps2sif_dma_waitq);
    spin_unlock(&ps2sif_dma_lock);
    return IRQ_HANDLED;
}

static uint32_t usrCmdHandler[256];

typedef struct {
	struct t_SifCmdHeader    sifcmd;
	u32 data[16];
} iop_sifCmdBufferIrq_t;

void handleRPCIRQ(iop_sifCmdBufferIrq_t *sifCmdBufferIrq, void *arg)
{
	do_IRQ(sifCmdBufferIrq->data[0]);
}


/*
 *  Initialize
 */

int __init ps2sif_init(void)
{
    struct sb_sifsetcmdbuffer_arg setcmdhandlerbufferparam;

    init_waitqueue_head(&ps2sif_dma_waitq);

    setcmdhandlerbufferparam.db = usrCmdHandler;
    setcmdhandlerbufferparam.size = sizeof(usrCmdHandler) / 8;

    if (sbios(SB_SIFINIT, 0) < 0) {
	printk(KERN_ERR "ps2sif: SIF init failed.\n");
	return -1;
    }
    if (sbios(SB_SIFINITCMD, 0) < 0) {
	printk(KERN_ERR "ps2sif: SIF CMD init failed.\n");
	return -1;
    }
    if (request_irq(IRQ_DMAC_5, sif0_dma_handler, 0, "SIF0 DMA", NULL)) {
	printk(KERN_ERR "ps2sif: Failed to setup SIF0 handler.\n");
	return -1;
    }
    if (request_irq(IRQ_DMAC_6, sif1_dma_handler, 0, "SIF1 DMA", NULL)) {
	printk(KERN_ERR "ps2sif: Failed to setup SIF1 handler.\n");
	return -1;
    }
    if (sbios(SB_SIFSETCMDBUFFER, &setcmdhandlerbufferparam) < 0) {
        printk("Failed to initialize EEDEBUG handler (1).\n");
    } else {
        if (ps2sif_addcmdhandler(0x20, handleRPCIRQ, NULL) < 0) {
            printk("Failed to initialize SIF IRQ handler.\n");
        }
    }
    if (sbios(SB_SIFINITRPC, 0) < 0) {
	printk(KERN_ERR "ps2sif: SIF init RPC failed.\n");
	return -1;
    }

    if (ps2sif_initiopheap() < 0) {
	printk(KERN_ERR "ps2sif: init IOP heap failed.\n");
	return -1;
    }
    printk(KERN_INFO "ps2sif: SIF initialized.\n");

    return 0;
}

void ps2sif_exit(void)
{
    sbios(SB_SIFEXITRPC, 0);
    sbios(SB_SIFEXITCMD, 0);
    free_irq(IRQ_DMAC_5, NULL);
    sbios(SB_SIFEXIT, 0);
    /* TBD: Test exit. */
    free_irq(IRQ_DMAC_6, NULL);
}
