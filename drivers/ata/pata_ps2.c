// SPDX-License-Identifier: GPL
/*
 * PlayStation 2 parallel ATA driver
 *
 * Copyright (C) 2006-2007  Paul Mundt
 * Copyright (C) 2016       Rick Gaiser
 * Copyright (C) 2020       Fredrik Noring
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>

#include <scsi/scsi_host.h>

#include <asm/mach-ps2/iop-heap.h>
#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/iop-registers.h>

#include "iop-module.h"

#define DRV_NAME "pata-ps2"

#if 0
#define PATA_PS2_IRX		0xAAAABBBB
#define PATA_PS2_GET_ADDR	3
#define PATA_PS2_SET_DIR	4

#define CMD_ATA_RW		0x18 // is this CMD number free?
struct ps2_ata_sg { /* 8 bytes */
	u32 addr;
	u32 size;
};

/* Commands need to be 16byte aligned */
struct ps2_ata_cmd_rw {
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

struct ps2_ata_rpc_get_addr {
	u32 ret;
	u32 addr;
	u32 size;
};

struct ps2_ata_rpc_set_dir {
	u32 dir;
};
#endif

struct ps2_port {
	struct device *dev;
	struct ata_port *ap;
	struct completion rpc_completion;

	struct delayed_work delayed_finish;

	u32 iop_data_buffer_addr;
	u32 iop_data_buffer_size;
};

#if 0
static ps2sif_clientdata_t cd_rpc;
static u8 pata_ps2_cmd_buffer[CMD_BUFFER_SIZE] __attribute__ ((aligned(64)));

static void pata_ps2_rpcend_callback(void *arg)
{
	struct ps2_port *pp = (struct ps2_port *)arg;

	complete(&pp->rpc_completion);
}

#define SPD_REGBASE			0x14000000 // EE
//#define SPD_REGBASE			0x10000000 // IOP
#define SPD_R_XFR_CTRL			0x32
#define SPD_R_0x38			0x38
#define SPD_R_IF_CTRL			0x64
#define   SPD_IF_ATA_RESET		  0x80
#define   SPD_IF_DMA_ENABLE		  0x04
#define SPD_R_PIO_MODE			0x70
#define SPD_R_MWDMA_MODE		0x72
#define SPD_R_UDMA_MODE			0x74

static void pata_ps2_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	u16 val;

	switch(adev->pio_mode)
	{
	case XFER_PIO_0:	val = 0x92;	break;
	case XFER_PIO_1:	val = 0x72;	break;
	case XFER_PIO_2:	val = 0x32;	break;
	case XFER_PIO_3:	val = 0x24;	break;
	case XFER_PIO_4:	val = 0x23;	break;
	default:
		dev_err(ap->dev, "Invalid PIO mode %d\n", adev->pio_mode);
		return;
	}

	outw(val, SPD_REGBASE + SPD_R_PIO_MODE);
}

static void pata_ps2_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	u16 val;

	switch(adev->dma_mode)
	{
	case XFER_MW_DMA_0:	val = 0xff;	break;
	case XFER_MW_DMA_1:	val = 0x45;	break;
	case XFER_MW_DMA_2:	val = 0x24;	break;
	case XFER_UDMA_0:	val = 0xa7;	break; /* UDMA16 */
	case XFER_UDMA_1:	val = 0x85;	break; /* UDMA25 */
	case XFER_UDMA_2:	val = 0x63;	break; /* UDMA33 */
	case XFER_UDMA_3:	val = 0x62;	break; /* UDMA44 */
	case XFER_UDMA_4:	val = 0x61;	break; /* UDMA66 */
	case XFER_UDMA_5:	val = 0x60;	break; /* UDMA100 ??? */
	default:
		dev_err(ap->dev, "Invalid DMA mode %d\n", adev->dma_mode);
		return;
	}

	if (adev->dma_mode < XFER_UDMA_0) {
		// MWDMA
		outw(val, SPD_REGBASE + SPD_R_MWDMA_MODE);
		outw((inw(SPD_REGBASE + SPD_R_IF_CTRL) & 0xfffe) | 0x48, SPD_REGBASE + SPD_R_IF_CTRL);
	}
	else {
		// UDMA
		outw(val, SPD_REGBASE + SPD_R_UDMA_MODE);
		outw(inw(SPD_REGBASE + SPD_R_IF_CTRL) | 0x49, SPD_REGBASE + SPD_R_IF_CTRL);
	}
}

static void pata_ps2_set_dir(int dir)
{
	u16 val;

	/* 0x38 ??: What does this do? this register also holds the number of blocks ready for DMA */
	outw(3, SPD_REGBASE + SPD_R_0x38);

	/* IF_CTRL: Save first bit (0=MWDMA, 1=UDMA) */
	val = inw(SPD_REGBASE + SPD_R_IF_CTRL) & 1;
	/* IF_CTRL: Set direction */
	val |= (dir != 0) ? 0x4c : 0x4e;
	outw(val, SPD_REGBASE + SPD_R_IF_CTRL);

	/* XFR_CTRL: Set direction */
	outw(dir | 0x6, SPD_REGBASE + SPD_R_XFR_CTRL);
}

static int dir = -1;
static void pata_ps2_dma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	int newdir;

	newdir = ((qc->tf.flags & ATA_TFLAG_WRITE) == 0) ? 0 : 1;
	if (dir != newdir) {
		dir = newdir;
		pata_ps2_set_dir(dir);
	}

	/* issue r/w command */
	qc->cursg = qc->sg;
	ap->ops->sff_exec_command(ap, &qc->tf);
}

#define DMA_SIZE_ALIGN(s) (((s)+15)&~15)
static void pata_ps2_dma_start(struct ata_queued_cmd *qc)
{
	struct ps2_ata_cmd_rw *cmd = (struct ps2_ata_cmd_rw *)pata_ps2_cmd_buffer;
	struct ps2_ata_sg *cmd_sg = (struct ps2_ata_sg *)(pata_ps2_cmd_buffer + sizeof(struct ps2_ata_cmd_rw));
	struct ps2_port *pp = qc->ap->private_data;
	struct scatterlist *sg;

	if ((qc->tf.flags & ATA_TFLAG_WRITE) != 0) {
		/* Write */
		sg = qc->cursg;
		BUG_ON(!sg);

		if (sg_dma_len(sg) > pp->iop_data_buffer_size) {
			dev_err(pp->dev, "write: %db too big for %d buffer\n", sg_dma_len(sg), pp->iop_data_buffer_size);
			return;
		}

		cmd->write    = 1;
		cmd->callback = 1;
		cmd->sg_count = 1;
		cmd_sg[0].addr = (u32)pp->iop_data_buffer_addr;
		cmd_sg[0].size = sg_dma_len(sg);

		//dev_info(pp->dev, "writing %db from 0x%pad\n", sg_dma_len(sg), (void *)sg_dma_address(sg));

		while (ps2sif_sendcmd(CMD_ATA_RW, cmd
				, DMA_SIZE_ALIGN(sizeof(struct ps2_ata_cmd_rw) + cmd->sg_count * sizeof(struct ps2_ata_sg))
				, (void *)sg_dma_address(sg), (void *)pp->iop_data_buffer_addr, DMA_SIZE_ALIGN(sg_dma_len(sg))) == 0) {
			cpu_relax();
		}
	}
	else {
		/* Read */
		cmd->write    = 0;
		cmd->callback = 1;
		cmd->sg_count = 0;

		/* Fill sg table */
		while (1) {
			sg = qc->cursg;
			BUG_ON(!sg);

			cmd_sg[cmd->sg_count].addr = (u32)sg_dma_address(sg);
			cmd_sg[cmd->sg_count].size = sg_dma_len(sg);
			cmd->sg_count++;
			if ((cmd->sg_count >= MAX_SG_COUNT) || sg_is_last(qc->cursg))
				break;

			qc->cursg = sg_next(qc->cursg);
		}

		//dev_info(pp->dev, "reading %d sg's\n", cmd->sg_count);

		while (ps2sif_sendcmd(CMD_ATA_RW, cmd
				, DMA_SIZE_ALIGN(sizeof(struct ps2_ata_cmd_rw) + cmd->sg_count * sizeof(struct ps2_ata_sg))
				, NULL, NULL, 0) == 0) {
			cpu_relax();
		}

	}
}

static unsigned int pata_ps2_qc_issue(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;

	switch (qc->tf.protocol) {
	case ATA_PROT_DMA:
		WARN_ON(qc->tf.flags & ATA_TFLAG_POLLING);

		ap->ops->sff_tf_load(ap, &qc->tf);  /* load tf registers */
		pata_ps2_dma_setup(qc);	    /* set up dma */
		pata_ps2_dma_start(qc);	    /* initiate dma */
		ap->hsm_task_state = HSM_ST_LAST;
		break;

	case ATAPI_PROT_DMA:
		dev_err(ap->dev, "Error, ATAPI not supported\n");
		BUG();

	default:
		return ata_sff_qc_issue(qc);
	}

	return 0;
}
#endif

static struct scsi_host_template pata_ps2_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations pata_ps2_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.qc_prep		= ata_noop_qc_prep,
#if 0
	.qc_issue		= pata_ps2_qc_issue,
#endif
	.cable_detect		= ata_cable_unknown,
#if 0
	.set_piomode		= pata_ps2_set_piomode,
	.set_dmamode		= pata_ps2_set_dmamode,
#endif
};

static void pata_ps2_setup_port(struct ata_ioports *ioaddr,
	void __iomem *base, unsigned int shift)
{
	ioaddr->cmd_addr = base;
	ioaddr->ctl_addr = base + 0x1c;
	ioaddr->altstatus_addr = ioaddr->ctl_addr;

	ioaddr->data_addr	= ioaddr->cmd_addr + (ATA_REG_DATA    << shift);
	ioaddr->error_addr	= ioaddr->cmd_addr + (ATA_REG_ERR     << shift);
	ioaddr->feature_addr	= ioaddr->cmd_addr + (ATA_REG_FEATURE << shift);
	ioaddr->nsect_addr	= ioaddr->cmd_addr + (ATA_REG_NSECT   << shift);
	ioaddr->lbal_addr	= ioaddr->cmd_addr + (ATA_REG_LBAL    << shift);
	ioaddr->lbam_addr	= ioaddr->cmd_addr + (ATA_REG_LBAM    << shift);
	ioaddr->lbah_addr	= ioaddr->cmd_addr + (ATA_REG_LBAH    << shift);
	ioaddr->device_addr	= ioaddr->cmd_addr + (ATA_REG_DEVICE  << shift);
	ioaddr->status_addr	= ioaddr->cmd_addr + (ATA_REG_STATUS  << shift);
	ioaddr->command_addr	= ioaddr->cmd_addr + (ATA_REG_CMD     << shift);
}

static irqreturn_t pata_ps2_interrupt(int irq, void *dev)
{
	// FIXME: printk("%s irq %d\n", __func__, irq);

	return ata_sff_interrupt(irq, dev);
}

#if 0
static void pata_ps2_dma_finished(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	ata_sff_interrupt(IRQ_SBUS_PCIC, ap->host);
}

static void pata_ps2_cmd_handle(void *data, void *harg)
{
	struct ps2_port *pp = (struct ps2_port *)harg;
	struct ata_port *ap = pp->ap;
	struct ata_queued_cmd *qc;
	struct ps2_ata_cmd_rw *cmd_reply = (struct ps2_ata_cmd_rw *)data;
	u8 status;
	unsigned long flags;

	//if (cmd_reply->write)
	//	dev_info(pp->dev, "cmd write done received\n");
	//else
	//	dev_info(pp->dev, "cmd read  done received\n");

	spin_lock_irqsave(&ap->host->lock, flags);

	qc = ata_qc_from_tag(ap, ap->link.active_tag);

	if (qc && !(qc->tf.flags & ATA_TFLAG_POLLING)) {
		if (!sg_is_last(qc->cursg)) {
			/* Start next DMA transfer */
			qc->cursg = sg_next(qc->cursg);
			pata_ps2_dma_start(qc);
		}
		else if ((cmd_reply->write == 0) || (cmd_reply->ata0_intr == 1)) {
			/* Wait for completion when there is no more data to transfer
			 *  - NOTE: When writing we need to wait for the completion interrupt
			 */
			status = ioread8(ap->ioaddr.altstatus_addr);
			if (status & (ATA_BUSY | ATA_DRQ)) {
				dev_info(pp->dev, "status = %d\n", status);
				ata_sff_queue_delayed_work(&pp->delayed_finish, 1);
			} else {
				pata_ps2_dma_finished(ap, qc);
			}
		}
	}

	spin_unlock_irqrestore(&ap->host->lock, flags);
}

static void pata_ps2_delayed_finish(struct work_struct *work)
{
	struct ps2_port *pp = container_of(work, struct ps2_port, delayed_finish.work);
	struct ata_port *ap = pp->ap;
	struct ata_host *host = ap->host;
	struct ata_queued_cmd *qc;
	unsigned long flags;
	u8 status;

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * If the port is not waiting for completion, it must have
	 * handled it previously.  The hsm_task_state is
	 * protected by host->lock.
	 */
	if (ap->hsm_task_state != HSM_ST_LAST)
		goto out;

	status = ioread8(ap->ioaddr.altstatus_addr);
	if (status & (ATA_BUSY | ATA_DRQ)) {
		//dev_info(pp->dev, "status = %d\n", status);
		/* Still busy, try again. */
		ata_sff_queue_delayed_work(&pp->delayed_finish, 1);
		goto out;
	}
	qc = ata_qc_from_tag(ap, ap->link.active_tag);
	if (qc && !(qc->tf.flags & ATA_TFLAG_POLLING))
		pata_ps2_dma_finished(ap, qc);
out:
	spin_unlock_irqrestore(&host->lock, flags);
}

static void pata_ps2_setup_rpc(struct ps2_port *pp)
{
	int rv;
	struct ps2_ata_rpc_get_addr *rpc_buffer = (struct ps2_ata_rpc_get_addr *)pata_ps2_cmd_buffer;

	/*
	 * Create our own CMD handler
	 */
	rv = ps2sif_addcmdhandler(CMD_ATA_RW, pata_ps2_cmd_handle, (void *)pp);
	if (rv < 0) {
		dev_err(pp->dev, "rpc setup: add cmd handler rv = %d.\n", rv);
		return;
	}

	/*
	 * Bind to RPC server
	 */
	rv = ps2sif_bindrpc(&cd_rpc, PATA_PS2_IRX, SIF_RPCM_NOWAIT, pata_ps2_rpcend_callback, (void *)pp);
	if (rv < 0) {
		dev_err(pp->dev, "rpc setup: bindrpc rv = %d.\n", rv);
		return;
	}
	wait_for_completion(&pp->rpc_completion);

	/*
	 * Set and Get IOP address
	 */
	rv = ps2sif_callrpc(&cd_rpc, PATA_PS2_GET_ADDR, SIF_RPCM_NOWAIT
			, NULL, 0
			, rpc_buffer, sizeof(struct ps2_ata_rpc_get_addr)
			, pata_ps2_rpcend_callback, (void *)pp);
	if (rv < 0) {
		dev_err(pp->dev, "rpc setup: PATA_PS2_GET_ADDR rv = %d.\n", rv);
		return;
	}
	wait_for_completion(&pp->rpc_completion);
	pp->iop_data_buffer_addr = rpc_buffer->addr;
	pp->iop_data_buffer_size = rpc_buffer->size;
	dev_info(pp->dev, "rpc setup: iop cmd buffer @ %pad, size = %d\n", (void *)pp->iop_data_buffer_addr, pp->iop_data_buffer_size);
}
#endif

static int pata_ps2_probe(struct platform_device *pdev)
{
	struct resource *regs;
	struct ata_host *host;
	struct ata_port *ap;
	struct ps2_port *pp;
	void __iomem *base;
	int irq;
	int err;

	err = iop_module_request("ata", 0x0100, NULL);
	if (err < 0)
		return err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		return irq;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(base)) {
		err = PTR_ERR(base);
		dev_err(&pdev->dev, "devm_ioremap_resource 0 failed with %d\n", err);
		goto err_ioremap;
	}

	pp = devm_kzalloc(&pdev->dev, sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		err = -ENOMEM;
		goto err_kzalloc;
	}

	host = ata_host_alloc(&pdev->dev, 1);
	if (!host) {
		err = -ENOMEM;
		goto err_host_alloc;
	}

	ap = host->ports[0];
	ap->private_data = pp;

	ap->ops = &pata_ps2_port_ops;
	ap->pio_mask = ATA_PIO3;
	// ap->mwdma_mask	= ATA_MWDMA2;
	// ap->udma_mask = ATA_UDMA4; // ATA_UDMA5;
	ap->flags |= ATA_FLAG_NO_ATAPI;

	pp->dev = &pdev->dev;
	pp->ap = ap;
	init_completion(&pp->rpc_completion);
	/* FIXME: INIT_DELAYED_WORK(&pp->delayed_finish, pata_ps2_delayed_finish); */

	/* FIXME: pata_ps2_setup_rpc(pp); */

#if 0
	// if (!irq) {
		ap->flags |= ATA_FLAG_PIO_POLLING;
	//	ata_port_desc(ap, "no IRQ, using PIO polling");
	// }
#endif

	pata_ps2_setup_port(&ap->ioaddr, base, 1);

	printk("%s cmd %x ctl %x status %x irq %u\n", __func__,
		(u32)ap->ioaddr.cmd_addr,
		(u32)ap->ioaddr.ctl_addr,
		(u32)ap->ioaddr.status_addr,
		irq);

	iop_set_dma_dpcr2(IOP_DMA_DPCR2_DEV9);

	return ata_host_activate(host,
		irq, pata_ps2_interrupt, IRQF_SHARED,
		&pata_ps2_sht);

err_host_alloc:
err_kzalloc:
err_ioremap:
	return err;
}

static int pata_ps2_remove(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);

	ata_host_detach(host);

	return 0;
}

static struct platform_driver pata_ps2_driver = {
	.probe		= pata_ps2_probe,
	.remove		= pata_ps2_remove,
	.driver = {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(pata_ps2_driver);

MODULE_AUTHOR("Rick Gaiser");
MODULE_AUTHOR("Fredrik Noring");
MODULE_DESCRIPTION("PlayStation 2 parallel ATA driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
