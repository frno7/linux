// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer interface (GIF)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs.h>

#include <uapi/asm/gif.h>
#include <uapi/asm/gs.h>

static void *gif_buffer;	/* FIXME: Device private data */

#define GIF_CTRL	0x10003000	/* GIF control (w) */
#define GIF_MODE	0x10003010	/* GIF mode (w) */
#define GIF_STAT	0x10003020	/* GIF status (r) */

#define GIF_TAG0	0x10003040	/* GIF tag 31:0 (r) */
#define GIF_TAG1	0x10003050	/* GIF tag 63:32 (r) */
#define GIF_TAG2	0x10003060	/* GIF tag 95:54 (r) */
#define GIF_TAG3	0x10003070	/* GIF tag 127:96 (r) */
#define GIF_CNT		0x10003080	/* GIF transfer status counter (r) */
#define GIF_P3CNT	0x10003090	/* PATH3 transfer status counter (r) */
#define GIF_P3TAG	0x100030a0	/* PATH3 GIF tag value (r) */

void gif_writel_ctrl(u32 value)
{
	outl(value, GIF_CTRL);
}
EXPORT_SYMBOL_GPL(gif_writel_ctrl);

void gif_write_ctrl(struct gif_ctrl value)
{
	u32 v;
	memcpy(&v, &value, sizeof(v));
	gif_writel_ctrl(v);
}
EXPORT_SYMBOL_GPL(gif_write_ctrl);

void gif_writel_mode(u32 value)
{
	outl(value, GIF_MODE);
}
EXPORT_SYMBOL_GPL(gif_writel_mode);

u32 gif_readl_stat(void)
{
	return inl(GIF_STAT);
}
EXPORT_SYMBOL_GPL(gif_readl_stat);

void gif_reset(void)
{
	gif_write_ctrl((struct gif_ctrl) { .rst = 1 });

	udelay(100);		/* 100 us */
}
EXPORT_SYMBOL_GPL(gif_reset);

void gif_stop(void)
{
	gif_write_ctrl((struct gif_ctrl) { .pse = 1 });
}
EXPORT_SYMBOL_GPL(gif_stop);

void gif_resume(void)
{
	gif_write_ctrl((struct gif_ctrl) { .pse = 0 });
}
EXPORT_SYMBOL_GPL(gif_resume);

bool gif_ready(void)
{
	size_t countout = 1000000;

	while ((inl(DMAC_GIF_CHCR) & 0x100) != 0 && countout > 0)
		countout--;

	return countout > 0;
}
EXPORT_SYMBOL_GPL(gif_ready);

/*
 * FIXME:
 *
 * Reading from the GIF requires the following steps:
 *
 * 1. Set transmission parameters.
 * 2. Access the FINISH register (any data can be written to it; a FINISH
 *    event occurs when data is input to the GS).
 * 3. Wait for the FINISH field of the CSR register becomes 1.
 * 4. Clear the FINISH field of the CSR register.
 * 5. Set the BUSDIR register to 1, which reverses transmission direction.
 * 6. Read data from the GIF.
 * 7. Set the BUSDIR register to 0, to restore normal transmission direction.
 *
 * The Host FIFO requires that the total data size is a multiple of 128 bytes
 * for DMA transmissions and 16 bytes for IO transmissions.
 */

void gif_write(union gif_data *base_package, size_t package_count)
{
	if (package_count > 0) {
		const size_t size = package_count * sizeof(*base_package);
		const dma_addr_t madr = virt_to_phys(base_package);

		dma_cache_wback((unsigned long)base_package, size);

		/* Wait for previous transmissions to finish. */
		while ((inl(DMAC_GIF_CHCR) & 0x100) != 0)
			;

		outl(madr, DMAC_GIF_MADR);
		outl(package_count, DMAC_GIF_QWC);
		outl(DMAC_CHCR_SENDN, DMAC_GIF_CHCR);
	}
}
EXPORT_SYMBOL_GPL(gif_write);

static int __init gif_init(void)
{
	gif_buffer = (void *)__get_free_page(GFP_DMA);
	if (gif_buffer == NULL)
		return -ENOMEM;

	return 0;
}

static void __exit gif_exit(void)
{
	free_page((unsigned long)gif_buffer);
}

module_init(gif_init);
module_exit(gif_exit);

MODULE_DESCRIPTION("PlayStation 2 Graphics Synthesizer interface driver");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
