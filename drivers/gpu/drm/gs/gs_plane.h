// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM plane
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_PLANE_H
#define GS_PLANE_H

#include <drm/drm_device.h>

#include "gs_drm.h"

int gs_plane_init(struct gs_device *gs);

#endif /* GS_PLANE_H */
