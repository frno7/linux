/*
 * USB OHCI HCD (Host Controller Driver) for the PlayStation 2.
 *
 * Copyright (C) 2017 Jürgen Urban
 * Copyright (C) 2017 Fredrik Noring
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include <asm/mach-ps2/sifdefs.h>

#include "ohci.h"

/* Enable USB OHCI hardware. */
#define DPCR2_ENA_USB 0x08000000

/* Enable PS2DEV (required for PATA and USB). */
#define DPCR2_ENA_PS2DEV 0x00000080

#define DRIVER_DESC "OHCI PS2 driver"
#define DRV_NAME "ohci-ps2"

/* Size allocated from IOP heap (maximum size of DMA memory). */
#define DMA_BUFFER_SIZE (256 * 1024)

/* Get driver private data. */
#define hcd_to_priv(hcd) (struct ps2_hcd *)(hcd_to_ohci(hcd)->priv)

struct ps2_hcd {
	void __iomem *dpcr2;	/* FIXME: Is a lock required for DPCR2? */
	dma_addr_t iop_dma_addr;
	bool wakeup;			/* Saved wake-up state for resume. */
};

static struct hc_driver __read_mostly ohci_ps2_hc_driver;

static void ohci_ps2_enable(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	BUG_ON(!ohci->regs);

	/* This is needed to get USB working on PS2. */
	ohci_writel(ohci, 1, &ohci->regs->roothub.portstatus[11]);
}

static void ohci_ps2_disable(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);

	WARN_ON(!ohci->regs);

	if (ohci->regs)
		ohci_writel(ohci, 0, &ohci->regs->roothub.portstatus[11]);
}

static void ohci_ps2_start_hc(struct usb_hcd *hcd)
{
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	/*
	 * Enable USB and PS2DEV.
	 *
	 * FIXME: What is the purpose of PS2DEV for USB?
	 * FIXME: As far as I remember the following call enables the clock,
	 * so that ohci->regs->fminterval can count.
	 */
	writel(readl(ps2priv->dpcr2) | DPCR2_ENA_USB | DPCR2_ENA_PS2DEV,
		ps2priv->dpcr2);
}

static void ohci_ps2_stop_hc(struct usb_hcd *hcd)
{
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	/* Disable USB. Leave PS2DEV enabled (could be still needed for PATA). */
	writel(readl(ps2priv->dpcr2) & ~DPCR2_ENA_USB, ps2priv->dpcr2);
}

static int ohci_ps2_reset(struct usb_hcd *hcd)
{
	int ret;

	ohci_ps2_start_hc(hcd);

	ret = ohci_setup(hcd);

	if (ret < 0) {
		ohci_ps2_stop_hc(hcd);
		return ret;
	}

	ohci_ps2_enable(hcd);

	return ret;
}

static int iopheap_alloc_coherent(struct platform_device *pdev, size_t size,
	int flags)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	if (ps2priv->iop_dma_addr != 0)
		return 0;

	ps2priv->iop_dma_addr = ps2sif_allociopheap(size);
	if (ps2priv->iop_dma_addr == 0) {
		dev_err(dev, "ps2sif_allociopheap failed\n");
		return -ENOMEM;
	}

	if (dma_declare_coherent_memory(dev,
			ps2sif_bustophys(ps2priv->iop_dma_addr),
			ps2priv->iop_dma_addr, size, flags)) {
		dev_err(dev, "dma_declare_coherent_memory failed\n");
		ps2sif_freeiopheap(ps2priv->iop_dma_addr);
		ps2priv->iop_dma_addr = 0;
		return -ENOMEM;
	}

	return 0;
}

static void iopheap_free_coherent(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	if (ps2priv->iop_dma_addr == 0)
		return;

	dma_release_declared_memory(dev);
	ps2sif_freeiopheap(ps2priv->iop_dma_addr);
	ps2priv->iop_dma_addr = 0;
}

static int ohci_hcd_ps2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *regs;
	struct resource *dpcr2;
	struct usb_hcd *hcd;
	struct ps2_hcd *ps2priv;
	int irq;
	int ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "platform_get_irq failed\n");
		return irq;
	}

	/* FIXME: Is request_mem_region recommended here? */

	hcd = usb_create_hcd(&ohci_ps2_hc_driver, dev, dev_name(dev));
	if (hcd == NULL)
		return -ENOMEM;

	ps2priv = hcd_to_priv(hcd);
	memset(ps2priv, 0, sizeof(*ps2priv));

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (regs == NULL) {
		dev_err(dev, "platform_get_resource 0 failed\n");
		ret = -ENOENT;
		goto err;
	}
	dpcr2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (dpcr2 == NULL) {
		dev_err(dev, "platform_get_resource 1 failed\n");
		ret = -ENOENT;
		goto err;
	}

	hcd->rsrc_start = regs->start;
	hcd->rsrc_len = resource_size(regs);
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto err;
	}

	ps2priv->dpcr2 = ioremap(dpcr2->start, resource_size(dpcr2));
	if (ps2priv->dpcr2 == NULL) {
		ret = -ENOMEM;
		goto err_ioremap_dpcr2;
	}

	ret = iopheap_alloc_coherent(pdev, DMA_BUFFER_SIZE, DMA_MEMORY_EXCLUSIVE);
	if (ret != 0)
		goto err_alloc_coherent;

	ret = usb_add_hcd(hcd, irq, 0);
	if (ret != 0)
		goto err_add_hcd;

	ret = device_wakeup_enable(hcd->self.controller);
	if (ret == 0)
		return ret;

	usb_remove_hcd(hcd);
err_add_hcd:
	iopheap_free_coherent(pdev);
err_alloc_coherent:
	iounmap(ps2priv->dpcr2);
err_ioremap_dpcr2:
	iounmap(hcd->regs);
err:
	usb_put_hcd(hcd);

	return ret;
}

static int ohci_hcd_ps2_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	usb_remove_hcd(hcd);

	ohci_ps2_disable(hcd);
	ohci_ps2_stop_hc(hcd);

	iopheap_free_coherent(pdev);
	iounmap(ps2priv->dpcr2);
	iounmap(hcd->regs);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ohci_hcd_ps2_suspend(struct platform_device *pdev,
				pm_message_t message)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);
	int ret;

	ps2priv->wakeup = device_may_wakeup(dev);
	if (ps2priv->wakeup)
		enable_irq_wake(hcd->irq);

	ret = ohci_suspend(hcd, ps2priv->wakeup);
	if (ret)
		return ret;

	ohci_ps2_disable(hcd);
	ohci_ps2_stop_hc(hcd);

	return ret;
}

static int ohci_hcd_ps2_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	if (ps2priv->wakeup)
		disable_irq_wake(hcd->irq);

	ohci_ps2_start_hc(hcd);
	ohci_ps2_enable(hcd);

	ohci_resume(hcd, ps2priv->wakeup);

	return 0;
}
#endif

static struct platform_driver ohci_hcd_ps2_driver = {
	.probe		= ohci_hcd_ps2_probe,
	.remove		= ohci_hcd_ps2_remove,
	.shutdown	= usb_hcd_platform_shutdown,
#ifdef	CONFIG_PM
	.suspend	= ohci_hcd_ps2_suspend,
	.resume		= ohci_hcd_ps2_resume,
#endif
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
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", DRV_NAME);

	ohci_init_driver(&ohci_ps2_hc_driver, &ps2_overrides);
	ohci_ps2_hc_driver.flags |= HCD_LOCAL_MEM;

	return platform_driver_register(&ohci_hcd_ps2_driver);
}
module_init(ohci_ps2_init);

static void __exit ohci_ps2_cleanup(void)
{
	platform_driver_unregister(&ohci_hcd_ps2_driver);
}
module_exit(ohci_ps2_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
