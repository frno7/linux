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

#include <asm/mach-ps2/sif.h>
#include <asm/mach-ps2/iop-module.h>

#define DRV_NAME "pata_ps2"
#define DRV_VERSION "1.0"

struct ps2_port {
	struct device *dev;
	struct ata_port *ap;
};

#define SPD_REGBASE			0x14000000 // EE
//#define SPD_REGBASE			0x10000000 // IOP
#define SPD_R_PIO_DATA			0x2e
#define SPD_R_XFR_CTRL			0x32
#define SPD_R_0x38			0x38
#define SPD_R_IF_CTRL			0x64
#define   SPD_IF_ATA_RESET		  0x80
#define   SPD_IF_DMA_ENABLE		  0x04
#define SPD_R_PIO_MODE			0x70
#define SPD_R_MWDMA_MODE		0x72
#define SPD_R_UDMA_MODE			0x74

static unsigned int pata_ps2_data_xfer(struct ata_queued_cmd *qc,
	unsigned char *buf, unsigned int buflen, int rw)
{
	unsigned int words = buflen >> 1;
	unsigned int i;

	printk("pata_ps2_data_xfer\n");

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		for (i = 0; i < words; i++)
			((u16 *)buf)[i] = inw(SPD_REGBASE + SPD_R_PIO_DATA);
	else
		for (i = 0; i < words; i++)
			outw(((u16 *)buf)[i], SPD_REGBASE + SPD_R_PIO_DATA);

	/* Transfer trailing byte, if any. */
	if (unlikely(buflen & 0x01)) {
		/* Point buf to the tail of buffer */
		buf += buflen - 1;

		if (rw == READ)
			*buf = inw(SPD_REGBASE + SPD_R_PIO_DATA);
		else
			outw(*buf, SPD_REGBASE + SPD_R_PIO_DATA);
		words++;
	}

	return buflen;
}

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

	printk("pata_ps2_set_piomode 0x%x\n", val);

	outw(val, SPD_REGBASE + SPD_R_PIO_MODE);
}

static unsigned int pata_ps2_qc_issue(struct ata_queued_cmd *qc)
{
	printk("pata_ps2_qc_issue\n");

	return ata_sff_qc_issue(qc);
}

static int pata_ps2_set_mode(struct ata_link *link, struct ata_device **unused)
{
	struct ata_device *dev;

	printk("pata_ps2_set_mode\n");

	ata_for_each_dev(dev, link, ENABLED) {
		/* We don't really care */
		dev->pio_mode = dev->xfer_mode = XFER_PIO_0; // FIXME: _4
		dev->xfer_shift = ATA_SHIFT_PIO;
		dev->flags |= ATA_DFLAG_PIO;
		ata_dev_info(dev, "configured for PIO\n");
	}

	return 0;
}

static struct scsi_host_template pata_ps2_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations pata_ps2_port_ops = {
	.inherits	= &ata_sff_port_ops,
	.cable_detect	= ata_cable_unknown,
	.qc_prep	= ata_noop_qc_prep,
#if 0
	.qc_issue	= pata_ps2_qc_issue,
	.sff_data_xfer	= pata_ps2_data_xfer,
#endif
	.set_piomode	= pata_ps2_set_piomode,
	.set_mode	= pata_ps2_set_mode,
};

static irqreturn_t pata_ps2_interrupt(int irq, void *dev)
{
	/* FIXME: return ata_sff_interrupt(irq, dev); */
	return IRQ_HANDLED;
}

static int pata_ps2_probe(struct platform_device *pdev)
{
	struct resource *io_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *ctl_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	struct resource *irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	struct ps2_port *pp;
	struct ata_host *host;
	struct ata_port *ap;
	int irq = 0;
	int irq_flags = 0;

	printk("pata_ps2_probe\n");

	if (io_res == NULL || ctl_res == NULL)
		return -EINVAL;

	/*
	 * And the IRQ
	 */
	if (irq_res && irq_res->start > 0) {
		irq = irq_res->start;
		irq_flags = IRQF_SHARED;
	}

	pp = devm_kzalloc(&pdev->dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return -ENOMEM;

	/*
	 * Now that that's out of the way, wire up the port..
	 */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		return -ENOMEM;

	ap = host->ports[0];
	ap->private_data = pp;

	ap->ops = &pata_ps2_port_ops;
	ap->pio_mask = ATA_PIO4;
#if 0	/* FIXME */
	ap->mwdma_mask	= ATA_MWDMA2;
	ap->udma_mask = ATA_UDMA4; // ATA_UDMA5;
#endif
	ap->flags |= ATA_FLAG_NO_ATAPI;

	pp->dev = &pdev->dev;
	pp->ap = ap;

	//if (!irq) {
		ap->flags |= ATA_FLAG_PIO_POLLING;
	//	ata_port_desc(ap, "no IRQ, using PIO polling");
	//}

	ap->ioaddr.cmd_addr = devm_ioremap(&pdev->dev,
		io_res->start, resource_size(io_res));
	ap->ioaddr.ctl_addr = devm_ioremap(&pdev->dev,
		ctl_res->start, resource_size(ctl_res));
	if (!ap->ioaddr.cmd_addr || !ap->ioaddr.ctl_addr) {
		dev_err(&pdev->dev, "failed to map IO/CTL base\n");
		return -ENOMEM;
	}

	ap->ioaddr.altstatus_addr = ap->ioaddr.ctl_addr;

	ap->ioaddr.data_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_DATA;
	ap->ioaddr.error_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_ERR;
	ap->ioaddr.feature_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_FEATURE;
	ap->ioaddr.nsect_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_NSECT;
	ap->ioaddr.lbal_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_LBAL;
	ap->ioaddr.lbam_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_LBAM;
	ap->ioaddr.lbah_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_LBAH;
	ap->ioaddr.device_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_DEVICE;
	ap->ioaddr.status_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_STATUS;
	ap->ioaddr.command_addr	= ap->ioaddr.cmd_addr + 2 * ATA_REG_CMD;

	ata_port_desc(ap, "cmd 0x%llx ctl 0x%llx",
		(unsigned long long)io_res->start,
		(unsigned long long)ctl_res->start);

	printk("pata_ps2_probe done\n");

	/* activate */
	return ata_host_activate(host, irq,
		irq ? pata_ps2_interrupt : NULL,
		irq_flags, &pata_ps2_sht);
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

static int __init pata_ps2_init(void)
{
	return platform_driver_register(&pata_ps2_driver);
}
module_init(pata_ps2_init);

static void __exit pata_ps2_exit(void)
{
	platform_driver_unregister(&pata_ps2_driver);
}
module_exit(pata_ps2_exit);

MODULE_AUTHOR("Rick Gaiser");
MODULE_DESCRIPTION("Playstation 2 PATA driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:" DRV_NAME);
