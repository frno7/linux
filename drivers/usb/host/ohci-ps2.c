/*
 * USB OHCI HCD (Host Controller Driver) for Playstation 2.
 *
 * Copyright (C) 2010 Mega Man
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/platform_device.h>

#include <asm/mach-ps2/sifdefs.h>

/* Size of buffer allocated from IOP heap. */
#define DMA_BUFFER_SIZE (256 * 1024)

#define ERROR(args...) printk(KERN_ERR "ohci-ps: " args);

static dma_addr_t iop_dma_addr = 0;

static int ohci_ps2_start(struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);

	ohci_hcd_init(ohci);
	ohci_init(ohci);
	ohci_run(ohci);
	hcd->state = HC_STATE_RUNNING;
	return 0;
}

static const struct hc_driver ohci_ps2_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"PS2 OHCI",
	.hcd_priv_size =	sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY | HCD_LOCAL_MEM | HCD_LOCAL_DMA,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_ps2_start,
	.stop =			ohci_stop,
	.shutdown =		ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend =		ohci_bus_suspend,
	.bus_resume =		ohci_bus_resume,
#endif
	.start_port_reset =	ohci_start_port_reset,
};

/*-------------------------------------------------------------------------*/

static int
iopheap_alloc_coherent(struct device *dev, size_t size, int flags)
{
	if (iop_dma_addr != 0)
		return 0;

	iop_dma_addr = ps2sif_allociopheap(size);
	if (iop_dma_addr != 0) {
		if (!dma_declare_coherent_memory(dev, ps2sif_bustophys(iop_dma_addr),
						 iop_dma_addr,
						 size,
						 flags)) {
			ERROR("cannot declare coherent memory\n");
			ps2sif_freeiopheap(iop_dma_addr);
			return -ENXIO;
		}
		return 0;
	} else {
		ERROR("Out of IOP heap memory.\n");
		return -ENOMEM;
	}
}

static void
iopheap_free_coherent(struct device *dev)
{
	if (iop_dma_addr == 0)
		return;

	dma_release_declared_memory(dev);
	ps2sif_freeiopheap(iop_dma_addr);
	iop_dma_addr = 0;
}

#define resource_len(r) (((r)->end - (r)->start) + 1)
static int ohci_hcd_ps2_probe(struct platform_device *pdev)
{
	struct resource *res = NULL;
	struct usb_hcd *hcd = NULL;
	int irq = -1;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ERROR("platform_get_resource error.");
		return -ENODEV;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ERROR("platform_get_irq error.");
		return -ENODEV;
	}
	ret = iopheap_alloc_coherent(&pdev->dev, DMA_BUFFER_SIZE, DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE);
	if (ret) {
		return ret;
	}

	/* initialize hcd */
	hcd = usb_create_hcd(&ohci_ps2_hc_driver, &pdev->dev, (char *)hcd_name);
	if (!hcd) {
		ERROR("Failed to create hcd");
		return -ENOMEM;
	}

	hcd->regs = (void __iomem *)res->start;
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_len(res);
	ret = usb_add_hcd(hcd, irq, 0);
	if (ret != 0) {
		ERROR("Failed to add hcd");
		usb_put_hcd(hcd);
		return ret;
	}

	return ret;
}

static int ohci_hcd_ps2_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	iopheap_free_coherent(&pdev->dev);

	return 0;
}

static struct platform_driver ohci_hcd_ps2_driver = {
	.probe		= ohci_hcd_ps2_probe,
	.remove		= ohci_hcd_ps2_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "ps2_ohci",
		.owner	= THIS_MODULE,
	},
};

MODULE_ALIAS("platform:ps2_ohci");
