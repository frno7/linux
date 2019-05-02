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

void gif_reset(void)
{
	gif_write_ctrl((struct gif_ctrl) { .rst = 1 });

	udelay(100);		/* 100 us */
}
EXPORT_SYMBOL_GPL(gif_reset);

bool gif_busy(void)
{
	return (inl(DMAC_GIF_CHCR) & DMAC_CHCR_BUSY) != 0;
}
EXPORT_SYMBOL_GPL(gif_busy);

bool gif_wait(void)
{
	size_t countout = 1000000;

	while (gif_busy() && countout > 0)
		countout--;

	return countout > 0;
}
EXPORT_SYMBOL_GPL(gif_wait);

void gif_write(union gif_data *base_package, size_t package_count)
{
	const size_t size = package_count * sizeof(*base_package);
	const dma_addr_t madr = virt_to_phys(base_package);

	if (!package_count)
		return;

	dma_cache_wback((unsigned long)base_package, size);

	/* Wait for previous transmissions to finish. */
	while (gif_busy())
		;

	outl(madr, DMAC_GIF_MADR);
	outl(package_count, DMAC_GIF_QWC);
	outl(DMAC_CHCR_SENDN, DMAC_GIF_CHCR);
}
EXPORT_SYMBOL_GPL(gif_write);

static int __init gif_init(void)
{
	return 0;
}

static void __exit gif_exit(void)
{
}

module_init(gif_init);
module_exit(gif_exit);

MODULE_DESCRIPTION("PlayStation 2 Graphics Synthesizer interface (GIF)");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
