// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM plane
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
#include "gs_plane.h"

#if 0
static enum drm_mode_status gs_pipe_mode_valid(
	struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	printk("drm: %s name %s\n", __func__, mode->name);

	return MODE_OK;
}

static int gs_pipe_check(struct drm_simple_display_pipe *pipe,
	struct drm_plane_state *plane_state, struct drm_crtc_state *crtc_state)
{
	printk("drm: %s\n", __func__);

	return 0;
}

static void gs_pipe_enable(struct drm_simple_display_pipe *pipe,
	struct drm_crtc_state *crtc_state, struct drm_plane_state *plane_state)
{
	const struct drm_crtc *crtc = &pipe->crtc;
	const struct drm_plane *plane = &pipe->plane;
	const struct drm_device *drm = crtc->dev;
	const struct gs_device *gs = drm->dev_private;
	const struct drm_display_mode *mode = &crtc_state->mode;
	const struct drm_framebuffer *fb = plane->state->fb;
	const u32 format = fb->format->format;

	printk("drm: %s format %u name %s\n", __func__, format, mode->name);

	// gs_mode_set(gs, &crtc_state->mode, plane_state->fb);

#if 0
	gs_fb_blit_fullscreen(plane_state->fb);
#endif
}
#endif

static void gs_plane_atomic_update(struct drm_plane *plane,
	struct drm_plane_state *old_pstate)
{
	struct gs_device *gs = container_of(plane, struct gs_device, plane);
	struct drm_crtc *crtc = &gs->crtc;

	printk("drm: %s\n", __func__);

#if 0
	struct drm_plane_state *state = gs->plane.state;
	if (pipe->plane.state->fb &&
	    gs->cpp != gs_cpp(pipe->plane.state->fb))
		gs_mode_set(gs, &crtc->mode,
				pipe->plane.state->fb);
#endif

#if 0
	struct drm_rect rect;
	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		gs_fb_blit_rect(pipe->plane.state->fb, &rect);
#endif

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);

		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;

		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const u32 gs_pixel_formats[] = {
	DRM_FORMAT_RGBA8888,	/* RGBA32 */
	DRM_FORMAT_RGBX8888,	/* RGB24 */
	DRM_FORMAT_RGBA5551,	/* RGBA16 */
};

static const struct drm_plane_helper_funcs gs_plane_helper_funcs = {
#if 1
	.atomic_update = gs_plane_atomic_update,
#else
	.prepare_fb = drm_simple_kms_plane_prepare_fb,
	.cleanup_fb = drm_simple_kms_plane_cleanup_fb,
	.atomic_check = drm_simple_kms_plane_atomic_check,
	.atomic_update = drm_simple_kms_plane_atomic_update,
#endif
};

static const struct drm_plane_funcs gs_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
#if 0
	.format_mod_supported   = drm_simple_kms_format_mod_supported,
#endif
};

static const struct drm_encoder_funcs gs_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int gs_plane_init(struct gs_device *gs)
{
	printk("drm: %s\n", __func__);

	/* FIXME: Look at tve200_display_init */

	drm_plane_helper_add(&gs->plane, &gs_plane_helper_funcs);
	return drm_universal_plane_init(&gs->dev, &gs->plane, 0, &gs_plane_funcs,
		gs_pixel_formats, ARRAY_SIZE(gs_pixel_formats),
		NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
}
