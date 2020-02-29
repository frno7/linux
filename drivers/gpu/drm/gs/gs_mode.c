// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM mode configuration
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/kernel.h>

#include <drm/drm.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_atomic_helper.h>

#include <uapi/asm/gs.h>	/* FIXME */

#include "gs_mode.h"

/**
 * framebuffer_size - framebuffer size in bytes for a given video resolution
 * @width: width of framebuffer in pixels
 * @height: height of framebuffer in pixels
 * @bits_per_pixel: number of bits per pixel
 *
 * This calculation is a lower bound estimate. A precise calculation would have
 * to take memory pages, blocks and column arrangements into account. To choose
 * the appropriate standard video mode such details can be disregarded, though.
 *
 * Return: the size in bytes of the framebuffer
 */
static u32 framebuffer_size(const u32 width, const u32 height,
      const u32 bits_per_pixel)
{
	return (width * height * bits_per_pixel) / 8;
}

/**
 * bits_per_pixel_fits - does the given bits per pixel fit in the framebuffer?
 * @width: width of framebuffer in pixels
 * @height: height of framebuffer in pixels
 * @bits_per_pixel: number of bits per pixel
 * @buffer_size: size in bytes of framebuffer
 *
 * The size calculation is approximate, but accurate enough for the standard
 * video modes.
 *
 * Return: %true if the resolution fits the framebuffer, otherwise %false
 */
static bool bits_per_pixel_fits(const u32 width, const u32 height,
      const u32 bits_per_pixel, const size_t buffer_size)
{
	return framebuffer_size(width, height, bits_per_pixel) <= buffer_size;
}

/**
 * bits_per_pixel_for_format - bits per pixel for the given pixel format
 * @pixel_format: fourcc code pixel format
 *
 * Return: number of bits per pixel, or zero for invalid formats
 */
static u32 bits_per_pixel_for_format(const u32 pixel_format)
{
	return pixel_format == DRM_FORMAT_RGBA8888 ? 32 :	/* RGBA32 */
	       pixel_format == DRM_FORMAT_RGBX8888 ? 32 :	/* RGB24 */
	       pixel_format == DRM_FORMAT_RGBA5551 ? 16 :	/* RGBA16 */
						      0 ;	/* Invalid */
}

/**
 * pixel_format_fits - does the given pixel format fit in the framebuffer?
 * @width: width of framebuffer in pixels
 * @height: height of framebuffer in pixels
 * @pixel_format: fourcc code pixel format
 * @buffer_size: size in bytes of framebuffer
 *
 * The size calculation is approximate, but accurate enough for the standard
 * video modes.
 *
 * Return: %true if the resolution fits the framebuffer, otherwise %false
 */
static bool pixel_format_fits(const u32 width, const u32 height,
      const u32 pixel_format, const size_t buffer_size)
{
	const u32 bits_per_pixel = bits_per_pixel_for_format(pixel_format);

	return bits_per_pixel != 0 &&
		bits_per_pixel_fits(width, height, bits_per_pixel, buffer_size);
}

static struct drm_framebuffer *gs_fb_create(
	struct drm_device *dev, struct drm_file *file_priv,
	const struct drm_mode_fb_cmd2 *mode_cmd)
{
	printk("drm: %s\n", __func__);		/* FIXME */

	if (!pixel_format_fits(mode_cmd->width, mode_cmd->height,
			mode_cmd->pixel_format, GS_MEMORY_SIZE))
		return ERR_PTR(-EINVAL);

	/* FIXME: See drm_fb_helper_alloc_fbi in udlfb_create */

	return drm_gem_fb_create_with_dirty(dev, file_priv, mode_cmd);	/* FIXME */
}

static const struct drm_mode_config_funcs gs_mode_config_funcs = {
	.fb_create = gs_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs gs_mode_config_helpers = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

void gs_mode_config_init(struct drm_device *dev)
{
	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;
	dev->mode_config.preferred_depth = 32;
	dev->mode_config.prefer_shadow = 0;

	dev->mode_config.funcs = &gs_mode_config_funcs;
	dev->mode_config.helper_private = &gs_mode_config_helpers;
}
