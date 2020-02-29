// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM console pan
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_PAN_H
#define GS_PAN_H

#include <drm/drm_fb_helper.h>

void write_cb_pan_display(const struct fb_var_screeninfo *var,
	const struct fb_info *info);

void gs_pan_init(struct fb_info *info);

#endif /* GS_PAN_H */
