// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM device
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_DRM_H
#define GS_DRM_H

#include <drm/drm_device.h>
#include <drm/drm_connector.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_fb_helper.h>

#include "gs_tile.h"

/**
 * struct gs_device - driver specific structure
 */
struct gs_device {
	struct drm_device dev;

	struct drm_crtc crtc;
	struct drm_plane plane;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct drm_framebuffer framebuffer;
	struct drm_fb_helper fb_helper;

	struct fb_info *info;
	struct ps2fb_par ps2par;
};

#endif /* GS_DRM_H */
