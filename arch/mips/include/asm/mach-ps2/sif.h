// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 sub-system interface (SIF)
 *
 * The SIF is an interface unit to the input/output processor (IOP).
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_SIF_H
#define __ASM_MACH_PS2_SIF_H

#include <linux/types.h>
#include <linux/completion.h>

#include "iop-memory.h"

#define SIF_MAINADDR 		0x1000f200	/* EE to IOP command buffer */
#define SIF_SUBADDR  		0x1000f210	/* IOP to EE command buffer */
#define SIF_MSFLAG   		0x1000f220	/* EE to IOP flag */
#define SIF_SMFLAG   		0x1000f230	/* IOP to EE flag */
#define SIF_SUBCTRL  		0x1000f240
#define SIF_UNKNF260		0x1000f260

/* Status flags for the sub-to-main (SM) and main-to-sub (MS) SIF registers. */
#define SIF_STATUS_SIFINIT	0x10000		/* SIF initialised */
#define SIF_STATUS_CMDINIT	0x20000		/* SIF CMD initialised */
#define SIF_STATUS_BOOTEND	0x40000		/* IOP bootup completed */

#define	SIF_CMD_ID_SYS		0x80000000
#define	SIF_CMD_ID_USR		0x00000000

#define SIF_CMD_CHANGE_SADDR	(SIF_CMD_ID_SYS | 0x00)
#define SIF_CMD_WRITE_SREG	(SIF_CMD_ID_SYS | 0x01)
#define SIF_CMD_INIT_CMD	(SIF_CMD_ID_SYS | 0x02)
#define SIF_CMD_RESET_CMD	(SIF_CMD_ID_SYS | 0x03)
#define SIF_CMD_RPC_END		(SIF_CMD_ID_SYS | 0x08)
#define SIF_CMD_RPC_BIND	(SIF_CMD_ID_SYS | 0x09)
#define SIF_CMD_RPC_CALL	(SIF_CMD_ID_SYS | 0x0a)
#define SIF_CMD_RPC_RDATA	(SIF_CMD_ID_SYS | 0x0c)
#define SIF_CMD_ATA		(SIF_CMD_ID_SYS | 0x13)
#define SIF_CMD_GAMEPAD		(SIF_CMD_ID_SYS | 0x14)
/*
 * Note: Numbers greater than 0x1f (31) cannot be sent from the kernel
 * to the IOP. However, they can still be sent in the reverse direction,
 * from the IOP to the kernel.
 *
 * So the following commands can only be sent from the IOP to the kernel:
 */
#define SIF_CMD_IRQ_RELAY	(SIF_CMD_ID_SYS | 0x20)
#define SIF_CMD_PRINTK		(SIF_CMD_ID_SYS | 0x21)

#define	SIF_SID_ID_SYS		0x80000000
#define	SIF_SID_ID_USR		0x00000000

#define SIF_SID_FILE_IO		(SIF_SID_ID_SYS | 0x01)
#define SIF_SID_HEAP		(SIF_SID_ID_SYS | 0x03)
#define SIF_SID_LOAD_MODULE	(SIF_SID_ID_SYS | 0x06)
#define SIF_SID_IRQ_RELAY	(SIF_SID_ID_SYS | 0x20)

#define SIF_CMD_PACKET_MAX	112
#define SIF_CMD_PACKET_DATA_MAX 96

/**
 * struct sif_cmd_header - 16-byte SIF command header
 * @packet_size: min 1x16 for header only, max 7*16 bytes
 * @data_size: data size in bytes
 * @data_addr: data address or zero
 * @cmd: command number
 * @opt: optional argument
 */
struct sif_cmd_header
{
	u32 packet_size : 8;
	u32 data_size : 24;
	u32 data_addr;
	u32 cmd;
	u32 opt;
};

int sif_cmd_opt_data(u32 cmd, u32 opt, const void *pkt,
	size_t pktsize, iop_addr_t dst, const void *src, size_t nbytes);

int sif_cmd_data(u32 cmd, const void *pkt, size_t pktsize,
	iop_addr_t dst, const void *src, size_t nbytes);

int sif_cmd_opt(u32 cmd, u32 opt, const void *pkt, size_t pktsize);

int sif_cmd(u32 cmd, const void *pkt, size_t pktsize);

/*
 * sif_cmd_payload - return payload of SIF command
 * @header: SIF command header
 *
 * The size of the payload is max %CMD_PACKET_PAYLOAD_MAX bytes.
 */
#define sif_cmd_payload(header)						\
	({								\
		typecheck(const struct sif_cmd_header *, (header));	\
		(void*)&(header)[1];					\
	})

/**
 * typedef sif_cmd_cb - SIF command handler
 * @header: SIF command header
 * @arg: argument passed to sif_request_cmd()
 */
typedef void (*sif_cmd_cb)(const struct sif_cmd_header *header, void *arg);

int sif_request_cmd(u32 cmd, sif_cmd_cb cb, void *arg);

struct sif_rpc_client
{
	iop_addr_t server;
	iop_addr_t server_buffer;

	size_t client_size_max;
	void *client_buffer;

	struct completion done;
};

int sif_rpc_bind(struct sif_rpc_client *client, u32 server_id);
void sif_rpc_unbind(struct sif_rpc_client *client);

int sif_rpc(struct sif_rpc_client *client, u32 rpc_id,
	const void *send, size_t send_size, void *recv, size_t recv_size);

#endif /* __ASM_MACH_PS2_SIF_H */
