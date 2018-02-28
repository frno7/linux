/*
 * PlayStation 2 sub-system interface (SIF)
 *
 * The sub-system interface is an interface unit to the I/O processor (IOP).
 *
 * Copyright (C) 2001      Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
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
#include <asm/mach-ps2/cdvd.h>
#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>
#include <asm/mach-ps2/sbios.h>
#include <linux/module.h>

#define SBIOS_SIF_INIT			16
#define SBIOS_SIF_EXIT			17
#define SBIOS_SIF_SETDMA		18
#define SBIOS_SIF_DMASTAT		19
#define SBIOS_SIF_SETDCHAIN		20
/* 21-23: reserved */

#define SBIOS_SIF_INITCMD		32
#define SBIOS_SIF_EXITCMD		33
#define SBIOS_SIF_SENDCMD		34
#define SBIOS_SIF_CMDINTRHDLR		35
#define SBIOS_SIF_ADDCMDHANDLER		36
#define SBIOS_SIF_REMOVECMDHANDLER	37
#define SBIOS_SIF_SETCMDBUFFER		38

#define SBIOS_SIF_INITRPC		48
#define SBIOS_SIF_EXITRPC		49
#define SBIOS_SIF_GETOTHERDATA		50
#define SBIOS_SIF_BINDRPC		51
#define SBIOS_SIF_CALLRPC		52
#define SBIOS_SIF_CHECKSTATRPC		53
#define SBIOS_SIF_SETRPCQUEUE		54
#define SBIOS_SIF_REGISTERRPC		55
#define SBIOS_SIF_REMOVERPC		56
#define SBIOS_SIF_REMOVERPCQUEUE	57
#define SBIOS_SIF_GETNEXTREQUEST	58
#define SBIOS_SIF_EXECREQUEST		59

static wait_queue_head_t ps2sif_dma_waitq;
static DEFINE_SPINLOCK(ps2sif_dma_lock);

/* SIF DMA functions */

unsigned int ps2sif_setdma(ps2sif_dmadata_t *sdd, int len)
{
	struct {
		ps2_addr_t sdd;
		int len;
	} arg = {
		.sdd = sdd,
		.len = len,
	};

	return sbios(SBIOS_SIF_SETDMA, &arg);
}
EXPORT_SYMBOL(ps2sif_setdma);

int ps2sif_dmastat(unsigned int id)
{
	struct {
		int id;
	} arg = {
		.id = id,
	};

	return sbios(SBIOS_SIF_DMASTAT, &arg);
}
EXPORT_SYMBOL(ps2sif_dmastat);

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
	struct {
		ps2_addr_t sdd;
		int len;
	} arg = {
		.sdd = sdd,
		.len = len,
	};
	int res;

	WAIT_DMA(((res = sbios(SBIOS_SIF_SETDMA, &arg)) == 0), state);

	return res;
}
EXPORT_SYMBOL(__ps2sif_setdma_wait);

int __ps2sif_dmastat_wait(unsigned int id, long state)
{
	struct {
		int id;
	} arg = {
		.id = id
	};
	int res;

	WAIT_DMA((0 <= (res = sbios(SBIOS_SIF_DMASTAT, &arg))), state);

	return res;
}
EXPORT_SYMBOL(__ps2sif_dmastat_wait);

void ps2sif_writebackdcache(const void *addr, int size)
{
	dma_cache_wback_inv((unsigned long)addr, size);
}
EXPORT_SYMBOL(ps2sif_writebackdcache);

/* SIF CMD functions. */

int ps2sif_addcmdhandler(u_int fid, ps2_addr_t func, ps2_addr_t data)
{
	struct {
		u_int fid;
		ps2_addr_t func;
		ps2_addr_t data;
	} arg = {
		.fid  = fid,
		.func = func,
		.data = data
	};

	return sbios(SBIOS_SIF_ADDCMDHANDLER, &arg);

}
EXPORT_SYMBOL(ps2sif_addcmdhandler);

int ps2sif_sendcmd(u_int fid, ps2_addr_t pp, int ps,
	ps2_addr_t src, ps2_addr_t dest, int size)
{
	struct {
		u_int fid;
		ps2_addr_t pp;
		int ps;
		ps2_addr_t src;
		ps2_addr_t dest;
		int size;
	} arg = {
		.fid  = fid,
		.pp   = pp,
		.ps   = ps,
		.src  = src,
		.dest = dest,
		.size = size
	};

	return sbios(SBIOS_SIF_SENDCMD, &arg);
}
EXPORT_SYMBOL(ps2sif_sendcmd);

/* SIF remote procedure call (RPC) functions. */

int ps2sif_getotherdata(ps2sif_receivedata_t *rd, void *src, void *dest,
	int size, unsigned int mode, ps2sif_endfunc_t func, void *para)
{
	struct {
		ps2_addr_t rd;
		ps2_addr_t src;
		ps2_addr_t dest;
		int size;
		u_int mode;
		ps2_addr_t func;
		ps2_addr_t para;
	} arg = {
		.rd   = rd,
		.src  = src,
		.dest = dest,
		.size = size,
		.mode = mode,
		.func = func,
		.para = para
	};

	return sbios(SBIOS_SIF_GETOTHERDATA, &arg);
}
EXPORT_SYMBOL(ps2sif_getotherdata);

int ps2sif_bindrpc(ps2sif_clientdata_t *bd, unsigned int command,
	unsigned int mode, ps2sif_endfunc_t func, void *para)
{
	struct {
		ps2_addr_t bd;
		u_int command;
		u_int mode;
		ps2_addr_t func;
		ps2_addr_t para;
	} arg = {
		.bd      = bd,
		.command = command,
		.mode    = mode,
		.func    = func,
		.para    = para
	};

	return sbios(SBIOS_SIF_BINDRPC, &arg);
}
EXPORT_SYMBOL(ps2sif_bindrpc);

int ps2sif_callrpc(ps2sif_clientdata_t *bd,
	unsigned int fno, unsigned int mode,
	void *send, int ssize,
	void *receive, int rsize,
	ps2sif_endfunc_t func, void *para)
{
	struct {
		ps2_addr_t bd;
		u_int fno;
		u_int mode;
		ps2_addr_t send;
		int ssize;
		ps2_addr_t receive;
		int rsize;
		ps2_addr_t func;
		ps2_addr_t para;
	} arg = {
		.bd      = bd,
		.fno     = fno,
		.mode    = mode,
		.send    = send,
		.ssize   = ssize,
		.receive = receive,
		.rsize   = rsize,
		.func    = func,
		.para    = para
	};

	return sbios(SBIOS_SIF_CALLRPC, &arg);
}
EXPORT_SYMBOL(ps2sif_callrpc);

int ps2sif_checkstatrpc(ps2sif_rpcdata_t *cd)
{
	struct {
		ps2_addr_t cd;
	} arg = {
		.cd = cd
	};

	return sbios(SBIOS_SIF_CHECKSTATRPC, &arg);
}
EXPORT_SYMBOL(ps2sif_checkstatrpc);

void ps2sif_setrpcqueue(ps2sif_queuedata_t *pSrqd,
	void (*callback)(void*), void *aarg)
{
	struct {
		ps2_addr_t pSrqd;
		ps2_addr_t callback;
		ps2_addr_t arg;
	} arg = {
		.pSrqd    = pSrqd,
		.callback = callback,
		.arg      = aarg
	};

	sbios(SBIOS_SIF_SETRPCQUEUE, &arg);
}
EXPORT_SYMBOL(ps2sif_setrpcqueue);

void ps2sif_registerrpc(ps2sif_servedata_t *pr,
	unsigned int command,
	ps2sif_rpcfunc_t func, void *buff,
	ps2sif_rpcfunc_t cfunc, void *cbuff,
	ps2sif_queuedata_t *pq)
{
	struct {
		ps2_addr_t pr;
		u_int command;
		ps2_addr_t func;
		ps2_addr_t buff;
		ps2_addr_t cfunc;
		ps2_addr_t cbuff;
		ps2_addr_t pq;
	} arg = {
		.pr      = pr,
		.command = command,
		.func    = func,
		.buff    = buff,
		.cfunc   = cfunc,
		.cbuff   = cbuff,
		.pq      = pq
	};

	sbios(SBIOS_SIF_REGISTERRPC, &arg);
}
EXPORT_SYMBOL(ps2sif_registerrpc);

ps2sif_servedata_t *ps2sif_removerpc(ps2sif_servedata_t *pr,
	ps2sif_queuedata_t *pq)
{
	struct {
		ps2_addr_t pr;
		ps2_addr_t pq;
	} arg = {
		.pr = pr,
		.pq = pq
	};

	return (ps2sif_servedata_t *)sbios(SBIOS_SIF_REMOVERPC, &arg);
}
EXPORT_SYMBOL(ps2sif_removerpc);

ps2sif_queuedata_t *ps2sif_removerpcqueue(ps2sif_queuedata_t *pSrqd)
{
	struct {
		ps2_addr_t pSrqd;
	} arg = {
		.pSrqd = pSrqd
	};

	return (ps2sif_queuedata_t *)sbios(SBIOS_SIF_REMOVERPCQUEUE, &arg);
}
EXPORT_SYMBOL(ps2sif_removerpcqueue);

ps2sif_servedata_t *ps2sif_getnextrequest(ps2sif_queuedata_t *qd)
{
	struct {
		ps2_addr_t qd;
	} arg = {
		.qd = qd
	};

	return (ps2sif_servedata_t *)sbios(SBIOS_SIF_GETNEXTREQUEST, &arg);
}
EXPORT_SYMBOL(ps2sif_getnextrequest);

void ps2sif_execrequest(ps2sif_servedata_t *rdp)
{
	struct {
		ps2_addr_t rdp;
	} arg = {
		.rdp = rdp
	};

	sbios(SBIOS_SIF_EXECREQUEST, &arg);
}
EXPORT_SYMBOL(ps2sif_execrequest);

static irqreturn_t sif0_dma_handler(int irq, void *dev_id)
{
	sbios(SBIOS_SIF_CMDINTRHDLR, 0);

	return IRQ_HANDLED;
}

static irqreturn_t sif1_dma_handler(int irq, void *dev_id)
{
	spin_lock(&ps2sif_dma_lock);
	wake_up(&ps2sif_dma_waitq);
	spin_unlock(&ps2sif_dma_lock);

	return IRQ_HANDLED;
}

struct iop_sifCmdBufferIrq {
	struct t_SifCmdHeader sifcmd;
	u32 data[16];
};

void handleRPCIRQ(struct iop_sifCmdBufferIrq *sifCmdBufferIrq, void *arg)
{
	do_IRQ(sifCmdBufferIrq->data[0]);	/* FIXME: What is this? */
}

int __init sif_init(void)
{
	static uint32_t usrCmdHandler[256]; /* FIXME: What is this? */

	struct {
		ps2_addr_t db;
		int size;
	} setcmdhandlerbufferparam = {
		.db = usrCmdHandler,
		.size = sizeof(usrCmdHandler) / 8,
	};
	int err;

	init_waitqueue_head(&ps2sif_dma_waitq);

	err = request_irq(IRQ_DMAC_5, sif0_dma_handler, 0, "SIF0 DMA", NULL);
	if (err) {
		printk(KERN_ERR "sif: Failed to setup SIF0 handler.\n");
		goto err_irq_sif0;
	}
	err = request_irq(IRQ_DMAC_6, sif1_dma_handler, 0, "SIF1 DMA", NULL);
	if (err) {
		printk(KERN_ERR "sif: Failed to setup SIF1 handler.\n");
		goto err_irq_sif1;
	}

	if (sbios(SBIOS_SIF_INIT, 0) < 0) {
		printk(KERN_ERR "sif: SIF init failed.\n");
		err = -EINVAL;
		goto err_sif_init;
	}
	if (sbios(SBIOS_SIF_INITCMD, 0) < 0) {
		printk(KERN_ERR "sif: SIF CMD init failed.\n");
		err = -EINVAL;
		goto err_sif_initcmd;
	}
	if (sbios(SBIOS_SIF_SETCMDBUFFER, &setcmdhandlerbufferparam) < 0) {
		printk("Failed to initialize EEDEBUG handler (1).\n");
	} else {
		if (ps2sif_addcmdhandler(0x20, handleRPCIRQ, NULL) < 0) {
			printk("Failed to initialize SIF IRQ handler.\n");
		}
	}
	if (sbios(SBIOS_SIF_INITRPC, 0) < 0) {
		printk(KERN_ERR "sif: SIF init RPC failed.\n");
		err = -EINVAL;
		goto err_sif_initrpc;
	}

	printk(KERN_INFO "sif: SIF initialized.\n");

	return 0;

err_sif_initrpc:
	sbios(SBIOS_SIF_EXITCMD, 0);

err_sif_initcmd:
	sbios(SBIOS_SIF_EXIT, 0);

err_sif_init:
	free_irq(IRQ_DMAC_6, NULL);

err_irq_sif1:
	free_irq(IRQ_DMAC_5, NULL);

err_irq_sif0:
	return err;
}

void sif_exit(void)
{
	sbios(SBIOS_SIF_EXITRPC, 0);
	sbios(SBIOS_SIF_EXITCMD, 0);
	sbios(SBIOS_SIF_EXIT, 0);

	free_irq(IRQ_DMAC_5, NULL);
	free_irq(IRQ_DMAC_6, NULL);
}
