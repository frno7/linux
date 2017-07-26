// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#ifndef _UAPI_MIPS_ASM_GS_H
#define _UAPI_MIPS_ASM_GS_H

#include <asm/bitfield.h>

/*
 * The Graphics Synthesizer frame buffer is subdivided into rectangular pages,
 * from left to right, top to bottom. The pages are further subdivided into
 * blocks, with different arrangements for PSMCT16 and PSMCT32. Blocks are
 * further subdivided into columns, which are finally subdivided into pixels.
 *
 * The FBW (frame buffer width) is defined as the pixel width divided by 64,
 * where 64 is the width of a page for PSMCT16 and PSMCT32.
 *
 * The TBP (texture base pointer), CBP (color base pointer), SBP (source base
 * pointer) and DBP (destination base pointer) are all addressed by block.
 *
 * As an example, consider the frame buffer resolution 1920x1080@16 which uses
 * 1920*1080*2 = 4147200 bytes of the 4 MiB = 4194304 bytes available memory.
 * There are 4194304-4147200 = 47104 bytes of free memory. The FBW is 1920/64
 * = 30, which means it is 30 pages wide. However, 1080/GS_PSMCT32_PAGE_HEIGHT
 * = 1080/64 = 16.875 which means that the last row of blocks are unused in
 * the last page row. These make up 30*4 = 120 blocks of the 47104/256 = 184
 * blocks available. To make effective use of free memory for this resolution
 * in particular, it is essential to compute accurate block addresses.
 */

#define GS_COLUMNS_PER_BLOCK	  4
#define GS_BLOCKS_PER_PAGE	 32

#define GS_PAGE_COUNT		512
#define GS_BLOCK_COUNT	(GS_PAGE_COUNT * GS_BLOCKS_PER_PAGE)
#define GS_COLUMN_COUNT	(GS_BLOCK_COUNT * GS_COLUMNS_PER_PAGE)

#define GS_COLUMN_SIZE		 64
#define GS_BLOCK_SIZE	(GS_COLUMNS_PER_BLOCK * GS_COLUMN_SIZE)
#define GS_PAGE_SIZE	(GS_BLOCKS_PER_PAGE * GS_BLOCK_SIZE)
#define GS_MEMORY_SIZE	(GS_PAGE_COUNT * GS_PAGE_SIZE)	/* 4 MiB of memory */

#define GS_FB_PAGE_WIDTH	  64
#define GS_FB_BASE_DIVISOR	2048	/* FIXME: GS_FB_PAGE_WORDS */

/* All pixel storage formats have 1 block column and 4 block rows. */
#define GS_BLOCK_COLS			 1
#define GS_BLOCK_ROWS			 4

/* 4 bit (16 color) texture pixel storage format. */
#define GS_PSMT4_PAGE_COLS		 4
#define GS_PSMT4_PAGE_ROWS		 8
#define GS_PSMT4_COLUMN_WIDTH		32
#define GS_PSMT4_COLUMN_HEIGHT		 4
#define GS_PSMT4_BLOCK_WIDTH	 (GS_PSMT4_COLUMN_WIDTH * GS_BLOCK_COLS)
#define GS_PSMT4_BLOCK_HEIGHT	 (GS_PSMT4_COLUMN_HEIGHT * GS_BLOCK_ROWS)
#define GS_PSMT4_PAGE_WIDTH	 (GS_PSMT4_BLOCK_WIDTH * GS_PSMT4_PAGE_COLS)
#define GS_PSMT4_PAGE_HEIGHT	 (GS_PSMT4_BLOCK_HEIGHT * GS_PSMT4_PAGE_ROWS)

/* 8 bit (256 color) texture pixel storage format. */
#define GS_PSMT8_PAGE_COLS		 8
#define GS_PSMT8_PAGE_ROWS		 4
#define GS_PSMT8_COLUMN_WIDTH		16
#define GS_PSMT8_COLUMN_HEIGHT		 4
#define GS_PSMT8_BLOCK_WIDTH	 (GS_PSMT8_COLUMN_WIDTH * GS_BLOCK_COLS)
#define GS_PSMT8_BLOCK_HEIGHT	 (GS_PSMT8_COLUMN_HEIGHT * GS_BLOCK_ROWS)
#define GS_PSMT8_PAGE_WIDTH	 (GS_PSMT8_BLOCK_WIDTH * GS_PSMT8_PAGE_COLS)
#define GS_PSMT8_PAGE_HEIGHT	 (GS_PSMT8_BLOCK_HEIGHT * GS_PSMT8_PAGE_ROWS)

/* 16 bit (true color) frame buffer and texture pixel storage format. */
#define GS_PSMCT16_PAGE_COLS		 4
#define GS_PSMCT16_PAGE_ROWS		 8
#define GS_PSMCT16_COLUMN_WIDTH		 8
#define GS_PSMCT16_COLUMN_HEIGHT	 2
#define GS_PSMCT16_BLOCK_WIDTH	 (GS_PSMCT16_COLUMN_WIDTH * GS_BLOCK_COLS)
#define GS_PSMCT16_BLOCK_HEIGHT	 (GS_PSMCT16_COLUMN_HEIGHT * GS_BLOCK_ROWS)
#define GS_PSMCT16_PAGE_WIDTH	 (GS_PSMCT16_BLOCK_WIDTH * GS_PSMCT16_PAGE_COLS)
#define GS_PSMCT16_PAGE_HEIGHT	 (GS_PSMCT16_BLOCK_HEIGHT * GS_PSMCT16_PAGE_ROWS)

/* 32 bit (true color) frame buffer and texture pixel storage format. */
#define GS_PSMCT32_PAGE_COLS		 8
#define GS_PSMCT32_PAGE_ROWS		 4
#define GS_PSMCT32_COLUMN_WIDTH		16
#define GS_PSMCT32_COLUMN_HEIGHT	 2
#define GS_PSMCT32_BLOCK_WIDTH	 (GS_PSMCT32_COLUMN_WIDTH * GS_BLOCK_COLS)
#define GS_PSMCT32_BLOCK_HEIGHT	 (GS_PSMCT32_COLUMN_HEIGHT * GS_BLOCK_ROWS)
#define GS_PSMCT32_PAGE_WIDTH	 (GS_PSMCT32_BLOCK_WIDTH * GS_PSMCT32_PAGE_COLS)
#define GS_PSMCT32_PAGE_HEIGHT	 (GS_PSMCT32_BLOCK_HEIGHT * GS_PSMCT32_PAGE_ROWS)

#define GS_ALPHA_ONE 	0x80	/* Alpha 0x80 = 1.0 */

struct gs_rgba16 {
	__BITFIELD_FIELD(__u16 a : 1,	/* Alpha */
	__BITFIELD_FIELD(__u16 b : 5,	/* Blue */
	__BITFIELD_FIELD(__u16 g : 5,	/* Green */
	__BITFIELD_FIELD(__u16 r : 5,	/* Red */
	;))))
};

struct gs_rgba32 {
	__BITFIELD_FIELD(__u32 a : 8,	/* Alpha */
	__BITFIELD_FIELD(__u32 b : 8,	/* Blue */
	__BITFIELD_FIELD(__u32 g : 8,	/* Green */
	__BITFIELD_FIELD(__u32 r : 8,	/* Red */
	;))))
};

enum gs_register_address {
	gs_addr_prim       = 0x00, gs_addr_rgbaq      = 0x01,
	gs_addr_st         = 0x02, gs_addr_uv         = 0x03,
	gs_addr_xyzf2      = 0x04, gs_addr_xyz2       = 0x05,
	gs_addr_tex0_1     = 0x06, gs_addr_tex0_2     = 0x07,
	gs_addr_clamp_1    = 0x08, gs_addr_clamp_2    = 0x09,
	gs_addr_fog        = 0x0a, gs_addr_xyzf3      = 0x0c,
	gs_addr_xyz3       = 0x0d, gs_addr_tex1_1     = 0x14,
	gs_addr_tex1_2     = 0x15, gs_addr_tex2_1     = 0x16,
	gs_addr_tex2_2     = 0x17, gs_addr_xyoffset_1 = 0x18,
	gs_addr_xyoffset_2 = 0x19, gs_addr_prmodecont = 0x1a,
	gs_addr_prmode     = 0x1b, gs_addr_texclut    = 0x1c,
	gs_addr_scanmsk    = 0x22, gs_addr_miptbp1_1  = 0x34,
	gs_addr_miptbp1_2  = 0x35, gs_addr_miptbp2_1  = 0x36,
	gs_addr_miptbp2_2  = 0x37, gs_addr_texa       = 0x3b,
	gs_addr_fogcol     = 0x3d, gs_addr_texflush   = 0x3f,
	gs_addr_scissor_1  = 0x40, gs_addr_scissor_2  = 0x41,
	gs_addr_alpha_1    = 0x42, gs_addr_alpha_2    = 0x43,
	gs_addr_dimx       = 0x44, gs_addr_dthe       = 0x45,
	gs_addr_colclamp   = 0x46, gs_addr_test_1     = 0x47,
	gs_addr_test_2     = 0x48, gs_addr_pabe       = 0x49,
	gs_addr_fba_1      = 0x4a, gs_addr_fba_2      = 0x4b,
	gs_addr_frame_1    = 0x4c, gs_addr_frame_2    = 0x4d,
	gs_addr_zbuf_1     = 0x4e, gs_addr_zbuf_2     = 0x4f,
	gs_addr_bitbltbuf  = 0x50, gs_addr_trxpos     = 0x51,
	gs_addr_trxreg     = 0x52, gs_addr_trxdir     = 0x53,
	gs_addr_hwreg      = 0x54, gs_addr_signal     = 0x60,
	gs_addr_finish     = 0x61, gs_addr_label      = 0x62,
	gs_addr_nop        = 0x7f
};

enum gs_prim_fix { gs_fragment_unfixed, gs_fragment_fixed };
enum gs_prim_ctxt { gs_context_1, gs_context_2 };
enum gs_prim_fst { gs_texturing_stq, gs_texturing_uv };
enum gs_prim_aa1 { gs_pass_antialiasing_off, gs_pass_antialiasing_on };
enum gs_prim_abe { gs_blendning_off, gs_blendning_on };
enum gs_prim_fge { gs_fogging_off, gs_fogging_on };
enum gs_prim_tme { gs_texturing_off, gs_texturing_on };
enum gs_prim_iip { gs_flat_shading, gs_gouraud_shading };
enum gs_prim_type {
	gs_point, gs_line, gs_linestrip, gs_triangle,
	gs_trianglestrip, gs_trianglefan, gs_sprite
};

/*
 * The IIP/TME/FGE/ABE/AA1/FST/CTXT/FIX fields are only enabled when
 * AC is 1 in the PRMODECONT register.
 */
struct gs_prim {
	__BITFIELD_FIELD(__u64 : 53,
	__BITFIELD_FIELD(__u64 fix : 1,		/* Fragment value control */
	__BITFIELD_FIELD(__u64 ctxt : 1,	/* Context */
	__BITFIELD_FIELD(__u64 fst : 1,		/* Texture coordinate method */
	__BITFIELD_FIELD(__u64 aa1 : 1,		/* Pass antialiasing */
	__BITFIELD_FIELD(__u64 abe : 1,		/* Alpha blendning */
	__BITFIELD_FIELD(__u64 fge : 1,		/* Fogging */
	__BITFIELD_FIELD(__u64 tme : 1,		/* Texture mapping */
	__BITFIELD_FIELD(__u64 iip : 1,		/* Shading method */
	__BITFIELD_FIELD(__u64 prim : 3,	/* Type of drawing primitive */
	;))))))))))
};

/*
 * The IIP/TME/FGE/ABE/AA1/FST/CTXT/FIX fields are only enabled when
 * AC is 0 in the PRMODECONT register.
 */
struct gs_prmode {
	__BITFIELD_FIELD(__u64 : 53,
	__BITFIELD_FIELD(__u64 fix : 1,		/* Fragment value control */
	__BITFIELD_FIELD(__u64 ctxt : 1,	/* Context */
	__BITFIELD_FIELD(__u64 fst : 1,		/* Texture coordinate method */
	__BITFIELD_FIELD(__u64 aa1 : 1,		/* Pass antialiasing */
	__BITFIELD_FIELD(__u64 abe : 1,		/* Alpha blendning */
	__BITFIELD_FIELD(__u64 fge : 1,		/* Fogging */
	__BITFIELD_FIELD(__u64 tme : 1,		/* Texture mapping */
	__BITFIELD_FIELD(__u64 iip : 1,		/* Shading method */
	__BITFIELD_FIELD(__u64 : 3,
	;))))))))))
};

struct gs_prmodecont {
	__BITFIELD_FIELD(__u64 : 63,
	__BITFIELD_FIELD(__u64 ac : 1,	/* Enable PRMODE (= 0) or PRIM (= 1) */
	;))
};

enum gs_pixel_format {
	gs_psmct32  = 0x00, gs_psmct24  = 0x01, gs_psmct16  = 0x02,
	gs_psmct16s = 0x0a, gs_psmt8    = 0x13, gs_psmt4    = 0x14,
	gs_psmt8h   = 0x1b, gs_psmt4hl  = 0x24, gs_psmt4hh  = 0x2c,
	gs_psmz32   = 0x30, gs_psmz24   = 0x31, gs_psmz16   = 0x32,
	gs_psmz16s  = 0x3a
};

/*
 * Host -> local: Only destination fields are used.
 *
 * Local -> host: Only source fields are used. The pixel formats PSMT4,
 * PSMT4HL and PSMT4HH cannot be used.
 *
 * Local -> local: Both source and destination fields are used. The bits
 * per pixel for source and destination must be equal.
 *
 * The rectangular area wraps around when exceeding the buffer width.
 *
 * Limitations on TRXPOS start x coordinate transmissions (not applicable
 * for local -> host):
 *
 *     Multiple of 2: PSMT8, PSMT8H
 *     Multiple of 4: PSMT4, PSMT4HL, PSMT4HH
 *
 * Limitations on TRXREG width (not applicable for local -> host):
 *
 *     Multiple of 2: PSMCT32, PSMZ32
 *     Multiple of 4: PSMCT16, PSMCT16S, PSMZ16, PSMZ16S
 *     Multiple of 8: PSMCT24, PSMZ24, PSMT8, PSMT8H, PSMT4, PSMT4HL, PSMT4HH
 */
struct gs_bitbltbuf {
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 dpsm : 6,	/* Destination pixel format */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 dbw : 6,		/* Destination width/64 */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 dbp : 14,	/* Destination base word/64 */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 spsm : 6,	/* Source pixel format */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 sbw : 6,		/* Source width/64 */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 sbp : 14,	/* Source base word/64 */
	;))))))))))))
};

enum gs_clamp_mode {
	gs_clamp_repeat, gs_clamp_clamp,
	gs_clamp_region_clamp, gs_clamp_region_repeat
};

struct gs_clamp {
	__BITFIELD_FIELD(__u64 : 20,
	__BITFIELD_FIELD(__u64 maxv : 10,	/* Upper v clamp parameter */
	__BITFIELD_FIELD(__u64 minv : 10,	/* Lower v clamp parameter */
	__BITFIELD_FIELD(__u64 maxu : 10,	/* Upper u clamp parameter */
	__BITFIELD_FIELD(__u64 minu : 10,	/* Lower u clamp parameter */
	__BITFIELD_FIELD(__u64 wmt : 2,		/* vertical wrap mode */
	__BITFIELD_FIELD(__u64 wms : 2,		/* Horizontal wrap mode */
	;)))))))
};

struct gs_frame_12 {	/* FIXME: frame_12 -> frame? */
	__BITFIELD_FIELD(__u64 fbmsk : 32,	/* Frame buffer drawing mask */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 psm : 6,		/* Frame buffer pixel format */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 fbw : 6,		/* Frame buffer width/64  */
	__BITFIELD_FIELD(__u64 : 7,
	__BITFIELD_FIELD(__u64 fbp : 9,		/* Frame buffer base/2048 */
	;)))))))
};

enum gs_scanmsk {
	gs_scanmsk_normal,   /* Reserved */
	gs_scanmsk_even = 2, gs_scanmsk_odd
};

struct gs_scanmsk_12 {
	__BITFIELD_FIELD(__u64 : 62,
	__BITFIELD_FIELD(__u64 msk : 2,		/* Raster address mask */
	;))
};

/*
 * All SCISSOR coordinates are in the window coordinate system.
 */
struct gs_scissor_12 {
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scay1 : 11,	/* Lower right y */
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scay0 : 11,	/* Upper left y */
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scax1 : 11,	/* Lower right x */
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scax0 : 11,	/* Upper left x */
	;))))))))
};

/* The pixel transmission order is enabled only for local -> local. */
enum gs_trxpos_dir {
	gs_trxpos_dir_ul_lr,	/* Upper left -> lower right */
	gs_trxpos_dir_ll_ur,	/* Lower left -> upper right */
	gs_trxpos_dir_ur_ll,	/* Upper right -> lower left */
	gs_trxpos_dir_lr_ul,	/* Lower right -> upper left */
};

/*
 * Host -> local: Only destination fields are used. DIR is ignored and the
 * pixel transmission order is always left to right and top to bottom.
 *
 * Local -> host: Only source fields are used. DIR is ignored and the pixel
 * transmission order is always left to right and top to bottom.
 *
 * Local -> local: Both source and destination fields are used. The pixel
 * transmission order DIR is used.
 */
struct gs_trxpos {
	__BITFIELD_FIELD(__u64 : 3,
	__BITFIELD_FIELD(__u64 dir : 2,		/* Pixel transmission order */
	__BITFIELD_FIELD(__u64 dsay : 11,	/* Destination start y */
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 dsax : 11,	/* Destination start x */
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 ssay : 11,	/* Source start y */
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 ssax : 11,	/* Source start x */
	;)))))))))
};

/*
 * The transmission coordinates are modulo 2048 (wrap around).
 */
struct gs_trxreg {
	__BITFIELD_FIELD(__u64 : 20,
	__BITFIELD_FIELD(__u64 rrh : 12,	/* Transmission area height */
	__BITFIELD_FIELD(__u64 : 20,
	__BITFIELD_FIELD(__u64 rrw : 12,	/* Transmission area width */
	;))))
};

enum gs_trxdir_xdir {
	gs_trxdir_host_to_local,
	gs_trxdir_local_to_host,
	gs_trxdir_local_to_local,
	gs_trxdir_nil,				/* Deactivated transmission */
};

/*
 * The TRXDIR register specifies the transmission
 * direction and activates the transmission.
 */
struct gs_trxdir {
	__BITFIELD_FIELD(__u64 : 62,
	__BITFIELD_FIELD(__u64 xdir : 2,	/* Transmission direction */
	;))
};

enum gs_alpha_test { gs_alpha_test_off, gs_alpha_test_on };
enum gs_alpha_method {
	gs_alpha_method_fail, gs_alpha_method_pass, gs_alpha_method_less,
	gs_alpha_method_lequal, gs_alpha_method_equal, gs_alpha_method_gequal,
	gs_alpha_method_greater, gs_alpha_method_notequal
};
enum gs_alpha_failed {
	gs_alpha_failed_keep, gs_alpha_failed_fb_only,
	gs_alpha_failed_zb_only, gs_alpha_failed_rgb_only
};
enum gs_alpha_dst_test { gs_alpha_dst_test_off, gs_alpha_dst_test_on };
enum gs_alpha_dst_method { gs_alpha_dst_pass0, gs_alpha_dst_pass1 };
enum gs_depth_test { gs_depth_test_off, gs_depth_test_on };
enum gs_depth_method {
	gs_depth_fail, gs_depth_pass, gs_depth_gequal, gs_depth_greater
};

/*
 * TEST_1 and TEST_2 are pixel test controls.
 *
 * The ZTE field must at all times be ON (OFF is not allowed). To emulate
 * ZTE OFF, set ZTST to PASS.
 */
struct gs_test_12 {
	__BITFIELD_FIELD(__u64 : 45,
	__BITFIELD_FIELD(__u64 ztst : 2,	/* Depth test method */
	__BITFIELD_FIELD(__u64 zte : 1,		/* Depth test (must be 1) */
	__BITFIELD_FIELD(__u64 datm : 1,	/* Destination alpha test mode */
	__BITFIELD_FIELD(__u64 date : 1,	/* Depth test */
	__BITFIELD_FIELD(__u64 afail : 2,	/* Destination alpha test */
	__BITFIELD_FIELD(__u64 aref : 8,	/* Alpha reference comparison */
	__BITFIELD_FIELD(__u64 atst : 3,	/* Alpha test method */
	__BITFIELD_FIELD(__u64 ate : 1,		/* Alpha test */
	;)))))))))
};

/*
 * The Q field is used both for calculating texture coordinates and
 * deciding level of detail (LOD).
 */
struct gs_rgbaq {
	__BITFIELD_FIELD(__u64 q : 32,	/* Normalized texture coordinate */
	__BITFIELD_FIELD(__u64 a : 8,	/* Alpha vertex value (0x80 = 1.0) */
	__BITFIELD_FIELD(__u64 b : 8,	/* Blue luminance element of vertex */
	__BITFIELD_FIELD(__u64 g : 8,	/* Green luminance element of vertex */
	__BITFIELD_FIELD(__u64 r : 8,	/* Red luminance element of vertex */
	;)))))
};

struct gs_uv {
	__BITFIELD_FIELD(__u64 : 34,
	__BITFIELD_FIELD(__u64 v : 14,	/* Texel coordinate v*16 */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 u : 14,	/* Texel coordinate u*16 */
	;))))
};

/*
 * Assigning XYZ2 moves the vertex queue one step forward. Drawing is not
 * started with XYZ3 (no Drawing Kick).
 *
 * X and Y are specified as fixed-point (4-bit scaling factor) in the
 * primitive coordinate system.
 */
struct gs_xyz23 {
	__BITFIELD_FIELD(__u64 z : 32,	/* Vertext coordinate z */
	__BITFIELD_FIELD(__u64 y : 16,	/* Vertext coordinate y*16 */
	__BITFIELD_FIELD(__u64 x : 16,	/* Vertext coordinate x*16 */
	;)))
};

struct gs_xyoffset_12 {
	__BITFIELD_FIELD(__u64 : 16,
	__BITFIELD_FIELD(__u64 ofy : 16,	/* Offset y*16 */
	__BITFIELD_FIELD(__u64 : 16,
	__BITFIELD_FIELD(__u64 ofx : 16,	/* Offset x*16 */
	;))))
};

enum gs_fifo { gs_fifo_neither, gs_fifo_empty, gs_fifo_almost_full };

struct gs_csr {
	__BITFIELD_FIELD(__u64 : 32,
	__BITFIELD_FIELD(__u64 id : 8,		/* GS id */
	__BITFIELD_FIELD(__u64 rev : 8,		/* GS revision */
	__BITFIELD_FIELD(__u64 fifo : 2,	/* Host interface FIFO status */
	__BITFIELD_FIELD(__u64 field : 1,	/* Field display currently */
	__BITFIELD_FIELD(__u64 nfield : 1,	/* VSync sampled FIELD */
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 reset : 1,	/* GS system reset (enabled
		during data write) */
	__BITFIELD_FIELD(__u64 flush : 1,	/* Drawing suspend and FIFO
		clear (enabled during data write) */
	__BITFIELD_FIELD(__u64 : 1,
	__BITFIELD_FIELD(__u64 zero : 2,	/* Must be zero */
	__BITFIELD_FIELD(__u64 edwint : 1,	/* Rectangular area write
		termination interrupt control */
	__BITFIELD_FIELD(__u64 vsint : 1,	/* VSync interrupt control */
	__BITFIELD_FIELD(__u64 hsint : 1,	/* HSync interrupt control */
	__BITFIELD_FIELD(__u64 finish : 1,	/* FINISH event contol */
	__BITFIELD_FIELD(__u64 signal : 1,	/* SIGNAL event control */
	;))))))))))))))))
};

enum gs_cmod {
	gs_cmod_default,
	/* Reserved */
	gs_cmod_ntsc = 2,
	gs_cmod_pal
};

/*
 * VCK = (13500000 * LC) / ((T1248 + 1) * SPML * RC)
 */
struct gs_smode1 {
	__BITFIELD_FIELD(__u64 : 27,
	__BITFIELD_FIELD(__u64 vhp : 1,
	__BITFIELD_FIELD(__u64 vcksel : 2,
	__BITFIELD_FIELD(__u64 slck2 : 1,
	__BITFIELD_FIELD(__u64 nvck : 1,
	__BITFIELD_FIELD(__u64 clksel : 2,
	__BITFIELD_FIELD(__u64 pevs : 1,
	__BITFIELD_FIELD(__u64 pehs : 1,
	__BITFIELD_FIELD(__u64 pvs : 1,		/* VSync output */
	__BITFIELD_FIELD(__u64 phs : 1,		/* HSync output */
	__BITFIELD_FIELD(__u64 gcont : 1,	/* Select RGBYC or YCrCb */
	__BITFIELD_FIELD(__u64 spml : 4,
	__BITFIELD_FIELD(__u64 pck2 : 2,
	__BITFIELD_FIELD(__u64 xpck : 1,
	__BITFIELD_FIELD(__u64 sint : 1,	/* PLL (Phase-locked loop) */
	__BITFIELD_FIELD(__u64 prst : 1,	/* PLL reset */
	__BITFIELD_FIELD(__u64 ex : 1,
	__BITFIELD_FIELD(__u64 cmod : 2,	/* Display mode */
	__BITFIELD_FIELD(__u64 slck : 1,
	__BITFIELD_FIELD(__u64 t1248 : 2,	/* PLL output divider */
	__BITFIELD_FIELD(__u64 lc : 7,		/* PLL loop divider */
	__BITFIELD_FIELD(__u64 rc : 3,		/* PLL reference divider */
	;))))))))))))))))))))))
};

enum gs_vesa_dpms {
	gs_vesa_dpms_on, gs_vesa_dpms_standby,
	gs_vesa_dpms_suspend, gs_vesa_dpms_off
};

/*
 * In FIELD mode every other line is read. In FRAME mode every line is read.
 *
 * FIELD: 0, 2, 4, ... / 1, 3, 5, ...
 * FRAME: 1, 2, 3, 4, 5, ...
 */
struct gs_smode2 {
	__BITFIELD_FIELD(__u64 : 60,
	__BITFIELD_FIELD(__u64 dpms : 2,	/* VESA DPMS mode */
	__BITFIELD_FIELD(__u64 ffmd : 1,	/* FIELD (= 0) or FRAME (= 1) */
	__BITFIELD_FIELD(__u64 intm : 1,	/* Enable interlace (= 1) */
	;))))
};

struct gs_srfsh {
	__BITFIELD_FIELD(__u64 : 60,
	__BITFIELD_FIELD(__u64 rfsh : 4,	/* DRAM refresh FIXME: Size? */
	;))
};

struct gs_synch1 {
	__BITFIELD_FIELD(__u64 hs : 21,		/* FIXME: Size? */
	__BITFIELD_FIELD(__u64 hsvs : 11,
	__BITFIELD_FIELD(__u64 hseq : 10,
	__BITFIELD_FIELD(__u64 hbp : 11,	/* Horizontal back porch */
	__BITFIELD_FIELD(__u64 hfp : 11,	/* Horizontal front porch */
	;)))))
};

struct gs_synch2 {
	__BITFIELD_FIELD(__u64 : 42,
	__BITFIELD_FIELD(__u64 hb : 11,
	__BITFIELD_FIELD(__u64 hf : 11,
	;)))
};

/*
 * VS   : Halflines with VSYNC
 * VDP  : Halflines with with video data
 * VBPE : Halflines without colorburst after VS ("back porch")
 * VBP  : Halflines with colorburst after VBPE ("back porch")
 * VFPE : Halflines without colorburst after VFP ("front porch")
 * VFP  : Halflines with colorburst after videodata ("front porch")
 */
struct gs_syncv {
	__BITFIELD_FIELD(__u64 vs : 11,
	__BITFIELD_FIELD(__u64 vdp : 11,
	__BITFIELD_FIELD(__u64 vbpe : 10,
	__BITFIELD_FIELD(__u64 vbp : 12,	/* Vertical back porch */
	__BITFIELD_FIELD(__u64 vfpe : 10,
	__BITFIELD_FIELD(__u64 vfp : 10,	/* Vertical front porch */
	;))))))
};

/*
 * Magnifications are factor-1, so 0 is 1x, 1 is 2x, 2 is 3x, etc.
 */
struct gs_display {
	__BITFIELD_FIELD(__u64 : 9,
	__BITFIELD_FIELD(__u64 dh : 11,		/* Display area height-1 (px) */
	__BITFIELD_FIELD(__u64 dw : 12,		/* Display area width-1 (VCK) */
	__BITFIELD_FIELD(__u64 magv : 5,	/* Vertical magnification */
	__BITFIELD_FIELD(__u64 magh : 4,	/* Horizontal magnification */
	__BITFIELD_FIELD(__u64 dy : 11,		/* Display y position (px) */
	__BITFIELD_FIELD(__u64 dx : 12,		/* Display x position (VCK) */
	;)))))))
};

struct gs_dispfb12 {
	__BITFIELD_FIELD(__u64 : 10,
	__BITFIELD_FIELD(__u64 dby : 11,	/* Upper left y position */
	__BITFIELD_FIELD(__u64 dbx : 11,	/* Upper left x position */
	__BITFIELD_FIELD(__u64 : 12,
	__BITFIELD_FIELD(__u64 psm : 5,		/* Pixel storage format */
	__BITFIELD_FIELD(__u64 fbw : 6,		/* Buffer width/64 */
	__BITFIELD_FIELD(__u64 fbp : 9,		/* Base pointer address/2048 */
	;)))))))
};

struct gs_pmode {
	__BITFIELD_FIELD(__u64 : 47,
	__BITFIELD_FIELD(__u64 zero : 1,	/* Must be zero */
	__BITFIELD_FIELD(__u64 alp : 8,		/* Fixed alpha (0xff = 1.0) */
	__BITFIELD_FIELD(__u64 slbg : 1,	/* Alpha blending method */
	__BITFIELD_FIELD(__u64 amod : 1,	/* OUT1 alpha output */
	__BITFIELD_FIELD(__u64 mmod : 1,	/* Alpha blending value */
	__BITFIELD_FIELD(__u64 crtmd : 3,	/* CRT output switching */
	__BITFIELD_FIELD(__u64 en2 : 1,		/* Enable read circuit 2 */
	__BITFIELD_FIELD(__u64 en1 : 1,		/* Enable read circuit 1 */
	;)))))))))
};

enum gs_busdir_dir { gs_busdir_host_to_local, gs_busdir_local_to_host };

struct gs_busdir {
	__BITFIELD_FIELD(__u64 : 63,
	__BITFIELD_FIELD(__u64 dir : 1,
	;))
};

enum gs_tfx {
	gs_tfx_modulate, gs_tfx_decal,
	gs_tfx_highlight, gs_tfx_highlight2
};

enum gs_tcc { gs_tcc_rgb, gs_tcc_rgba };
enum gs_csm { gs_csm1, gs_csm2 };

struct gs_tex0 {
	__BITFIELD_FIELD(__u64 cld : 3,		/* CLUT buffer load control */
	__BITFIELD_FIELD(__u64 csa : 5,		/* CLUT entry offset */
	__BITFIELD_FIELD(__u64 csm : 1,		/* CLUT storage mode */
	__BITFIELD_FIELD(__u64 cpsm : 4,	/* CLUT pixel storage format */
	__BITFIELD_FIELD(__u64 cbp : 14,	/* CLUT buffer base pointer */
	__BITFIELD_FIELD(__u64 tfx : 2,		/* Texture function */
	__BITFIELD_FIELD(__u64 tcc : 1,		/* Texture color component */
	__BITFIELD_FIELD(__u64 th : 4,		/* Texture height (2^h) */
	__BITFIELD_FIELD(__u64 tw : 4,		/* Texture width (2^w) */
	__BITFIELD_FIELD(__u64 psm : 6,		/* Texture storage format */
	__BITFIELD_FIELD(__u64 tbw : 6,		/* Texture buffer width */
	__BITFIELD_FIELD(__u64 tbp0 : 14,	/* Texture base pointer */
	;))))))))))))
};

enum gs_lcm { gs_lcm_formula, gs_lcm_fixed };

enum gs_lod {
	gs_lod_nearest,
	gs_lod_linear,
	gs_lod_nearest_mipmap_nearest,
	gs_lod_nearest_mipmap_linear,
	gs_lod_linear_mipmap_nearest,
	gs_lod_linear_mipmap_linear
};

enum gs_aem { gs_aem_normal, gs_aem_transparent };

struct gs_texa {
	__BITFIELD_FIELD(__u64 : 24,
	__BITFIELD_FIELD(__u64 ta1 : 8,		/* Alpha when A=1 in RGBA16 */
	__BITFIELD_FIELD(__u64 : 16,
	__BITFIELD_FIELD(__u64 aem : 1,		/* Alpha expanding method */
	__BITFIELD_FIELD(__u64 : 7,
	__BITFIELD_FIELD(__u64 ta0 : 8,		/* Alpha when A=0 in RGBA16 */
	;))))))
};

struct gs_tex1 {
	__BITFIELD_FIELD(__u64 k : 11,		/* LOD parameter K */
	__BITFIELD_FIELD(__u64 : 11,
	__BITFIELD_FIELD(__u64 l : 2,		/* LOD parameter L */
	__BITFIELD_FIELD(__u64 : 9,
	__BITFIELD_FIELD(__u64 mtba : 1,	/* MIPMAP base address spec. */
	__BITFIELD_FIELD(__u64 mmin : 3,	/* Reduced texture filter */
	__BITFIELD_FIELD(__u64 mmag : 1,	/* Expanded texture filter */
	__BITFIELD_FIELD(__u64 mxl : 3,		/* Maximum MIP level */
	__BITFIELD_FIELD(__u64 : 1,
	__BITFIELD_FIELD(__u64 lcm : 1,		/* LOD calculation method */
	;))))))))))
};

struct gs_tex2 {
	__BITFIELD_FIELD(__u64 cld : 3,		/* CLUT buffer load control */
	__BITFIELD_FIELD(__u64 csa : 5,		/* CLUT entry offset */
	__BITFIELD_FIELD(__u64 csm : 1,		/* CLUT storage mode */
	__BITFIELD_FIELD(__u64 cpsm : 4,	/* CLUT pixel storage format */
	__BITFIELD_FIELD(__u64 cbp : 14,	/* CLUT buffer base pointer */
	__BITFIELD_FIELD(__u64 : 11,
	__BITFIELD_FIELD(__u64 psm : 6,		/* Texture storage format */
	__BITFIELD_FIELD(__u64 : 20,
	;))))))))
};

enum gs_zmsk { gs_zbuf_on, gs_zbuf_off };

struct gs_zbuf {
	__BITFIELD_FIELD(__u64 : 31,
	__BITFIELD_FIELD(__u64 zmsk : 1,	/* Z value drawing mask */
	__BITFIELD_FIELD(__u64 : 4,
	__BITFIELD_FIELD(__u64 psm : 4,		/* Z value storage format */
	__BITFIELD_FIELD(__u64 : 15,
	__BITFIELD_FIELD(__u64 zbp : 9,		/* Z buffer base pointer/2048 */
	;))))))
};

enum gs_dthe_mode { gs_dthe_off, gs_dthe_on };

struct gs_dthe {
	__BITFIELD_FIELD(__u64 : 63,
	__BITFIELD_FIELD(__u64 dthe : 1,	/* Dithering off=0 or on=1 */
	;))
};

/* FIXME: Move from UAPI */

#define DECLARE_READQ_REG(reg) \
	u64 gs_readq_##reg(void)

#define DECLARE_WRITEQ_REG(reg) \
	void gs_writeq_##reg(u64 value)

#define DECLARE_READQ_WRITEQ_REG(reg) \
	DECLARE_READQ_REG(reg); \
	DECLARE_WRITEQ_REG(reg)

DECLARE_READQ_WRITEQ_REG(pmode);
DECLARE_READQ_WRITEQ_REG(smode1);
DECLARE_READQ_WRITEQ_REG(smode2);
DECLARE_READQ_WRITEQ_REG(srfsh);
DECLARE_READQ_WRITEQ_REG(srfsh);
DECLARE_READQ_WRITEQ_REG(synch1);
DECLARE_READQ_WRITEQ_REG(synch2);
DECLARE_READQ_WRITEQ_REG(syncv);
DECLARE_READQ_WRITEQ_REG(dispfb1);
DECLARE_READQ_WRITEQ_REG(display1);
DECLARE_READQ_WRITEQ_REG(dispfb2);
DECLARE_READQ_WRITEQ_REG(display2);
DECLARE_READQ_WRITEQ_REG(extbuf);
DECLARE_READQ_WRITEQ_REG(extdata);
DECLARE_READQ_WRITEQ_REG(extwrite);
DECLARE_READQ_WRITEQ_REG(bgcolor);
DECLARE_READQ_WRITEQ_REG(csr);
DECLARE_READQ_WRITEQ_REG(imr);
DECLARE_READQ_WRITEQ_REG(busdir);
DECLARE_READQ_WRITEQ_REG(siglblid);
DECLARE_READQ_WRITEQ_REG(syscnt);

#define DECLARE_READ_REG(reg, str) \
	struct gs_##str gs_read_##reg(void)

#define DECLARE_WRITE_REG(reg, str) \
	void gs_write_##reg(struct gs_##str value)

#define DECLARE_READ_WRITE_REG(reg, str) \
	DECLARE_READ_REG(reg, str); \
	DECLARE_WRITE_REG(reg, str)

DECLARE_READ_WRITE_REG(pmode, pmode);
DECLARE_READ_WRITE_REG(smode1, smode1);
DECLARE_READ_WRITE_REG(smode2, smode2);
DECLARE_READ_WRITE_REG(srfsh, srfsh);
DECLARE_READ_WRITE_REG(synch1, synch1);
DECLARE_READ_WRITE_REG(synch2, synch2);
DECLARE_READ_WRITE_REG(syncv, syncv);
DECLARE_READ_WRITE_REG(display1, display);
DECLARE_READ_WRITE_REG(display2, display);
DECLARE_READ_WRITE_REG(dispfb1, dispfb12);
DECLARE_READ_WRITE_REG(dispfb2, dispfb12);
DECLARE_READ_WRITE_REG(csr, csr);
DECLARE_READ_WRITE_REG(busdir, busdir);

void gs_flush(void);
void gs_reset(void);

u32 gs_psmct16_block_count(const u32 fbw, const u32 fbh);
u32 gs_psmct32_block_count(const u32 fbw, const u32 fbh);

u32 gs_psmct16_blocks_free(const u32 fbw, const u32 fbh);
u32 gs_psmct32_blocks_free(const u32 fbw, const u32 fbh);

u32 gs_psmct32_block_address(const u32 fbw, const u32 block_index);
u32 gs_psmct16_block_address(const u32 fbw, const u32 block_index);

/* Frame buffer coordinate system to primitive coordinate system. */
static inline int gs_fbcs_to_pcs(const int c)
{
	return c * 16;	/* The 4 least significant bits are fractional */
}

/* Pixel coordinate system to texel coordinate system. */
static inline int gs_pxcs_to_tcs(const int c)
{
	return c * 16;	/* The 4 least significant bits are fractional */
}

#endif /* _UAPI_MIPS_ASM_GS_H */
