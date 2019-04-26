// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer (GS)
 *
 * Copyright (C) 2019 Fredrik Noring
 *
 * The Graphics Synthesizer draws primitives such as triangles and sprites
 * to its 4 MiB local frame buffer. It can handle shading, texture mapping,
 * z-buffering, alpha blending, edge antialiasing, fogging, scissoring, etc.
 *
 * PAL, NTSC and VESA video modes are supported. Interlace and noninterlaced
 * can be switched. The resolution is variable from 256 x 224 to 1920 x 1080.
 */

#ifndef __ASM_PS2_GS_H
#define __ASM_PS2_GS_H

#include <asm/types.h>

#include <asm/mach-ps2/gs-registers.h>

u32 gs_video_clock(const u32 t1248, const u32 lc, const u32 rc);
u32 gs_video_clock_for_smode1(const struct gs_smode1 smode1);

/* Returns number blocks to represent the given frame buffer resolution. */
u32 gs_psm_ct16_block_count(const u32 fbw, const u32 fbh);
u32 gs_psm_ct32_block_count(const u32 fbw, const u32 fbh);

u32 gs_psm_ct16_blocks_available(const u32 fbw, const u32 fbh);
u32 gs_psm_ct32_blocks_available(const u32 fbw, const u32 fbh);

/* Returns block address given a block index starting at the top left corner. */
u32 gs_psm_ct32_block_address(const u32 fbw, const u32 block_index);
u32 gs_psm_ct16_block_address(const u32 fbw, const u32 block_index);

/* Frame buffer coordinate system to primitive coordinate system. */
static inline int gs_fbcs_to_pcs(const int c)
{
	return c * 16;	/* The 4 least significant bits are fractional. */
}

/* Pixel coordinate system to texel coordinate system. */
static inline int gs_pxcs_to_tcs(const int c)
{
	return c * 16 + 8;  /* The 4 least significant bits are fractional. */
}

struct device *gs_device(void);	/* FIXME: Is this method appropriate? */

#endif /* __ASM_PS2_GS_H */
