// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM mode configuration
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_MODE_H
#define GS_MODE_H

#include <drm/drm_device.h>

void gs_mode_config_init(struct drm_device *dev);

#endif /* GS_MODE_H */
