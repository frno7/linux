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

/**
 * gs_video_clock - video clock (VCK) frequency given SMODE1 bit fields
 * @t1248 - &gs_smode1.t1248 PLL output divider
 * @lc - &gs_smode1.lc PLL loop divider
 * @rc - &gs_smode1.rc PLL reference divider
 *
 * Return: video clock (VCK)
 */
u32 gs_video_clock(const u32 t1248, const u32 lc, const u32 rc)
{
	return (13500000 * lc) / ((t1248 + 1) * rc);
}
EXPORT_SYMBOL_GPL(gs_video_clock);

/**
 * gs_video_clock_for_smode1 - video clock (VCK) frequency given SMODE1
 * 	register value
 * @smode1: SMODE1 register value
 *
 * Return: video clock (VCK)
 */
u32 gs_video_clock_for_smode1(const struct gs_smode1 smode1)
{
	return gs_video_clock(smode1.t1248, smode1.lc, smode1.rc);
}
EXPORT_SYMBOL_GPL(gs_video_clock_for_smode1);

/**
 * gs_psm_ct16_block_count - number of blocks for 16-bit pixel storage
 * @fbw: buffer width/64
 * @fbh: buffer height
 *
 * Return: number of blocks for 16-bit pixel storage of given width and height
 */
u32 gs_psm_ct16_block_count(const u32 fbw, const u32 fbh)
{
	const u32 block_cols = fbw * GS_PSM_CT16_PAGE_COLS;
	const u32 block_rows = (fbh + GS_PSM_CT16_BLOCK_HEIGHT - 1) /
		GS_PSM_CT16_BLOCK_HEIGHT;

	return block_cols * block_rows;
}
EXPORT_SYMBOL_GPL(gs_psm_ct16_block_count);

/**
 * gs_psm_ct32_block_count - number of blocks for 32-bit pixel storage
 * @fbw: buffer width/64
 * @fbh: buffer height
 *
 * Return: number of blocks for 32-bit pixel storage of given width and height
 */
u32 gs_psm_ct32_block_count(const u32 fbw, const u32 fbh)
{
	const u32 block_cols = fbw * GS_PSM_CT32_PAGE_COLS;
	const u32 block_rows = (fbh + GS_PSM_CT32_BLOCK_HEIGHT - 1) /
		GS_PSM_CT32_BLOCK_HEIGHT;

	return block_cols * block_rows;
}
EXPORT_SYMBOL_GPL(gs_psm_ct32_block_count);

/**
 * gs_psm_ct16_block_address - 16-bit block address given a block index
 * @fbw: buffer width/64
 * @block_index: block index starting at the top left corner
 *
 * Return: block address for a given block index
 */
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

/**
 * gs_psm_ct32_block_address - 32-bit block address given a block index
 * @fbw: buffer width/64
 * @block_index: block index starting at the top left corner
 *
 * Return: block address for a given block index
 */
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

static u32 div_round_ps(u32 a, u32 b)
{
	return DIV_ROUND_CLOSEST_ULL(a * 1000000000000ll, b);
}

static u32 vck_to_pix_clock(const u32 vck, const u32 spml)
{
	return div_round_ps(spml, vck);
}

/**
 * gs_synch_gen_for_vck - determine video synchronization register fields for
 * 	a given video clock
 * @vck: video clock to compute video synchronization register fields for
 *
 * Some combinations of registers appear to generate equivalent video clock
 * frequencies. For the standard ones, the combinations used by Sony are
 * preferred and tabulated. For all others, a search is performed.
 *
 * Return: video synchronization register fields RC, LC, T1248 and SPML
 **/
struct gs_synch_gen gs_synch_gen_for_vck(const u32 vck)
{
	static const struct gs_synch_gen preferred[] = {
		{ .spml = 2, .t1248 = 1, .lc = 15, .rc = 2 }, /*  50.625 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 32, .rc = 4 }, /*  54.000 MHz */
		{ .spml = 4, .t1248 = 1, .lc = 32, .rc = 4 }, /*  54.000 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 28, .rc = 3 }, /*  63.000 MHz */
		{ .spml = 1, .t1248 = 1, .lc = 22, .rc = 2 }, /*  74.250 MHz */
		{ .spml = 1, .t1248 = 1, .lc = 35, .rc = 3 }, /*  78.750 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 71, .rc = 6 }, /*  79.875 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 44, .rc = 3 }, /*  99.000 MHz */
		{ .spml = 1, .t1248 = 0, .lc =  8, .rc = 1 }, /* 108.000 MHz */
		{ .spml = 2, .t1248 = 0, .lc = 58, .rc = 6 }, /* 130.500 MHz */
		{ .spml = 1, .t1248 = 0, .lc = 10, .rc = 1 }, /* 135.000 MHz */
		{ .spml = 1, .t1248 = 1, .lc = 22, .rc = 1 }  /* 148.500 MHz */
	};

	struct gs_synch_gen sg = { };
	u32 spml, t1248, lc, rc;
	int best = -1;
	int diff, i;

	for (i = 0; i < ARRAY_SIZE(preferred); i++) {
		spml  = preferred[i].spml;
		t1248 = preferred[i].t1248;
		lc    = preferred[i].lc;
		rc    = preferred[i].rc;

		diff = abs(vck - vck_to_pix_clock(
					gs_video_clock(t1248, lc, rc), spml));

		if (best == -1 || diff < best) {
			best = diff;
			sg = (struct gs_synch_gen) {
				.rc = rc,
				.lc = lc,
				.t1248 = t1248,
				.spml = spml
			};
		}
	}

	for (spml  = 1; spml  <   5; spml++)
	for (t1248 = 0; t1248 <   2; t1248++)
	for (lc    = 1; lc    < 128; lc++)
	for (rc    = 1; rc    <   7; rc++) {
		diff = abs(vck - vck_to_pix_clock(
					gs_video_clock(t1248, lc, rc), spml));

		if (best == -1 || diff < best) {
			best = diff;
			sg = (struct gs_synch_gen) {
				.rc = rc,
				.lc = lc,
				.t1248 = t1248,
				.spml = spml
			};
		}
	}

	return sg;
}
EXPORT_SYMBOL_GPL(gs_synch_gen_for_vck);

/**
 * gs_rfsh_from_synch_gen - DRAM refresh for the given synchronization registers
 *
 * Return: DRAM refresh register value
 */
u32 gs_rfsh_from_synch_gen(const struct gs_synch_gen sg)
{
	const u32 pck = gs_video_clock(sg.t1248, sg.lc, sg.rc) / sg.spml;

	return pck < 20000000 ? 8 :
	       pck < 70000000 ? 4 : 2;
}
EXPORT_SYMBOL_GPL(gs_rfsh_from_synch_gen);

struct device *gs_device_driver(void)
{
	return gs_dev;
}
EXPORT_SYMBOL_GPL(gs_device_driver);

static int gs_probe(struct platform_device *pdev)
{
	gs_dev = &pdev->dev;

	gs_irq_init();

	gif_reset();

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
