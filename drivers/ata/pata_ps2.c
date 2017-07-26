/*
 * Playstation 2 PATA driver
 *
 * Copyright (C) 2016 - 2016  Rick Gaiser
 *
 * Based on pata_platform:
 *
 *   Copyright (C) 2006 - 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <scsi/scsi_host.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>

#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/iopmodules.h>

#define DRV_NAME "pata_ps2"
#define DRV_VERSION "1.0"

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

struct ps2_port {
	struct device *dev;
	struct ata_port *ap;
	struct completion rpc_completion;

	struct workqueue_struct *wq;
	struct delayed_work delayed_finish;

	u32 iop_data_buffer_addr;
	u32 iop_data_buffer_size;
};


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

static struct scsi_host_template pata_ps2_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations pata_ps2_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.qc_prep		= ata_noop_qc_prep,
	.qc_issue		= pata_ps2_qc_issue,
	.cable_detect		= ata_cable_unknown,
	.set_piomode		= pata_ps2_set_piomode,
	.set_dmamode		= pata_ps2_set_dmamode,
};

static void pata_ps2_setup_port(struct ata_ioports *ioaddr,
				     unsigned int shift)
{
	/* Fixup the port shift for platforms that need it */
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

static void pata_ps2_dma_finished(struct ata_port *ap, struct ata_queued_cmd *qc)
{
	ata_sff_interrupt(IRQ_SBUS_PCIC, ap->host);
}

static irqreturn_t pata_ps2_interrupt(int irq, void *dev)
{
	//return ata_sff_interrupt(irq, dev);
	return IRQ_HANDLED;
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
				queue_delayed_work(pp->wq, &pp->delayed_finish, 1);
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
		queue_delayed_work(pp->wq, &pp->delayed_finish, 1);
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

static int __devinit __pata_ps2_probe(struct device *dev,
				    struct resource *io_res,
				    struct resource *ctl_res,
				    struct resource *irq_res,
				    unsigned int ioport_shift)
{
	struct ps2_port *pp;
	struct ata_host *host;
	struct ata_port *ap;
	unsigned int mmio;
	int irq = 0;
	int irq_flags = 0;

	/*
	 * Check for MMIO
	 */
	mmio = (( io_res->flags == IORESOURCE_MEM) &&
		(ctl_res->flags == IORESOURCE_MEM));

	/*
	 * And the IRQ
	 */
	if (irq_res && irq_res->start > 0) {
		irq = irq_res->start;
		irq_flags = irq_res->flags;
	}

	pp = devm_kzalloc(dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	/*
	 * Now that that's out of the way, wire up the port..
	 */
	host = ata_host_alloc(dev, 1);
	if (!host)
		return -ENOMEM;

	ap = host->ports[0];
	ap->private_data = pp;

	ap->ops = &pata_ps2_port_ops;
	ap->pio_mask = ATA_PIO4;
	ap->mwdma_mask	= ATA_MWDMA2;
	ap->udma_mask = ATA_UDMA4; // ATA_UDMA5;
	ap->flags |= ATA_FLAG_NO_ATAPI;

	pp->dev = dev;
	pp->ap = ap;
	init_completion(&pp->rpc_completion);
	pp->wq = create_singlethread_workqueue(DRV_NAME);
	if (!pp->wq)
		return -1;
	INIT_DELAYED_WORK(&pp->delayed_finish, pata_ps2_delayed_finish);

	pata_ps2_setup_rpc(pp);


	/*
	 * Use polling mode if there's no IRQ
	 */
	//if (!irq) {
		ap->flags |= ATA_FLAG_PIO_POLLING;
	//	ata_port_desc(ap, "no IRQ, using PIO polling");
	//}

	/*
	 * Handle the MMIO case
	 */
	if (mmio) {
		ap->ioaddr.cmd_addr = devm_ioremap(dev, io_res->start,
				resource_size(io_res));
		ap->ioaddr.ctl_addr = devm_ioremap(dev, ctl_res->start,
				resource_size(ctl_res));
	} else {
		ap->ioaddr.cmd_addr = devm_ioport_map(dev, io_res->start,
				resource_size(io_res));
		ap->ioaddr.ctl_addr = devm_ioport_map(dev, ctl_res->start,
				resource_size(ctl_res));
	}
	if (!ap->ioaddr.cmd_addr || !ap->ioaddr.ctl_addr) {
		dev_err(dev, "failed to map IO/CTL base\n");
		return -ENOMEM;
	}

	ap->ioaddr.altstatus_addr = ap->ioaddr.ctl_addr;

	pata_ps2_setup_port(&ap->ioaddr, ioport_shift);

	ata_port_desc(ap, "%s cmd 0x%llx ctl 0x%llx", mmio ? "mmio" : "ioport",
		      (unsigned long long)io_res->start,
		      (unsigned long long)ctl_res->start);

	/* activate */
	return ata_host_activate(host, irq, irq ? pata_ps2_interrupt : NULL,
				 irq_flags, &pata_ps2_sht);
}

/**
 *	__pata_ps2_remove		-	unplug a platform interface
 *	@dev: device
 *
 *	A platform bus ATA device has been unplugged. Perform the needed
 *	cleanup. Also called on module unload for any active devices.
 */
int __pata_ps2_remove(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);

	ata_host_detach(host);

	return 0;
}
EXPORT_SYMBOL_GPL(__pata_ps2_remove);

static int __devinit pata_ps2_probe(struct platform_device *pdev)
{
	struct resource *io_res;
	struct resource *ctl_res;
	struct resource *irq_res;

	if (load_module_firmware("ps2/pata_ps2.irx", 0) < 0) {
		dev_err(&pdev->dev, "loading ps2/pata_ps2.irx failed\n");
		return -ENODEV;
	}

	/*
	 * Simple resource validation ..
	 */
	if ((pdev->num_resources != 3) && (pdev->num_resources != 2)) {
		dev_err(&pdev->dev, "invalid number of resources\n");
		return -EINVAL;
	}

	/*
	 * Get the I/O base first
	 */
	io_res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (io_res == NULL) {
		io_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (unlikely(io_res == NULL))
			return -EINVAL;
	}

	/*
	 * Then the CTL base
	 */
	ctl_res = platform_get_resource(pdev, IORESOURCE_IO, 1);
	if (ctl_res == NULL) {
		ctl_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (unlikely(ctl_res == NULL))
			return -EINVAL;
	}

	/*
	 * And the IRQ
	 */
	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq_res)
		irq_res->flags = IRQF_SHARED;

	return __pata_ps2_probe(&pdev->dev, io_res, ctl_res, irq_res, 1);
}

static int __devexit pata_ps2_remove(struct platform_device *pdev)
{
	return __pata_ps2_remove(&pdev->dev);
}

static struct platform_driver pata_ps2_driver = {
	.probe		= pata_ps2_probe,
	.remove		= __devexit_p(pata_ps2_remove),
	.driver = {
		.name		= DRV_NAME,
		.owner		= THIS_MODULE,
	},
};

static int __init pata_ps2_init(void)
{
	return platform_driver_register(&pata_ps2_driver);
}

static void __exit pata_ps2_exit(void)
{
	platform_driver_unregister(&pata_ps2_driver);
}
module_init(pata_ps2_init);
module_exit(pata_ps2_exit);

MODULE_AUTHOR("Rick Gaiser");
MODULE_DESCRIPTION("Playstation 2 PATA driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:" DRV_NAME);
