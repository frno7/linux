// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 frame buffer driver
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_crtc_helper.h>

#include <asm/io.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gif.h>
#include <uapi/asm/gs.h>

#include "gs_connector.h"
#include "gs_drm.h"
#include "gs_environment.h"
#include "gs_mode.h"
#include "gs_pan.h"
#include "gs_pipe.h"
#include "gs_tile.h"

#define DEVICE_NAME "gs-drm"

/* Module parameters */
static char *video_option;
#if 0
static char *mode_margin = "";
#endif

struct ps2fb_par *info_to_ps2par(const struct fb_info *info)
{
#if 0
	struct drm_fb_helper *helper;
	struct drm_device *dev;
	struct gs_device *gs;

	printk("drm: %s info: %lx\n", __func__, (unsigned long)info);
	helper = info->par;
	printk("drm: %s helper: %lx\n", __func__, (unsigned long)helper);
	dev = helper->dev;
	printk("drm: %s dev: %lx\n", __func__, (unsigned long)dev);
	gs = dev->dev_private;
	printk("drm: %s gs: %lx\n", __func__, (unsigned long)gs);

	return &gs->ps2par;
#else
	struct drm_fb_helper *helper = info->par;
	struct drm_device *dev = helper->dev;
	struct gs_device *gs = dev->dev_private;

	return &gs->ps2par;
#endif
}

/**
 * gs_var_to_fbw - frame buffer width for a given virtual x resolution
 * @var: screen info object to compute FBW for
 *
 * Return: frame buffer width (FBW) in 64-pixel unit
 */
u32 gs_var_to_fbw(const struct fb_var_screeninfo *var)
{
	/*
	 * Round up to nearest GS_FB_PAGE_WIDTH (64 px) since there are
	 * valid resolutions such as 720 px that do not divide 64 properly.
	 */
	return (var->xres_virtual + GS_FB_PAGE_WIDTH - 1) / GS_FB_PAGE_WIDTH;
}

/**
 * gs_var_to_psm - frame buffer pixel storage mode for a given bits per pixel
 * @var: screen info object to compute PSM for
 *
 * Return: frame buffer pixel storage mode
 */
enum gs_psm gs_var_to_psm(const struct fb_var_screeninfo *var)
{
	if (var->bits_per_pixel == 1)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 16)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 32)
		return gs_psm_ct32;

	DRM_WARN_ONCE("%s: Unsupported bits per pixel %u\n",
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
	const enum gs_psm psm = gs_var_to_psm(var);
	const u32 fbw = gs_var_to_fbw(var);

	if (psm == gs_psm_ct16)
		return gs_psm_ct16_block_count(fbw, var->yres_virtual);
	if (psm == gs_psm_ct32)
		return gs_psm_ct32_block_count(fbw, var->yres_virtual);

	DRM_WARN_ONCE("%s: Unsupported pixel storage mode %u\n",
		__func__, psm);

	return 0;
}

/**
 * gs_color_base_pointer - colour base pointer
 * @info: frame buffer info object
 *
 * Return: block index of the colour palette, that follows the display buffer
 */
u32 gs_color_base_pointer(const struct fb_info *info)
{
	const struct ps2fb_par *par = info_to_ps2par(info);

	return par->cb.block_count;
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

static int gs_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *info)
{
	const struct gs_rgba32 color = {
		.r = red    >> 8,
		.g = green  >> 8,
		.b = blue   >> 8,
		.a = transp >> 8
	};
	struct ps2fb_par *par = info_to_ps2par(info);
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
	struct ps2fb_par *par = info_to_ps2par(info);
	union package * const base_package = par->package.buffer;
	union package *package = base_package;

	if (!gif_wait()) {
		DRM_ERROR("Failed to clear the screen, GIF is busy\n");
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

static int gs_set_par(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;

	printk("drm: %s %u %u %u %u %u %u %u %u %u\n",
		__func__,
		info->var.xres,
		info->var.yres,
		info->var.pixclock,
		info->var.left_margin,
		info->var.right_margin,
		info->var.upper_margin,
		info->var.lower_margin,
		info->var.hsync_len,
		info->var.vsync_len);

	return drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper);
}

static int gs_check_var(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	printk("drm: %s %u %u %u %u %u %u %u %u %u\n",
		__func__,
		var->xres,
		var->yres,
		var->pixclock,
		var->left_margin,
		var->right_margin,
		var->upper_margin,
		var->lower_margin,
		var->hsync_len,
		var->vsync_len);

	return 0;
}

static int init_console_buffer(struct fb_info *info)
{
	static struct fb_ops fbops = {
		.owner		= THIS_MODULE,
		.fb_setcolreg	= gs_fb_setcolreg,
		.fb_blank	= drm_fb_helper_blank,
#if 1
		.fb_set_par	= gs_set_par,
		.fb_check_var	= gs_check_var,
#else
		.fb_set_par	= drm_fb_helper_set_par,	// FIXME: Do directly
		.fb_check_var	= drm_fb_helper_check_var,	// FIXME: Do directly
#endif
	};

	struct ps2fb_par *par = info_to_ps2par(info);

	fb_info(info, "Graphics Synthesizer console frame buffer device\n");

	info->screen_size = 0;
	info->screen_base = NULL;	/* mmap is unsupported by hardware */

	info->fix.smem_start = 0;	/* GS frame buffer is local memory */
	info->fix.smem_len = GS_MEMORY_SIZE;

	strlcpy(info->fix.id, "PS2 GS", ARRAY_SIZE(info->fix.id));

	info->flags = FBINFO_DEFAULT;
	info->fbops = &fbops;

	gs_pan_init(info);
	gs_tile_init(info);

	info->pseudo_palette = par->pseudo_palette;

	return 0;
}

int gs_dumb_create(struct drm_file *file_priv,
	struct drm_device *dev, struct drm_mode_create_dumb *args)
{
	printk("drm: %s\n", __func__);

	return 0;	/* FIXME */
}

static struct drm_driver gs_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC,

	.dumb_create	 = gs_dumb_create,

	.name		 = "gs",
	.desc		 = "PlayStation 2 Graphics Synthesizer",
	.date		 = "20200202",
	.major		 = 1,
	.minor		 = 0,
};

static void gs_destroy_framebuffer(struct drm_framebuffer *framebuffer)
{
	printk("drm: %s\n", __func__);

	drm_framebuffer_cleanup(framebuffer);
}

static int gs_dirty_framebuffer(struct drm_framebuffer *fb,
	struct drm_file *file_priv, unsigned int flags, unsigned int color,
	struct drm_clip_rect *rects, unsigned int num_rects)
{
	printk("drm: %s\n", __func__);

	/* FIXME */

	return 0;
}

static const struct drm_framebuffer_funcs gs_framebuffer_funcs = {
	.destroy = gs_destroy_framebuffer,
	.dirty = gs_dirty_framebuffer,
};

static void fb_helper_fill_fix(struct fb_info *info, u32 pitch, u32 depth)
{
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 1;
	info->fix.ypanstep = 1;
	info->fix.ywrapstep = 0;
	info->fix.accel = FB_ACCEL_PLAYSTATION_2;

	info->fix.line_length = pitch;

	/* FIXME */
	invalidate_palette(info_to_ps2par(info));
	info->fix.xpanstep = info->var.xres_virtual > info->var.xres ? 1 : 0;
	info->fix.ypanstep = info->var.yres_virtual > info->var.yres ? 1 : 0;
	info->fix.ywrapstep = 1;
	info->fix.line_length = info->var.xres_virtual * info->var.bits_per_pixel / 8;
}

static void fb_helper_fill_var(struct fb_info *info,
				   struct drm_fb_helper *fb_helper,
				   uint32_t fb_width, uint32_t fb_height)
{
	struct drm_framebuffer *fb = fb_helper->fb;

	WARN_ON((drm_format_info_block_width(fb->format, 0) > 1) ||
		(drm_format_info_block_height(fb->format, 0) > 1));
	// info->pseudo_palette = fb_helper->pseudo_palette;
	info->var.xres = fb->width;
	info->var.yres = fb->height;
	info->var.xres_virtual = fb->width;
	info->var.yres_virtual = fb->height;
	info->var.bits_per_pixel = fb->format->cpp[0] * 8;
	info->var.accel_flags = FB_ACCELF_TEXT;
	info->var.xoffset = 0;
	info->var.yoffset = 0;
	info->var.activate = FB_ACTIVATE_NOW;

	// FIXME: gs_connector_width_height_to_var(&info->var, fb->width, fb->height);

	/* FIXME */
	info->var.accel_flags = 0;
	info->var.bits_per_pixel = 16;	/* FIXME: For 1920x1080 */

	if (info->var.bits_per_pixel == 16) {
		info->var.red    = (struct fb_bitfield){ .offset =  0, .length = 5 };
		info->var.green  = (struct fb_bitfield){ .offset =  5, .length = 5 };
		info->var.blue   = (struct fb_bitfield){ .offset = 10, .length = 5 };
		info->var.transp = (struct fb_bitfield){ .offset = 15, .length = 1 };
	} else if (info->var.bits_per_pixel == 32) {
		info->var.red    = (struct fb_bitfield){ .offset =  0, .length = 8 };
		info->var.green  = (struct fb_bitfield){ .offset =  8, .length = 8 };
		info->var.blue   = (struct fb_bitfield){ .offset = 16, .length = 8 };
		info->var.transp = (struct fb_bitfield){ .offset = 24, .length = 8 };
	}
}

int gs_fb_probe(struct drm_fb_helper *helper,
	struct drm_fb_helper_surface_size *sizes)
{
	const struct drm_mode_fb_cmd2 mode_cmd = {
		.width = sizes->surface_width,
		.height = sizes->surface_height,
		.pixel_format = DRM_FORMAT_RGBA5551, /* FIXME: drm_mode_legacy_fb_format(
			sizes->surface_bpp, sizes->surface_depth), */
		.pitches =  {
			sizes->surface_width *
				(ALIGN(sizes->surface_bpp, 16) / 8),
		},
	};
	struct drm_device *dev = helper->dev;
	struct gs_device *gs = dev->dev_private;
	int err;

	printk("drm: %s\n", __func__);

	printk("drm: %s ... 1\n", __func__);
	gs->info->par = helper; // FIXME

	drm_helper_mode_fill_fb_struct(dev, &gs->framebuffer, &mode_cmd);
	err = drm_framebuffer_init(dev, &gs->framebuffer, &gs_framebuffer_funcs);
	if (err)
		goto err_framebuffer_init;

	printk("drm: %s ... 2\n", __func__);

	helper->fb = &gs->framebuffer;

	err = init_console_buffer(gs->info);
	printk("drm: %s: init_console_buffer: err %d\n", __func__, err);
	if (err < 0)
		goto err_init_buffer;

	// FIXME: drm_fb_helper_fill_info(gs->info, helper, sizes);
	fb_helper_fill_fix(gs->info, helper->fb->pitches[0], helper->fb->format->depth);
	fb_helper_fill_var(gs->info, helper, sizes->fb_width, sizes->fb_height);

	gs->ps2par.cb.block_count = var_to_block_count(gs->info);
	write_cb_environment(gs->info);
	write_cb_pan_display(&gs->info->var, gs->info);
	clear_screen(gs->info);

	fb_info(gs->info, "%d tiles maximum for %ux%u font\n",
		gs->info->tileops->fb_get_tilemax(gs->info),
		gs->ps2par.cb.tile.width, gs->ps2par.cb.tile.height);

	printk("drm: %s ... 3\n", __func__);

	return 0;

err_init_buffer:
	/* FIXME */
err_framebuffer_init:
	return err;
}

static const struct drm_fb_helper_funcs gs_fb_helper_funcs = {
	.fb_probe = gs_fb_probe,
};

int drm_fb_helper_single_fb_probe(struct drm_fb_helper *fb_helper, int preferred_bpp); // FIXME
void drm_setup_crtcs_fb(struct drm_fb_helper *fb_helper); // FIXME

/*
 * FIXME: Corresponds to drm_fb_helper_initial_config without the problems with
 * register_framebuffer -> do_register_framebuffer -> fb_add_videomode with
 * missing refresh, timings, etc.
 */
static int gs_fb_helper_initial_config(struct drm_fb_helper *fb_helper, int bpp_sel)
{
	struct drm_device *dev = fb_helper->dev;
	struct fb_info *info = fb_helper->fbdev;
	unsigned int width, height;
	int err;

	printk("%s: A\n", __func__);

	mutex_lock(&fb_helper->lock);

	printk("%s: B\n", __func__);

	width = dev->mode_config.max_width;
	height = dev->mode_config.max_height;

	drm_client_modeset_probe(&fb_helper->client, width, height);
	printk("%s: C\n", __func__);
	err = drm_fb_helper_single_fb_probe(fb_helper, bpp_sel);
	printk("%s: D\n", __func__);
	if (err < 0) {
		if (err == -EAGAIN) {
			fb_helper->preferred_bpp = bpp_sel;
			fb_helper->deferred_setup = true;
			err = 0;
		}
		mutex_unlock(&fb_helper->lock);

		return err;
	}
	printk("%s: E\n", __func__);
	drm_setup_crtcs_fb(fb_helper);
	printk("%s: F\n", __func__);

	fb_helper->deferred_setup = false;

	printk("%s: G\n", __func__);

	/* Need to drop locks to avoid recursive deadlock in
	 * register_framebuffer. This is ok because the only thing left to do is
	 * register the fbdev emulation instance in kernel_fb_helper_list. */
	mutex_unlock(&fb_helper->lock);
	printk("%s: H\n", __func__);

	err = register_framebuffer(info);
	if (err) {
		dev_err(dev->dev, "Failed to register framebuffer with %d\n", err);
		return err;
	}
	printk("%s: I\n", __func__);

	dev_info(dev->dev, "fb%d: %s frame buffer device\n",
		 info->node, info->fix.id);

#if 0 /* FIXME */
	mutex_lock(&kernel_fb_helper_lock);
	if (list_empty(&kernel_fb_helper_list))
		register_sysrq_key('v', &sysrq_drm_fb_helper_restore_op);

	list_add(&fb_helper->kernel_fb_list, &kernel_fb_helper_list);
	mutex_unlock(&kernel_fb_helper_lock);
#endif /* FIXME */
	printk("%s: J\n", __func__);

	return 0;
}

static int gs_probe(struct platform_device *pdev)
{
	struct drm_device *dev;
	struct gs_device *gs;
	int err;

	printk("drm: %s\n", __func__);

	gs = kzalloc(sizeof(*gs), GFP_KERNEL);
	if (gs == NULL) {
		dev_err(&pdev->dev, "gs device allocation failed\n");
		err = -ENOMEM;
		goto err_gs_alloc;
	}

	spin_lock_init(&gs->ps2par.lock);

	gs->ps2par.package.buffer = (union package *)__get_free_page(GFP_DMA);
	if (!gs->ps2par.package.buffer) {
		dev_err(&pdev->dev, "Failed to allocate package buffer\n");
		err = -ENOMEM;
		goto err_package_buffer;
	}
	gs->ps2par.package.capacity = PAGE_SIZE / sizeof(union package);

	gs->info = framebuffer_alloc(0, &pdev->dev);
	if (!gs->info) {
		dev_err(&pdev->dev, "framebuffer_alloc failed\n");
		err = -ENOMEM;
		goto err_framebuffer_alloc;
	}
	err = fb_alloc_cmap(&gs->info->cmap, PALETTE_SIZE, 0);
	if (err) {
		dev_err(&pdev->dev, "fb_alloc_cmap failed with %d\n", err);
		goto err_fb_alloc_cmap;
	}
	gs->info->apertures = alloc_apertures(1);
	if (!gs->info->apertures) {
		err = -ENOMEM;
		goto err_alloc_apertures;
	}
	INIT_LIST_HEAD(&gs->info->modelist);
	gs->info->var = (struct fb_var_screeninfo) { };
	gs->info->skip_vt_switch = true;

	dev = &gs->dev;
	err = drm_dev_init(dev, &gs_driver, &pdev->dev);
	if (err)
		goto err_dev_init;
	dev->dev_private = gs;

	gs_mode_config_init(&gs->dev);

	err = gs_connector_init(dev, &gs->connector);
	if (err < 0)
		goto err_connector_init;

	err = gs_pipe_init(gs);
	if (err < 0)
		goto err_pipe_init;

	drm_mode_config_reset(dev);

	/*
	 * The GIF asserts DMA completion interrupts that are uninteresting
	 * because DMA operations are fast enough to busy-wait for.
	 */
	disable_irq(IRQ_DMAC_GIF);

	// FIXME:
	// drm_fb_helper_fbdev_setup ->
	// drm_fb_helper_initial_config ->
	// __drm_fb_helper_initial_config_and_unlock (info->var.pixclock = 0) ->
	// register_framebuffer ->
	// do_register_framebuffer ->
	// fb_add_videomode (missing refresh, timings, etc.)
#if 1
	drm_fb_helper_prepare(dev, &gs->fb_helper, &gs_fb_helper_funcs);
	err = drm_fb_helper_init(dev, &gs->fb_helper, 0);
	if (err < 0) {
		DRM_DEV_ERROR(dev->dev, "drm_fb_helper_init failed with %d\n", err);
		goto err_drm_fb_helper_init;
	}
	gs->fb_helper.fbdev = gs->info;
	// FIXME: Why not DRIVER_ATOMIC? drm_helper_disable_unused_functions(dev);
	err = gs_fb_helper_initial_config(&gs->fb_helper,
		dev->mode_config.preferred_depth);
	// FIXME: err
#else
	err = drm_fb_helper_fbdev_setup(dev,
		&gs->fb_helper, &gs_fb_helper_funcs,
		dev->mode_config.preferred_depth, 1);
	printk("drm: %s: drm_fb_helper_fbdev_setup: err %d\n", __func__, err);
	if (err)
		goto err_fb_helper_fbdev_setup;
#endif

	err = drm_dev_register(dev, 0);
	if (err)
		goto err_dev_register;

	platform_set_drvdata(pdev, gs);

	return 0;

#if 0
err_register_framebuffer:
	fb_dealloc_cmap(&info->cmap);
err_alloc_cmap:
err_find_mode:
err_init_buffer:
#endif
	drm_fb_helper_fbdev_teardown(dev);
err_dev_register:
err_fb_helper_fbdev_setup:
	enable_irq(IRQ_DMAC_GIF);
err_drm_fb_helper_init:
err_pipe_init:
	// FIXME: drm_mode_config_cleanup(dev);
err_connector_init:
	// FIXME: drm_dev_put(dev);
err_dev_init:
	free_page((unsigned long)gs->ps2par.package.buffer);
err_alloc_apertures:
err_fb_alloc_cmap:
err_framebuffer_alloc:
err_package_buffer:
	kfree(gs);
err_gs_alloc:
	return err;
}

static int gs_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);
	struct gs_device *gs = dev->dev_private;
	int err = 0;

	printk("drm: %s\n", __func__);

	if (dev != NULL) {
		drm_fb_helper_fbdev_teardown(dev);
		drm_dev_unregister(dev);
		drm_mode_config_cleanup(dev);
		drm_dev_put(dev);
	}

	if (!gif_wait()) {
		dev_err(dev->dev, "Failed to complete GIF DMA transfer\n");
		err = -EBUSY;
	}
	enable_irq(IRQ_DMAC_GIF);

	free_page((unsigned long)gs->ps2par.package.buffer);

	return err;
}

static struct platform_driver gs_platform_driver = {
	.probe		= gs_probe,
	.remove		= gs_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

static struct platform_device *gs_platform_device;

static int __init gs_drm_init(void)
{
	int err;

	printk("drm: %s\n", __func__);

	/* Default to a suitable PAL or NTSC broadcast mode. */
	if (!video_option)
		video_option = gs_region_pal() ? "576x460@50i" : "576x384@60i";

	gs_platform_device = platform_device_alloc("gs_drm", 0);
	if (!gs_platform_device)
		return -ENOMEM;

	err = platform_device_add(gs_platform_device);
	if (err < 0) {
		platform_device_put(gs_platform_device);
		return err;
	}

	return platform_driver_register(&gs_platform_driver);
}

static void __exit gs_drm_exit(void)
{
	printk("drm: %s\n", __func__);

	platform_driver_unregister(&gs_platform_driver);
	platform_device_unregister(gs_platform_device);
}

module_init(gs_drm_init);
module_exit(gs_drm_exit);

MODULE_DESCRIPTION("PlayStation 2 Graphics Synthesizer");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL v2");
