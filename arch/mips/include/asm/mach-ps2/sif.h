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

#endif /* __ASM_MACH_PS2_SIF_H */
