// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer (GS)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

/**
 * DOC:
 *
 * The Graphics Synthesizer draws primitives such as triangles and sprites
 * to its 4 MiB local frame buffer. It can handle shading, texture mapping,
 * z-buffering, alpha blending, edge antialiasing, fogging, scissoring, etc.
 *
 * PAL, NTSC and VESA video modes are supported. Interlace and noninterlaced
 * can be switched. The resolution is variable from 256x224 to 1920x1080.
 */

#ifndef __ASM_PS2_GS_H
#define __ASM_PS2_GS_H

#include <asm/types.h>

#include <asm/mach-ps2/gs-registers.h>

#define GS_REG_BASE	0x12000000

/**
 * struct gs_synch_gen - Graphics Synthesizer SMODE1 register video clock fields
 * @rc: PLL reference divider
 * @lc: PLL loop divider
 * @t1248: PLL output divider
 * @spml: FIXME
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

/**
 * gs_region_pal - is the machine for a PAL video mode region?
 *
 * See also gs_region_ntsc(). The system region is determined by rom_version().
 *
 * Return: %true if PAL video mode is appropriate for the region, else %false
 */
bool gs_region_pal(void);

/**
 * gs_region_ntsc - is the machine for an NTSC video mode region?
 *
 * See also gs_region_pal(). The system region is determined by rom_version().
 *
 * Return: %true if NTSC video mode is appropriate for the region, else %false
 */
bool gs_region_ntsc(void);

/**
 * gs_video_clock - video clock (VCK) frequency given SMODE1 bit fields
 * @t1248 - &gs_smode1.t1248 PLL output divider
 * @lc - &gs_smode1.lc PLL loop divider
 * @rc - &gs_smode1.rc PLL reference divider
 *
 * Return: video clock (VCK)
 */
u32 gs_video_clock(const u32 t1248, const u32 lc, const u32 rc);

/**
 * gs_video_clock - video clock (VCK) frequency given SMODE1 register value
 * @smode1: SMODE1 register value
 *
 * Return: video clock (VCK)
 */
u32 gs_video_clock_for_smode1(const struct gs_smode1 smode1);

/**
 * gs_psm_ct16_block_count - number of blocks for 16-bit pixel storage
 * @fbw: buffer width/64
 * @fbh: buffer height
 *
 * Return: number of blocks for 16-bit pixel storage of given width and height
 */
u32 gs_psm_ct16_block_count(const u32 fbw, const u32 fbh);

/**
 * gs_psm_ct32_block_count - number of blocks for 32-bit pixel storage
 * @fbw: buffer width/64
 * @fbh: buffer height
 *
 * Return: number of blocks for 32-bit pixel storage of given width and height
 */
u32 gs_psm_ct32_block_count(const u32 fbw, const u32 fbh);

/* FIXME */
u32 gs_psm_ct16_blocks_available(const u32 fbw, const u32 fbh);

/* FIXME */
u32 gs_psm_ct32_blocks_available(const u32 fbw, const u32 fbh);

/**
 * gs_psm_ct32_block_address - 32-bit block address given a block index
 * @fbw: buffer width/64
 * @block_index: block index starting at the top left corner
 *
 * Return: block address for a given block index
 */
u32 gs_psm_ct32_block_address(const u32 fbw, const u32 block_index);

/**
 * gs_psm_ct16_block_address - 16-bit block address given a block index
 * @fbw: buffer width/64
 * @block_index: block index starting at the top left corner
 *
 * Return: block address for a given block index
 */
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

/* FIXME */
struct gs_synch_gen gs_synch_gen_for_vck(const u32 vck);

/* FIXME */
u32 gs_rfsh_from_synch_gen(const struct gs_synch_gen sg);

struct device *gs_device_driver(void);	/* FIXME: Is this method appropriate? */

#endif /* __ASM_PS2_GS_H */
