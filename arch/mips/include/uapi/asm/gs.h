// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 * PlayStation 2 Graphics Synthesizer (GS)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef _UAPI_MIPS_ASM_GS_H
#define _UAPI_MIPS_ASM_GS_H

#include <asm/bitfield.h>

/**
 * DOC: The PlayStation 2 Graphics Synthesizer
 *
 * The Graphics Synthesizer frame buffer is subdivided into rectangular pages,
 * from left to right, top to bottom. Pages are further subdivided into blocks,
 * with different arrangements for the PSMCT16 and PSMCT32 pixel storage modes.
 * Blocks are further subdivided into columns that are finally subdivided into
 * pixels.
 *
 * The frame buffer width (FBW) is defined as the pixel width divided by 64,
 * where 64 is the width of a page for PSMCT16 and PSMCT32.
 *
 * The texture base pointer (TBP), color base pointer (CBP), source base
 * pointer (SBP) and destination base pointer (DBP) are all addressed by block.
 *
 * As an example, consider the frame buffer resolution 1920x1080@16 that uses
 * 1920*1080*2 = 4147200 bytes of the 4 MiB = 4194304 bytes available memory.
 * There are 4194304-4147200 = 47104 bytes of free memory. The FBW is 1920/64
 * = 30, which means it is 30 pages wide. However, 1080/GS_PSM_CT32_PAGE_HEIGHT
 * = 1080/64 = 16.875, which means that the last row of blocks are unused in
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
#define GS_PSM_CT16_PAGE_COLS		 4
#define GS_PSM_CT16_PAGE_ROWS		 8
#define GS_PSM_CT16_COLUMN_WIDTH	 8
#define GS_PSM_CT16_COLUMN_HEIGHT	 2
#define GS_PSM_CT16_BLOCK_WIDTH	 (GS_PSM_CT16_COLUMN_WIDTH * GS_BLOCK_COLS)
#define GS_PSM_CT16_BLOCK_HEIGHT (GS_PSM_CT16_COLUMN_HEIGHT * GS_BLOCK_ROWS)
#define GS_PSM_CT16_PAGE_WIDTH	 (GS_PSM_CT16_BLOCK_WIDTH * GS_PSM_CT16_PAGE_COLS)
#define GS_PSM_CT16_PAGE_HEIGHT	 (GS_PSM_CT16_BLOCK_HEIGHT * GS_PSM_CT16_PAGE_ROWS)

/* 32 bit (true color) frame buffer and texture pixel storage format. */
#define GS_PSM_CT32_PAGE_COLS		 8
#define GS_PSM_CT32_PAGE_ROWS		 4
#define GS_PSM_CT32_COLUMN_WIDTH	16
#define GS_PSM_CT32_COLUMN_HEIGHT	 2
#define GS_PSM_CT32_BLOCK_WIDTH	 (GS_PSM_CT32_COLUMN_WIDTH * GS_BLOCK_COLS)
#define GS_PSM_CT32_BLOCK_HEIGHT (GS_PSM_CT32_COLUMN_HEIGHT * GS_BLOCK_ROWS)
#define GS_PSM_CT32_PAGE_WIDTH	 (GS_PSM_CT32_BLOCK_WIDTH * GS_PSM_CT32_PAGE_COLS)
#define GS_PSM_CT32_PAGE_HEIGHT	 (GS_PSM_CT32_BLOCK_HEIGHT * GS_PSM_CT32_PAGE_ROWS)

#define GS_ALPHA_ONE 	0x80	/* Alpha 0x80 = 1.0 */

/**
 * struct gs_rgba16 - 16-bit red, green, blue colour and alpha transparency
 * @a: 1-bit alpha transparency
 * @b: 5-bit blue colour component
 * @g: 5-bit green colour component
 * @r: 5-bit red colour component
 *
 * Alpha is treated as 0 when @a is 0, and as %GS_ALPHA_ONE when @a is 1.
 */
struct gs_rgba16 {
	__BITFIELD_FIELD(__u16 a : 1,
	__BITFIELD_FIELD(__u16 b : 5,
	__BITFIELD_FIELD(__u16 g : 5,
	__BITFIELD_FIELD(__u16 r : 5,
	;))))
};

/**
 * struct gs_rgba32 - 32-bit red, green, blue colour and alpha transparency
 * @a: 8-bit alpha transparency, with %GS_ALPHA_ONE representing 1.0
 * @b: 8-bit blue colour component
 * @g: 8-bit green colour component
 * @r: 8-bit red colour component
 */
struct gs_rgba32 {
	__BITFIELD_FIELD(__u32 a : 8,
	__BITFIELD_FIELD(__u32 b : 8,
	__BITFIELD_FIELD(__u32 g : 8,
	__BITFIELD_FIELD(__u32 r : 8,
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

/**
 * struct gs_prim - drawing primitive
 * @fix: fragment value control
 * @ctxt: context
 * @fst: texture coordinate method
 * @aa1: pass antialiasing
 * @abe: alpha blendning
 * @fge: fogging
 * @tme: texture mapping
 * @iip: shading method
 * @prim: type of drawing primitive
 *
 * The IIP/TME/FGE/ABE/AA1/FST/CTXT/FIX fields are only enabled when AC is 1
 * in the PRMODECONT register.
 */
struct gs_prim {
	__BITFIELD_FIELD(__u64 : 53,
	__BITFIELD_FIELD(__u64 fix : 1,
	__BITFIELD_FIELD(__u64 ctxt : 1,
	__BITFIELD_FIELD(__u64 fst : 1,
	__BITFIELD_FIELD(__u64 aa1 : 1,
	__BITFIELD_FIELD(__u64 abe : 1,
	__BITFIELD_FIELD(__u64 fge : 1,
	__BITFIELD_FIELD(__u64 tme : 1,
	__BITFIELD_FIELD(__u64 iip : 1,
	__BITFIELD_FIELD(__u64 prim : 3,
	;))))))))))
};

/**
 * struct gs_prmode - attributes of drawing primitives
 * @fix: fragment value control
 * @ctxt: context
 * @fst: texture coordinate method
 * @aa1: pass antialiasing
 * @abe: alpha blendning
 * @fge: fogging
 * @tme: texture mapping
 * @iip: shading method
 *
 * The IIP/TME/FGE/ABE/AA1/FST/CTXT/FIX fields are only enabled when AC is 0
 * in the PRMODECONT register.
 */
struct gs_prmode {
	__BITFIELD_FIELD(__u64 : 53,
	__BITFIELD_FIELD(__u64 fix : 1,
	__BITFIELD_FIELD(__u64 ctxt : 1,
	__BITFIELD_FIELD(__u64 fst : 1,
	__BITFIELD_FIELD(__u64 aa1 : 1,
	__BITFIELD_FIELD(__u64 abe : 1,
	__BITFIELD_FIELD(__u64 fge : 1,
	__BITFIELD_FIELD(__u64 tme : 1,
	__BITFIELD_FIELD(__u64 iip : 1,
	__BITFIELD_FIELD(__u64 : 3,
	;))))))))))
};

/**
 * struct gs_prmodecont - primitive attribute setting method
 * @ac: enable PRMODE (= 0) or PRIM (= 1)
 */
struct gs_prmodecont {
	__BITFIELD_FIELD(__u64 : 63,
	__BITFIELD_FIELD(__u64 ac : 1,
	;))
};

enum gs_psm {
	gs_psm_ct32  = 0x00, gs_psm_ct24  = 0x01, gs_psm_ct16 = 0x02,
	gs_psm_ct16s = 0x0a, gs_psm_gpu24 = 0x12, gs_psm_t8   = 0x13,
	gs_psm_t4    = 0x14, gs_psm_t8h   = 0x1b, gs_psm_t4hl = 0x24,
	gs_psm_t4hh  = 0x2c, gs_psm_z32   = 0x30, gs_psm_z24  = 0x31,
	gs_psm_z16   = 0x32, gs_psm_z16s  = 0x3a
};

/**
 * struct gs_bitbltbuf - transmission between buffers
 * @dpsm: destination pixel format
 * @dbw: destination width/64
 * @dbp: destination base word/64
 * @spsm: source pixel format
 * @sbw: source width/64
 * @sbp: source base word/64
 *
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
	__BITFIELD_FIELD(__u64 dpsm : 6,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 dbw : 6,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 dbp : 14,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 spsm : 6,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 sbw : 6,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 sbp : 14,
	;))))))))))))
};

enum gs_clamp_mode {
	gs_clamp_repeat, gs_clamp_clamp,
	gs_clamp_region_clamp, gs_clamp_region_repeat
};

/**
 * struct gs_clamp - texture wrap mode
 * @maxv: upper v clamp parameter
 * @minv: lower v clamp parameter
 * @maxu: upper u clamp parameter
 * @minu: lower u clamp parameter
 * @wmt: vertical wrap mode
 * @wms: horizontal wrap mode
 */
struct gs_clamp {
	__BITFIELD_FIELD(__u64 : 20,
	__BITFIELD_FIELD(__u64 maxv : 10,
	__BITFIELD_FIELD(__u64 minv : 10,
	__BITFIELD_FIELD(__u64 maxu : 10,
	__BITFIELD_FIELD(__u64 minu : 10,
	__BITFIELD_FIELD(__u64 wmt : 2,
	__BITFIELD_FIELD(__u64 wms : 2,
	;)))))))
};

/**
 * struct gs_frame_12 - frame buffer
 * @fbmsk: frame buffer drawing mask
 * @psm: frame buffer pixel format
 * @fbw: frame buffer width/64
 * @fbp: frame buffer base/2048
 */
struct gs_frame_12 {	/* FIXME: frame_12 -> frame? */
	__BITFIELD_FIELD(__u64 fbmsk : 32,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 psm : 6,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 fbw : 6,
	__BITFIELD_FIELD(__u64 : 7,
	__BITFIELD_FIELD(__u64 fbp : 9,
	;)))))))
};

/**
 * enum gs_scanmsk - raster address mask
 * @gs_scanmsk_normal: normal drawing (not masked)
 * @gs_scanmsk_even: drawing of pixel with even y coordinate is prohibited
 * @gs_scanmsk_odd: drawing of pixel with odd y coordinate is prohibited
 */
enum gs_scanmsk {
	gs_scanmsk_normal,
	/* Reserved */
	gs_scanmsk_even = 2,
	gs_scanmsk_odd
};

/**
 * struct gs_scanmsk_12 - raster address mask
 * @msk: raster address mask
 */
struct gs_scanmsk_12 {
	__BITFIELD_FIELD(__u64 : 62,
	__BITFIELD_FIELD(__u64 msk : 2,
	;))
};

/**
 * struct gs_scissor_12 - scissoring area
 * @scay1: lower right y
 * @scay0: upper left y
 * @scax1: lower right x
 * @scax0: upper left x
 *
 * All SCISSOR coordinates are in the window coordinate system.
 */
struct gs_scissor_12 {
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scay1 : 11,
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scay0 : 11,
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scax1 : 11,
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 scax0 : 11,
	;))))))))
};

/**
 * enum gs_alpha_a - specification of input color value A
 * @gs_alpha_a_cs: RGB value of the source is used
 * @gs_alpha_a_cd: RGB value in the frame buffer is used
 * @gs_alpha_a_0: zero
 */
enum gs_alpha_a {
	gs_alpha_a_cs,
	gs_alpha_a_cd,
	gs_alpha_a_0,
};

/**
 * enum gs_alpha_b - specification of input color value B
 * @gs_alpha_b_cs: RGB value of the source is used
 * @gs_alpha_b_cd: RGB value in the frame buffer is used
 * @gs_alpha_b_0: zero
 */
enum gs_alpha_b {
	gs_alpha_b_cs,
	gs_alpha_b_cd,
	gs_alpha_b_0,
};

/**
 * enum gs_alpha_c - specification of input color value C
 * @gs_alpha_c_as: alpha of the source is used
 * @gs_alpha_c_ad: alpha in the frame buffer is used
 * @gs_alpha_c_fix: the fix-field value is used as alpha
 */
enum gs_alpha_c {
	gs_alpha_c_cs,
	gs_alpha_c_cd,
	gs_alpha_c_fix,
};

/**
 * enum gs_alpha_d - specification of input color value D
 * @gs_alpha_d_cs: RGB value of the source is used
 * @gs_alpha_d_cd: RGB value in the frame buffer is used
 * @gs_alpha_d_0: zero
 */
enum gs_alpha_d {
	gs_alpha_d_cs,
	gs_alpha_d_cd,
	gs_alpha_d_0,
};

/**
 * struct gs_alpha_12 - alpha-blending attributes
 * @fix: fixed alpha value, when @c is gs_alpha_c_fix
 * @d: specification of input color value D
 * @c: specification of input color value C
 * @b: specification of input color value B
 * @a: specification of input color value A
 *
 * The blending function is (((A - B) * C) >> 7) + D.
 */
struct gs_alpha_12 {
	__BITFIELD_FIELD(__u64 : 24,
	__BITFIELD_FIELD(__u64 fix : 8,
	__BITFIELD_FIELD(__u64 : 24,
	__BITFIELD_FIELD(__u64 d : 2,
	__BITFIELD_FIELD(__u64 c : 2,
	__BITFIELD_FIELD(__u64 b : 2,
	__BITFIELD_FIELD(__u64 a : 2,
	;)))))))
};

/**
 * enum gs_trxpos_dir - transmission direction
 * @gs_trxpos_dir_ul_lr: upper left -> lower right
 * @gs_trxpos_dir_ll_ur: lower left -> upper right
 * @gs_trxpos_dir_ur_ll: upper right -> lower left
 * @gs_trxpos_dir_lr_ul: lower right -> upper left
 *
 * The pixel transmission order is enabled only for local -> local.
 */
enum gs_trxpos_dir {
	gs_trxpos_dir_ul_lr,
	gs_trxpos_dir_ll_ur,
	gs_trxpos_dir_ur_ll,
	gs_trxpos_dir_lr_ul,
};

/**
 * struct gs_trxpos - transmission areass in buffers
 * @dir: Pixel transmission order
 * @dsay: Destination start y
 * @dsax: Destination start x
 * @ssay: Source start y
 * @ssax: Source start x
 *
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
	__BITFIELD_FIELD(__u64 dir : 2,
	__BITFIELD_FIELD(__u64 dsay : 11,
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 dsax : 11,
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 ssay : 11,
	__BITFIELD_FIELD(__u64 : 5,
	__BITFIELD_FIELD(__u64 ssax : 11,
	;)))))))))
};

/**
 * struct gs_trxreg - transmission areas in buffers
 * @rrh: transmission area height
 * @rrw: transmission area width
 *
 * The transmission coordinates are modulo 2048 (wrap around).
 */
struct gs_trxreg {
	__BITFIELD_FIELD(__u64 : 20,
	__BITFIELD_FIELD(__u64 rrh : 12,
	__BITFIELD_FIELD(__u64 : 20,
	__BITFIELD_FIELD(__u64 rrw : 12,
	;))))
};

enum gs_trxdir_xdir {
	gs_trxdir_host_to_local,
	gs_trxdir_local_to_host,
	gs_trxdir_local_to_local,
	gs_trxdir_nil,				/* Deactivated transmission */
};

/**
 * struct gs_trxdir - activation of transmission between buffers
 * @xdir: transmission direction
 *
 * The TRXDIR register specifies the transmission
 * direction and activates the transmission.
 */
struct gs_trxdir {
	__BITFIELD_FIELD(__u64 : 62,
	__BITFIELD_FIELD(__u64 xdir : 2,
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

/**
 * struct gs_test_12 - pixel test control
 * @ztst: depth test method
 * @zte: depth test (must be 1)
 * @datm: destination alpha test mode
 * @date: depth test
 * @afail: destination alpha test
 * @aref: alpha reference comparison
 * @atst: alpha test method
 * @ate: alpha test
 *
 * The ZTE field must at all times be ON (OFF is not allowed). To emulate
 * ZTE OFF, set ZTST to PASS.
 */
struct gs_test_12 {
	__BITFIELD_FIELD(__u64 : 45,
	__BITFIELD_FIELD(__u64 ztst : 2,
	__BITFIELD_FIELD(__u64 zte : 1,
	__BITFIELD_FIELD(__u64 datm : 1,
	__BITFIELD_FIELD(__u64 date : 1,
	__BITFIELD_FIELD(__u64 afail : 2,
	__BITFIELD_FIELD(__u64 aref : 8,
	__BITFIELD_FIELD(__u64 atst : 3,
	__BITFIELD_FIELD(__u64 ate : 1,
	;)))))))))
};

/**
 * struct gs_rgbaq - vertex color
 * @q: normalized texture coordinate
 * @a: alpha vertex value (0x80 = 1.0)
 * @b: blue luminance element of vertex
 * @g: green luminance element of vertex
 * @r: red luminance element of vertex
 *
 * The Q field is used both for calculating texture coordinates and deciding
 * level of detail (LOD).
 */
struct gs_rgbaq {
	__BITFIELD_FIELD(__u64 q : 32,
	__BITFIELD_FIELD(__u64 a : 8,
	__BITFIELD_FIELD(__u64 b : 8,
	__BITFIELD_FIELD(__u64 g : 8,
	__BITFIELD_FIELD(__u64 r : 8,
	;)))))
};

/**
 * struct gs_uv - vertex texture coordinates
 * @v: texel coordinate v*16
 * @u: texel coordinate u*16
 */
struct gs_uv {
	__BITFIELD_FIELD(__u64 : 34,
	__BITFIELD_FIELD(__u64 v : 14,
	__BITFIELD_FIELD(__u64 : 2,
	__BITFIELD_FIELD(__u64 u : 14,
	;))))
};

/**
 * struct gs_xyz23 - vertex coordinates without a drawing kick
 * @z: vertext coordinate z
 * @y: vertext coordinate y*16
 * @x: vertext coordinate x*16
 *
 * Assigning XYZ2 moves the vertex queue one step forward. Drawing is not
 * started with XYZ3 (no Drawing Kick).
 *
 * X and Y are specified as fixed-point (4-bit scaling factor) in the
 * primitive coordinate system.
 */
struct gs_xyz23 {
	__BITFIELD_FIELD(__u64 z : 32,
	__BITFIELD_FIELD(__u64 y : 16,
	__BITFIELD_FIELD(__u64 x : 16,
	;)))
};

/**
 * struct gs_xyoffset_12 - primitive to window coordinate offsets
 * @ofy: offset y*16
 * @ofx: offset x*16
 *
 * Coordinate offsets from the primitive coordinate system to the window
 * coordinate system.
 */
struct gs_xyoffset_12 {
	__BITFIELD_FIELD(__u64 : 16,
	__BITFIELD_FIELD(__u64 ofy : 16,
	__BITFIELD_FIELD(__u64 : 16,
	__BITFIELD_FIELD(__u64 ofx : 16,
	;))))
};

enum gs_tfx {
	gs_tfx_modulate, gs_tfx_decal,
	gs_tfx_highlight, gs_tfx_highlight2
};

enum gs_tcc { gs_tcc_rgb, gs_tcc_rgba };
enum gs_csm { gs_csm1, gs_csm2 };

/**
 * struct gs_tex0 - texture setting
 * @cld: CLUT buffer load control
 * @csa: CLUT entry offset
 * @csm: CLUT storage mode
 * @cpsm: CLUT pixel storage format
 * @cbp: CLUT buffer base pointer
 * @tfx: texture function
 * @tcc: texture color component
 * @th: texture height (2^h)
 * @tw: texture width (2^w)
 * @psm: texture storage format
 * @tbw: texture buffer width
 * @tbp0: texture base pointer
 */
struct gs_tex0 {
	__BITFIELD_FIELD(__u64 cld : 3,
	__BITFIELD_FIELD(__u64 csa : 5,
	__BITFIELD_FIELD(__u64 csm : 1,
	__BITFIELD_FIELD(__u64 cpsm : 4,
	__BITFIELD_FIELD(__u64 cbp : 14,
	__BITFIELD_FIELD(__u64 tfx : 2,
	__BITFIELD_FIELD(__u64 tcc : 1,
	__BITFIELD_FIELD(__u64 th : 4,
	__BITFIELD_FIELD(__u64 tw : 4,
	__BITFIELD_FIELD(__u64 psm : 6,
	__BITFIELD_FIELD(__u64 tbw : 6,
	__BITFIELD_FIELD(__u64 tbp0 : 14,
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

/**
 * struct gs_texa - texture alpha value setting
 * @ta1: alpha when A=1 in RGBA16
 * @aem: alpha expanding method
 * @ta0: alpha when A=0 in RGBA16
 */
struct gs_texa {
	__BITFIELD_FIELD(__u64 : 24,
	__BITFIELD_FIELD(__u64 ta1 : 8,
	__BITFIELD_FIELD(__u64 : 16,
	__BITFIELD_FIELD(__u64 aem : 1,
	__BITFIELD_FIELD(__u64 : 7,
	__BITFIELD_FIELD(__u64 ta0 : 8,
	;))))))
};

/**
 * struct gs_tex1 - texture setting
 * @k: LOD parameter K
 * @l: LOD parameter L
 * @mtba: MIPMAP base address specification of level 1 or more
 * @mmin: reduced texture filter
 * @mmag: expanded texture filter
 * @mxl: maximum MIP level
 * @lcm: LOD calculation method
 */
struct gs_tex1 {
	__BITFIELD_FIELD(__u64 k : 11,
	__BITFIELD_FIELD(__u64 : 11,
	__BITFIELD_FIELD(__u64 l : 2,
	__BITFIELD_FIELD(__u64 : 9,
	__BITFIELD_FIELD(__u64 mtba : 1,
	__BITFIELD_FIELD(__u64 mmin : 3,
	__BITFIELD_FIELD(__u64 mmag : 1,
	__BITFIELD_FIELD(__u64 mxl : 3,
	__BITFIELD_FIELD(__u64 : 1,
	__BITFIELD_FIELD(__u64 lcm : 1,
	;))))))))))
};

/**
 * struct gs_tex2 - texture setting
 * @cld: CLUT buffer load control
 * @csa: CLUT entry offset
 * @csm: CLUT storage mode
 * @cpsm: CLUT pixel storage format
 * @cbp: CLUT buffer base pointer
 * @psm: Texture storage format
 */
struct gs_tex2 {
	__BITFIELD_FIELD(__u64 cld : 3,
	__BITFIELD_FIELD(__u64 csa : 5,
	__BITFIELD_FIELD(__u64 csm : 1,
	__BITFIELD_FIELD(__u64 cpsm : 4,
	__BITFIELD_FIELD(__u64 cbp : 14,
	__BITFIELD_FIELD(__u64 : 11,
	__BITFIELD_FIELD(__u64 psm : 6,
	__BITFIELD_FIELD(__u64 : 20,
	;))))))))
};

enum gs_zmsk { gs_zbuf_on, gs_zbuf_off };

/**
 * struct gs_zbuf - z buffer setting
 * @zmsk: z value drawing mask
 * @psm: z value storage format (PSMZ32, PSMZ24, PSMZ16 or PSMZ16S)
 * @zbp: z buffer base pointer/2048
 */
struct gs_zbuf {
	__BITFIELD_FIELD(__u64 : 31,
	__BITFIELD_FIELD(__u64 zmsk : 1,
	__BITFIELD_FIELD(__u64 : 4,
	__BITFIELD_FIELD(__u64 psm : 4,
	__BITFIELD_FIELD(__u64 : 15,
	__BITFIELD_FIELD(__u64 zbp : 9,
	;))))))
};

enum gs_dthe_mode { gs_dthe_off, gs_dthe_on };

/**
 * struct gs_dthe - dither control
 * @dthe: dithering off=0 or on=1
 *
 * If the pixel storage mode of the frame buffer is PSMCT32 or PSMCT24,
 * dithering should be turned off.
 */
struct gs_dthe {
	__BITFIELD_FIELD(__u64 : 63,
	__BITFIELD_FIELD(__u64 dthe : 1,
	;))
};

#endif /* _UAPI_MIPS_ASM_GS_H */
