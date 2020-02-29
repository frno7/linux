// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM tile console
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/kernel.h>

#include <drm/drm_fb_helper.h>
#include <drm/drm_print.h>

#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs.h>

#include "gs_tile.h"

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

	DRM_WARN_ONCE("%s: Unsupported image depth %u\n",
		__func__, image->depth);

	return 0;
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
	const enum gs_psm psm = gs_var_to_psm(var);
	const u32 fbw = gs_var_to_fbw(var);

	if (psm == gs_psm_ct16)
		return gs_psm_ct16_block_address(fbw, block_index);
	if (psm == gs_psm_ct32)
		return gs_psm_ct32_block_address(fbw, block_index);

	DRM_WARN_ONCE("%s: Unsupported pixel storage format %u\n",
		__func__, psm);

	return 0;
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
	const struct ps2fb_par *par = info_to_ps2par(info);

	return block_address_for_index(
		par->cb.block_count + PALETTE_BLOCK_COUNT + block_index, info);
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
	const struct ps2fb_par *par = info_to_ps2par(info);

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
 * write_cb_tile - write console buffer tile to the GIF
 * @tile_index: index of the tile, starting from zero for the first glyph
 * @image: the image of the tile to write
 * @info: frame buffer info object
 */
static void write_cb_tile(const int tile_index,
	const struct fb_image *image, struct fb_info *info)
{
	struct ps2fb_par *par = info_to_ps2par(info);
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
 * gs_cb_texflush - flush texture buffer after palette or texture updates
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
static void gs_cb_texflush(struct fb_info *info)
{
	struct ps2fb_par *par = info_to_ps2par(info);
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
 * gs_settile - set console buffer font tiles
 * @info: frame buffer info object
 * @map: font map use as tiles
*/
static void gs_settile(struct fb_info *info, struct fb_tilemap *map)
{
	const u32 glyph_size = ALIGN(map->width, 8) * map->height / 8;
	struct ps2fb_par *par = info_to_ps2par(info);
	const u8 *font = map->data;
	int i;

	if (!font)
		return;	/* FIXME: Why is fb_settile called with a NULL font? */

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (map->width > GS_PSMT4_BLOCK_WIDTH ||
	    map->height > GS_PSMT4_BLOCK_HEIGHT ||
	    map->depth != 1) {
		DRM_ERROR("Unsupported font parameters: "
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

	gs_cb_texflush(info);
}

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
	struct ps2fb_par *par = info_to_ps2par(info);
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
			.dbp = gs_color_base_pointer(info)
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
	struct ps2fb_par *par = info_to_ps2par(info);
	union package * const base_package = par->package.buffer;
	union package *package = base_package;
	const u32 cbp = gs_color_base_pointer(info);
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
	const int psm = gs_var_to_psm(var);
	const int fbw = gs_var_to_fbw(var);

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
 * gs_copyarea - copy console buffer area
 * @area: area to copy
 * @info: frame buffer info object
 */
void gs_copyarea(const struct fb_copyarea *area, struct fb_info *info)
{
	const enum gs_psm psm = gs_var_to_psm(&info->var);
	struct ps2fb_par *par = info_to_ps2par(info);
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
		DRM_WARN_ONCE("%s: "
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
 * gs_tilecopy - copy console buffer tiles
 * @info: frame buffer info object
 * @area: tile area to copy
 */
static void gs_tilecopy(struct fb_info *info, struct fb_tilearea *area)
{
	const struct ps2fb_par *par = info_to_ps2par(info);
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

	gs_copyarea(&a, info);
}

/**
 * gs_tilefill - tile fill operation
 * @info: frame buffer info object
 * @rect: rectangle to fill with tiles
 */
static void gs_tilefill(struct fb_info *info, struct fb_tilerect *rect)
{
	struct ps2fb_par *par = info_to_ps2par(info);
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	spin_lock_irqsave(&par->lock, flags);

	write_tilefill(info, *rect);

	spin_unlock_irqrestore(&par->lock, flags);
}

/**
 * gs_tileblit - tile bit block transfer operation
 * @info: frame buffer info object
 * @blit: tile bit block to transfer
 */
static void gs_tileblit(struct fb_info *info, struct fb_tileblit *blit)
{
	struct ps2fb_par *par = info_to_ps2par(info);
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

static void gs_tilecursor(struct fb_info *info,
	struct fb_tilecursor *cursor)
{
	/*
	 * FIXME: Drawing the cursor seems to require composition such as
	 * xor, which is not possible here. If the character under the
	 * cursor was known, a simple change of foreground and background
	 * colors could be implemented to achieve the same effect.
	 */
}

/**
 * gs_get_tilemax - maximum number of tiles
 * @info: frame buffer info object
 *
 * Return: the maximum number of tiles
 */
static int gs_get_tilemax(struct fb_info *info)
{
	const struct ps2fb_par *par = info_to_ps2par(info);
	const u32 block_tile_count =
		par->cb.tile.block.cols *
		par->cb.tile.block.rows;
	const s32 blocks_available =
		GS_BLOCK_COUNT - par->cb.block_count - PALETTE_BLOCK_COUNT;

	return blocks_available > 0 ? blocks_available * block_tile_count : 0;
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

static struct fb_tile_ops gs_tileops = {
	.fb_settile	= gs_settile,
	.fb_tilecopy	= gs_tilecopy,
	.fb_tilefill    = gs_tilefill,
	.fb_tileblit    = gs_tileblit,
	.fb_tilecursor  = gs_tilecursor,
	.fb_get_tilemax = gs_get_tilemax
};

void gs_tile_init(struct fb_info *info)
{
	struct ps2fb_par *par = info_to_ps2par(info);

	info->tileops = &gs_tileops;

	info->flags |= FBINFO_MISC_TILEBLITTING |
		       FBINFO_HWACCEL_COPYAREA  |
		       FBINFO_HWACCEL_FILLRECT  |
		       FBINFO_HWACCEL_IMAGEBLIT |
		       FBINFO_READS_FAST;

	/*
	 * BITBLTBUF for pixel format CT32 requires divisibility by 2,
	 * and CT16 requires divisibility by 4. So 4 is a safe choice.
	 */
	info->pixmap.blit_x = block_dimensions(GS_PSMT4_BLOCK_WIDTH, 4);
	info->pixmap.blit_y = block_dimensions(GS_PSMT4_BLOCK_HEIGHT, 1);

	/* 8x8 default font tile size for fb_get_tilemax */
	par->cb.tile = cb_tile(8, 8);
}
