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

#define PALETTE_SIZE 256
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

	struct cb_cursor {
		u32 sx;
		u32 sy;
		u32 dy;
		u32 tw;
		u32 th;
		bool draw;
		struct gs_rgbaq fg_rgbaq;
	} cursor;
};

/**
 * struct ps2fb_par - driver specific structure
 * @lock: spin lock to be taken for all structure operations
 * @mode: frame buffer video mode
 * @pseudo_palette: pseudo palette, used for texture colouring
 * @grayscale: perform grayscale colour conversion if %true
 * @cb: console buffer definition
 * @package: tags and datafor the GIF
 * @package.capacity: maximum number of GIF packages in 16-byte unit
 * @package.buffer: DMA buffer for GIF packages
 */
struct ps2fb_par {
	spinlock_t lock;

	struct fb_videomode mode;
	struct gs_rgba32 pseudo_palette[PALETTE_SIZE];
	bool grayscale;

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
 * console_pseudo_palette - Graphics Synthesizer RGBA for a palette index
 * @regno: pseudo palette index number
 * @par: driver specific object
 *
 * Return: RGBA colour object for the Graphics Synthesizer
 */
static struct gs_rgbaq console_pseudo_palette(const u32 regno,
	const struct ps2fb_par *par)
{
	const struct gs_rgba32 c = regno < PALETTE_SIZE ?
		par->pseudo_palette[regno] : (struct gs_rgba32) { };
	const u32 a = (c.a + 1) / 2;	/* 0x80 = GS_ALPHA_ONE = 1.0 */

	if (par->grayscale) {
		/*
		 * Construct luminance Y' = 0.299R' + 0.587G' + 0.114B' with
		 * fixed-point integer arithmetic, where 77 + 150 + 29 = 256.
		 */
		const u32 y = (c.r*77 + c.g*150 + c.b*29) >> 8;

		return (struct gs_rgbaq) { .r = y, .g = y, .b = y, .a = a };
	}

	return (struct gs_rgbaq) { .r = c.r, .g = c.g, .b = c.b, .a = a };
}

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
 * block_address_for_index - frame buffer block address for given block index
 * @block_index: index of block to compute the address of
 * @info: frame buffer info object
 *
 * Return: block address, or zero for unsupported pixel storage modes
 */
static u32 block_address_for_index(const u32 block_index,
	const struct fb_info *info)
{
	const struct fb_var_screeninfo *var = &info->var;
	const enum gs_psm psm = var_to_psm(var, info);
	const u32 fbw = var_to_fbw(var);

	if (psm == gs_psm_ct16)
		return gs_psm_ct16_block_address(fbw, block_index);
	if (psm == gs_psm_ct32)
		return gs_psm_ct32_block_address(fbw, block_index);

	fb_warn_once(info, "%s: Unsupported pixel storage format %u\n",
		__func__, psm);
	return 0;
}

/**
 * color_base_pointer - colour base pointer
 * @info: frame buffer info object
 *
 * Return: block index of the colour palette, that follows the display buffer
 */
static u32 color_base_pointer(const struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;

	return par->cb.block_count;
}

/**
 * texture_address_for_index - frame buffer texture address for given index
 * @block_index: index of block to compute the address of
 * @info: frame buffer info object
 *
 * Return: texture address, or zero for unsupported pixel storage modes
 */
static u32 texture_address_for_index(const u32 block_index,
	const struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;

	return block_address_for_index(
		par->cb.block_count + PALETTE_BLOCK_COUNT + block_index,
		info);
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
 * texture_for_tile - texture base pointer and texel coordinates for tile index
 * @tile_index: index of tile to compute the texture for
 * @info: frame buffer info object
 *
 * Returns texture base pointer and texel coordinates
 */
static struct tile_texture texture_for_tile(const u32 tile_index,
	const struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;

	const u32 texture_tile_count =
		par->cb.tile.block.cols * par->cb.tile.block.rows;
	const u32 block_tile = tile_index / texture_tile_count;
	const u32 texture_tile = tile_index % texture_tile_count;
	const u32 block_address = texture_address_for_index(block_tile, info);

	const u32 row = texture_tile / par->cb.tile.block.cols;
	const u32 col = texture_tile % par->cb.tile.block.cols;

	return (struct tile_texture) {
		.tbp	= block_address,
		.u	= col * par->cb.tile.width2,
		.v	= row * par->cb.tile.height2
	};
}

/**
  * valid_bitbltbuf_width - is the BITBLTBUF width valid?
  * @width: width in pixels to check
  * @psm: pixel storage mode
  *
  * In local-to-local BITBLTBUF transmissions, the following restrictions
  * to the TRXREG register width field apply depending on the pixel storage
  * mode[1]:
  *
  * - multiple of 2: PSMCT32, PSMZ32
  * - multiple of 4: PSMCT16, PSMCT16S, PSMZ16, PSMZ16S
  * - multiple of 8: PSMCT24, PSMZ24, PSMT8, PSMT8H, PSMT4, PSMT4HL, PSMT4HH
  *
  * Return: %true if the given width is valid, otherwise %false
  */
static bool valid_bitbltbuf_width(int width, enum gs_psm psm)
{
	if (width < 1)
		return false;
	if (psm == gs_psm_ct32 && (width & 1) != 0)
		return false;
	if (psm == gs_psm_ct16 && (width & 3) != 0)
		return false;

	return true;
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
		.addr = gs_addr_alpha_1,
		.data.alpha_1 = {
			.a = gs_alpha_a_cs,
			.b = gs_alpha_b_cd,
			.c = gs_alpha_c_fix,
			.d = gs_alpha_d_0,
			.fix = GS_ALPHA_ONE
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
 * package_copyarea - package copy area tags and data for the GIF
 * @package: DMA buffer to put packages in
 * @area: area to copy
 * @info: frame buffer info object
 *
 * Return: number of generated GIF packages in 16-byte unit
 */
static size_t package_copyarea(union package *package,
	const struct fb_copyarea *area, const struct fb_info *info)
{
	const struct fb_var_screeninfo *var = &info->var;
	union package * const base_package = package;
	const int psm = var_to_psm(var, info);
	const int fbw = var_to_fbw(var);

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = {
			.spsm = psm, .sbw = fbw,
			.dpsm = psm, .dbw = fbw
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = {
			.ssax = area->sx, .ssay = area->sy,
			.dsax = area->dx, .dsay = area->dy,
			.dir  = area->dy < area->sy ||
				(area->dy == area->sy && area->dx < area->sx) ?
				gs_trxpos_dir_ul_lr : gs_trxpos_dir_lr_ul
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = {
			.rrw = area->width,
			.rrh = area->height
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_local_to_local }
	};

	return package - base_package;
}

/**
 * ps2fb_cb_copyarea - copy console buffer area
 * @area: area to copy
 * @info: frame buffer info object
 */
void ps2fb_cb_copyarea(const struct fb_copyarea *area, struct fb_info *info)
{
	const enum gs_psm psm = var_to_psm(&info->var, info);
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (area->width < 1 || area->height < 1)
		return;
	if (!valid_bitbltbuf_width(area->width, psm)) {
		/*
		 * Some widths are not entirely supported with BITBLTBUF,
		 * but there will be more graphical glitches by refusing
		 * to proceed. Besides, info->pixmap.blit_x says that
		 * they are unsupported so unless someone wants to have
		 * odd fonts we will not end up here anyway. Warn once
		 * here for the protocol.
		 */
		fb_warn_once(info, "%s: "
			"Unsupported width %u for pixel storage format %u\n",
			__func__, area->width, psm);
	}

	spin_lock_irqsave(&par->lock, flags);

        if (gif_wait()) {
		union package * const base_package = par->package.buffer;
		union package *package = base_package;

		package += package_copyarea(package, area, info);

		gif_write(&base_package->gif, package - base_package);
	}

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 * pixel - background or foreground pixel palette index for given coordinates
 * @image: image to sample pixel from
 * @x: x coordinate, relative to the top left corner
 * @y: y coordinate, relative to the top left corner
 * @info: frame buffer info object
 *
 * The background palette index is given for coordinates outside of the image.
 *
 * Return: background or foreground palette index
 */
static u32 pixel(const struct fb_image * const image,
	const int x, const int y, struct fb_info *info)
{
	if (x < 0 || x >= image->width ||
	    y < 0 || y >= image->height)
		return image->bg_color;

	if (image->depth == 1)
		return (image->data[y*((image->width + 7) >> 3) + (x >> 3)] &
			(0x80 >> (x & 0x7))) ?
			image->fg_color : image->bg_color;

	fb_warn_once(info, "%s: Unsupported image depth %u\n",
		__func__, image->depth);
	return 0;
}

/**
 * ps2fb_cb_texflush - flush texture buffer after palette or texture updates
 * @info: frame buffer info object
 *
 * Before using converted data from host-to-local or local-to-local
 * transmissions as texture or colour lookup tables for the first time, the
 * texture buffer must be disabled with the TEXFLUSH register.
 *
 * The register write waits for the completion of the current drawing process,
 * and then disables the texture data read to the texture page buffer. Any
 * data can be written to it. A drawing process succeeding the write to the
 * register is started after the texture page buffer is disabled.
 */
static void ps2fb_cb_texflush(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	union package * const base_package = par->package.buffer;
	union package *package = base_package;
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	spin_lock_irqsave(&par->lock, flags);

        if (!gif_wait())
		goto timeout;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 1
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_texflush
	};

	gif_write(&base_package->gif, package - base_package);

timeout:
	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 * package_psmt4_texture - package PSMT4 texture tags and data for the GIF
 * @package: DMA buffer to put packages in
 * @image: image to copy
 * @info: frame buffer info object
 *
 * Return: number of generated GIF packages in 16-byte unit
 */
static size_t package_psmt4_texture(union package *package,
	const struct fb_image *image, struct fb_info *info)
{
	union package * const base_package = package;
	const u32 width2 = texture_least_power_of_2(image->width);
	const u32 height2 = texture_least_power_of_2(image->height);
	const u32 texels_per_quadword = 32;	/* PSMT4 are 4 bit texels */
	const u32 nloop = (width2 * height2 + texels_per_quadword - 1) /
		texels_per_quadword;
	u32 x, y;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_image_mode,
		.nloop = nloop,
		.eop = 1
	};

	for (y = 0; y < height2; y++)
	for (x = 0; x < width2; x += 2) {
		const int p0 = pixel(image, x + 0, y, info);
		const int p1 = pixel(image, x + 1, y, info);
		const int i = 4*y + x/2;

		package[i/16].gif.image[i%16] =
			(p1 ? 0x10 : 0) | (p0 ? 0x01 : 0);
	}

	package += nloop;

	return package - base_package;
}

/**
 * write_cb_tile - write console buffer tile to the GIF
 * @tile_index: index of the tile, starting from zero for the first glyph
 * @image: the image of the tile to write
 * @info: frame buffer info object
 */
static void write_cb_tile(const int tile_index,
	const struct fb_image *image, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	const struct tile_texture tt = texture_for_tile(tile_index, info);
	union package * const base_package = par->package.buffer;
	union package *package = base_package;

        if (!gif_wait())
		return;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = {
			.dpsm = gs_psm_t4,
			.dbw = GS_PSMT4_BLOCK_WIDTH / 64,
			.dbp = tt.tbp
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = {
			.dsax = tt.u,
			.dsay = tt.v
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = {
			.rrw = texture_least_power_of_2(image->width),
			.rrh = texture_least_power_of_2(image->height)
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_host_to_local }
	};

	package += package_psmt4_texture(package, image, info);

	gif_write(&base_package->gif, package - base_package);
}

/**
 * ps2fb_cb_settile - set console buffer font tiles
 * @info: frame buffer info object
 * @map: font map use as tiles
*/
static void ps2fb_cb_settile(struct fb_info *info, struct fb_tilemap *map)
{
	const u32 glyph_size = ALIGN(map->width, 8) * map->height / 8;
	struct ps2fb_par *par = info->par;
	const u8 *font = map->data;
	int i;

	if (!font)
		return;	/* FIXME: Why is fb_settile called with a NULL font? */

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (map->width > GS_PSMT4_BLOCK_WIDTH ||
	    map->height > GS_PSMT4_BLOCK_HEIGHT ||
	    map->depth != 1) {
		fb_err(info, "Unsupported font parameters: "
			"width %d height %d depth %d length %d\n",
			map->width, map->height, map->depth, map->length);
		return;
	}

	par->cb.tile = cb_tile(map->width, map->height);

	for (i = 0; i < map->length; i++) {
		const struct fb_image image = {
			.width = map->width,
			.height = map->height,
			.fg_color = 1,
			.bg_color = 0,
			.depth = 1,
			.data = &font[i * glyph_size],
		};
		unsigned long flags;

		spin_lock_irqsave(&par->lock, flags);
		write_cb_tile(i, &image, info);
		spin_unlock_irqrestore(&par->lock, flags);
	}

	ps2fb_cb_texflush(info);
}

/**
 * package_palette - package palette tags and data for the GIF
 * @package: DMA buffer to put packages in
 * @bg: background colour palette index
 * @fg: foreground colour palette index
 * @info: frame buffer info object
 *
 * Return: number of generated GIF packages in 16-byte unit
 */
static size_t package_palette(union package *package,
	const int bg, const int fg, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	union package * const base_package = par->package.buffer;
	const struct gs_rgbaq bg_rgbaq = console_pseudo_palette(bg, par);
	const struct gs_rgbaq fg_rgbaq = console_pseudo_palette(fg, par);

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = {
			.dpsm = gs_psm_ct32,
			.dbw = 1,	/* Palette is one block wide */
			.dbp = color_base_pointer(info)
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = {
			.dsax = 0,
			.dsay = 0
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = {
			.rrw = 2,	/* Background and foreground color */
			.rrh = 1
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_host_to_local }
	};

	GIF_PACKAGE_TAG(package) {
		.flg = gif_image_mode,
		.nloop = 1,
		.eop = 1
	};
	package->gif.rgba32[0] = (struct gs_rgba32) {
		.r = bg_rgbaq.r,
		.g = bg_rgbaq.g,
		.b = bg_rgbaq.b,
		.a = bg_rgbaq.a
	};
	package->gif.rgba32[1] = (struct gs_rgba32) {
		.r = fg_rgbaq.r,
		.g = fg_rgbaq.g,
		.b = fg_rgbaq.b,
		.a = fg_rgbaq.a
	};
	package++;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 1
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_texflush
	};

	return package - base_package;
}

/**
 * write_tilefill - write console buffer tile fill operation to the GIF
 * @info: frame buffer info object
 * @rect: rectangle to fill with tiles
 */
static void write_tilefill(struct fb_info *info, const struct fb_tilerect rect)
{
	const struct tile_texture tt = texture_for_tile(rect.index, info);
	struct ps2fb_par *par = info->par;
	union package * const base_package = par->package.buffer;
	union package *package = base_package;
	const u32 cbp = color_base_pointer(info);
	const u32 dsax = par->cb.tile.width * rect.sx;
	const u32 dsay = par->cb.tile.height * rect.sy;
	const u32 rrw = par->cb.tile.width * rect.width;
	const u32 rrh = par->cb.tile.height * rect.height;
	const u32 tw2 = par->cb.tile.width2;
	const u32 th2 = par->cb.tile.height2;

	/* Determine whether background or foreground color needs update. */
	const bool cld = (par->cb.bg != rect.bg || par->cb.fg != rect.fg);

        if (!gif_wait())
		return;

	if (cld) {
		package += package_palette(package, rect.bg, rect.fg, info);
		par->cb.bg = rect.bg;
		par->cb.fg = rect.fg;
	}

	GIF_PACKAGE_TAG(package) {
		.flg = gif_reglist_mode,
		.reg0 = gif_reg_prim,
		.reg1 = gif_reg_nop,
		.reg2 = gif_reg_tex0_1,
		.reg3 = gif_reg_clamp_1,
		.reg4 = gif_reg_uv,
		.reg5 = gif_reg_xyz2,
		.reg6 = gif_reg_uv,
		.reg7 = gif_reg_xyz2,
		.nreg = 8,
		.nloop = 1,
		.eop = 1
	};
	GIF_PACKAGE_REG(package) {
		.lo.prim = {
			.prim = gs_sprite,
			.tme = gs_texturing_on,
			.fst = gs_texturing_uv
		}
	};
	GIF_PACKAGE_REG(package) {
		.lo.tex0 = {
			.tbp0 = tt.tbp,
			.tbw = GS_PSMT4_BLOCK_WIDTH / 64,
			.psm = gs_psm_t4,
			.tw = 5,	/* 2^5 = 32 texels wide PSMT4 block */
			.th = 4,	/* 2^4 = 16 texels high PSMT4 block */
			.tcc = gs_tcc_rgba,
			.tfx = gs_tfx_decal,
			.cbp = cbp,
			.cpsm = gs_psm_ct32,
			.csm = gs_csm1,
			.cld = cld ? 1 : 0
		},
		.hi.clamp_1 = {
			.wms = gs_clamp_region_repeat,
			.wmt = gs_clamp_region_repeat,
			.minu = tw2 - 1,  /* Mask, tw is always a power of 2 */
			.maxu = tt.u,
			.minv = th2 - 1,  /* Mask, th is always a power of 2 */
			.maxv = tt.v
		}
	};
	GIF_PACKAGE_REG(package) {
		.lo.uv = {
			.u = gs_pxcs_to_tcs(tt.u),
			.v = gs_pxcs_to_tcs(tt.v)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(dsax),
			.y = gs_fbcs_to_pcs(dsay)
		}
	};
	GIF_PACKAGE_REG(package) {
		.lo.uv = {
			.u = gs_pxcs_to_tcs(tt.u + rrw),
			.v = gs_pxcs_to_tcs(tt.v + rrh)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(dsax + rrw),
			.y = gs_fbcs_to_pcs(dsay + rrh)
		}
	};

	gif_write(&base_package->gif, package - base_package);
}

/**
 * ps2fb_cb_tilecopy - copy console buffer tiles
 * @info: frame buffer info object
 * @area: tile area to copy
 */
static void ps2fb_cb_tilecopy(struct fb_info *info, struct fb_tilearea *area)
{
	const struct ps2fb_par *par = info->par;
	const u32 tw = par->cb.tile.width;
	const u32 th = par->cb.tile.height;
	const struct fb_copyarea a = {
		.dx	= tw * area->dx,
		.dy	= th * area->dy,
		.width	= tw * area->width,
		.height	= th * area->height,
		.sx	= tw * area->sx,
		.sy	= th * area->sy
	};

	ps2fb_cb_copyarea(&a, info);
}

/**
 * ps2fb_cb_tilefill - tile fill operation
 * @info: frame buffer info object
 * @rect: rectangle to fill with tiles
 */
static void ps2fb_cb_tilefill(struct fb_info *info, struct fb_tilerect *rect)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	spin_lock_irqsave(&par->lock, flags);

	write_tilefill(info, *rect);

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 * ps2fb_cb_tileblit - tile bit block transfer operation
 * @info: frame buffer info object
 * @blit: tile bit block to transfer
 */
static void ps2fb_cb_tileblit(struct fb_info *info, struct fb_tileblit *blit)
{
	struct ps2fb_par *par = info->par;
	int i = 0, dx, dy;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	/*
	 * Note: This could be done faster, possibly often as a single
	 * set of GIF packages.
	 */
	for (dy = 0; i < blit->length && dy < blit->height; dy++)
	for (dx = 0; i < blit->length && dx < blit->width; dx++, i++) {
		unsigned long flags;

		spin_lock_irqsave(&par->lock, flags);

		write_tilefill(info, (struct fb_tilerect) {
			.sx = blit->sx + dx,
			.sy = blit->sy + dy,
			.width = 1,
			.height = 1,
			.index = blit->indices[i],
			.fg = blit->fg,
			.bg = blit->bg
		});

		spin_unlock_irqrestore(&par->lock, flags);
	}
}

static size_t package_cb_tilecursor(union package *package,
	const struct cb_cursor *c)
{

	union package * const base_package = package;
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
		.lo.prim = {
			.abe = gs_blendning_on,
			.prim = gs_sprite
		},
		.hi.rgbaq = c->fg_rgbaq
	};
	GIF_PACKAGE_REG(package) {
		.lo.xyz2 = {
			.x = gs_fbcs_to_pcs(c->sx),
			.y = gs_fbcs_to_pcs(c->sy + c->th - c->dy)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(c->sx + c->tw),
			.y = gs_fbcs_to_pcs(c->sy + c->th)
		}
	};

	return package - base_package;
}

static void write_cb_tilecursor(struct ps2fb_par *par,
	const struct fb_var_screeninfo *var,
	const struct fb_tilecursor *cursor)
{
	union package * const base_package = par->package.buffer;
	union package *package = base_package;
	const u32 tw = par->cb.tile.width;
	const u32 th = par->cb.tile.height;
	const u32 ch =
		cursor->shape == FB_TILE_CURSOR_NONE        ? 0 :
		cursor->shape == FB_TILE_CURSOR_UNDERLINE   ? 1 :
		cursor->shape == FB_TILE_CURSOR_LOWER_THIRD ? th / 3 :
		cursor->shape == FB_TILE_CURSOR_LOWER_HALF  ? th / 2 :
		cursor->shape == FB_TILE_CURSOR_TWO_THIRDS  ? (2 * th) / 3 :
		cursor->shape == FB_TILE_CURSOR_BLOCK       ? th : th;
	const struct cb_cursor c = {
		.sx = (var->xoffset + tw * cursor->sx) % var->xres_virtual,
		.sy = (var->yoffset + th * cursor->sy) % var->yres_virtual,
		.dy = ch,
		.tw = tw,
		.th = th,
		.draw = cursor->mode,
		.fg_rgbaq = console_pseudo_palette(cursor->fg, par)
	};

	if (!gif_wait())
		return;

	if (par->cb.cursor.draw)
		package += package_cb_tilecursor(package, &par->cb.cursor);

	if (c.draw)
		package += package_cb_tilecursor(package, &c);

	par->cb.cursor = c;

	gif_write(&base_package->gif, package - base_package);
}

static void ps2fb_cb_tilecursor(struct fb_info *info,
	struct fb_tilecursor *cursor)
{
	const struct fb_var_screeninfo *var = &info->var;
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	spin_lock_irqsave(&par->lock, flags);

	write_cb_tilecursor(par, var, cursor);

	spin_unlock_irqrestore(&par->lock, flags);
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
 * invalidate_palette - set invalid palette indices to force update
 * @par: driver specific object
 *
 * The background and foreground palette indices will mismatch in the next
 * tile file, and thereby force a palette update.
 */
static void invalidate_palette(struct ps2fb_par *par)
{
	par->cb.bg = ~0;
	par->cb.fg = ~0;
}

static int ps2fb_setcolreg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *info)
{
	const struct gs_rgba32 color = {
		.r = red    >> 8,
		.g = green  >> 8,
		.b = blue   >> 8,
		.a = transp >> 8
	};
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	if (regno >= PALETTE_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&par->lock, flags);

	par->pseudo_palette[regno] = color;
	invalidate_palette(par);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
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
 * write_cb_pan_display - write console buffer pan operation to the GIF
 * @var: screen info object
 * @info: frame buffer info object
 *
 * XPAN, YPAN and YWRAP are hardware accelerated. YWRAP is implemented using
 * the two independent rectangular area read output circuits of the Graphics
 * Synthesizer. The DISPLAY1 register outputs the upper part and the DISPLAY2
 * register outputs the lower part.
 */
static void write_cb_pan_display(const struct fb_var_screeninfo *var,
	const struct fb_info *info)
{
	const struct gs_display display = gs_read_display1();
	const int psm = var_to_psm(var, info);
	const int fbw = var_to_fbw(var);
	const int yo = var->yoffset % var->yres_virtual;
	const int dh1 = min_t(int, var->yres_virtual - yo, var->yres);
	const int dh2 = var->yres - dh1;

	GS_WRITE_DISPLAY1(
		.dh = dh1 - 1,
		.dw = display.dw,
		.magv = display.magv,
		.magh = display.magh,
		.dy = display.dy,
		.dx = display.dx,
	);

	GS_WRITE_DISPLAY2(
		.dh = dh2 - 1,
		.dw = display.dw,
		.magv = display.magv,
		.magh = display.magh,
		.dy = display.dy + dh1,
		.dx = display.dx,
	);

	GS_WRITE_DISPFB1(
		.fbw = fbw,
		.psm = psm,
		.dbx = var->xoffset,
		.dby = yo
	);

	GS_WRITE_DISPFB2(
		.fbw = fbw,
		.psm = psm,
		.dbx = var->xoffset,
		.dby = 0,
	);

	GS_WRITE_PMODE(
		.en1 = 1,
		.en2 = dh2 ? 1 : 0,
		.crtmd = 1
	);
}

/**
 * changed_cb_pan_display - is display panning needed?
 * @var: screen info object
 *
 * Panning is undefined until the DISPFB1 register has been set.
 *
 * Return: %true if the horizontal or vertical panning is needed, otherwise
 *	%false
 */
static bool changed_cb_pan_display(const struct fb_var_screeninfo *var)
{
	if (gs_valid_dispfb1()) {
		const struct gs_dispfb dspfb12 = gs_read_dispfb1();
		const int yo = var->yoffset % var->yres_virtual;

		return dspfb12.dbx != var->xoffset || dspfb12.dby != yo;
	}

	return false;	/* DISPFB1 is incomparable before video mode is set. */
}

/**
 * ps2fb_cb_pan_display - pan the display
 * @var: screen info object
 * @info: frame buffer info object
 *
 * Return: 0 on success, otherwise a negative error number
 */
static int ps2fb_cb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);

	if (changed_cb_pan_display(var))
		write_cb_pan_display(var, info);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
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

        /* Check virtual resolution. */
        if (var->xres_virtual < var->xres)
           var->xres_virtual = var->xres;
        if (var->yres_virtual < var->yres)
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
	par->grayscale = (var->grayscale == 1);
	invalidate_palette(par);

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.xpanstep = var->xres_virtual > var->xres ? 1 : 0;
	info->fix.ypanstep = var->yres_virtual > var->yres ? 1 : 0;
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
		write_cb_pan_display(&info->var, info);

		clear_screen(info);
	}

	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		fb_info(info, "%d tiles maximum for %ux%u font\n",
			info->tileops->fb_get_tilemax(info),
			par->cb.tile.width, par->cb.tile.height);

	return err;
}

/**
 * ps2fb_blank - VESA display power management signaling (DPMS)
 * @blank: frame buffer blanking level
 * @info: frame buffer info object
 *
 * Return: 0 on success, otherwise a negative error number
 */
static int ps2fb_blank(int blank, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	struct gs_smode2 smode2;
	unsigned long flags;

	if (!gs_valid_smode2())
		return -EIO;	/* Blanking before video mode is set. */

	spin_lock_irqsave(&par->lock, flags);

	smode2 = gs_read_smode2();

	smode2.dpms = blank == FB_BLANK_POWERDOWN     ? gs_dpms_off :
		      blank == FB_BLANK_NORMAL        ? gs_dpms_standby :
		      blank == FB_BLANK_VSYNC_SUSPEND ? gs_dpms_suspend :
		      blank == FB_BLANK_HSYNC_SUSPEND ? gs_dpms_suspend :
							gs_dpms_on;
	gs_write_smode2(smode2);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
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
		.fb_blank	= ps2fb_blank,
		.fb_setcolreg	= ps2fb_setcolreg,
		.fb_set_par	= ps2fb_cb_set_par,
		.fb_check_var	= ps2fb_cb_check_var,
		.fb_pan_display	= ps2fb_cb_pan_display,
	};

	static struct fb_tile_ops tileops = {
		.fb_settile	= ps2fb_cb_settile,
		.fb_tilecopy	= ps2fb_cb_tilecopy,
		.fb_tilefill    = ps2fb_cb_tilefill,
		.fb_tileblit    = ps2fb_cb_tileblit,
		.fb_tilecursor  = ps2fb_cb_tilecursor,
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
		      FBINFO_HWACCEL_COPYAREA |
		      FBINFO_HWACCEL_FILLRECT |
		      FBINFO_HWACCEL_IMAGEBLIT |
		      FBINFO_HWACCEL_XPAN |
		      FBINFO_HWACCEL_YPAN |
		      FBINFO_HWACCEL_YWRAP |
		      FBINFO_PARTIAL_PAN_OK |
		      FBINFO_READS_FAST;

	info->flags |= FBINFO_MISC_TILEBLITTING;
	info->tileops = &tileops;

	/*
	 * BITBLTBUF for pixel format CT32 requires divisibility by 2,
	 * and CT16 requires divisibility by 4. So 4 is a safe choice.
	 */
	info->pixmap.blit_x = block_dimensions(GS_PSMT4_BLOCK_WIDTH, 4);
	info->pixmap.blit_y = block_dimensions(GS_PSMT4_BLOCK_HEIGHT, 1);

	info->pseudo_palette = par->pseudo_palette;

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

	if (fb_alloc_cmap(&info->cmap, PALETTE_SIZE, 0) < 0) {
		fb_err(info, "fb_alloc_cmap failed\n");
		err = -ENOMEM;
		goto err_alloc_cmap;
	}
	fb_set_cmap(&info->cmap, info);

	/*
	 * The GIF asserts DMA completion interrupts that are uninteresting
	 * because DMA operations are fast enough to busy-wait for.
	 */
	disable_irq(IRQ_DMAC_GIF);

	if (register_framebuffer(info) < 0) {
		fb_err(info, "register_framebuffer failed\n");
		err = -EINVAL;
		goto err_register_framebuffer;
	}

	platform_set_drvdata(pdev, info);

	return 0;

err_register_framebuffer:
	enable_irq(IRQ_DMAC_GIF);
	fb_dealloc_cmap(&info->cmap);
err_alloc_cmap:
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
	enable_irq(IRQ_DMAC_GIF);
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
