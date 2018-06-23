// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 USB 1.1 OHCI host controller (HCD)
 *
 * Copyright (C) 2017 Jürgen Urban
 * Copyright (C) 2018 Fredrik Noring
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include <asm/mach-ps2/iop-heap.h>
#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/iop-registers.h>

#include "ohci.h"

#define DRIVER_DESC "PlayStation 2 USB OHCI host controller"
#define DRV_NAME "ohci-ps2"

/* Size allocated from IOP heap (maximum size of DMA memory). */
#define DMA_BUFFER_SIZE (256 * 1024)

#define hcd_to_priv(hcd) ((struct ps2_hcd *)(hcd_to_ohci(hcd)->priv))

/**
 * struct ps2_hcd - private device driver structure
 * @iop_dma_addr: input/output processor (IOP) DMA buffer address
 */
struct ps2_hcd {
	dma_addr_t iop_dma_addr;
};

static struct hc_driver __read_mostly ohci_ps2_hc_driver;
static irqreturn_t (*ohci_irq)(struct usb_hcd *hcd);

static void ohci_ps2_enable(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci_writel(ohci, 1, &ohci->regs->roothub.portstatus[11]);
}

static void ohci_ps2_disable(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	ohci_writel(ohci, 0, &ohci->regs->roothub.portstatus[11]);
}

static void ohci_ps2_start_hc(struct usb_hcd *hcd)
{
	iop_set_dma_dpcr2(IOP_DMA_DPCR2_OHCI);

	outw(1, IOP_OHCI_BASE + 0x80);
}

static void ohci_ps2_stop_hc(struct usb_hcd *hcd)
{
	iop_clr_dma_dpcr2(IOP_DMA_DPCR2_OHCI);
}

static int ohci_ps2_reset(struct usb_hcd *hcd)
{
	int err;

	ohci_ps2_start_hc(hcd);

	err = ohci_setup(hcd);
	if (err) {
		ohci_ps2_stop_hc(hcd);
		return err;
	}

	ohci_ps2_enable(hcd);

	return 0;
}

static irqreturn_t ohci_ps2_irq(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	struct ohci_regs __iomem *regs = ohci->regs;

	/*
	 * OHCI_INTR_MIE needs to be disabled, most likely due to a
	 * hardware bug. Without it, reading a large amount of data
	 * (> 1 GB) from a mass storage device results in a freeze.
	 */
	ohci_writel(ohci, OHCI_INTR_MIE, &regs->intrdisable);

	return ohci_irq(hcd);	/* Call normal IRQ handler. */
}

static int iopheap_alloc_dma_buffer(struct platform_device *pdev, size_t size)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);
	int err;

	ps2priv->iop_dma_addr = iop_alloc(size);
	if (!ps2priv->iop_dma_addr) {
		dev_err(dev, "iop_alloc failed\n");
		return -ENOMEM;
	}

	err = usb_hcd_setup_local_mem(hcd,
		iop_bus_to_phys(ps2priv->iop_dma_addr),
		ps2priv->iop_dma_addr, size);
	if (err) {
		dev_err(dev, "usb_hcd_setup_local_mem failed with %d\n", err);
		iop_free(ps2priv->iop_dma_addr);
		ps2priv->iop_dma_addr = 0;
		return err;
	}

	return 0;
}

static void iopheap_free_dma_buffer(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	if (!ps2priv->iop_dma_addr)
		return;

	iop_free(ps2priv->iop_dma_addr);
	ps2priv->iop_dma_addr = 0;
}

static int ohci_hcd_ps2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *regs;
	struct usb_hcd *hcd;
	struct ps2_hcd *ps2priv;
	int irq;
	int err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "platform_get_irq failed\n");
		return irq;
	}

	hcd = usb_create_hcd(&ohci_ps2_hc_driver, dev, dev_name(dev));
	if (!hcd) {
		dev_err(dev, "usb_create_hcd failed\n");
		return -ENOMEM;
	}

	ps2priv = hcd_to_priv(hcd);
	memset(ps2priv, 0, sizeof(*ps2priv));

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(dev, "platform_get_resource 0 failed\n");
		err = -ENOENT;
		goto err_platform;
	}

	hcd->rsrc_start = regs->start;
	hcd->rsrc_len = resource_size(regs);
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		dev_err(dev, "ioremap failed with %d\n", err);
		goto err_ioremap;
	}

	err = iopheap_alloc_dma_buffer(pdev, DMA_BUFFER_SIZE);
	if (err)
		goto err_alloc_dma_buffer;

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err) {
		dev_err(dev, "usb_add_hcd failed with %d\n", err);
		goto err_add_hcd;
	}

	err = device_wakeup_enable(hcd->self.controller);
	if (err) {
		dev_err(dev, "device_wakeup_enable failed with %d\n", err);
		goto err_wakeup;
	}

	return 0;

err_wakeup:
	usb_remove_hcd(hcd);
err_add_hcd:
	iopheap_free_dma_buffer(pdev);
err_alloc_dma_buffer:
	iounmap(hcd->regs);
err_ioremap:
err_platform:
	usb_put_hcd(hcd);
	return err;
}

static int ohci_hcd_ps2_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);

	ohci_ps2_disable(hcd);
	ohci_ps2_stop_hc(hcd);

	iopheap_free_dma_buffer(pdev);
	iounmap(hcd->regs);

	usb_put_hcd(hcd);

	return 0;
}

static struct platform_driver ohci_hcd_ps2_driver = {
	.probe		= ohci_hcd_ps2_probe,
	.remove		= ohci_hcd_ps2_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= DRV_NAME,
	},
};

static const struct ohci_driver_overrides ps2_overrides __initconst = {
	.reset		= ohci_ps2_reset,
	.product_desc	= DRIVER_DESC,
	.extra_priv_size = sizeof(struct ps2_hcd),
};

static int __init ohci_ps2_init(void)
{
	if (usb_disabled()) {
		pr_err(DRV_NAME ": Initialization failed: USB is disabled\n");
		return -ENODEV;
	}

	ohci_init_driver(&ohci_ps2_hc_driver, &ps2_overrides);

	ohci_irq = ohci_ps2_hc_driver.irq;	/* Save normal IRQ handler. */
	ohci_ps2_hc_driver.irq = ohci_ps2_irq;	/* Install IRQ workaround. */

	return platform_driver_register(&ohci_hcd_ps2_driver);
}
module_init(ohci_ps2_init);

static void __exit ohci_ps2_exit(void)
{
	platform_driver_unregister(&ohci_hcd_ps2_driver);
}
module_exit(ohci_ps2_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Fredrik Noring");
MODULE_AUTHOR("Jürgen Urban");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
