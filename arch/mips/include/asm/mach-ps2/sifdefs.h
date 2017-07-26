/*
 *  PlayStation 2 SIFRPC
 *
 *  Copyright (C) 2001      Sony Computer Entertainment Inc.
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

#ifndef __ASM_PS2_SIFDEFS_H
#define __ASM_PS2_SIFDEFS_H

#include <linux/types.h>
#include "sbios.h"

/*
 * SIF DMA defines
 */

#define SIF_DMA_INT_I	0x2
#define SIF_DMA_INT_O	0x4

typedef struct {
	uint32_t data;
	uint32_t addr;
	uint32_t size;
	uint32_t mode;
} ps2sif_dmadata_t;

extern unsigned int ps2sif_setdma(ps2sif_dmadata_t *sdd, int len);
extern int ps2sif_dmastat(unsigned int id);
extern unsigned int __ps2sif_setdma_wait(ps2sif_dmadata_t *, int, long);
extern int __ps2sif_dmastat_wait(unsigned int, long);
extern void ps2sif_writebackdcache(const void *, int);

#define ps2sif_setdma_wait(sdd, len)			\
	__ps2sif_setdma_wait((sdd), (len), TASK_UNINTERRUPTIBLE)
#define ps2sif_setdma_wait_interruptible(sdd, len)	\
	__ps2sif_setdma_wait((sdd), (len), TASK_INTERRUPTIBLE)
#define ps2sif_dmastat_wait(id)			\
	((void)__ps2sif_dmastat_wait((id), TASK_UNINTERRUPTIBLE))
#define ps2sif_dmastat_wait_interruptible(id)		\
	__ps2sif_dmastat_wait((id), TASK_INTERRUPTIBLE)

/*
 * SIF RPC defines
 */

typedef struct _sif_rpc_data {
	void			*paddr;	/* packet address */
	unsigned int		pid;	/* packet id */
	struct wait_queue	*wq;	/* wait queue */
	unsigned int		mode;	/* call mode */
} ps2sif_rpcdata_t;

typedef void (*ps2sif_endfunc_t)(void *);

typedef struct _sif_client_data {
	struct _sif_rpc_data	rpcd;
	unsigned int		command;
	void			*buff;
	void			*cbuff;
	ps2sif_endfunc_t	func;
	void			*para;
	struct _sif_serve_data	*serve;
} ps2sif_clientdata_t;

typedef struct _sif_receive_data {
	struct _sif_rpc_data	rpcd;
	void			*src;
	void			*dest;
	int			size;
	ps2sif_endfunc_t	func;
	void			*para;
} ps2sif_receivedata_t;

typedef void *(*ps2sif_rpcfunc_t)(unsigned int, void *, int);

typedef struct _sif_serve_data {
	unsigned int		command;
	ps2sif_rpcfunc_t	func;
	void			*buff;
	int			size;
	ps2sif_rpcfunc_t	cfunc;
	void			*cbuff;
	int			csize;
	ps2sif_clientdata_t *client;
	void			*paddr;
	unsigned int		fno;
	void			*receive;
	int			rsize;
	int			rmode;
	unsigned int		rid;
	struct _sif_serve_data	*link;
	struct _sif_serve_data	*next;
	struct _sif_queue_data	*base;
} ps2sif_servedata_t;

typedef struct _sif_queue_data {
	int             	active;
	struct _sif_serve_data	*link;
	struct _sif_serve_data	*start;
	struct _sif_serve_data	*end;
	struct _sif_queue_data	*next;
	struct wait_queue	*waitq;
	void			(*callback)(void*);
	void			*callback_arg;
} ps2sif_queuedata_t;

/* call & bind mode */
#define SIF_RPCM_NOWAIT		0x01	/* not wait for end of function */
#define SIF_RPCM_NOWBDC		0x02	/* no write back d-cache */

/* calling error */
#define SIF_RPCE_GETP	1	/* fail to get packet data */
#define SIF_RPCE_SENDP	2	/* fail to send dma packet */
#define E_IOP_INTR_CONTEXT 100
#define E_IOP_DEPENDANCY 200
#define E_LF_NOT_IRX 201
#define E_LF_FILE_IO_ERROR 204
#define E_LF_FILE_NOT_FOUND 203
#define E_IOP_NO_MEMORY 400
#define E_SIF_PKT_ALLOC 0xd610	/* Can't allocate SIF packet. */
#define E_SIF_PKT_SEND 0xd611
#define E_SIF_RPC_BIND 0xd612
#define E_SIF_RPC_CALL 0xd613

/* functions */

int __init ps2sif_init(void);
int ps2sif_addcmdhandler(u_int fid, ps2_addr_t func, ps2_addr_t data);
int ps2sif_sendcmd(u_int fid, ps2_addr_t pp, int ps, ps2_addr_t src, ps2_addr_t dest, int size);
int ps2sif_bindrpc(ps2sif_clientdata_t *, unsigned int, unsigned int, ps2sif_endfunc_t, void *);
int ps2sif_callrpc(ps2sif_clientdata_t *, unsigned int, unsigned int, void *, int, void *, int, ps2sif_endfunc_t, void *);

int ps2sif_checkstatrpc(ps2sif_rpcdata_t *);

void ps2sif_setrpcqueue(ps2sif_queuedata_t *, void (*)(void*), void *);
ps2sif_servedata_t *ps2sif_getnextrequest(ps2sif_queuedata_t *);
void ps2sif_execrequest(ps2sif_servedata_t *);
void ps2sif_registerrpc(ps2sif_servedata_t *, unsigned int, ps2sif_rpcfunc_t, void *, ps2sif_rpcfunc_t, void *, ps2sif_queuedata_t *);
int ps2sif_getotherdata(ps2sif_receivedata_t *, void *, void *, int, unsigned int, ps2sif_endfunc_t, void *);
ps2sif_servedata_t *ps2sif_removerpc(ps2sif_servedata_t *, ps2sif_queuedata_t *);
ps2sif_queuedata_t *ps2sif_removerpcqueue(ps2sif_queuedata_t *);

/*
 * IOP heap defines
 */

dma_addr_t ps2sif_allociopheap(int);
int ps2sif_freeiopheap(dma_addr_t);
dma_addr_t ps2sif_phystobus(phys_addr_t a);
phys_addr_t ps2sif_bustophys(dma_addr_t a);

/*
 * SBIOS defines
 */

int sbios_rpc(int func, void *arg, int *result);

#endif /* __ASM_PS2_SIFDEFS_H */
