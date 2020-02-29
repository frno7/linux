// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM tile console
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_TILE_H
#define GS_TILE_H

#include <drm/drm_fb_helper.h>

#include <asm/mach-ps2/dmac.h>

#include <uapi/asm/gif.h>
#include <uapi/asm/gs.h>

#define PALETTE_SIZE 256	// FIXME
#define PALETTE_BLOCK_COUNT 1	/* One block is used for the indexed colors */

#define GIF_PACKAGE_TAG(package) ((package)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_REG(package) ((package)++)->gif.reg = (struct gif_data_reg)
#define GIF_PACKAGE_AD(package)  ((package)++)->gif.packed.ad = (struct gif_packed_ad)
#define DMA_PACKAGE_TAG(package) ((package)++)->dma = (struct dma_tag)

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
 * @pseudo_palette: pseudo palette, used for texture colouring
 * @grayscale: perform grayscale colour conversion if %true
 * @cb: console buffer definition
 * @package: tags and datafor the GIF
 * @package.capacity: maximum number of GIF packages in 16-byte unit
 * @package.buffer: DMA buffer for GIF packages
 */
struct ps2fb_par {
	spinlock_t lock;

	struct gs_rgba32 pseudo_palette[PALETTE_SIZE];
	bool grayscale;

	struct console_buffer cb;

	struct {
		size_t capacity;
		union package *buffer;
	} package;
};

// FIXME
struct ps2fb_par *info_to_ps2par(const struct fb_info *info);
u32 gs_color_base_pointer(const struct fb_info *info);
u32 gs_var_to_fbw(const struct fb_var_screeninfo *var);
enum gs_psm gs_var_to_psm(const struct fb_var_screeninfo *var);

void gs_tile_init(struct fb_info *info);

#endif /* GS_TILE_H */
