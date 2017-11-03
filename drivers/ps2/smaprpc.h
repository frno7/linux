/*
 * PlayStation 2 Ethernet device driver header file
 *
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef	__SMAP_H__
#define	__SMAP_H__

#include <linux/version.h>

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/mii.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/mach-ps2/sif.h>
#include <asm/mach-ps2/dmarelay.h>


#define SMAP_TX_QUEUE_MAX 20

struct smap_soft_regs {
	u32 tx_buffer_addr;
	u32 tx_buffer_size;
	u32 _spare1;
	u32 _spare2;
};

/*
 * SMAP control structure(smap channel)
 */
struct smaprpc_chan {
	spinlock_t spinlock;
	struct net_device *net_dev;
	u_int32_t flags;
	u_int32_t irq;
	struct completion compl;
	struct net_device_stats net_stats;

	ps2sif_clientdata_t cd_smap_rpc;
	int rpc_initialized;
	struct mutex smap_rpc_mutex;

	void *shared_addr;
	unsigned int shared_size;

	struct smap_soft_regs *soft_regs;
	u32 iop_data_buffer_addr;
	u32 iop_data_buffer_size;
	volatile int tx_queued;

	struct mii_bus *mii;
	int phy_addr;
	struct phy_device *phydev;
	int last_link;
};

/* flags */
#define	SMAP_F_OPENED		(1<<0)
//#define	SMAP_F_LINKESTABLISH	(1<<1)
//#define	SMAP_F_LINKVALID	(1<<2)
//#define	SMAP_F_CHECK_FORCE100M	(1<<3)
//#define	SMAP_F_CHECK_FORCE10M	(1<<4)
//#define	SMAP_F_INITDONE		(1<<5)
//#define	SMAP_F_SPD_100M		(1<<8)
//#define	SMAP_F_DUP_FULL		(1<<9)
//#define	SMAP_F_TXDNV_DISABLE	(1<<16)
//#define	SMAP_F_RXDNV_DISABLE	(1<<17)
//#define	SMAP_F_DMA_ENABLE	(1<<24)
//#define	SMAP_F_DMA_TX_ENABLE	(1<<25)
//#define	SMAP_F_DMA_RX_ENABLE	(1<<26)
//#define	SMAP_F_PRINT_PKT	(1<<30)
#define	SMAP_F_PRINT_MSG	(1<<31)

#endif /* __SMAP_H__ */
