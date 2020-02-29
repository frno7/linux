// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM pipe
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/kernel.h>

#include <asm/mach-ps2/irq.h>

#include <drm/drm_device.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_vblank.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_atomic_helper.h>

#include "gs_crtc.h"
#include "gs_pipe.h"
#include "gs_plane.h"

static const u32 gs_pixel_formats[] = {
	DRM_FORMAT_RGBA8888,	/* RGBA32 */
	DRM_FORMAT_RGBX8888,	/* RGB24 */
	DRM_FORMAT_RGBA5551,	/* RGBA16 */
};

static const struct drm_encoder_funcs gs_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int gs_pipe_init(struct gs_device *gs)
{
	int err;

	printk("drm: %s\n", __func__);

	/* FIXME: Look at tve200_display_init */

	err = gs_plane_init(gs);
	if (err)
		return err;

	err = gs_crtc_init(&gs->crtc, &gs->plane, &gs->dev);
	if (err)
		return err;

	gs->encoder.possible_crtcs = drm_crtc_mask(&gs->crtc);
	err = drm_encoder_init(&gs->dev, &gs->encoder,
		&gs_encoder_funcs, DRM_MODE_ENCODER_NONE, NULL);
	if (err)
		return err;

	return drm_connector_attach_encoder(&gs->connector, &gs->encoder);
}
