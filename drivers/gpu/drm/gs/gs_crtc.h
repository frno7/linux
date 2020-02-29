// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM crtc
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_CRTC_H
#define GS_CRTC_H

#include <drm/drm_crtc.h>

int gs_crtc_init(struct drm_crtc *crtc,
	struct drm_plane *plane,
	struct drm_device *dev);

#endif /* GS_CRTC_H */
