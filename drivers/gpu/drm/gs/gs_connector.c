// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM connector
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/device.h>
#include <linux/kernel.h>

#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_connector.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_atomic_state_helper.h>

#include "gs_connector.h"
#include "gs_drm.h"

static const struct fb_videomode standard_modes[] = {
	/* PAL */
	{ "640x256@50", 50, 640, 256, 74074, 100, 61, 34, 22, 63, 2, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "720x288@50", 50, 720, 288, 74074, 70, 11, 19, 3, 63, 3, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "640x512@50i", 50, 640, 512, 74074, 100, 61, 67, 41, 63, 5, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "720x576@50i", 50, 720, 576, 74074, 70, 11, 39, 5, 63, 5, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "720x576@50", 50, 720, 576, 37037, 70, 11, 39, 5, 63, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1280x720@50", 50, 1280, 720, 13468, 220, 400, 19, 6, 80, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1920x1080@50i", 50, 1920, 1080, 13468, 148, 484, 36, 4, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "1920x1080@50", 50, 1920, 1080, 6734, 148, 484, 36, 4, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },

	/* PAL with borders to ensure that the whole screen is visible */
	{ "576x460@50i", 50, 576, 460, 74074, 142, 83, 97, 63, 63, 5, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "576x460@50", 50, 576, 460, 37037, 142, 83, 97, 63, 63, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "1124x644@50", 50, 1124, 644, 13468, 298, 478, 57, 44, 80, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "1688x964@50i", 50, 1688, 964, 13468, 264, 600, 94, 62, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "1688x964@50", 50, 1688, 964, 6734, 264, 600, 94, 62, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },

	/* NTSC */
	{ "640x224@60", 60, 640, 224, 74074, 95, 60, 22, 14, 63, 3, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "720x240@60", 60, 720, 240, 74074, 58, 17, 15, 5, 63, 3, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "640x448@60i", 60, 640, 448, 74074, 95, 60, 44, 27, 63, 6, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "720x480@60i", 60, 720, 480, 74074, 58, 17, 30, 9, 63, 6, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "720x480@60", 60, 720, 480, 37037, 58, 17, 30, 9, 63, 6, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1280x720@60", 60, 1280, 720, 13481, 220, 70, 19, 6, 80, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1920x1080@60i", 60, 1920, 1080, 13481, 148, 44, 36, 4, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "1920x1080@60", 60, 1920, 1080, 6741, 148, 44, 36, 4, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },

	/* NTSC with borders to ensure that the whole screen is visible */
	{ "576x384@60i", 60, 576, 384, 74074, 130, 89, 78, 57, 63, 6, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "576x384@60", 60, 576, 384, 37037, 130, 89, 78, 57, 63, 6, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "1124x644@60", 60, 1124, 644, 13481, 298, 148, 57, 44, 80, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "1688x964@60i", 60, 1688, 964, 13481, 264, 160, 94, 62, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "1688x964@60", 60, 1688, 964, 6741, 264, 160, 94, 62, 88, 5, FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },

	/* VESA */
	{ "640x480@60", 60, 640, 480, 39682,  48, 16, 33, 10, 96, 2, 0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "640x480@75", 75, 640, 480, 31746, 120, 16, 16, 1, 64, 3, 0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "800x600@60", 60, 800, 600, 25000, 88, 40, 23, 1, 128, 4, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "800x600@75", 75, 800, 600, 20202, 160, 16, 21, 1, 80, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "1024x768@60", 60, 1024, 768, 15384, 160, 24, 29, 3, 136, 6, 0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "1024x768@75", 75, 1024, 768, 12690, 176, 16, 28, 1, 96, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "1280x1024@60", 60, 1280, 1024, 9259, 248, 48, 38, 1, 112, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "1280x1024@75", 75, 1280, 1024, 7407, 248, 16, 38, 1, 144, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA }
};

int gs_connector_width_height_to_var(struct fb_var_screeninfo *var,
	unsigned int width, unsigned int height)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(standard_modes); i++) {
		const struct fb_videomode *m = &standard_modes[i];

		if (m->xres == width && m->yres == height &&
		    !(m->vmode & FB_VMODE_INTERLACED)) { // FIXME: Handle interlace properly
			fb_videomode_to_var(var, m);
			return 0;
		}
	}

	return -EINVAL;
}

static int rename_connector(const char *name, struct drm_connector *connector)
{
	char *s = kasprintf(GFP_KERNEL, name);

	if (!s)
		return -ENOMEM;

	kfree(connector->name);
	connector->name = s;

	return 0;
}

static int gs_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct gs_device *gs = dev->dev_private;
	int count = 0;
	int i;

	printk("drm: %s %zu\n", __func__, ARRAY_SIZE(standard_modes));

	for (i = 0; i < ARRAY_SIZE(standard_modes); i++) {
		const struct fb_videomode *s = &standard_modes[i];
		struct drm_display_mode m = {
			.type = DRM_MODE_TYPE_DRIVER,
			.clock = DIV_ROUND_CLOSEST(1000000000, s->pixclock),
			.hdisplay = s->xres,
			.hsync_start = s->xres + s->right_margin,
			.hsync_end = s->xres + s->right_margin + s->hsync_len,
			.htotal = s->xres + s->right_margin + s->hsync_len + s->left_margin,
			.vdisplay = s->yres,
			.vsync_start = s->yres + s->lower_margin,
			.vsync_end = s->yres + s->lower_margin + s->vsync_len,
			.vtotal = s->yres + s->lower_margin + s->vsync_len + s->upper_margin,
			.flags = (s->sync & FB_SYNC_BROADCAST ? DRM_MODE_FLAG_BCAST : 0) |
				 (s->vmode & FB_VMODE_INTERLACED ? DRM_MODE_FLAG_INTERLACE : 0),
		};
		struct drm_display_mode *mode;

		strlcpy(m.name, s->name, sizeof(m.name));

		mode = drm_mode_duplicate(dev, &m);
		if (mode != NULL) {
			drm_mode_probed_add(connector, mode);
			fb_add_videomode(s, &gs->info->modelist);

			count++;
		} else
			dev_dbg(dev->dev, "Failed to duplicate mode\n");
	}

	drm_set_preferred_mode(connector, 576, 460);	/* FIXME: PAL */
	// drm_set_preferred_mode(connector, 1280, 720);	/* FIXME */
	// drm_set_preferred_mode(connector, 1920, 1080);	/* FIXME */

	return count;
}

static const struct drm_connector_helper_funcs gs_connector_helper_funcs = {
	.get_modes = gs_get_modes,
};

static const struct drm_connector_funcs gs_connector_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes, // FIXME: Skip helper and fill in modes directly?
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

int gs_connector_init(struct drm_device *dev, struct drm_connector *connector)
{
	int err;

	printk("drm: %s\n", __func__);

	err = drm_connector_init(dev, connector,
		&gs_connector_funcs, DRM_MODE_CONNECTOR_TV);
	if (err)
		goto err_connector_init;

	err = rename_connector("AV-MULTI-OUT", connector);
	if (err) {
		dev_dbg(dev->dev, "Connector rename failed with %d\n", err);
		err = 0;
	}

	connector->interlace_allowed = true;
	connector->doublescan_allowed = true;

	/*
	 * FIXME: Indicate the following subconnectors?
	 *
	 * DRM_MODE_SUBCONNECTOR_Composite
	 * DRM_MODE_SUBCONNECTOR_SVIDEO
	 * DRM_MODE_SUBCONNECTOR_SCART
	 * DRM_MODE_SUBCONNECTOR_VGA
	 * DRM_MODE_SUBCONNECTOR_Component
	 * DRM_MODE_SUBCONNECTOR_DTerminal
	 */

	drm_connector_helper_add(connector, &gs_connector_helper_funcs);

	return 0;

err_connector_init:
	return err;
}
