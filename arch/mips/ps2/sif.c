/*
 * PlayStation 2 sub-system interface (SIF)
 *
 * The sub-system interface is an interface unit to the I/O processor (IOP).
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched/signal.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>

#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>
#include <linux/module.h>

typedef __u32 iop_addr_t;	/* FIXME: iop.h? */

static void *dmac_buffer0;	/* FIXME: Device private data */
static void *dmac_buffer1;	/* FIXME: Device private data */
static void *dmac_receive;	/* FIXME: Device private data */

#define D6_CHCR		0x1000c400	/* Channel 6 control */
#define D6_MADR		0x1000c410	/* Channel 6 memory address */
#define D6_QWC		0x1000c420	/* Channel 6 quad word count */

#define CMD_PACKET_MAX		128
#define CMD_PACKET_DATA_MAX 	112
#define CMD_HANDLER_MAX	64

struct sif_cmd_header
{
   u32 psize : 8;	/* Packet size. Min: 1x16 (header only), max: 7*16 */
   u32 dsize : 24;	/* IOP data size in bytes */
   u32 dst;		/* IOP data address or NULL */
   u32 cid;		/* Command id */
   u32 opt;
};

struct ca_pkt {
	u32 buf;
};

struct sr_pkt {
	u32	sreg;
	int	val;
};

struct t_SifCmdHandlerData
{
	sif_cmd_handler_t handler;
	void *arg;
};

struct cmd_data {
	/** Command packet received from the IOP */
	void	*pktbuf;
	/** Address of IOP SIF DMA receive address */
	dma_addr_t iopbuf;
	struct t_SifCmdHandlerData sys_cmd_handlers[CMD_HANDLER_MAX];
	struct t_SifCmdHandlerData usr_cmd_handlers[CMD_HANDLER_MAX];
};

static dma_addr_t sif0_dma_addr;
static struct cmd_data _sif_cmd_data;

static DEFINE_SPINLOCK(sregs_lock);
static s32 sregs[32];

static void cmd_change_addr(void *packet, void *arg)
{
	struct cmd_data *cmd_data = (struct cmd_data *)arg;
	struct ca_pkt *pkt = (struct ca_pkt *)packet;

	cmd_data->iopbuf = (dma_addr_t)pkt->buf;
}

static void cmd_write_sreg(void *packet, void *arg)
{
	struct sr_pkt *pkt = (struct sr_pkt *)packet;
	unsigned long flags;

	spin_lock_irqsave(&sregs_lock, flags);
	sregs[pkt->sreg] = pkt->val;
	spin_unlock_irqrestore(&sregs_lock, flags);
}

static s32 read_sreg(s32 sreg)
{
	unsigned long flags;
	s32 value;

	spin_lock_irqsave(&sregs_lock, flags);
	value = sregs[sreg];
	spin_unlock_irqrestore(&sregs_lock, flags);

	return value;
}

/* DMAC */
#define EE_DMAC_SIF0_CHCR	0x1000c000
#define EE_DMAC_SIF0_MADR	0x1000c010
#define EE_DMAC_SIF0_QWC	0x1000c020

#define EE_DMAC_SIF1_CHCR	0x1000c400
#define EE_DMAC_SIF1_MADR	0x1000c410
#define EE_DMAC_SIF1_QWC	0x1000c420
#define EE_DMAC_SIF1_TADR	0x1000c430

/* SIF */
#define EE_SIF_MAINADDR 	0x1000f200	/* EE to IOP command buffer */
#define EE_SIF_SUBADDR  	0x1000f210	/* IOP to EE command buffer */
#define EE_SIF_MSFLAG   	0x1000f220	/* EE to IOP flag */
#define EE_SIF_SMFLAG   	0x1000f230	/* IOP to EE flag */
#define EE_SIF_SUBCTRL  	0x1000f240
#define EE_SIF_UNKNF260		0x1000f260

/* DMAC */
#define EE_DMAC_ENABLER		0x1000f520
#define EE_DMAC_ENABLEW		0x1000f590
#define   EE_DMAC_CPND	(1<<16)

#define NUM_SIFREGS 32
#define QWC_SIZE 16
#define QWC_PAYLOAD_FIRST_PKT 7

void sif_write_msflag(u32 value)
{
	outl(value, EE_SIF_MSFLAG);
}

u32 sif_read_msflag(void)
{
	return inl(EE_SIF_MSFLAG);
}

void sif_write_smflag(u32 value)
{
	outl(value, EE_SIF_SMFLAG);
}

u32 sif_read_smflag(void)
{
	return inl(EE_SIF_SMFLAG);
}

bool sif_smflag_cmdinit(void)
{
	return (sif_read_smflag() & SIF_STAT_CMDINIT) != 0;
}

bool sif_smflag_bootend(void)
{
	return (sif_read_smflag() & SIF_STAT_BOOTEND) != 0;
}

bool sif1_ready(void)
{
	size_t countout = 1000000;

	while ((inl(D6_CHCR) & 0x100) != 0 && countout > 0)
		countout--;

	return countout > 0;
}

static u32 nbytes_to_wc(size_t nbytes)
{
	const size_t wc = nbytes / 4;

	BUG_ON(nbytes & 0x3);	/* Word count must align */

	return wc;
}

static u32 nbytes_to_qwc(size_t nbytes)
{
	const size_t qwc = nbytes / 16;

	BUG_ON(nbytes & 0xf);	/* Quadword count must align */
	BUG_ON(qwc > 0xffff);	/* QWC DMA field is only 16 bits */

	return qwc;
}

struct iop_dma_tag {
	__BITFIELD_FIELD(u32 ert : 1,
	__BITFIELD_FIELD(u32 int_0 : 1,	/* Assert IOP interupt on completion */
	__BITFIELD_FIELD(u32 : 6,
	__BITFIELD_FIELD(u32 addr : 24,	/* IOP address */
	;))))
	u32 wc;				/* 4 byte word count */
	u64 : 64;
};

static int sif1_write_ert_int_0(const struct sif_cmd_header *header,
	bool ert, bool int_0, iop_addr_t dst, const void *src, size_t nbytes)
{
	const size_t header_size = header != NULL ? sizeof(*header) : 0;
	const size_t aligned_size = (header_size + nbytes + 15) & ~(size_t)15;
	const struct iop_dma_tag iop_dma_tag = {
		.ert	= ert,
		.int_0	= int_0,
		.addr	= dst,
		.wc	= nbytes_to_wc(aligned_size)
	};
	const size_t dma_nbytes = sizeof(iop_dma_tag) + aligned_size;
	u8 *dma_buffer = dmac_buffer1;
	dma_addr_t madr;

	if (!aligned_size)
		return 0;

	/* Wait for previous transmission to finish. */
	if (!sif1_ready())
		BUG();

	/* FIXME: Check dma_nbytes fits */

	memcpy(&dma_buffer[0], &iop_dma_tag, sizeof(iop_dma_tag));
	memcpy(&dma_buffer[sizeof(iop_dma_tag)], header, header_size);
	memcpy(&dma_buffer[sizeof(iop_dma_tag) + header_size], src, nbytes);

	madr = dma_map_single(NULL, dma_buffer, dma_nbytes, DMA_TO_DEVICE);
	/* FIXME: dma_mapping_error */

	outl(madr, D6_MADR);
	outl(nbytes_to_qwc(dma_nbytes), D6_QWC);
	outl(CHCR_SENDN_TIE, D6_CHCR);

	dma_unmap_single(NULL, madr, dma_nbytes, DMA_TO_DEVICE);

	return 0;	/* FIXME: Check errors for all sif1_writes */
}

static int sif1_write(const struct sif_cmd_header *header,
	iop_addr_t dst, const void *src, size_t nbytes)
{
	return sif1_write_ert_int_0(header, false, false, dst, src, nbytes);
}

static int sif1_write_irq(const struct sif_cmd_header *header,
	iop_addr_t dst, const void *src, size_t nbytes)
{
	return sif1_write_ert_int_0(header, true, true, dst, src, nbytes);
}

int sbcall_sifexit(void)
{
	return 0;
}

/** Function is same in RTE. Reenable DMAC channel. */
int sbcall_sifsetdchain(void)
{
	/* Chain mode; enable tag interrupt; start DMA.  */
	/*
	 * dir: 0 -> to _memory
	 * mod: 01 -> chain.
	 * asp: 00 -> no address pushed by call tag
	 * tte: 0 -> does not transfer DMAtag itself
	 * tie: 1 -> enables IRQ bit of DMAtag
	 * str: 1 -> Starts DMA. Maintains 1 while operating.
	 */
	outl(0, EE_DMAC_SIF0_QWC);
	outl(0, EE_DMAC_SIF0_MADR);
	outl(0x184, EE_DMAC_SIF0_CHCR);

	return 0;
}

/* Initialization. Functions is same as in RTE with one exception. */
int sbcall_sifinit(void)
{
	outl(0xff, EE_SIF_UNKNF260);
	outl(sif0_dma_addr, EE_SIF_MAINADDR);
	sif_write_msflag(SIF_STAT_CMDINIT);
	sif_write_msflag(SIF_STAT_BOOTEND);

	outl(0, EE_DMAC_SIF0_CHCR);
	outl(0, EE_DMAC_SIF0_MADR);
	outl(0, EE_DMAC_SIF1_CHCR);

	sbcall_sifsetdchain();

	return 0;
}

int sbcall_sifstopdma(void)
{
	/* FIXME */
	outl(0, EE_DMAC_SIF0_CHCR);
	outl(0, EE_DMAC_SIF0_QWC);
	inl(EE_DMAC_SIF0_QWC);
	return 0;
}

bool sif_cmd_opt_copy(int cid, u32 opt, void *pkt, int pktsize,
	iop_addr_t dst, const void *src, int nbytes)
{
	const struct sif_cmd_header header = {
		.cid  = cid,
		.psize = sizeof(header) + pktsize,
		.dsize = nbytes,
		.dst = dst,
		.opt = opt
	};

	BUG_ON(pktsize > CMD_PACKET_DATA_MAX);	/* FIXME */

	sif1_write(NULL, dst, src, nbytes);
	sif1_write_irq(&header, _sif_cmd_data.iopbuf, pkt, pktsize);

	return true;
}

bool sif_cmd_copy(int cid, void *pkt, int pktsize,
	iop_addr_t dst, const void *src, int nbytes)
{
	return sif_cmd_opt_copy(cid, 0, pkt, pktsize, dst, src, nbytes);
}

bool sif_cmd_opt(int cid, u32 opt, void *pkt, int pktsize)
{
	return sif_cmd_opt_copy(cid, opt, pkt, pktsize, 0, NULL, 0);
}

bool sif_cmd(int cid, void *pkt, int pktsize)
{
	return sif_cmd_copy(cid, pkt, pktsize, 0, NULL, 0);
}

static irqreturn_t sif0_dma_handler(int irq, void *dev_id)
{
	struct sif_cmd_header header;
	u8 data[CMD_PACKET_MAX - sizeof(header)] __attribute__((aligned(16)));
	const struct cmd_data * const cmd_data = &_sif_cmd_data;
	const u8 * const pktbuf = cmd_data->pktbuf;
	unsigned int id;

	if (inl(EE_DMAC_SIF0_CHCR) & 0x100)
		return IRQ_NONE;

	dma_sync_single_for_cpu(NULL, sif0_dma_addr,
		sizeof(header) + sizeof(data), DMA_FROM_DEVICE);

	memcpy(&header, &pktbuf[0], sizeof(header));
	memcpy(&data[0], &pktbuf[sizeof(header)], sizeof(data));

	sbcall_sifsetdchain();		/* Restart DMA for next packet */

	id = header.cid & ~SIF_CMD_ID_SYSTEM;
	if (id < CMD_HANDLER_MAX) {
		const struct t_SifCmdHandlerData * const cmd_handlers =
			(header.cid & SIF_CMD_ID_SYSTEM) ?
				cmd_data->sys_cmd_handlers :
				cmd_data->usr_cmd_handlers;

		if (cmd_handlers[id].handler != NULL)
			cmd_handlers[id].handler(data, cmd_handlers[id].arg);
		else
			printk("sif: unknown command id %x\n", header.cid);
	} else
		printk("sif: invalid command id %x\n", header.cid);

	return IRQ_HANDLED;
}

#define RPC_PACKET_SIZE	64

/* Set if the packet has been allocated */
#define PACKET_F_ALLOC	0x01

struct rpc_data {
	int	pid;
	void	*pkt_table;
	int	pkt_table_len;
	int	unused1;
	int	unused2;
	u8	*rdata_table;
	int	rdata_table_len;
	u8	*client_table;
	int	client_table_len;
	int	rdata_table_idx;
	void	*active_queue;
};

extern struct rpc_data _sif_rpc_data;

void *_rpc_get_fpacket(struct rpc_data *rpc_data);

void *_rpc_get_fpacket(struct rpc_data *rpc_data)
{
	int index;

	index = rpc_data->rdata_table_idx % rpc_data->rdata_table_len;
	rpc_data->rdata_table_idx = index + 1;

	return (void *)(rpc_data->rdata_table + (index * RPC_PACKET_SIZE));
}

int sif_rpc_bind(struct t_SifRpcClientData *cd, int sid)
{
	SifRpcBindPkt_t bind = {	/* FIXME: const */
		.sid        = sid,
		.pkt_addr   = &bind,
		.client     = cd
	};

	memset(cd, 0, sizeof(*cd));
	cd->hdr.pkt_addr = &bind;
	cd->hdr.rpc_id = bind.rpc_id;	/* FIXME: Remove */
	init_completion(&cd->hdr.cmp);

	if (!sif_cmd(0x80000009, &bind, sizeof(bind)))
		return -EIO;

	wait_for_completion(&cd->hdr.cmp);

	return cd->server != NULL ? 0 : -EAGAIN; /* FIXME: Proper errpr */
}
EXPORT_SYMBOL(sif_rpc_bind);

int sif_rpc_async(struct t_SifRpcClientData *cd, int rpc_number,
	const void *sendbuf, int ssize, void *recvbuf, int rsize,
	SifRpcEndFunc_t endfunc, void *efarg)
{
	const size_t dmac_rsize = (rsize != 0 ? rsize : 16);	/* FIXME: Not zero? */
	SifRpcCallPkt_t call = {
		.rpc_number  = rpc_number,
		.send_size   = ssize,
		.receive     = dma_map_single(NULL, dmac_receive, dmac_rsize,
			DMA_FROM_DEVICE), /* FIXME: dma_mapping_error */
		.recv_size   = rsize,
		.rmode       = 1,
		.pkt_addr    = &call,
		.client      = cd,
		.server      = cd->server
	};

	if (cd->hdr.pkt_addr != 0) {
		printk("sif_rpc: Client 0x%x rpc_number 0x%x is already in use.\n", (uint32_t) cd, rpc_number);
		return -ENOMEM;
	}

	cd->end_function  = endfunc;
	cd->end_param     = efarg;
	cd->hdr.pkt_addr  = &call;
	cd->hdr.rpc_id    = call.rpc_id;
	reinit_completion(&cd->hdr.cmp);

#if 0
	if (mode & SIF_RPC_M_NOWAIT) {
		if (!endfunc)
			call->rmode = 0;

		if (!sif_cmd_copy(0x8000000a, call, sizeof(*call),
			(dma_addr_t)cd->buff, sendbuf, ssize)) { /* FIXME: buff -> dma_addr_t? */
			rpc_packet_free(call);
			return -EIO;
		}

		return 0;
	}
#endif

	/* The following code is normally not executed. */
	if (!sif_cmd_copy(0x8000000a, &call, sizeof(call),
		(dma_addr_t)cd->buff, sendbuf, ssize)) {	/* FIXME: buff -> dma_addr_t? */
		return -EIO;
	}

	wait_for_completion(&cd->hdr.cmp);

	dma_unmap_single(NULL, call.receive, dmac_rsize, DMA_FROM_DEVICE);

	memcpy(recvbuf, dmac_receive, rsize);

	return 0;
}
EXPORT_SYMBOL(sif_rpc_async);

int sif_rpc(struct t_SifRpcClientData *cd, int rpc_number,
	const void *sendbuf, int ssize, void *recvbuf, int rsize)
{
	return sif_rpc_async(cd, rpc_number,
		sendbuf, ssize, recvbuf, rsize,
		NULL, NULL);
}
EXPORT_SYMBOL(sif_rpc);

/* The packets sent on EE RPC requests are allocated from this table.  */
static u8 pkt_table[2048] __attribute__((aligned(64)));
/* A ring buffer used to allocate packets sent on IOP requests.  */
static u8 rdata_table[2048] __attribute__((aligned(64)));
static u8 client_table[2048] __attribute__((aligned(64)));

struct rpc_data _sif_rpc_data = {
	pid:			1,
	pkt_table:		pkt_table,
	pkt_table_len:		sizeof(pkt_table)/RPC_PACKET_SIZE,
	rdata_table:		rdata_table,
	rdata_table_len:	sizeof(rdata_table)/RPC_PACKET_SIZE,
	client_table:		client_table,
	client_table_len:	sizeof(client_table)/RPC_PACKET_SIZE,
	rdata_table_idx:	0
};

/* Command 0x80000008 */
static void cmd_rpc_end(void *data, void *arg)
{
	SifRpcRendPkt_t *request = data;
	struct t_SifRpcClientData *client = request->client;
	void *pkt_addr;
	SifRpcEndFunc_t volatile end_function;
	void * volatile end_param;

	pkt_addr = client->hdr.pkt_addr;
	complete_all(&client->hdr.cmp);

	if (request->cid == 0x8000000a) {
		/* Response to RPC call. */
		end_function = client->end_function;
		end_param = client->end_param;
	} else if (request->cid == 0x80000009) {
		/* Response to Bind call. */
		client->server = request->server;
		client->buff   = request->buff;
		client->cbuff  = request->cbuff;

		end_function = client->end_function;
		end_param = client->end_param;
	} else {
		end_function = NULL;
		end_param = NULL;
	}

	client->hdr.pkt_addr = NULL;

	if (end_function != NULL)
		end_function(end_param);
}

static void *search_svdata(u32 sid, struct rpc_data *rpc_data)
{
	SifRpcServerData_t *server;
	SifRpcDataQueue_t *queue = rpc_data->active_queue;

	if (!queue)
		return NULL;

	while (queue) {
		server = queue->link;
		while (server) {
			if (server->sid == sid)
				return server;

			server = server->link;
		}

		queue = queue->next;
	}

	return NULL;
}

/* Command 0x80000009 */
static void cmd_rpc_bind(void *data, void *arg)
{
	SifRpcBindPkt_t *bind = data;
	SifRpcRendPkt_t *rend;
	SifRpcServerData_t *server;

	// printk("cmd_rpc_bind\n");

	rend = _rpc_get_fpacket(arg);
	rend->pkt_addr = bind->pkt_addr;
	rend->client = bind->client;
	rend->cid = 0x80000009;

	server = search_svdata(bind->sid, arg);
	if (!server) {
		rend->server = NULL;
		rend->buff   = NULL;
		rend->cbuff  = NULL;
	} else {
		rend->server = server;
		rend->buff   = server->buff;
		rend->cbuff  = server->cbuff;
	}

	sif_cmd(0x80000008, rend, sizeof(*rend));
}

SifRpcServerData_t *SifGetNextRequest(SifRpcDataQueue_t *qd)
{
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;

	SifRpcServerData_t *server;

	spin_lock_irqsave(&lock, flags);

	server = qd->start;
	qd->active = 1;

	if (server) {
		qd->active = 0;
		qd->start  = server->next;
	}

	spin_unlock_irqrestore(&lock, flags);

	return server;
}

static void *_rpc_get_fpacket2(struct rpc_data *rpc_data, int rid)
{
	if (rid < 0 || rid < rpc_data->client_table_len)
		return _rpc_get_fpacket(rpc_data);
	else
		return rpc_data->client_table + (rid * RPC_PACKET_SIZE);
}

void SifExecRequest(SifRpcServerData_t *sd)
{
	SifRpcRendPkt_t *rend;
	void *rec = NULL;

	static DEFINE_SPINLOCK(lock);
	unsigned long flags;

	rec = sd->func(sd->rpc_number, sd->buff, sd->size);

	spin_lock_irqsave(&lock, flags);

	if (sd->rid & 4)
		rend = (SifRpcRendPkt_t *)
			_rpc_get_fpacket2(&_sif_rpc_data, (sd->rid >> 16) & 0xffff);
	else
		rend = (SifRpcRendPkt_t *)
			_rpc_get_fpacket(&_sif_rpc_data);

	spin_unlock_irqrestore(&lock, flags);

	rend->client = sd->client;
	rend->cid    = 0x8000000a;
	rend->rpc_id = 0;  /* XXX: is this correct? */

	if (sd->rmode) {
		if (!sif_cmd_copy(0x80000008, rend, sizeof(*rend),
			(dma_addr_t)sd->receive,	/* FIXME: dma_addr_t? */
			rec, sd->rsize))
			return;
	}

	rend->rec_id = 0;

	if (sd->rsize) {
		sif1_write(NULL, (dma_addr_t)sd->receive, rec, sd->rsize); /* FIXME: (dma_addr_t)? */
	} else {
		sif1_write(NULL, (dma_addr_t)rend, sd->pkt_addr, 64); /* FIXME: (dma_addr_t)? */
	}
}

/* Command 0x8000000a */
static void cmd_rpc_call(void *data, void *arg)
{
	SifRpcCallPkt_t *request = data;
	SifRpcServerData_t *server = request->server;
	SifRpcDataQueue_t *base = server->base;

	BUG();

	// printk("cmd_rpc_call\n");

	if (base->start)
		base->end->link = server;
	else
		base->start = server;

	base->end          = server;
	server->pkt_addr   = request->pkt_addr;
	server->client     = request->client;
	server->rpc_number = request->rpc_number;
	server->size       = request->send_size;
	server->receive    = request->receive;
	server->rsize      = request->recv_size;
	server->rmode      = request->rmode;
	server->rid        = request->rec_id;

	/* XXX: Could be done easier without queue or in a thread? */
	/* XXX: The following is done in interrupt context, should not be done here. */
	server = SifGetNextRequest(base);
	if (server != NULL) {
		SifExecRequest(server);
	}
#if 0 /* XXX: This code is in PS2SDK. */
	if (base->thread_id < 0 || base->active == 0)
		return;

	iWakeupThread(base->thread_id);
#endif
}

/* Command 0x8000000c */
static void cmd_rpc_rdata(void *data, void *arg)
{
	SifRpcOtherDataPkt_t *rdata = data;
	SifRpcRendPkt_t *rend;

	BUG();

	// printk("cmd_rpc_rdata\n");

	rend = (SifRpcRendPkt_t *)_rpc_get_fpacket(arg);
	rend->pkt_addr = rdata->pkt_addr;
	rend->client = (struct t_SifRpcClientData *)rdata->receive;
	rend->cid = 0x8000000c;

	sif_cmd_copy(0x80000008, rend, sizeof(*rend),
		(dma_addr_t)rdata->dest, rdata->src, rdata->size); /* FIXME */
}

int sif_request_cmd(unsigned int cmd, sif_cmd_handler_t handler, void *arg)
{
	const unsigned int id = cmd & ~SIF_CMD_ID_SYSTEM;
	struct t_SifCmdHandlerData *handlers = (cmd & SIF_CMD_ID_SYSTEM) ?
		_sif_cmd_data.sys_cmd_handlers :
		_sif_cmd_data.usr_cmd_handlers;

	if (id >= CMD_HANDLER_MAX)
		return -EINVAL;

	handlers[id].handler = handler;
	handlers[id].arg = arg;

	return 0;
}

void handleRPCIRQ(void *data, void *arg)
{
	SifRpcRendPkt_t *request = data;
	static int once = 0;
	if (!once) {
		once = 1;
	}

	do_IRQ(request->rec_id);
}

static irqreturn_t sif1_dma_handler(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}

int iop_reset(void)
{
	enum { RESET_ARG_MAX = 79 };

	struct {
		u32 arglen;
		u32 mode;
		char arg[RESET_ARG_MAX + 1];
	} reset_pkt = {
		.arglen = sizeof("rom0:UDNL rom0:OSDCNF"),
		.mode = 0,
		.arg = "rom0:UDNL rom0:OSDCNF"
	};

	/* FIXME: Stop DMA */

	sif_write_smflag(SIF_STAT_BOOTEND);

	sif_cmd(SIF_CMD_RESET_CMD, &reset_pkt, sizeof(reset_pkt));

	sif_write_smflag(SIF_STAT_SIFINIT);
	sif_write_smflag(SIF_STAT_CMDINIT);

	while (!sif_smflag_bootend())
		msleep(1);	/* FIXME: Timeout? */

	return 0;
}

void sif_cmd_change(dma_addr_t cmd_buffer)
{
	struct ca_pkt cmd = {
		.buf = cmd_buffer
	};

	sif_cmd(SIF_CMD_CHANGE_SADDR, &cmd, sizeof(cmd));
	/* FIXME: Check error */
}
EXPORT_SYMBOL(sif_cmd_change);

void sif_cmd_init(dma_addr_t cmd_buffer)
{
	struct ca_pkt cmd = {
		.buf = cmd_buffer
	};

	sif_cmd_opt(SIF_CMD_INIT_CMD, 0, &cmd, sizeof(cmd));
	/* FIXME: Check error */
}
EXPORT_SYMBOL(sif_cmd_init);

void sif_rpc_init(void)
{
	sif_cmd_opt(SIF_CMD_INIT_CMD, 1, NULL, 0);
	/* FIXME: Check error */

	while (!read_sreg(SIF_SREG_RPCINIT))
		msleep(1);	/* FIXME: Timeout? */
}
EXPORT_SYMBOL(sif_rpc_init);

dma_addr_t sif_read_subaddr(void)
{
	while (!sif_smflag_cmdinit())
		msleep(1);	/* FIXME: Timeout? */

	return inl(EE_SIF_SUBADDR);
}

static int __init sif_init(void)
{
	int err;

	printk("sif_init\n");

	dmac_buffer0 = (void *)__get_free_page(GFP_DMA);
	dmac_buffer1 = (void *)__get_free_page(GFP_DMA);
	dmac_receive = (void *)__get_free_page(GFP_DMA);

	sif0_dma_addr = dma_map_single(NULL, dmac_buffer0, PAGE_SIZE, DMA_FROM_DEVICE);

#if 1
	_sif_cmd_data.iopbuf = sif_read_subaddr();
	outl(0xff, EE_SIF_UNKNF260);
	outl(sif0_dma_addr, EE_SIF_MAINADDR);
	sif_write_msflag(SIF_STAT_CMDINIT);
	sif_write_msflag(SIF_STAT_BOOTEND);
	printk("sif_init MSFLAG %x\n", sif_read_msflag());
	iop_reset();
#endif

	_sif_cmd_data.pktbuf = dmac_buffer0;

	sif_request_cmd(SIF_CMD_CHANGE_SADDR, cmd_change_addr, &_sif_cmd_data);
	sif_request_cmd(SIF_CMD_WRITE_SREG,   cmd_write_sreg,  &_sif_cmd_data);

	sif_request_cmd(SIF_CMD_RPC_END,      cmd_rpc_end,     &_sif_rpc_data);
	sif_request_cmd(SIF_CMD_RPC_BIND,     cmd_rpc_bind,    &_sif_rpc_data);
	sif_request_cmd(SIF_CMD_RPC_CALL,     cmd_rpc_call,    &_sif_rpc_data);
	sif_request_cmd(SIF_CMD_RPC_RDATA,    cmd_rpc_rdata,   &_sif_rpc_data);
	sif_request_cmd(0x20, handleRPCIRQ, NULL);	/* FIXME */

	sbcall_sifinit();

	err = request_irq(IRQ_DMAC_5, sif0_dma_handler, 0, "SIF0 DMA", NULL);
	if (err) {
		printk(KERN_ERR "sif: Failed to setup SIF0 handler.\n");
		goto err_irq_sif0;
	}

	outl(1 << 5, PS2_D_STAT);
	if (!(inl(PS2_D_STAT) & (1 << (5 + 16))))
		outl(1 << (5 + 16), PS2_D_STAT);
	BUG_ON(!(inl(PS2_D_STAT) & (1 << (5 + 16))));
	BUG_ON(!(inl(EE_DMAC_SIF0_CHCR) & 0x100));

#if 1
	err = request_irq(IRQ_DMAC_6, sif1_dma_handler, 0, "SIF1 DMA", NULL);
	if (err) {
		printk(KERN_ERR "sif: Failed to setup SIF1 handler.\n");
		goto err_irq_sif0;
	}

	outl(1 << 6, PS2_D_STAT);
	if (!(inl(PS2_D_STAT) & (1 << (6 + 16))))
		outl(1 << (6 + 16), PS2_D_STAT);
	BUG_ON(!(inl(PS2_D_STAT) & (1 << (6 + 16))));
#endif


	_sif_cmd_data.iopbuf = sif_read_subaddr();
#if 0
	sif_cmd_change(sif0_dma_addr);
#else
	sif_cmd_init(sif0_dma_addr);
	sif_rpc_init();
#endif

err_irq_sif0:

	printk("sif_init iopbuf %x err %d\n", _sif_cmd_data.iopbuf, err);

	return err;
}

static void __exit sif_exit(void)
{
	dma_unmap_single(NULL, sif0_dma_addr, PAGE_SIZE, DMA_FROM_DEVICE);

#if 0
	sbios(SBIOS_SIF_EXITRPC, 0);
	sbios(SBIOS_SIF_EXITCMD, 0);
	sbios(SBIOS_SIF_EXIT, 0);

	free_irq(IRQ_DMAC_5, NULL);
	free_irq(IRQ_DMAC_6, NULL);
#endif
}

module_init(sif_init);
module_exit(sif_exit);

MODULE_LICENSE("GPL");
