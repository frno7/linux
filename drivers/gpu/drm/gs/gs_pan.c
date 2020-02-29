// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM console pan
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/kernel.h>

#include <asm/mach-ps2/gs.h>

#include "gs_tile.h" // FIXME

#include "gs_pan.h"

/**
 * write_cb_pan_display - write console buffer pan operation to the GIF
 * @var: screen info object
 * @info: frame buffer info object
 *
 * XPAN, YPAN and YWRAP are hardware accelerated. YWRAP is implemented using
 * the two independent rectangular area read output circuits of the Graphics
 * Synthesizer. The DISPLAY1 register outputs the upper part and the DISPLAY2
 * register outputs the lower part.
 */
void write_cb_pan_display(const struct fb_var_screeninfo *var,
	const struct fb_info *info)
{
	const struct gs_display display = gs_read_display1();
	const int psm = gs_var_to_psm(var);
	const int fbw = gs_var_to_fbw(var);
	const int yo = var->yoffset % var->yres_virtual;
	const int dh1 = min_t(int, var->yres_virtual - yo, var->yres);
	const int dh2 = var->yres - dh1;

	GS_WRITE_DISPLAY1(
		.dh = dh1 - 1,
		.dw = display.dw,
		.magv = display.magv,
		.magh = display.magh,
		.dy = display.dy,
		.dx = display.dx,
	);

	GS_WRITE_DISPLAY2(
		.dh = dh2 - 1,
		.dw = display.dw,
		.magv = display.magv,
		.magh = display.magh,
		.dy = display.dy + dh1,
		.dx = display.dx,
	);

	GS_WRITE_DISPFB1(
		.fbw = fbw,
		.psm = psm,
		.dbx = var->xoffset,
		.dby = yo
	);

	GS_WRITE_DISPFB2(
		.fbw = fbw,
		.psm = psm,
		.dbx = var->xoffset,
		.dby = 0,
	);

	GS_WRITE_PMODE(
		.en1 = 1,
		.en2 = dh2 ? 1 : 0,
		.crtmd = 1
	);
}

/**
 * changed_cb_pan_display - is display panning needed?
 * @var: screen info object
 *
 * Panning is undefined until the DISPFB1 register has been set.
 *
 * Return: %true if the horizontal or vertical panning is needed, otherwise
 *	%false
 */
static bool changed_cb_pan_display(const struct fb_var_screeninfo *var)
{
	if (gs_valid_dispfb1()) {
		const struct gs_dispfb dspfb12 = gs_read_dispfb1();
		const int yo = var->yoffset % var->yres_virtual;

		return dspfb12.dbx != var->xoffset || dspfb12.dby != yo;
	}

	return false;	/* DISPFB1 is incomparable before video mode is set. */
}

/**
 * ps2fb_cb_pan_display - pan the display
 * @var: screen info object
 * @info: frame buffer info object
 *
 * Return: 0 on success, otherwise a negative error number
 */
static int ps2fb_cb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	struct ps2fb_par *par = info_to_ps2par(info);
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);

	if (changed_cb_pan_display(var))
		write_cb_pan_display(var, info);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

void gs_pan_init(struct fb_info *info)
{
	info->fbops->fb_pan_display = ps2fb_cb_pan_display; /* FIXME: pan_display_atomic */

	info->flags |= FBINFO_HWACCEL_XPAN |
		       FBINFO_HWACCEL_YPAN |
		       FBINFO_HWACCEL_YWRAP |
		       FBINFO_PARTIAL_PAN_OK;
}
