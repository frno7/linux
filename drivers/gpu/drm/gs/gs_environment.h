// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM console drawing environment
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_ENVIRONMENT_H
#define GS_ENVIRONMENT_H

#include <drm/drm_fb_helper.h>

void write_cb_environment(struct fb_info *info);

#endif /* GS_ENVIRONMENT_H */
