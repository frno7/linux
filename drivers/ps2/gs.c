// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer (GS)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/rom.h>

#include <uapi/asm/gs.h>

static struct device *gs_dev;

bool gs_region_pal(void)
{
	return rom_version().region == 'E';
}
EXPORT_SYMBOL_GPL(gs_region_pal);

bool gs_region_ntsc(void)
{
	return !gs_region_pal();
}
EXPORT_SYMBOL_GPL(gs_region_ntsc);

u32 gs_video_clock(const u32 t1248, const u32 lc, const u32 rc)
{
	return (13500000 * lc) / ((t1248 + 1) * rc);
}
EXPORT_SYMBOL_GPL(gs_video_clock);

u32 gs_video_clock_for_smode1(const struct gs_smode1 smode1)
{
	return gs_video_clock(smode1.t1248, smode1.lc, smode1.rc);
}
EXPORT_SYMBOL_GPL(gs_video_clock_for_smode1);

u32 gs_psm_ct16_block_count(const u32 fbw, const u32 fbh)
{
	const u32 block_cols = fbw * GS_PSM_CT16_PAGE_COLS;
	const u32 block_rows = (fbh + GS_PSM_CT16_BLOCK_HEIGHT - 1) /
		GS_PSM_CT16_BLOCK_HEIGHT;

	return block_cols * block_rows;
}
EXPORT_SYMBOL_GPL(gs_psm_ct16_block_count);

u32 gs_psm_ct32_block_count(const u32 fbw, const u32 fbh)
{
	const u32 block_cols = fbw * GS_PSM_CT32_PAGE_COLS;
	const u32 block_rows = (fbh + GS_PSM_CT32_BLOCK_HEIGHT - 1) /
		GS_PSM_CT32_BLOCK_HEIGHT;

	return block_cols * block_rows;
}
EXPORT_SYMBOL_GPL(gs_psm_ct32_block_count);

u32 gs_psm_ct16_blocks_available(const u32 fbw, const u32 fbh)
{
	const u32 block_count = gs_psm_ct16_block_count(fbw, fbh);

	return block_count <= GS_BLOCK_COUNT ?
		GS_BLOCK_COUNT - block_count : 0;
}
EXPORT_SYMBOL_GPL(gs_psm_ct16_blocks_available);

u32 gs_psm_ct32_blocks_available(const u32 fbw, const u32 fbh)
{
	const u32 block_count = gs_psm_ct32_block_count(fbw, fbh);

	return block_count <= GS_BLOCK_COUNT ?
		GS_BLOCK_COUNT - block_count : 0;
}
EXPORT_SYMBOL_GPL(gs_psm_ct32_blocks_available);

u32 gs_psm_ct16_block_address(const u32 fbw, const u32 block_index)
{
	static const u32 block[GS_PSM_CT16_PAGE_ROWS][GS_PSM_CT16_PAGE_COLS] = {
		{  0,  2,  8, 10 },
		{  1,  3,  9, 11 },
		{  4,  6, 12, 14 },
		{  5,  7, 13, 15 },
		{ 16, 18, 24, 26 },
		{ 17, 19, 25, 27 },
		{ 20, 22, 28, 30 },
		{ 21, 23, 29, 31 }
	};

	const u32 fw = GS_PSM_CT16_PAGE_COLS * fbw;
	const u32 fc = block_index % fw;
	const u32 fr = block_index / fw;
	const u32 bc = fc % GS_PSM_CT16_PAGE_COLS;
	const u32 br = fr % GS_PSM_CT16_PAGE_ROWS;
	const u32 pc = fc / GS_PSM_CT16_PAGE_COLS;
	const u32 pr = fr / GS_PSM_CT16_PAGE_ROWS;

	return GS_BLOCKS_PER_PAGE * (fbw * pr + pc) + block[br][bc];
}
EXPORT_SYMBOL_GPL(gs_psm_ct16_block_address);

u32 gs_psm_ct32_block_address(const u32 fbw, const u32 block_index)
{
	static const u32 block[GS_PSM_CT32_PAGE_ROWS][GS_PSM_CT32_PAGE_COLS] = {
		{  0,  1,  4,  5, 16, 17, 20, 21 },
		{  2,  3,  6,  7, 18, 19, 22, 23 },
		{  8,  9, 12, 13, 24, 25, 28, 29 },
		{ 10, 11, 14, 15, 26, 27, 30, 31 }
	};

	const u32 fw = GS_PSM_CT32_PAGE_COLS * fbw;
	const u32 fc = block_index % fw;
	const u32 fr = block_index / fw;
	const u32 bc = fc % GS_PSM_CT32_PAGE_COLS;
	const u32 br = fr % GS_PSM_CT32_PAGE_ROWS;
	const u32 pc = fc / GS_PSM_CT32_PAGE_COLS;
	const u32 pr = fr / GS_PSM_CT32_PAGE_ROWS;

	return GS_BLOCKS_PER_PAGE * (fbw * pr + pc) + block[br][bc];
}
EXPORT_SYMBOL_GPL(gs_psm_ct32_block_address);

struct device *gs_device_driver(void)
{
	return gs_dev;
}
EXPORT_SYMBOL_GPL(gs_device_driver);

static int gs_probe(struct platform_device *pdev)
{
	gs_dev = &pdev->dev;

	gs_irq_init();	/* FIXME Errors? */

	gif_reset();	/* FIXME Errors? */

#if 0
	gs_reset();
	setcrtc(1, 1);
#endif

	return 0;
}

static int gs_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver gs_driver = {
	.probe		= gs_probe,
	.remove		= gs_remove,
	.driver = {
		.name	= "gs",
	},
};

static int __init gs_init(void)
{
	return platform_driver_register(&gs_driver);
}

static void __exit gs_exit(void)
{
	platform_driver_unregister(&gs_driver);
}

module_init(gs_init);
module_exit(gs_exit);

MODULE_DESCRIPTION("PlayStation 2 Graphics Synthesizer device driver");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
