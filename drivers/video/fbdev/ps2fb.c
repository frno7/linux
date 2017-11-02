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

static u32 block_dimensions(u32 dim, u32 alignment)
{
	u32 mask = 0;
	u32 d;

	for (d = 1; d <= dim; d++)
		if (d % alignment == 0)
			mask |= 1 << (d - 1);

	return mask;
}

static int init_console_buffer(struct platform_device *pdev,
	struct fb_info *info)
{
	static struct fb_ops fbops = {
		.owner		= THIS_MODULE,
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

	strlcpy(info->fix.id, "PS2 GS", ARRAY_SIZE(info->fix.id));
	info->fix.accel = FB_ACCEL_PLAYSTATION_2;

	err = init_console_buffer(pdev, info);
	if (err < 0)
		goto err_init_buffer;

	info->mode = &par->mode;

	if (register_framebuffer(info) < 0) {
		fb_err(info, "register_framebuffer failed\n");
		err = -EINVAL;
		goto err_register_framebuffer;
	}

	platform_set_drvdata(pdev, info);

	return 0;

err_register_framebuffer:
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
