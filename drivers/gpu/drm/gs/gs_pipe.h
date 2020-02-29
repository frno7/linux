// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM pipe
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_PIPE_H
#define GS_PIPE_H

#include <drm/drm_device.h>

#include "gs_drm.h"

int gs_pipe_init(struct gs_device *gs);

#endif /* GS_PIPE_H */
