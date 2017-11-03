/*
 * PlayStation 2 Ethernet device driver
 *
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/platform_device.h>
#include <linux/kthread.h>

#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/sbios.h>

#include "smaprpc.h"

#define DRV_NAME "smaprpc"

/* RPC from EE -> IOP */
#define SMAP_BIND_RPC_ID	0x0815e000
//#define SMAP_CMD_SEND		1
#define SMAP_CMD_SET_BUFFER	2
#define SMAP_CMD_GET_MAC_ADDR	3
#define SMAP_CMD_SET_MAC_ADDR	4
#define SMAP_CMD_MDIO_READ	5
#define SMAP_CMD_MDIO_WRITE	6
#define SMAP_CMD_SET_LNK	7

/* CMD from IOP -> EE */
#define SIF_SMAP_RECEIVE	0x07

/* CMD: EE <-> IOP */
#define CMD_SMAP_RW		0x17 // is this CMD number free?
#define CMD_SMAP_RESET		0x19 // is this CMD number free?

struct ps2_smap_sg { /* 8 bytes */
	u32 addr;
	u32 size;
};

/* Commands need to be 16byte aligned */
struct ps2_smap_cmd_rw {
	/* Header: 16 bytes */
	struct t_SifCmdHeader sifcmd;
	/* Data: 8 bytes */
	u32 write:1;
	u32 callback:1;
	u32 ata0_intr:1;
	u32 sg_count:29;
	u32 _spare;
};
#define MAX_CMD_SIZE (112)
#define CMD_BUFFER_SIZE (MAX_CMD_SIZE)
#define MAX_SG_COUNT ((CMD_BUFFER_SIZE - sizeof(struct ps2_ata_cmd_rw)) / sizeof(struct ps2_ata_sg))

static u32 smap_rpc_data[16] __attribute__ ((aligned(64)));
static u8 smap_cmd_buffer[64] __attribute__ ((aligned(64)));
#define DATA_BUFFER_SIZE (32*1024)
u8 _data_buffer[DATA_BUFFER_SIZE] __attribute((aligned(64)));
static u8 * _data_buffer_wr_pointer = _data_buffer;
static u8 * _data_buffer_rd_pointer = _data_buffer;

/*--------------------------------------------------------------------------*/

/* When rd_pointer == wr_pointer this can mean the buffer is EMPTY or FULL.
 * To prevent this we return the free size - 1, so the buffer can never be full.
 * This means when rd_pointer == wr_pointer, the buffer is empty.
 */
static inline u32 data_buffer_free(void)
{
	if (_data_buffer_wr_pointer >= _data_buffer_rd_pointer)
		return (DATA_BUFFER_SIZE - (_data_buffer_wr_pointer - _data_buffer)) - 1;
	else
		return (_data_buffer_rd_pointer - _data_buffer_wr_pointer) - 1;
}

static inline u32 data_buffer_free_next(void)
{
	if (_data_buffer_wr_pointer >= _data_buffer_rd_pointer)
		return (_data_buffer_rd_pointer - _data_buffer) - 1;
	else
		return 0;
}

static inline void data_buffer_reset(void)
{
	if (_data_buffer_wr_pointer >= _data_buffer_rd_pointer)
		_data_buffer_wr_pointer = _data_buffer;
}

/*--------------------------------------------------------------------------*/

static int smaprpc_start_xmit(struct sk_buff *skb, struct net_device *net_dev);
static struct net_device_stats *smaprpc_get_stats(struct net_device *net_dev);
static int smaprpc_open(struct net_device *net_dev);
static int smaprpc_close(struct net_device *net_dev);
static int smaprpc_ioctl(struct net_device *net_dev, struct ifreq *ifr,	int cmd);
static void smaprpc_rpcend_notify(void *arg);
static void smaprpc_rpc_setup(struct smaprpc_chan *smap);
extern int smaprpc_mdio_unregister(struct net_device *ndev);
extern int smaprpc_mdio_register(struct net_device *ndev);

/*--------------------------------------------------------------------------*/

#define DMA_SIZE_ALIGN(s) (((s)+15)&~15)
static void smaprpc_dma_write(struct smaprpc_chan *smap, dma_addr_t ee_addr, size_t datasize, u32 iop_addr)
{
	struct ps2_smap_cmd_rw *cmd = (struct ps2_smap_cmd_rw *)smap_cmd_buffer;
	struct ps2_smap_sg *cmd_sg = (struct ps2_smap_sg *)(smap_cmd_buffer + sizeof(struct ps2_smap_cmd_rw));

	cmd->write    = 1;
	cmd->callback = 1;
	cmd->sg_count = 1;
	cmd_sg[0].addr = iop_addr;
	cmd_sg[0].size = datasize;

	while (ps2sif_sendcmd(CMD_SMAP_RW, cmd
			, DMA_SIZE_ALIGN(sizeof(struct ps2_smap_cmd_rw) + cmd->sg_count * sizeof(struct ps2_smap_sg))
			, (void *)ee_addr, (void *)iop_addr, DMA_SIZE_ALIGN(datasize)) == 0) {
		cpu_relax();
	}
}

static void smaprpc_cmd_reset(struct smaprpc_chan *smap)
{
	struct t_SifCmdHeader *cmd = (struct t_SifCmdHeader *)smap_cmd_buffer;

	while (ps2sif_sendcmd(CMD_SMAP_RESET, cmd, DMA_SIZE_ALIGN(sizeof(struct t_SifCmdHeader))
			, NULL, NULL, 0) == 0) {
		cpu_relax();
	}
}

/* return value: 0 if success, !0 if error */
static int smaprpc_start_xmit(struct sk_buff *skb, struct net_device *net_dev)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);
	dma_addr_t ee_addr;
	u32 offset;
	unsigned long flags;
	u32 dma_size = DMA_SIZE_ALIGN(skb->len);

	/* Reset the ring-buffer if needed */
	if (data_buffer_free() < dma_size)
		data_buffer_reset();
	offset = _data_buffer_wr_pointer - _data_buffer;

	/* Copy data to ring-buffer */
	memcpy(_data_buffer_wr_pointer, skb->data, skb->len);

	/* Flush caches */
	ee_addr = virt_to_phys(_data_buffer_wr_pointer);
	dma_sync_single_for_cpu(NULL, _data_buffer_wr_pointer, skb->len, DMA_TO_DEVICE);

	//printk("%s: (%d) TX strt @ %pad, size = %d\n", smap->net_dev->name, smap->tx_queued+1, (void *)(smap->iop_data_buffer_addr + offset), skb->len);

	/* Start async data transfer to IOP */
	smaprpc_dma_write(smap, ee_addr, skb->len, smap->iop_data_buffer_addr + offset);

	/* Advance the ring-buffer */
	_data_buffer_wr_pointer += dma_size;

	netif_trans_update(net_dev);

	//printk("%s: rd->wr = %d->%d (%d)\n", smap->net_dev->name, _data_buffer_rd_pointer - _data_buffer, _data_buffer_wr_pointer - _data_buffer, dma_size);

	spin_lock_irqsave(&smap->spinlock, flags);

	smap->tx_queued++;
	if ((smap->tx_queued >= SMAP_TX_QUEUE_MAX) || ((data_buffer_free() < DMA_SIZE_ALIGN(1520)) && (data_buffer_free_next() < DMA_SIZE_ALIGN(1520)))) {
		//printk("%s: TX strt -> sleep(%d)\n", smap->net_dev->name, smap->tx_queued);
		netif_stop_queue(smap->net_dev);
	}

	spin_unlock_irqrestore(&smap->spinlock, flags);

	dev_kfree_skb(skb);

	return 0;
}

static void handleSmapTXDone(void *data, void *arg)
{
	struct ps2_smap_cmd_rw *cmd = (struct ps2_smap_cmd_rw *)data;
	struct ps2_smap_sg *cmd_sg = (struct ps2_smap_sg *)((u8*)data + sizeof(struct ps2_smap_cmd_rw));
	struct smaprpc_chan *smap = (struct smaprpc_chan *)arg;
	u32 dma_size = DMA_SIZE_ALIGN(cmd_sg[0].size);

	// Validate the packet
	if ((cmd->write != 1) || (cmd->callback != 1) || (cmd->sg_count != 1)) {
		printk("%s: TX done -> cmd error, write = %d, callback = %d, sg_count = %d\n", smap->net_dev->name, cmd->write, cmd->callback, cmd->sg_count);
		return;
	}

	if (cmd_sg[0].size > 1520) {
		printk("%s: TX done -> cmd_sg error, size = %d\n", smap->net_dev->name, cmd_sg[0].size);
		return;
	}

	if ((cmd_sg[0].addr < smap->iop_data_buffer_addr) || ((cmd_sg[0].addr + dma_size) > (smap->iop_data_buffer_addr + smap->iop_data_buffer_size))) {
		printk("%s: TX done -> cmd_sg error, addr = %d, size = %d\n", smap->net_dev->name, cmd_sg[0].addr, cmd_sg[0].size);
		return;
	}

	_data_buffer_rd_pointer = _data_buffer + (cmd_sg[0].addr + dma_size - smap->iop_data_buffer_addr);
	smap->tx_queued--;

	if (smap->tx_queued < 0) {
		printk("%s: TX done -> error, queued=%d\n", smap->net_dev->name, smap->tx_queued);
		smap->tx_queued = 0;
	}

	if ((smap->tx_queued < SMAP_TX_QUEUE_MAX) && ((data_buffer_free() >= DMA_SIZE_ALIGN(1520)) || (data_buffer_free_next() >= DMA_SIZE_ALIGN(1520)))) {
		if (netif_queue_stopped(smap->net_dev)) {
			//printk("%s: TX done -> wake (%d)\n", smap->net_dev->name, smap->tx_queued);
			netif_wake_queue(smap->net_dev);
		}
	}
}

static void smaprpc_tx_timeout(struct net_device *net_dev)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);
	unsigned long flags;

	printk("%s: timeout: free = %db, fnext = %db, queued = %d, stopped = %d\n", smap->net_dev->name, data_buffer_free(), data_buffer_free_next(), smap->tx_queued, netif_queue_stopped(smap->net_dev));

	/* Reset */
	spin_lock_irqsave(&smap->spinlock, flags);
	smap->tx_queued = 0;
	_data_buffer_rd_pointer = _data_buffer;
	_data_buffer_wr_pointer = _data_buffer;
	spin_unlock_irqrestore(&smap->spinlock, flags);

	smaprpc_cmd_reset(smap);

	/* Start transmission again */
	netif_wake_queue(smap->net_dev);

	return;
}

/*--------------------------------------------------------------------------*/

static struct net_device_stats *smaprpc_get_stats(struct net_device *net_dev)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);

	return (&smap->net_stats);
}

static void
smaprpc_adjust_link(struct net_device *net_dev)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);
	struct phy_device *phydev = smap->phydev;
	int link_state;
	int rv;

	if (phydev == NULL)
		return;

	/* hash together the state values to decide if something has changed */
	link_state = phydev->speed | (phydev->duplex << 1) | phydev->link;
	if (smap->last_link != link_state) {
		smap->last_link = link_state;

		if (phydev->link == 0)
			netif_carrier_off(net_dev);

		mutex_lock(&smap->smap_rpc_mutex);

			smap_rpc_data[0] = phydev->link                ? 1 : 0;
			smap_rpc_data[1] = phydev->duplex              ? 1 : 0;
			smap_rpc_data[2] = (phydev->speed == SPEED_10) ? 1 : 0;

			do {
				rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_SET_LNK,
					SIF_RPCM_NOWAIT,
					smap_rpc_data, 3*sizeof(u32), // send
					smap_rpc_data, 1*sizeof(u32), // receive
					(ps2sif_endfunc_t) smaprpc_rpcend_notify, (void *) &smap->compl);
			} while (rv == -E_SIF_PKT_ALLOC);

			if (rv != 0) {
				printk("%s: SMAP_CMD_SET_LNK failed, (%d)\n", smap->net_dev->name, rv);
				rv = -1;
			} else {
				wait_for_completion(&smap->compl);
				rv = smap_rpc_data[0];
			}

		mutex_unlock(&smap->smap_rpc_mutex);

		if (phydev->link != 0)
			netif_carrier_on(net_dev);

		phy_print_status(phydev);
	}
}

static int smaprpc_open(struct net_device *net_dev)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);
	char phy_id_fmt[MII_BUS_ID_SIZE + 3];

	if (smap->flags & SMAP_F_PRINT_MSG) {
		printk("%s: PlayStation 2 SMAP open\n", net_dev->name);
	}

	if (smap->flags & SMAP_F_OPENED) {
		printk("%s: already opend\n", net_dev->name);
		return -EBUSY;
	}

	snprintf(phy_id_fmt, MII_BUS_ID_SIZE + 3, PHY_ID_FMT, smap->mii->id, 1);
	smap->phydev = phy_connect(net_dev, phy_id_fmt, &smaprpc_adjust_link, PHY_INTERFACE_MODE_MII);
	if (IS_ERR(smap->phydev)) {
		pr_err("%s: Could not attach to PHY\n", net_dev->name);
		return -ENODEV;
	}

	smap->phydev->supported &= PHY_BASIC_FEATURES;
	smap->phydev->advertising = smap->phydev->supported;

	phy_start(smap->phydev);

	netif_start_queue(net_dev);
	netif_carrier_off(net_dev);

	smap->flags |= SMAP_F_OPENED;

	return 0;
}

static int
smaprpc_set_mac_address(struct net_device *net_dev, void *p)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);
	struct sockaddr *addr = p;
	int rv;

	if (netif_running(net_dev))
		return -EBUSY;

	mutex_lock(&smap->smap_rpc_mutex);

		memcpy(smap_rpc_data, addr->sa_data, ETH_ALEN);

		do {
			rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_SET_MAC_ADDR,
				SIF_RPCM_NOWAIT,
				smap_rpc_data, ETH_ALEN, // send
				NULL, 0, // receive
				(ps2sif_endfunc_t) smaprpc_rpcend_notify, (void *) &smap->compl);
		} while (rv == -E_SIF_PKT_ALLOC);

		if (rv != 0) {
			printk("%s: SMAP_CMD_SET_MAC_ADDR failed, (%d)\n", smap->net_dev->name,
				rv);
		} else {
			wait_for_completion(&smap->compl);
			memcpy(net_dev->dev_addr, addr->sa_data, ETH_ALEN);
		}

	mutex_unlock(&smap->smap_rpc_mutex);

	return 0;
}

int
smaprpc_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct smaprpc_chan *smap = netdev_priv(ndev);
	int rv;

	mutex_lock(&smap->smap_rpc_mutex);

		smap_rpc_data[0] = phyaddr;
		smap_rpc_data[1] = phyreg;

		do {
			rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_MDIO_READ,
				SIF_RPCM_NOWAIT,
				smap_rpc_data, 2*sizeof(u32), // send
				smap_rpc_data, 1*sizeof(u32), // receive
				(ps2sif_endfunc_t) smaprpc_rpcend_notify, (void *) &smap->compl);
		} while (rv == -E_SIF_PKT_ALLOC);

		if (rv != 0) {
			printk("%s: SMAP_CMD_MDIO_READ failed, (%d)\n", smap->net_dev->name, rv);
			rv = -1;
		} else {
			wait_for_completion(&smap->compl);
			rv = smap_rpc_data[0];
		}

	mutex_unlock(&smap->smap_rpc_mutex);

	return rv;
}

int
smaprpc_mdio_write(struct mii_bus *bus, int phyaddr, int phyreg, u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct smaprpc_chan *smap = netdev_priv(ndev);
	int rv;

	mutex_lock(&smap->smap_rpc_mutex);

		smap_rpc_data[0] = phyaddr;
		smap_rpc_data[1] = phyreg;
		smap_rpc_data[2] = phydata;

		do {
			rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_MDIO_WRITE,
				SIF_RPCM_NOWAIT,
				smap_rpc_data, 3*sizeof(u32), // send
				smap_rpc_data, 1*sizeof(u32), // receive
				(ps2sif_endfunc_t) smaprpc_rpcend_notify, (void *) &smap->compl);
		} while (rv == -E_SIF_PKT_ALLOC);

		if (rv != 0) {
			printk("%s: SMAP_CMD_MDIO_READ failed, (%d)\n", smap->net_dev->name, rv);
			rv = -1;
		} else {
			wait_for_completion(&smap->compl);
			rv = smap_rpc_data[0];
		}

	mutex_unlock(&smap->smap_rpc_mutex);

	return rv;
}

static int smaprpc_close(struct net_device *net_dev)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);
	unsigned long flags;

	if (smap->flags & SMAP_F_PRINT_MSG) {
		printk("%s: PlayStation 2 SMAP close\n", net_dev->name);
	}

	if ((smap->flags & SMAP_F_OPENED) == 0) {
		printk("PlayStation 2 SMAP: not opened\n");
		return(-EINVAL);
	}

	spin_lock_irqsave(&smap->spinlock, flags);
	smap->flags &= ~SMAP_F_OPENED;
	spin_unlock_irqrestore(&smap->spinlock, flags);

	/* Stop and disconnect the PHY */
	if (smap->phydev) {
		phy_stop(smap->phydev);
		phy_disconnect(smap->phydev);
		smap->phydev = NULL;
	}

	netif_stop_queue(net_dev);
	netif_carrier_off(net_dev);

	return 0;
}

/*--------------------------------------------------------------------------*/

static int smaprpc_ioctl(struct net_device *net_dev, struct ifreq *ifr, int cmd)
{
	struct smaprpc_chan *smap = netdev_priv(net_dev);
	int retval = 0;

	if (!netif_running(net_dev))
		return -EINVAL;

	if (!smap->phydev)
		return -EINVAL;

	switch (cmd) {
	default:
		retval = phy_mii_ioctl(smap->phydev, ifr, cmd);
		break;
	}

	return retval;
}

static void smaprpc_rpc_setup(struct smaprpc_chan *smap)
{
	int loop;

	int rv;

	volatile int j;

	if (smap->rpc_initialized) {
		return;
	}

	/* bind smaprpc.irx module */
	for (loop = 100; loop; loop--) {
		rv = ps2sif_bindrpc(&smap->cd_smap_rpc, SMAP_BIND_RPC_ID,
			SIF_RPCM_NOWAIT, smaprpc_rpcend_notify, (void *) &smap->compl);
		if (rv < 0) {
			printk("%s: smap rpc setup: bind rv = %d.\n", smap->net_dev->name,
				rv);
			break;
		}
		wait_for_completion(&smap->compl);
		if (smap->cd_smap_rpc.serve != 0)
			break;
		j = 0x010000;
		while (j--);
	}
	if (smap->cd_smap_rpc.serve == 0) {
		printk("%s: smap rpc setup: bind error 1, network will not work\n",
			smap->net_dev->name);
		return;
	}

	do {
		rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_GET_MAC_ADDR,
			SIF_RPCM_NOWAIT,
			NULL, 0, // send
			smap_rpc_data, 1*sizeof(u32) + ETH_ALEN, // receive
			(ps2sif_endfunc_t) smaprpc_rpcend_notify, (void *) &smap->compl);
	} while (rv == -E_SIF_PKT_ALLOC);
	if (rv != 0) {
		printk("%s: SMAP_CMD_GET_MAC_ADDR failed, (%d)\n", smap->net_dev->name,
			rv);
	} else {
		wait_for_completion(&smap->compl);
		memcpy(smap->net_dev->dev_addr, &smap_rpc_data[1], ETH_ALEN);
		printk("%s: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
			smap->net_dev->name,
			smap->net_dev->dev_addr[0],
			smap->net_dev->dev_addr[1],
			smap->net_dev->dev_addr[2],
			smap->net_dev->dev_addr[3],
			smap->net_dev->dev_addr[4],
			smap->net_dev->dev_addr[5]);
	}

	smap->shared_size = 32 * 1024;
	smap->shared_addr = kmalloc(smap->shared_size, GFP_KERNEL);
	if (smap->shared_addr != NULL) {
		smap_rpc_data[0] = virt_to_phys(smap->shared_addr);
		smap_rpc_data[1] = smap->shared_size;
		do {
			rv = ps2sif_callrpc(&smap->cd_smap_rpc, SMAP_CMD_SET_BUFFER,
				SIF_RPCM_NOWAIT,
				smap_rpc_data, 2*sizeof(u32), // send
				smap_rpc_data, 2*sizeof(u32), // receive
				(ps2sif_endfunc_t) smaprpc_rpcend_notify, (void *) &smap->compl);
		} while (rv == -E_SIF_PKT_ALLOC);
		if (rv != 0) {
			printk("%s: SMAP_CMD_SET_BUFFER failed, (rv = %d). Receive will not work.\n",
				smap->net_dev->name, rv);
		} else {
			wait_for_completion(&smap->compl);
			if (smap_rpc_data[0] != 0) {
				printk("%s: SMAP_CMD_SET_BUFFER failed, (0x%08x). Receive will not work.\n",
					smap->net_dev->name, smap_rpc_data[0]);
			}
			smap->soft_regs = (struct smap_soft_regs *)phys_to_virt(iop_bus_to_phys(smap_rpc_data[1]));
			smap->iop_data_buffer_addr = smap->soft_regs->tx_buffer_addr;
			smap->iop_data_buffer_size = smap->soft_regs->tx_buffer_size;
			/* FIXME: 7d7992108d02aa92ad4c77e5d9ce14088c942e75 dev_info(&smap->net_dev->dev, "rpc setup: iop cmd buffer @ %pad, size = %d\n", (void *)smap->iop_data_buffer_addr, smap->iop_data_buffer_size); */
		}
	} else {
		printk("%s: Failed to allocate receive buffer. Receive will not work.\n",
			smap->net_dev->name);
	}
	smap->rpc_initialized = -1;
}

static void smaprpc_rpcend_notify(void *arg)
{
	complete((struct completion *) arg);
	return;
}

static void handleSmapIRQ(void *cmd_data, void *arg)
{
	struct ps2_smap_cmd_rw *cmd = (struct ps2_smap_cmd_rw *)cmd_data;
	struct ps2_smap_sg *cmd_sg = (struct ps2_smap_sg *)((u8*)cmd_data + sizeof(struct ps2_smap_cmd_rw));
	struct smaprpc_chan *smap = (struct smaprpc_chan *)arg;
	struct sk_buff *skb;
	int i;
	u8 *data;

	//printk("%s: handleSmapIRQ size=%d\n", smap->net_dev->name, cmd_sg[0].size);

	for (i = 0; i < cmd->sg_count; i++) {
		data = phys_to_virt(cmd_sg[i].addr);
		dma_cache_inv((unsigned long) data, cmd_sg[i].size);

		skb = netdev_alloc_skb(smap->net_dev, cmd_sg[i].size + 2);
		if (skb == NULL) {
			printk("%s: handleSmapIRQ, skb alloc error\n", smap->net_dev->name);
			return;
		}
		skb_reserve(skb, 2);		/* 16 byte align the data fields */
		skb_copy_to_linear_data(skb, data, cmd_sg[i].size);
		skb_put(skb, cmd_sg[i].size);
		skb->dev = smap->net_dev;
		skb->protocol = eth_type_trans(skb, smap->net_dev);
		netif_rx(skb);
	}
}

/*--------------------------------------------------------------------------*/

/* ethtool support */
static int smap_nway_reset(struct net_device *ndev)
{
	struct smaprpc_chan *smap = netdev_priv(ndev);
	return phy_start_aneg(smap->phydev);
}

static const struct ethtool_ops smaprpc_ethtool_ops = {
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.nway_reset = smap_nway_reset,
	.get_link = ethtool_op_get_link,
	//.get_ts_info = ethtool_op_get_ts_info,
};

extern int ps2_pccard_present;

static const struct net_device_ops smaprpc_netdev_ops = {
	.ndo_open		= smaprpc_open,
	.ndo_stop		= smaprpc_close,
	.ndo_do_ioctl		= smaprpc_ioctl,
	.ndo_start_xmit		= smaprpc_start_xmit,
	.ndo_get_stats		= smaprpc_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= smaprpc_set_mac_address,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_tx_timeout		= smaprpc_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = NULL,
#endif
};

static int smaprpc_probe(struct platform_device *dev)
{
	struct net_device *net_dev = NULL;
	struct smaprpc_chan *smap = NULL;

	if (ps2_pccard_present == 0) {
		printk("PlayStation 2 Ethernet device NOT present.\n");
		printk("HACK: continuing anyway!\n");
		//return (-ENODEV);
	}

	net_dev = alloc_etherdev(sizeof(struct smaprpc_chan));
	if (!net_dev) {
		pr_err("alloc_etherdev failed\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(net_dev, &dev->dev);
	platform_set_drvdata(dev, net_dev);

	smap = netdev_priv(net_dev);

	/* clear control structure */
	memset(smap, 0, sizeof(struct smaprpc_chan));

	/* init network device structure */
	ether_setup(net_dev);
	smap->net_dev = net_dev;
	smap->tx_queued = 0;

	init_completion(&smap->compl);
	net_dev->watchdog_timeo = 5; // 5 jiffies?
	net_dev->netdev_ops = &smaprpc_netdev_ops;
	net_dev->ethtool_ops = &smaprpc_ethtool_ops;

	spin_lock_init(&smap->spinlock);
	mutex_init(&smap->smap_rpc_mutex);

	if (ps2sif_addcmdhandler(SIF_SMAP_RECEIVE, handleSmapIRQ, smap) < 0) {
		printk("Failed to initialize smap IRQ handler. Receive will not work.\n");
	}

	if (ps2sif_addcmdhandler(CMD_SMAP_RW, handleSmapTXDone, smap) < 0) {
		printk("Failed to initialize smap IRQ handler. Receive will not work.\n");
	}

	if (register_netdev(net_dev)) {
		goto error;
	}
	smaprpc_rpc_setup(smap);
	if (!smap->rpc_initialized) {
		goto error2;
	}

	/* MDIO bus Registration */
	if (smaprpc_mdio_register(net_dev) < 0) {
		pr_debug("%s: MDIO bus registration failed", __func__);
		goto error2;
	}

	printk("PlayStation 2 SMAP(Ethernet) rpc device driver.\n");
	return 0;

error2:
	unregister_netdev(net_dev);
error:
	printk("PlayStation 2 SMAP(Ethernet) rpc device not found.\n");
	free_netdev(net_dev);
	return (-ENODEV);
}

static int smaprpc_driver_remove(struct platform_device *pdev)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct smaprpc_chan *smap = netdev_priv(net_dev);

	if (smap->rpc_initialized) {
		/* Remove interrupt handler. */
		struct sb_sifremovecmdhandler_arg param;

		param.fid = SIF_SMAP_RECEIVE;
		if (sbios(SB_SIFREMOVECMDHANDLER, &param) < 0) {
			printk("Failed to remove smap IRQ handler.\n");
		}
	}

	if (smap->shared_addr != NULL) {
		kfree(smap->shared_addr);
	}

	if (net_dev->flags & IFF_UP)
		dev_close(net_dev);

	smaprpc_mdio_unregister(net_dev);
	netif_carrier_off(net_dev);
	unregister_netdev(net_dev);

	/* XXX: Disable device. */

	free_netdev(net_dev);
	return 0;
}

static struct platform_driver smap_driver = {
	.remove	= smaprpc_driver_remove,
	.driver	= {
		.name	= DRV_NAME,
	},
};

module_platform_driver_probe(smap_driver, smaprpc_probe);

MODULE_AUTHOR("Jürgen Urban");
MODULE_DESCRIPTION("PlayStation 2 SMAP(Ethernet) rpc device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
