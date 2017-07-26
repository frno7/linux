/*
 * PlayStation 2 sub-system interface (SIF)
 *
 * The sub-system interface is an interface unit to the I/O processor (IOP).
 */

#ifndef __ASM_PS2_SIF_H
#define __ASM_PS2_SIF_H

#include <linux/types.h>
#include <linux/completion.h>

#define	SIF_CMD_ID_SYSTEM	0x80000000

#define SIF_CMD_CHANGE_SADDR	(SIF_CMD_ID_SYSTEM | 0x00)
#define SIF_CMD_WRITE_SREG	(SIF_CMD_ID_SYSTEM | 0x01)
#define SIF_CMD_INIT_CMD	(SIF_CMD_ID_SYSTEM | 0x02)
#define SIF_CMD_RESET_CMD	(SIF_CMD_ID_SYSTEM | 0x03)
#define SIF_CMD_RPC_END		(SIF_CMD_ID_SYSTEM | 0x08)
#define SIF_CMD_RPC_BIND	(SIF_CMD_ID_SYSTEM | 0x09)
#define SIF_CMD_RPC_CALL	(SIF_CMD_ID_SYSTEM | 0x0a)
#define SIF_CMD_RPC_RDATA	(SIF_CMD_ID_SYSTEM | 0x0c)

#define SIF_SID_FILE_IO		(SIF_CMD_ID_SYSTEM | 0x01)
#define SIF_SID_HEAP		(SIF_CMD_ID_SYSTEM | 0x03)
#define SIF_SID_LOAD_MODULE	(SIF_CMD_ID_SYSTEM | 0x06)

#define SIF_ERR_INTR_CONTEXT		100
#define	SIF_ERR_IRX_DEPENDANCY		200
#define	SIF_ERR_IRX_INVALID		201
#define	SIF_ERR_IRX_FILE_NOT_FOUND	203
#define	SIF_ERR_IRX_FILE_IO_ERROR	204
#define SIF_ERR_OUT_OF_MEMORY		400

/** System SREG */
#define SIF_SREG_RPCINIT	0

typedef void (*sif_cmd_handler_t)(void *data, void *arg);

/* FIXME: __must_check */
int sif_request_cmd(unsigned int cmd, sif_cmd_handler_t handler, void *arg);

typedef void * (*SifRpcFunc_t)(int fno, void *buffer, int length);
typedef void (*SifRpcEndFunc_t)(void *end_param);

typedef struct t_SifRpcPktHeader {
	int			rec_id;
	void			*pkt_addr;
	int			rpc_id;
} SifRpcPktHeader_t;

typedef struct t_SifRpcRendPkt
{
   int				rec_id;		/* 04 */
   void				*pkt_addr;	/* 05 */
   int				rpc_id;		/* 06 */

   struct t_SifRpcClientData	*client;	/* 7 */
   u32                          cid;		/* 8 */
   struct t_SifRpcServerData	*server;	/* 9 */
   void				*buff,		/* 10 */
      				*cbuff;		/* 11 */
} SifRpcRendPkt_t;

typedef struct t_SifRpcOtherDataPkt
{
   int				rec_id;		/* 04 */
   void				*pkt_addr;	/* 05 */
   int				rpc_id;		/* 06 */

   struct t_SifRpcReceiveData	*receive;	/* 07 */
   void				*src;		/* 08 */
   void				*dest;		/* 09 */
   int				size;		/* 10 */
} SifRpcOtherDataPkt_t;

typedef struct t_SifRpcBindPkt
{
   int				rec_id;		/* 04 */
   void				*pkt_addr;	/* 05 */
   int				rpc_id;		/* 06 */
   struct t_SifRpcClientData	*client;	/* 07 */
   int				sid;		/* 08 */
} SifRpcBindPkt_t;

typedef struct t_SifRpcCallPkt
{
   int				rec_id;		/* 04 */
   void				*pkt_addr;	/* 05 */
   int				rpc_id;		/* 06 */
   struct t_SifRpcClientData	*client;	/* 07 */
   int				rpc_number;	/* 08 */
   int				send_size;	/* 09 */
   dma_addr_t			receive;	/* 10 */
   int				recv_size;	/* 11 */
   int				rmode;		/* 12 */
   struct t_SifRpcServerData	*server;	/* 13 */
} SifRpcCallPkt_t;

typedef struct t_SifRpcServerData
{
   int				sid;		/* 04	00 */

   SifRpcFunc_t			func;		/* 05	01 */
   void				*buff;		/* 06	02 */
   int				size;		/* 07	03 */

   SifRpcFunc_t			cfunc;		/* 08	04 */
   void				*cbuff;		/* 09	05 */
   int				size2;		/* 10	06 */

   struct t_SifRpcClientData	*client;	/* 11	07 */
   void				*pkt_addr;	/* 12	08 */
   int				rpc_number;	/* 13	09 */

   dma_addr_t 			receive;	/* 14	10 */
   int				rsize;		/* 15	11 */
   int				rmode;		/* 16	12 */
   int				rid;		/* 17	13 */

   struct t_SifRpcServerData	*link;		/* 18	14 */
   struct t_SifRpcServerData	*next;		/* 19	15 */
   struct t_SifRpcDataQueue	*base;		/* 20	16 */
} SifRpcServerData_t;

typedef struct t_SifRpcDataQueue
{
   int				thread_id,	/* 00 */
      				active;		/* 01 */
   struct t_SifRpcServerData	*link,		/* 02 */
      				*start,		/* 03 */
                                *end;		/* 04 */
   struct t_SifRpcDataQueue	*next;  	/* 05 */
} SifRpcDataQueue_t;

#define SIF_DMA_INT_I	0x2
#define SIF_DMA_INT_O	0x4

#define SIF_DMA_ERT	0x40

#define SIF_REG_ID_SYSTEM	0x80000000

//Status bits for the SM and MS SIF registers
/** SIF initialized */
#define SIF_STAT_SIFINIT	0x10000
/** SIFCMD initialized */
#define SIF_STAT_CMDINIT	0x20000
/** Bootup completed */
#define SIF_STAT_BOOTEND	0x40000

/** Enable interrupt after transfer. */
#define TGE_SIFDMA_ATTR_INT_I	0x02
#define TGE_SIFDMA_ATTR_INT_O	0x04
/** Transfer IOP DMAtag without data. */
#define TGE_SIFDMA_ATTR_DMATAG	0x20
#define TGE_SIFDMA_ATTR_ERT		0x40

/**
 * Structure describing data transfered from EE to IOP.
 */
typedef struct t_SifDmaTransfer
{
   void				*src, /** Data to be transfered (located in EE memory). */
      				*dest; /** Where to copy the data in IOP memory. */
   int				size; /** Size in bytes to transfer. */
	/** Flags:
	 *   TGE_SIFDMA_ATTR_INT_I
	 *   TGE_SIFDMA_ATTR_DMATAG
	 *   TGE_SIFDMA_ATTR_INT_O
	 *   TGE_SIFDMA_ATTR_ERT
	 */
   int				attr;
} SifDmaTransfer_t;

struct t_SifRpcHeader
{
	void *pkt_addr;
	u32 rpc_id;
	u32 mode;
	struct completion cmp;
};

struct t_SifRpcClientData
{
	struct t_SifRpcHeader hdr;
	u32 command;
	void *buff;
	void *cbuff;
	SifRpcEndFunc_t end_function;
	void *end_param;
	struct t_SifRpcServerData *server;
};

struct t_SifRpcReceiveData	/* FIXME: Used? */
{
	struct t_SifRpcHeader hdr;
	void *src;
	void *dest;
	int size;
};

u32 SifSetDma(SifDmaTransfer_t *sdd, s32 len);
s32 SifDmaStat(u32 id);

/* SIF RPC client API */
int sif_rpc_bind(struct t_SifRpcClientData *cd, int sid);

int sif_rpc(struct t_SifRpcClientData *client, int rpc_number,
	const void *send, int ssize, void *receive, int rsize);
int sif_rpc_async(struct t_SifRpcClientData *cd, int rpc_number,
	const void *sendbuf, int ssize, void *recvbuf, int rsize,
	SifRpcEndFunc_t endfunc, void *efarg);

bool sif_cmd_copy(int cid, void *pkt, int pktsize,
	dma_addr_t dst, const void *src, int nbytes);
bool sif_cmd(int cid, void *pkt, int pktsize);

#endif /* __ASM_PS2_SIF_H */
