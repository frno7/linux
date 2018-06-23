// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 USB 1.1 OHCI HCD (Host Controller Driver)
 *
 * Copyright (C) 2017 Jürgen Urban
 * Copyright (C) 2018 Fredrik Noring
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/iop-registers.h>

#include "ohci.h"

#define DRIVER_DESC "OHCI PS2 driver"
#define DRV_NAME "ohci-ps2"

/* Size allocated from IOP heap (maximum size of DMA memory). */
#define DMA_BUFFER_SIZE (256 * 1024)

/* Get driver private data. */
#define hcd_to_priv(hcd) (struct ps2_hcd *)(hcd_to_ohci(hcd)->priv)

struct ps2_hcd {
	u64 dma_mask;
	dma_addr_t iop_dma_addr;
	bool wakeup;			/* Saved wake-up state for resume. */
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
}

static void ohci_ps2_stop_hc(struct usb_hcd *hcd)
{
	iop_clr_dma_dpcr2(IOP_DMA_DPCR2_OHCI);
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

static irqreturn_t ohci_ps2_irq(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	struct ohci_regs __iomem *regs = ohci->regs;

	/*
	 * FIXME: For some reason OHCI_INTR_MIE is required in the
	 * IRQ handler. Without it, reading a large amount of data
	 * (> 1 GB) from a mass storage device results in a freeze.
	 */
	ohci_writel(ohci, OHCI_INTR_MIE, &regs->intrdisable);

	return ohci_irq(hcd); /* Call normal IRQ handler. */
}

static int iopheap_alloc_coherent(struct platform_device *pdev,
	size_t size, int flags)
{
	struct device *dev = &pdev->dev;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ps2_hcd *ps2priv = hcd_to_priv(hcd);

	if (ps2priv->iop_dma_addr != 0)
		return 0;

	ps2priv->dma_mask = DMA_BIT_MASK(21);
	dev->dma_mask = &ps2priv->dma_mask;
	dev->coherent_dma_mask = DMA_BIT_MASK(21);

	ps2priv->iop_dma_addr = iop_alloc(size);
	if (ps2priv->iop_dma_addr == 0) {
		dev_err(dev, "iop_alloc failed\n");
		return -ENOMEM;
	}

	if (dma_declare_coherent_memory(dev,
			iop_bus_to_phys(ps2priv->iop_dma_addr),
			ps2priv->iop_dma_addr, size, flags)) {
		dev_err(dev, "dma_declare_coherent_memory failed\n");
		iop_free(ps2priv->iop_dma_addr);
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

	hcd->rsrc_start = regs->start;
	hcd->rsrc_len = resource_size(regs);
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto err;
	}

	ret = iopheap_alloc_coherent(pdev,
		DMA_BUFFER_SIZE, DMA_MEMORY_EXCLUSIVE);
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
	iounmap(hcd->regs);
err:
	usb_put_hcd(hcd);

	return ret;
}

static int ohci_hcd_ps2_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);

	ohci_ps2_disable(hcd);
	ohci_ps2_stop_hc(hcd);

	iopheap_free_coherent(pdev);
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

	/*
	 * FIXME: For some reason
	 *
	 *   ohci_writel(ohci, OHCI_INTR_MIE, &regs->intrdisable);
	 *
	 * is required in the IRQ handler. Without it, reading a large
	 * amount of data (> 1 GB) from a mass storage device results in
	 * a freeze.
	 */
	ohci_irq = ohci_ps2_hc_driver.irq; /* Save normal IRQ handler. */
	ohci_ps2_hc_driver.irq = ohci_ps2_irq; /* Install IRQ workaround. */

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
