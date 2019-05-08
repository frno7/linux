// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 frame buffer driver
 *
 * Copyright (C) 2019 Fredrik Noring
 */

/**
 * DOC: The PlayStation 2 frame buffer console
 *
 * The frame buffer supports a tiled frame buffer console. The main limitation
 * is the lack of memory mapping (mmap), since the Graphics Synthesizer has
 * local frame buffer memory that is not directly accessible from the main bus.
 * The GS has 4 MiB of local memory.
 *
 * The console drawing primitives are synchronous to allow printk at any time.
 * This is highly useful for debugging but it is not the fastest possible
 * implementation. The console is nevertheless very fast and makes use of
 * several hardware accelerated features of the Graphics Synthesizer.
 *
 * The maximum practical resolution is 1920x1080p at 16 bits per pixel that
 * requires 4147200 bytes of local memory, leaving 47104 bytes for a tiled
 * font, which at 8x8 pixels and a minimum 4 bits indexed texture palette is
 * at most 1464 characters. The indexed palette makes switching colours easy.
 * &struct fb_tile_ops is accelerated with GS texture sprites that are fast
 * (GS local copy) for the kernel via simple DMA GS commands via the GIF.
 *
 * The local memory is organised as follows: first comes the display buffer,
 * then one block of a palette, and finally the font installed as a texture.
 *
 * All frame buffer transmissions are done by DMA via GIF PATH3.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <asm/io.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gif.h>
#include <uapi/asm/gs.h>

#define DEVICE_NAME "ps2fb"

#define PALETTE_BLOCK_COUNT 1	/* One block is used for the indexed colors */

#define GIF_PACKAGE_TAG(package) ((package)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_REG(package) ((package)++)->gif.reg = (struct gif_data_reg)
#define GIF_PACKAGE_AD(package)  ((package)++)->gif.packed.ad = (struct gif_packed_ad)
#define DMA_PACKAGE_TAG(package) ((package)++)->dma = (struct dma_tag)

/* Module parameters */
static char *mode_option;

union package {
	union gif_data gif;
	struct dma_tag dma;
};

/**
 * struct tile_texture - texture representing a tile
 * @tbp: texture base pointer
 * @u: texel u coordinate (x coordinate)
 * @v: texel v coordinate (y coordinate)
 */
struct tile_texture {
	u32 tbp;
	u32 u;
	u32 v;
};

/**
 * struct console_buffer - console buffer
 * @block_count: number of frame buffer blocks
 * @bg: background color index
 * @fg: foreground color index
 * @tile: tile dimensions
 * @tile.width: width in pixels
 * @tile.height: height in pixels
 * @tile.width2: least width in pixels, power of 2
 * @tile.height2: least height in pixels, power of 2
 * @tile.block: tiles are stored as textures in the PSMT4 pixel storage format
 * 	with both cols and rows as powers of 2
 * @tile.block.cols: tile columns per GS block
 * @tile.block.rows: tile rows per GS block
 */
struct console_buffer {
	u32 block_count;

	u32 bg;
	u32 fg;

	struct cb_tile {
		u32 width;
		u32 height;

		u32 width2;
		u32 height2;

		struct {
			u32 cols;
			u32 rows;
		} block;
	} tile;
};

/**
 * struct ps2fb_par - driver specific structure
 * @lock: spin lock to be taken for all structure operations
 * @cb: console buffer definition
 * @package: tags and datafor the GIF
 * @package.capacity: maximum number of GIF packages in 16-byte unit
 * @package.buffer: DMA buffer for GIF packages
 */
struct ps2fb_par {
	spinlock_t lock;

	struct console_buffer cb;

	struct {
		size_t capacity;
		union package *buffer;
	} package;
};

/**
 * struct gs_sync_param - Graphics Synthesizer registers for video modes
 * @smode1: SMODE1 register
 * @smode2: SMODE2 register
 * @srfsh: SRFSH register
 * @synch1: SYNCH1 register
 * @synch2: SYNCH2 register
 * @syncv: SYNCV register
 * @display: DISPLAY1 or DISPLAY2 register
 *
 * These are the essential Graphics Synthesizer video synchronisation register
 * parameters.
 */
struct gs_sync_param {
	struct gs_smode1 smode1;
	struct gs_smode2 smode2;
	struct gs_srfsh srfsh;
	struct gs_synch1 synch1;
	struct gs_synch2 synch2;
	struct gs_syncv syncv;
	struct gs_display display;
};

static const struct fb_videomode standard_modes[] = {
	/* PAL */
	{ "256p", 50, 640, 256, 74074, 100, 61, 34, 22, 63, 2,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "288p", 50, 720, 288, 74074, 70, 11, 19, 3, 63, 3,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "512i", 50, 640, 512, 74074, 100, 61, 67, 41, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "576i", 50, 720, 576, 74074, 70, 11, 39, 5, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "576p", 50, 720, 576, 37037, 70, 11, 39, 5, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "720p", 50, 1280, 720, 13468, 220, 400, 19, 6, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1080i", 50, 1920, 1080, 13468, 148, 484, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "1080p", 50, 1920, 1080, 6734, 148, 484, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },

	/* PAL with borders to ensure that the whole screen is visible */
	{ "460i", 50, 576, 460, 74074, 142, 83, 97, 63, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "460p", 50, 576, 460, 37037, 142, 83, 97, 63, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "644p", 50, 1124, 644, 13468, 298, 478, 57, 44, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "964i", 50, 1688, 964, 13468, 264, 600, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "964p", 50, 1688, 964, 6734, 264, 600, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },

	/* NTSC */
	{ "224p", 60, 640, 224, 74074, 95, 60, 22, 14, 63, 3,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "240p", 60, 720, 240, 74074, 58, 17, 15, 5, 63, 3,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "448i", 60, 640, 448, 74074, 95, 60, 44, 27, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "480i", 60, 720, 480, 74074, 58, 17, 30, 9, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "480p", 60, 720, 480, 37037, 58, 17, 30, 9, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "720p", 60, 1280, 720, 13481, 220, 70, 19, 6, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1080i", 60, 1920, 1080, 13481, 148, 44, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "1080p", 60, 1920, 1080, 6741, 148, 44, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },

	/* NTSC with borders to ensure that the whole screen is visible */
	{ "384i", 60, 576, 384, 74074, 130, 89, 78, 57, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "384p", 60, 576, 384, 37037, 130, 89, 78, 57, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "644p", 60, 1124, 644, 13481, 298, 148, 57, 44, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "964i", 60, 1688, 964, 13481, 264, 160, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "964p", 60, 1688, 964, 6741, 264, 160, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },

	/* VESA */
	{ "vesa-1a", 60, 640, 480, 39682,  48, 16, 33, 10, 96, 2,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-1c", 75, 640, 480, 31746, 120, 16, 16, 1, 64, 3,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-2b", 60, 800, 600, 25000, 88, 40, 23, 1, 128, 4,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-2d", 75, 800, 600, 20202, 160, 16, 21, 1, 80, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-3b", 60, 1024, 768, 15384, 160, 24, 29, 3, 136, 6,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-3d", 75, 1024, 768, 12690, 176, 16, 28, 1, 96, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-4a", 60, 1280, 1024, 9259, 248, 48, 38, 1, 112, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-4b", 75, 1280, 1024, 7407, 248, 16, 38, 1, 144, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA }
};

/**
 * var_to_fbw - frame buffer width for a given virtual x resolution
 * @var: screen info object to compute FBW for
 *
 * Return: frame buffer width (FBW) in 64-pixel unit
 */
static u32 var_to_fbw(const struct fb_var_screeninfo *var)
{
	/*
	 * Round up to nearest GS_FB_PAGE_WIDTH (64 px) since there are
	 * valid resolutions such as 720 px that do not divide 64 properly.
	 */
	return (var->xres_virtual + GS_FB_PAGE_WIDTH - 1) / GS_FB_PAGE_WIDTH;
}

/**
 * var_to_psm - frame buffer pixel storage mode for a given bits per pixel
 * @var: screen info object to compute PSM for
 * @info: frame buffer info object
 *
 * Return: frame buffer pixel storage mode
 */
static enum gs_psm var_to_psm(const struct fb_var_screeninfo *var,
	const struct fb_info *info)
{
	if (var->bits_per_pixel == 1)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 16)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 32)
		return gs_psm_ct32;

	fb_warn_once(info, "%s: Unsupported bits per pixel %u\n",
		__func__, var->bits_per_pixel);
	return gs_psm_ct32;
}

/**
 * var_to_block_count - number of frame buffer blocks for a given video mode
 * @var: screen info object to compute the number of blocks for
 *
 * The Graphics Synthesizer frame buffer is subdivided into rectangular pages,
 * from left to right, top to bottom. Pages are further subdivided into blocks,
 * with different arrangements for PSMCT16 and PSMCT32. Blocks are further
 * subdivided into columns, which are finally subdivided into pixels.
 *
 * The video display buffer, textures and palettes share the same frame buffer.
 * This function can be used to compute the first free block after the video
 * display buffer.
 *
 * Return: number of blocks, or zero for unsupported pixel storage modes
 */
static u32 var_to_block_count(const struct fb_info *info)
{
	const struct fb_var_screeninfo *var = &info->var;
	const enum gs_psm psm = var_to_psm(var, info);
	const u32 fbw = var_to_fbw(var);

	if (psm == gs_psm_ct16)
		return gs_psm_ct16_block_count(fbw, var->yres_virtual);
	if (psm == gs_psm_ct32)
		return gs_psm_ct32_block_count(fbw, var->yres_virtual);

	fb_warn_once(info, "%s: Unsupported pixel storage mode %u\n",
		__func__, psm);
	return 0;
}

/**
 * texture_least_power_of_2 - round up to a power of 2, not less than 8
 * @n: integer to round up
 *
 * Return: least integer that is a power of 2 and not less than @n or 8
 */
static u32 texture_least_power_of_2(u32 n)
{
	return max(1 << get_count_order(n), 8);
}

/**
 * cb_tile - create a console buffer tile object
 * @width: width of tile in pixels
 * @height: height of tile in pixels
 *
 * Return: a console buffer tile object with the given width and height
 */
static struct cb_tile cb_tile(u32 width, u32 height)
{
	const u32 width2 = texture_least_power_of_2(width);
	const u32 height2 = texture_least_power_of_2(height);

	return (struct cb_tile) {
		.width = width,
		.height = height,

		.width2 = width2,
		.height2 = height2,

		.block = {
			.cols = GS_PSMT4_BLOCK_WIDTH / width2,
			.rows = GS_PSMT4_BLOCK_HEIGHT / height2,
		},
	};
}

/**
 * display_buffer_size - display buffer size for a given video resolution
 *
 * This calculation is a lower bound estimate. A precise calculation would have
 * to take memory pages, blocks and column arrangements into account. To choose
 * the appropriate standard video mode such details can be disregarded, though.
 *
 * Return: the size in bytes of the display buffer
 */
static u32 display_buffer_size(const u32 xres_virtual, const u32 yres_virtual,
      const u32 bits_per_pixel)
{
	return (xres_virtual * yres_virtual * bits_per_pixel) / 8;
}

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
		.fbw  = var_to_fbw(var),
		.psm  = var_to_psm(var, info)
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
		struct ps2fb_par *par = info->par;
		union package * const base_package = par->package.buffer;
		union package *package = base_package;

		package += package_environment(package,
			var_to_env(&info->var, info));

		gif_write(&base_package->gif, package - base_package);
	} else
		fb_err(info, "Failed to write GS environment, GIF is busy\n");
}

/**
 * ps2fb_cb_get_tilemax - maximum number of tiles
 * @info: frame buffer info object
 *
 * Return: the maximum number of tiles
 */
static int ps2fb_cb_get_tilemax(struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;
	const u32 block_tile_count =
		par->cb.tile.block.cols *
		par->cb.tile.block.rows;
	const s32 blocks_available =
		GS_BLOCK_COUNT - par->cb.block_count - PALETTE_BLOCK_COUNT;

	return blocks_available > 0 ? blocks_available * block_tile_count : 0;
}

/**
 * clear_screen - clear the displayed buffer screen
 * @info: frame buffer info object
 */
static void clear_screen(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	union package * const base_package = par->package.buffer;
	union package *package = base_package;

	if (!gif_wait()) {
		fb_err(info, "Failed to clear the screen, GIF is busy\n");
		return;
	}

	GIF_PACKAGE_TAG(package) {
		.flg = gif_reglist_mode,
		.reg0 = gif_reg_prim,
		.reg1 = gif_reg_rgbaq,
		.reg2 = gif_reg_xyz2,
		.reg3 = gif_reg_xyz2,
		.nreg = 4,
		.nloop = 1,
		.eop = 1
	};
	GIF_PACKAGE_REG(package) {
		.lo.prim = { .prim = gs_sprite },
		.hi.rgbaq = { .a = GS_ALPHA_ONE }
	};
	GIF_PACKAGE_REG(package) {
		.lo.xyz2 = {
			.x = gs_fbcs_to_pcs(0),
			.y = gs_fbcs_to_pcs(0)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(info->var.xres_virtual),
			.y = gs_fbcs_to_pcs(info->var.yres_virtual)
		}
	};

	gif_write(&base_package->gif, package - base_package);
}

/**
 * bits_per_pixel_fits - does the given resolution fit the given buffer size?
 * @xres_virtual: virtual x resolution in pixels
 * @yres_virtual: virtual y resolution in pixels
 * @bits_per_pixel: number of bits per pixel
 * @buffer_size: size in bytes of display buffer
 *
 * The size calculation is approximate, but accurate enough for the standard
 * video modes.
 *
 * Return: %true if the resolution fits the given buffer size, otherwise %false
 */
static bool bits_per_pixel_fits(const u32 xres_virtual, const u32 yres_virtual,
      const int bits_per_pixel, const size_t buffer_size)
{
	return display_buffer_size(xres_virtual, yres_virtual,
		bits_per_pixel) <= buffer_size;
}

/**
 * default_bits_per_pixel - choose either 16 or 32 bits per pixel
 * @xres_virtual: virtual x resolution in pixels
 * @yres_virtual: virtual y resolution in pixels
 * @buffer_size: size in bytes of display buffer
 *
 * 32 bits per pixel is returned unless this does not fit the given buffer size.
 *
 * The size calculation is approximate, but accurate enough for the standard
 * video modes.
 *
 * Return: 16 or 32 bits per pixel
 */
static int default_bits_per_pixel(
	const u32 xres_virtual, const u32 yres_virtual,
	const size_t buffer_size)
{
	return bits_per_pixel_fits(xres_virtual, yres_virtual,
		32, buffer_size) ? 32 : 16;
}

/**
 * filled_var_videomode - is the screen info video mode filled in?
 * @var: screen info object to check
 *
 * Return: %true if the video mode is filled in, otherwise %false
 */
static bool filled_var_videomode(const struct fb_var_screeninfo *var)
{
	return var->xres > 0 && var->hsync_len > 0 &&
	       var->yres > 0 && var->vsync_len > 0 && var->pixclock > 0;
}

static int ps2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	/* Check whether video mode defaults are needed. */
	if (!filled_var_videomode(var)) {
		const struct fb_videomode *vm =
			fb_find_best_mode(var, &info->modelist);

		if (!vm)
			return -EINVAL;

		fb_videomode_to_var(var, vm);
	}

        /* GS video register resolution is limited to 2048. */
        if (var->xres < 1 || 2048 < var->xres ||
	    var->yres < 1 || 2048 < var->yres)
		return -EINVAL;

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->xoffset = 0;
	var->yoffset = 0;

        /* Check bits per pixel. */
        if (!var->bits_per_pixel)
		var->bits_per_pixel = default_bits_per_pixel(
		     var->xres_virtual, var->yres_virtual, info->fix.smem_len);
	else if (var->bits_per_pixel != 16 &&
		 var->bits_per_pixel != 32)
		return -EINVAL;
        if (!bits_per_pixel_fits(var->xres_virtual, var->yres_virtual,
			var->bits_per_pixel, info->fix.smem_len))
		var->bits_per_pixel = default_bits_per_pixel(
		     var->xres_virtual, var->yres_virtual, info->fix.smem_len);
        if (!bits_per_pixel_fits(var->xres_virtual, var->yres_virtual,
			var->bits_per_pixel, info->fix.smem_len))
		return -ENOMEM;
	if (var->bits_per_pixel == 16) {
		var->red    = (struct fb_bitfield){ .offset =  0, .length = 5 };
		var->green  = (struct fb_bitfield){ .offset =  5, .length = 5 };
		var->blue   = (struct fb_bitfield){ .offset = 10, .length = 5 };
		var->transp = (struct fb_bitfield){ .offset = 15, .length = 1 };
	} else if (var->bits_per_pixel == 32) {
		var->red    = (struct fb_bitfield){ .offset =  0, .length = 8 };
		var->green  = (struct fb_bitfield){ .offset =  8, .length = 8 };
		var->blue   = (struct fb_bitfield){ .offset = 16, .length = 8 };
		var->transp = (struct fb_bitfield){ .offset = 24, .length = 8 };
	} else
		return -EINVAL;		/* Unsupported bits per pixel. */

        /* Screen rotations are not supported. */
	if (var->rotate)
		return -EINVAL;

        return 0;
}

static int ps2fb_cb_check_var(
	struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&par->lock, flags);
	err = ps2fb_check_var(var, info);
	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		if (info->tileops->fb_get_tilemax(info) < 256)
			err = -ENOMEM;

	return err;
}

/**
 * refresh_for_var - display refresh frequency for a given screen info
 * @var: screen info object to compute the display frequency for
 *
 * Return: display refresh frequency in hertz
 */
static u32 refresh_for_var(const struct fb_var_screeninfo *var)
{
	const u32 htotal = var->hsync_len +
		var->left_margin + var->xres + var->right_margin;
	const u32 vtotal = var->vsync_len +
		var->upper_margin + var->yres + var->lower_margin;
	const u32 ptotal = htotal * vtotal;

	return DIV_ROUND_CLOSEST_ULL(DIV_ROUND_CLOSEST_ULL(
		1000000000000ull * ((var->vmode & FB_VMODE_INTERLACED) ? 2 : 1),
		var->pixclock), ptotal);
}

/**
 * vm_to_cmod - determine the CMOD field for the SMODE1 register
 * @vm: video mode object to compute CMOD for
 *
 * Result: PAL, NTSC or VESA
 */
static enum gs_smode1_cmod vm_to_cmod(const struct fb_videomode *vm)
{
	const u32 htotal = vm->hsync_len +
		vm->left_margin + vm->xres + vm->right_margin;
	const u32 vtotal = vm->vsync_len +
		vm->upper_margin + vm->yres + vm->lower_margin;
	const u32 ptotal = htotal * vtotal;
	const u32 refresh = DIV_ROUND_CLOSEST_ULL(DIV_ROUND_CLOSEST_ULL(
		1000000000000ull * ((vm->vmode & FB_VMODE_INTERLACED) ? 2 : 1),
		vm->pixclock), ptotal);

	if (vm->sync & FB_SYNC_BROADCAST)
		return refresh < 55 ? gs_cmod_pal :
		       refresh < 65 ? gs_cmod_ntsc :
				      gs_cmod_vesa;

	return gs_cmod_vesa;
}

/**
 * vm_to_sp_sdtv - standard-definition television video synch parameters
 * @vm: video mode object to compute synchronisation parameters for
 *
 * The numeric register field constants come from fixed SDTV video modes made
 * by Sony. The main complication is that these values are the basis to compute
 * arbitrary top, bottom, left and right display margin (border) settings for
 * both PAL and NTSC. This is useful to for example precisely center the image
 * for a given analogue video display.
 *
 * The SDTV modes are designed to work with S-video, SCART and component video
 * cables, in addition to the PS2 HDMI adapter based on the Macro Silicon
 * MS9282 chip.
 *
 * The MAGV and MAGH fields for vertical and horizontal magnification in the
 * DISPLAY registers could be used to support lower resolution video modes,
 * for example 320x200 that was popular with many 8-bit and 16-bit computers,
 * but that is left for a future extension.
 *
 * Return: Graphics Synthesizer SDTV video mode synchronisation parameters
 */
static struct gs_sync_param vm_to_sp_sdtv(const struct fb_videomode *vm)
{
	const u32 cmod = vm_to_cmod(vm);
	const u32 intm = (vm->vmode & FB_VMODE_INTERLACED) ? 1 : 0;
	const u32 vs   = cmod == gs_cmod_pal ? 5 : 6;
	const u32 hb   = cmod == gs_cmod_pal ? 1680 : 1652;
	const u32 hf   = 2892 - hb;
	const u32 hs   = 254;
	const u32 hbp  = cmod == gs_cmod_pal ? 262 : 222;
	const u32 hfp  = cmod == gs_cmod_pal ? 48 : 64;
	const u32 vdp  = cmod == gs_cmod_pal ? 576 : 480;
	const u32 vbpe = vs;
	const u32 vbp  = cmod == gs_cmod_pal ? 33 : 26;
	const u32 vfpe = vs;
	const u32 vfp  = (vm->vmode & FB_VMODE_INTERLACED) ? 1 :
		cmod == gs_cmod_pal ? 4 : 2;
	const u32 tw = hb + hf;
	const u32 th = vdp;
	const u32 dw = min_t(u32, vm->xres * 4, tw);
	const u32 dh = min_t(u32, vm->yres * (intm ? 1 : 2), th);
	const u32 dx = hs + hbp + (tw - dw)/2 - 1;
	const u32 dy = (vs + vbp + vbpe + (th - dh)/2) / (intm ? 1 : 2) - 1;

	return (struct gs_sync_param) {
		.smode1 = {
			.vhp    =    0, .vcksel = 1, .slck2 = 1, .nvck = 1,
			.clksel =    1, .pevs   = 0, .pehs  = 0, .pvs  = 0,
			.phs    =    0, .gcont  = 0, .spml  = 4, .pck2 = 0,
			.xpck   =    0, .sint   = 1, .prst  = 0, .ex   = 0,
			.cmod   = cmod, .slck   = 0, .t1248 = 1,
			.lc     =   32, .rc     = 4
		},
		.smode2 = {
			.intm = intm
		},
		.srfsh = {
			.rfsh = 8
		},
		.synch1 = {
			.hs   = hs,
			.hsvs = cmod == gs_cmod_pal ? 1474 : 1462,
			.hseq = cmod == gs_cmod_pal ? 127 : 124,
			.hbp  = hbp,
			.hfp  = hfp
		},
		.synch2 = {
			.hb = hb,
			.hf = hf
		},
		.syncv = {
			.vs   = vs,
			.vdp  = vdp,
			.vbpe = vbpe,
			.vbp  = vbp,
			.vfpe = vfpe,
			.vfp  = vfp
		},
		.display = {
			.dh   = vm->yres - 1,
			.dw   = vm->xres * 4 - 1,
			.magv = 0,
			.magh = 3,
			.dy   = dy,
			.dx   = dx
		}
	};
}

/**
 * vm_to_sp_hdtv - high-definition television video synch parameters
 * @vm: video mode object to compute synchronisation parameters for
 * @sg: Graphics Synthesizer SMODE1 register video clock fields
 *
 * Some of the numeric register field constants come from fixed DTV video
 * modes made by Sony. The main complication is that these values are the
 * basis to compute arbitrary top, bottom, left and right display margin
 * (border) settings. This is useful to for example precisely center the
 * image for a given analogue video display.
 *
 * The HDTV modes are designed to work with component video cables and the
 * PS2 HDMI adapter based on the Macro Silicon MS9282 chip.
 *
 * The MAGV and MAGH fields for vertical and horizontal magnification in the
 * DISPLAY registers could be used to emulate SDTV resolution video modes such
 * as 320x200 that was popular with many 8-bit and 16-bit computers, but that
 * is left for a future extension.
 *
 * Return: Graphics Synthesizer HDTV video mode synchronisation parameters
 */
static struct gs_sync_param vm_to_sp_hdtv(
	const struct fb_videomode *vm, const struct gs_synch_gen sg)
{
	const u32 spml  = sg.spml;
	const u32 t1248 = sg.t1248;
	const u32 lc    = sg.lc;
	const u32 rc    = sg.rc;
	const u32 vc    = vm->yres <= 576 ? 1 : 0;
	const u32 hadj  = spml / 2;
	const u32 vhp   = (vm->vmode & FB_VMODE_INTERLACED) ? 0 : 1;
	const u32 hb    = vm->xres * spml * 3 / 5;

	return (struct gs_sync_param) {
		.smode1 = {
			.vhp    = vhp, .vcksel = vc, .slck2 =     1, .nvck = 1,
			.clksel =   1, .pevs   =  0, .pehs  =     0, .pvs  = 0,
			.phs    =   0, .gcont  =  0, .spml  =  spml, .pck2 = 0,
			.xpck   =   0, .sint   =  1, .prst  =     0, .ex   = 0,
			.cmod   =   0, .slck   =  0, .t1248 = t1248,
			.lc     =  lc, .rc     = rc
		},
		.smode2 = {
			.intm = (vm->vmode & FB_VMODE_INTERLACED) ? 1 : 0
		},
		.srfsh = {
			.rfsh = gs_rfsh_from_synch_gen(sg)
		},
		.synch1 = {
			.hs   = vm->hsync_len * spml,
			.hsvs = (vm->left_margin + vm->xres +
				 vm->right_margin - vm->hsync_len) * spml / 2,
			.hseq = vm->hsync_len * spml,
			.hbp  = vm->left_margin * spml - hadj,
			.hfp  = vm->right_margin * spml + hadj
		},
		.synch2 = {
			.hb = hb,
			.hf = vm->xres * spml - hb
		},
		.syncv = {
			.vs   = vm->vsync_len,
			.vdp  = vm->yres,
			.vbpe = 0,
			.vbp  = vm->upper_margin,
			.vfpe = 0,
			.vfp  = vm->lower_margin
		},
		.display = {
			.dh   = vm->yres - 1,
			.dw   = vm->xres * spml - 1,
			.magv = 0,
			.magh = spml - 1,
			.dy   = vm->vsync_len + vm->upper_margin - 1,
			.dx   = (vm->hsync_len + vm->left_margin) * spml - 1 - hadj
		}
	};
}

/**
 * vm_to_sp_vesa - VESA computer display mode synch parameters
 * @vm: video mode object to compute synchronisation parameters for
 * @sg: Graphics Synthesizer SMODE1 register video clock fields
 *
 * Some of the numeric register field constants come from fixed VESA video
 * modes made by Sony. The main complication is that these values are the
 * basis to compute arbitrary top, bottom, left and right display margin
 * (border) settings. This is useful to for example precisely center the
 * image for a given analogue video display.
 *
 * The VESA modes are designed to work with the synch-on-green (SOG) VGA cable
 * that Sony distributed with the Linux kit for the PlayStation 2. Modern
 * computer displays typically do not support synch-on-green, so an adapter
 * is most likely necessary for these modes.
 *
 * Return: Graphics Synthesizer VESA display mode synchronisation parameters
 */
static struct gs_sync_param vm_to_sp_vesa(
	const struct fb_videomode *vm, const struct gs_synch_gen sg)
{
	const u32 spml  = sg.spml;
	const u32 t1248 = sg.t1248;
	const u32 lc    = sg.lc;
	const u32 rc    = sg.rc;
	const u32 hadj  = spml / 2;
	const u32 vhp   = (vm->vmode & FB_VMODE_INTERLACED) ? 0 : 1;
	const u32 hb    = vm->xres * spml * 3 / 5;

	return (struct gs_sync_param) {
		.smode1 = {
			.vhp    = vhp, .vcksel =  0, .slck2 =     1, .nvck = 1,
			.clksel =   1, .pevs   =  0, .pehs  =     0, .pvs  = 0,
			.phs    =   0, .gcont  =  0, .spml  =  spml, .pck2 = 0,
			.xpck   =   0, .sint   =  1, .prst  =     0, .ex   = 0,
			.cmod   =   0, .slck   =  0, .t1248 = t1248,
			.lc     =  lc, .rc     = rc
		},
		.smode2 = {
			.intm = (vm->vmode & FB_VMODE_INTERLACED) ? 1 : 0
		},
		.srfsh = {
			.rfsh = gs_rfsh_from_synch_gen(sg)
		},
		.synch1 = {
			.hs   = vm->hsync_len * spml,
			.hsvs = (vm->left_margin + vm->xres +
				 vm->right_margin - vm->hsync_len) * spml / 2,
			.hseq = vm->hsync_len * spml,
			.hbp  = vm->left_margin * spml - hadj,
			.hfp  = vm->right_margin * spml + hadj
		},
		.synch2 = {
			.hb = hb,
			.hf = vm->xres * spml - hb
		},
		.syncv = {
			.vs   = vm->vsync_len,
			.vdp  = vm->yres,
			.vbpe = 0,
			.vbp  = vm->upper_margin,
			.vfpe = 0,
			.vfp  = vm->lower_margin
		},
		.display = {
			.dh   = vm->yres - 1,
			.dw   = vm->xres * spml - 1,
			.magv = 0,
			.magh = spml - 1,
			.dy   = vm->vsync_len + vm->upper_margin - 1,
			.dx   = (vm->hsync_len + vm->left_margin) * spml - 1 - hadj
		}
	};
}

static struct gs_sync_param vm_to_sp_for_synch_gen(
	const struct fb_videomode *vm, const struct gs_synch_gen sg)
{
	const bool bc = vm->sync & FB_SYNC_BROADCAST;
	const bool il = vm->vmode & FB_VMODE_INTERLACED;
	struct gs_sync_param sp =
		vm->yres <= 288 &&       bc ? vm_to_sp_sdtv(vm) :
		vm->yres <= 576 && il && bc ? vm_to_sp_sdtv(vm) :
					 bc ? vm_to_sp_hdtv(vm, sg) :
					      vm_to_sp_vesa(vm, sg);

	sp.smode1.gcont = gs_gcont_ycrcb;
	sp.smode1.sint = 1;
	sp.smode1.prst = 0;

	return sp;
}

static struct gs_sync_param vm_to_sp(const struct fb_videomode *vm)
{
	return vm_to_sp_for_synch_gen(vm, gs_synch_gen_for_vck(vm->pixclock));
}

static int ps2fb_set_par(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	const struct fb_var_screeninfo *var = &info->var;
	const struct fb_videomode *mm = fb_match_mode(var, &info->modelist);
	const struct fb_videomode vm = (struct fb_videomode) {
		.refresh      = refresh_for_var(var),
		.xres         = var->xres,
		.yres         = var->yres,
		.pixclock     = var->pixclock,
		.left_margin  = var->left_margin,
		.right_margin = var->right_margin,
		.upper_margin = var->upper_margin,
		.lower_margin = var->lower_margin,
		.hsync_len    = var->hsync_len,
		.vsync_len    = var->vsync_len,
		.sync         = var->sync,
		.vmode        = var->vmode,
		.flag         = mm != NULL ? mm->flag : 0
	};
	const struct gs_sync_param sp = vm_to_sp(&vm);
	struct gs_smode1 smode1 = sp.smode1;

	par->mode = vm;

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
	info->fix.ywrapstep = 1;
	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;

	gs_write_smode1(smode1);
	gs_write_smode2(sp.smode2);
	gs_write_srfsh(sp.srfsh);
	gs_write_synch1(sp.synch1);
	gs_write_synch2(sp.synch2);
	gs_write_syncv(sp.syncv);
	gs_write_display1(sp.display);

	GS_WRITE_DISPFB1(
		.fbw = var_to_fbw(var),
		.psm = var_to_psm(var, info),
		.dbx = var->xoffset,
		.dby = var->yoffset,
	);

	GS_WRITE_PMODE(
		.en1 = 1,
		.crtmd = 1
	);

	smode1.prst = 1;
	gs_write_smode1(smode1);

	udelay(2500);

	smode1.sint = 0;
	smode1.prst = 0;
	gs_write_smode1(smode1);

	return 0;
}

static int ps2fb_cb_set_par(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&par->lock, flags);

	err = ps2fb_set_par(info);
	if (!err) {
		par->cb.block_count = var_to_block_count(info);

		write_cb_environment(info);

		clear_screen(info);
	}

	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		fb_info(info, "%d tiles maximum for %ux%u font\n",
			info->tileops->fb_get_tilemax(info),
			par->cb.tile.width, par->cb.tile.height);

	return err;
}

static u32 block_dimensions(u32 dim, u32 alignment)
{
	u32 mask = 0;
	u32 d;

	for (d = 1; d <= dim; d++)
		if (d % alignment == 0)
			mask |= 1 << (d - 1);

	return mask;
}

static void fill_modes(struct device *dev, struct list_head *head)
{
	int i;

	INIT_LIST_HEAD(head);

	for (i = 0; i < ARRAY_SIZE(standard_modes); i++)
		if (fb_add_videomode(&standard_modes[i], head) < 0)
			dev_err(dev, "fb_add_videomode failed\n");
}

static int init_console_buffer(struct platform_device *pdev,
	struct fb_info *info)
{
	static struct fb_ops fbops = {
		.owner		= THIS_MODULE,
		.fb_set_par	= ps2fb_cb_set_par,
		.fb_check_var	= ps2fb_cb_check_var,
	};

	static struct fb_tile_ops tileops = {
		.fb_get_tilemax = ps2fb_cb_get_tilemax
	};

	struct ps2fb_par *par = info->par;

	fb_info(info, "Graphics Synthesizer console frame buffer device\n");

	info->screen_size = 0;
	info->screen_base = NULL;	/* mmap is unsupported by hardware */

	info->fix.smem_start = 0;	/* GS frame buffer is local memory */
	info->fix.smem_len = GS_MEMORY_SIZE;

	info->fbops = &fbops;
	info->flags = FBINFO_DEFAULT |
		      FBINFO_READS_FAST;

	info->flags |= FBINFO_MISC_TILEBLITTING;
	info->tileops = &tileops;

	/*
	 * BITBLTBUF for pixel format CT32 requires divisibility by 2,
	 * and CT16 requires divisibility by 4. So 4 is a safe choice.
	 */
	info->pixmap.blit_x = block_dimensions(GS_PSMT4_BLOCK_WIDTH, 4);
	info->pixmap.blit_y = block_dimensions(GS_PSMT4_BLOCK_HEIGHT, 1);

	/* 8x8 default font tile size for fb_get_tilemax */
	par->cb.tile = cb_tile(8, 8);

	return 0;
}

static int ps2fb_probe(struct platform_device *pdev)
{
	struct ps2fb_par *par;
	struct fb_info *info;
	int err;

	info = framebuffer_alloc(sizeof(*par), &pdev->dev);
	if (info == NULL) {
		dev_err(&pdev->dev, "framebuffer_alloc failed\n");
		err = -ENOMEM;
		goto err_framebuffer_alloc;
	}

	par = info->par;

	spin_lock_init(&par->lock);

	par->package.buffer = (union package *)__get_free_page(GFP_DMA);
	if (!par->package.buffer) {
		dev_err(&pdev->dev, "Failed to allocate package buffer\n");
		err = -ENOMEM;
		goto err_package_buffer;
	}
	par->package.capacity = PAGE_SIZE / sizeof(union package);

	fill_modes(&pdev->dev, &info->modelist);

	strlcpy(info->fix.id, "PS2 GS", ARRAY_SIZE(info->fix.id));
	info->fix.accel = FB_ACCEL_PLAYSTATION_2;

	err = init_console_buffer(pdev, info);
	if (err < 0)
		goto err_init_buffer;

	fb_info(info, "Mode option is \"%s\"\n", mode_option);

	info->var = (struct fb_var_screeninfo) { };
	if (!fb_find_mode(&info->var, info, mode_option,
			standard_modes, ARRAY_SIZE(standard_modes), NULL, 32)) {
		fb_err(info, "Failed to find video mode \"%s\"\n",
			mode_option);
		err = -EINVAL;
		goto err_find_mode;
	}

	info->mode = &par->mode;

	if (register_framebuffer(info) < 0) {
		fb_err(info, "register_framebuffer failed\n");
		err = -EINVAL;
		goto err_register_framebuffer;
	}

	platform_set_drvdata(pdev, info);

	return 0;

err_register_framebuffer:
err_find_mode:
err_init_buffer:
	free_page((unsigned long)par->package.buffer);
err_package_buffer:
	framebuffer_release(info);
err_framebuffer_alloc:
	return err;
}

static int ps2fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct ps2fb_par *par = info->par;
	int err = 0;

	if (info != NULL) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	if (!gif_wait()) {
		fb_err(info, "Failed to complete GIF DMA transfer\n");
		err = -EBUSY;
	}
	free_page((unsigned long)par->package.buffer);

	return err;
}

static struct platform_driver ps2fb_driver = {
	.probe		= ps2fb_probe,
	.remove		= ps2fb_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

static struct platform_device *ps2fb_device;

static int __init ps2fb_init(void)
{
	int err;

#ifndef MODULE
	char *options = NULL;
	char *this_opt;

	if (fb_get_options(DEVICE_NAME, &options))
		return -ENODEV;
	if (!options || !*options)
		goto no_options;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "mode_option:", 12))
			mode_option = &this_opt[12];
		else if ('0' <= this_opt[0] && this_opt[0] <= '9')
			mode_option = this_opt;
		else
			pr_warn(DEVICE_NAME ": Unrecognized option \"%s\"\n",
				this_opt);
	}

no_options:
#endif /* !MODULE */

	/* Default to a suitable PAL or NTSC broadcast mode. */
	if (!mode_option)
		mode_option = gs_region_pal() ? "576x460i@50" : "576x384i@60";

	ps2fb_device = platform_device_alloc("ps2fb", 0);
	if (!ps2fb_device)
		return -ENOMEM;

	err = platform_device_add(ps2fb_device);
	if (err < 0) {
		platform_device_put(ps2fb_device);
		return err;
	}

	return platform_driver_register(&ps2fb_driver);
}

static void __exit ps2fb_exit(void)
{
	platform_driver_unregister(&ps2fb_driver);
	platform_device_unregister(ps2fb_device);
}

module_init(ps2fb_init);
module_exit(ps2fb_exit);

module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option,
	"Specify initial video mode as \"<xres>x<yres>[-<bpp>][@<refresh>]\"");

MODULE_DESCRIPTION("PlayStation 2 frame buffer driver");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
