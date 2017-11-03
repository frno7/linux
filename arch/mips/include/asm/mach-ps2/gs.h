// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer (GS)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_GS_H
#define __ASM_MACH_PS2_GS_H

#include <asm/types.h>

#include <asm/mach-ps2/gs-registers.h>

#define GS_REG_BASE	0x12000000

/**
 * struct gs_synch_gen - Graphics Synthesizer SMODE1 register video clock fields
 * @rc: PLL reference divider
 * @lc: PLL loop divider
 * @t1248: PLL output divider
 * @spml: sub-pixel magnification level
 *
 * These fields determine the Graphics Synthesizer video clock
 *
 * 	VCK = (13500000 * @lc) / ((@t1248 + 1) * @spml * @rc).
 *
 * See also &struct gs_smode1.
 */
struct gs_synch_gen {
	u32 rc : 3;
	u32 lc : 7;
	u32 t1248 : 2;
	u32 spml : 4;
};

bool gs_region_pal(void);

bool gs_region_ntsc(void);

u32 gs_video_clock(const u32 t1248, const u32 lc, const u32 rc);

u32 gs_video_clock_for_smode1(const struct gs_smode1 smode1);

u32 gs_psm_ct16_block_count(const u32 fbw, const u32 fbh);

u32 gs_psm_ct32_block_count(const u32 fbw, const u32 fbh);

u32 gs_psm_ct16_blocks_available(const u32 fbw, const u32 fbh);

u32 gs_psm_ct32_blocks_available(const u32 fbw, const u32 fbh);

u32 gs_psm_ct32_block_address(const u32 fbw, const u32 block_index);

u32 gs_psm_ct16_block_address(const u32 fbw, const u32 block_index);

/**
 * gs_fbcs_to_pcs - frame buffer coordinate to primitive coordinate
 * @c: frame buffer coordinate
 *
 * Return: primitive coordinate
 */
static inline int gs_fbcs_to_pcs(const int c)
{
	return c * 16;	/* The 4 least significant bits are fractional. */
}

/**
 * gs_pxcs_to_tcs - pixel coordinate to texel coordinate
 * @c: pixel coordinate
 *
 * Return: texel coordinate
 */
static inline int gs_pxcs_to_tcs(const int c)
{
	return c * 16 + 8;  /* The 4 least significant bits are fractional. */
}

struct gs_synch_gen gs_synch_gen_for_vck(const u32 vck);

u32 gs_rfsh_from_synch_gen(const struct gs_synch_gen sg);

struct device *gs_device_driver(void);	/* FIXME: Is this method appropriate? */

#endif /* __ASM_MACH_PS2_GS_H */
