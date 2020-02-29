// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM console drawing environment
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/kernel.h>

#include <drm/drm_print.h>

#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs.h>

#include "gs_tile.h" // FIXME

#include "gs_environment.h"

/**
 * struct environment - Graphics Synthesizer drawing environment context
 * @xres: x resolution in pixels
 * @yres: y resolution in pixels
 * @fbw: frame buffer width in 64-pixel unit
 * @psm: pixel storage mode
 * @fbp: frame buffer base pointer in 2048-pixel unit
 */
struct environment {
	u32 xres;
	u32 yres;
	u32 fbw;
	enum gs_psm psm;
	u32 fbp;
};

/**
 * var_to_env - Graphics Synthesizer drawing environment for a given video mode
 * @var: screen info object
 * @info: frame buffer info object
 *
 * Return: Graphics Synthesizer drawing environment parameters
 */
static struct environment var_to_env(const struct fb_var_screeninfo *var,
	const struct fb_info *info)
{
	return (struct environment) {
		.xres = var->xres,
		.yres = var->yres,
		.fbw  = gs_var_to_fbw(var),
		.psm  = gs_var_to_psm(var)
	};
}

/**
 * package_environment - package drawing environment tags and data for the GIF
 * @package: DMA buffer to put packages in
 * @env: drawing environment to package
 *
 * Various parameters are used to draw Graphics Synthesizer primitives, for
 * example texture information and drawing attributes set by the PRIM register.
 * These parameters are called the drawing environment. The environment remains
 * in effect for multiple primitives until it is reset.
 *
 * Some environment registers such as XYOFFSET_1 and XYOFFSET_2 represent the
 * same function and can be chosen with the CTXT primitive attribute.
 *
 * The following registers are available:
 *
 * ============ =============================================================
 * XYOFFSET_1/2 offset value of vertex coordinates
 * PRMODECONT   PRIM attributes enabled/disabled
 * TEX0_1/2     attributes of texture buffer and texture mapping
 * TEX1_1/2     attributes of texture mapping
 * TEX2_1/2     colour lookup table entry
 * CLAMP_1/2    wrap mode of texture mapping
 * TEXCLUT      colour lookup table setting
 * SCANMSK      drawing control with y coordinate of pixel
 * MIPTBP1_1/2  base pointer for MIPMAP on each level
 * MIPTBP2_1/2  base pointer for MIPMAP on each level
 * TEXA         reference value when expanding alpha value of TEX16 and TEX24
 * FOGCOL       fogging distant color
 * SCISSOR_1/2  scissoring area
 * ALPHA_1/2    alpha-blending attributes
 * DIMX         dither matrix
 * DTHE         dithering enabled/disabled
 * COLCLAMP     color clamp/mask
 * TEST_1/2     pixel operation
 * PABE         alpha-blending in pixel units enabled/disabled
 * FBA_1/2      alpha correction value
 * FRAME_1/2    frame buffer setting
 * ZBUF_1/2     z buffer setting
 * ============ =============================================================
 *
 * Return: number of generated GIF packages in 16-byte unit
 */
static size_t package_environment(union package *package,
	const struct environment env)
{
	union package * const base_package = package;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 11
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_frame_1,
		.data.frame_1 = {
			.fbw = env.fbw,
			.fbp = env.fbp,
			.psm = env.psm
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_xyoffset_1,
		.data.xyoffset_1 = {
			.ofx = 0,
			.ofy = 0,
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_scissor_1,
		.data.scissor_1 = {
			.scax0 = 0, .scax1 = env.xres,
			.scay0 = 0, .scay1 = env.yres
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_scanmsk,
		.data.scanmsk = {
			.msk = gs_scanmsk_normal
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_prmode,
		.data.prmode = { }	/* Reset PRMODE to a known value */
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_prmodecont,
		.data.prmodecont = {
			.ac = 1
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_test_1,
		.data.test_1 = {
			.zte  = gs_depth_test_on,	/* Must always be ON */
			.ztst = gs_depth_pass		/* Emulate OFF */
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_texa,
		.data.texa = {
			.ta0 = GS_ALPHA_ONE,
			.aem = gs_aem_normal,
			.ta1 = GS_ALPHA_ONE
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_tex1_1,
		.data.tex1 = {
			.lcm = gs_lcm_fixed,
			.mmag = gs_lod_nearest,
			.mmin = gs_lod_nearest,
			.k = 0
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_zbuf_1,
		.data.zbuf = {
			.zmsk = gs_zbuf_off
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_dthe,
		.data.dthe = {
			.dthe = gs_dthe_off
		}
	};

	return package - base_package;
}

/**
 * write_cb_environment - write console buffer GS drawing environment to the GIF
 * @info: frame buffer info object
 *
 * Write various parameters used to draw Graphics Synthesizer primitives, for
 * example texture information and drawing attributes set by the PRIM register.
 * The environment remains in effect for multiple primitives until it is reset.
 */
void write_cb_environment(struct fb_info *info)
{
        if (gif_wait()) {
		struct ps2fb_par *par = info_to_ps2par(info);
		union package * const base_package = par->package.buffer;
		union package *package = base_package;

		package += package_environment(package,
			var_to_env(&info->var, info));

		gif_write(&base_package->gif, package - base_package);
	} else
		DRM_ERROR("Failed to write GS environment, GIF is busy\n");
}
