// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM crtc
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/delay.h>
#include <linux/kernel.h>

#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_atomic_helper.h>

#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/gs-registers.h>

#include "gs_crtc.h"
#include "gs_drm.h"

/**
 * struct gs_sync_param - Graphics Synthesizer registers for video modes
 * @smode1: SMODE1 register
 * @smode2: SMODE2 register
 * @srfsh: SRFSH register
 * @synch1: SYNCH1 register
 * @synch2: SYNCH2 register
 * @syncv: SYNCV register
 * @display: DISPLAY1 or DISPLAY2 register
 *
 * These are the essential Graphics Synthesizer video synchronisation register
 * parameters.
 */
struct gs_sync_param {
	struct gs_smode1 smode1;
	struct gs_smode2 smode2;
	struct gs_srfsh srfsh;
	struct gs_synch1 synch1;
	struct gs_synch2 synch2;
	struct gs_syncv syncv;
	struct gs_display display;
};

/**
 * vm_to_cmod - determine the CMOD field for the SMODE1 register
 * @vm: video mode object to compute CMOD for
 *
 * Result: PAL, NTSC or VESA
 */
static enum gs_smode1_cmod vm_to_cmod(const struct fb_videomode *vm)
{
	const u32 htotal = vm->hsync_len +
		vm->left_margin + vm->xres + vm->right_margin;
	const u32 vtotal = vm->vsync_len +
		vm->upper_margin + vm->yres + vm->lower_margin;
	const u32 ptotal = htotal * vtotal;
	const u32 refresh = DIV_ROUND_CLOSEST_ULL(DIV_ROUND_CLOSEST_ULL(
		1000000000000ull * ((vm->vmode & FB_VMODE_INTERLACED) ? 2 : 1),
		vm->pixclock), ptotal);

	if (vm->sync & FB_SYNC_BROADCAST)
		return refresh < 55 ? gs_cmod_pal :
		       refresh < 65 ? gs_cmod_ntsc :
				      gs_cmod_vesa;

	return gs_cmod_vesa;
}

/**
 * vm_to_sp_sdtv - standard-definition television video synch parameters
 * @vm: video mode object to compute synchronisation parameters for
 *
 * The numeric register field constants come from fixed SDTV video modes made
 * by Sony. The main complication is that these values are the basis to compute
 * arbitrary top, bottom, left and right display margin (border) settings for
 * both PAL and NTSC. This is useful to for example precisely center the image
 * for a given analogue video display.
 *
 * The SDTV modes are designed to work with S-video, SCART and component video
 * cables, in addition to the PS2 HDMI adapter based on the Macro Silicon
 * MS9282 chip.
 *
 * The MAGV and MAGH fields for vertical and horizontal magnification in the
 * DISPLAY registers could be used to support lower resolution video modes,
 * for example 320x200 that was popular with many 8-bit and 16-bit computers,
 * but that is left for a future extension.
 *
 * Return: Graphics Synthesizer SDTV video mode synchronisation parameters
 */
static struct gs_sync_param vm_to_sp_sdtv(const struct fb_videomode *vm)
{
	const u32 cmod = vm_to_cmod(vm);
	const u32 intm = (vm->vmode & FB_VMODE_INTERLACED) ? 1 : 0;
	const u32 vs   = cmod == gs_cmod_pal ? 5 : 6;
	const u32 hb   = cmod == gs_cmod_pal ? 1680 : 1652;
	const u32 hf   = 2892 - hb;
	const u32 hs   = 254;
	const u32 hbp  = cmod == gs_cmod_pal ? 262 : 222;
	const u32 hfp  = cmod == gs_cmod_pal ? 48 : 64;
	const u32 vdp  = cmod == gs_cmod_pal ? 576 : 480;
	const u32 vbpe = vs;
	const u32 vbp  = cmod == gs_cmod_pal ? 33 : 26;
	const u32 vfpe = vs;
	const u32 vfp  = (vm->vmode & FB_VMODE_INTERLACED) ? 1 :
		cmod == gs_cmod_pal ? 4 : 2;
	const u32 tw = hb + hf;
	const u32 th = vdp;
	const u32 dw = min_t(u32, vm->xres * 4, tw);
	const u32 dh = min_t(u32, vm->yres * (intm ? 1 : 2), th);
	const u32 dx = hs + hbp + (tw - dw)/2 - 1;
	const u32 dy = (vs + vbp + vbpe + (th - dh)/2) / (intm ? 1 : 2) - 1;

	return (struct gs_sync_param) {
		.smode1 = {
			.vhp    =    0, .vcksel = 1, .slck2 = 1, .nvck = 1,
			.clksel =    1, .pevs   = 0, .pehs  = 0, .pvs  = 0,
			.phs    =    0, .gcont  = 0, .spml  = 4, .pck2 = 0,
			.xpck   =    0, .sint   = 1, .prst  = 0, .ex   = 0,
			.cmod   = cmod, .slck   = 0, .t1248 = 1,
			.lc     =   32, .rc     = 4
		},
		.smode2 = {
			.intm = intm
		},
		.srfsh = {
			.rfsh = 8
		},
		.synch1 = {
			.hs   = hs,
			.hsvs = cmod == gs_cmod_pal ? 1474 : 1462,
			.hseq = cmod == gs_cmod_pal ? 127 : 124,
			.hbp  = hbp,
			.hfp  = hfp
		},
		.synch2 = {
			.hb = hb,
			.hf = hf
		},
		.syncv = {
			.vs   = vs,
			.vdp  = vdp,
			.vbpe = vbpe,
			.vbp  = vbp,
			.vfpe = vfpe,
			.vfp  = vfp
		},
		.display = {
			.dh   = vm->yres - 1,
			.dw   = vm->xres * 4 - 1,
			.magv = 0,
			.magh = 3,
			.dy   = dy,
			.dx   = dx
		}
	};
}

/**
 * vm_to_sp_hdtv - high-definition television video synch parameters
 * @vm: video mode object to compute synchronisation parameters for
 * @sg: Graphics Synthesizer SMODE1 register video clock fields
 *
 * Some of the numeric register field constants come from fixed DTV video
 * modes made by Sony. The main complication is that these values are the
 * basis to compute arbitrary top, bottom, left and right display margin
 * (border) settings. This is useful to for example precisely center the
 * image for a given analogue video display.
 *
 * The HDTV modes are designed to work with component video cables and the
 * PS2 HDMI adapter based on the Macro Silicon MS9282 chip.
 *
 * The MAGV and MAGH fields for vertical and horizontal magnification in the
 * DISPLAY registers could be used to emulate SDTV resolution video modes such
 * as 320x200 that was popular with many 8-bit and 16-bit computers, but that
 * is left for a future extension.
 *
 * Return: Graphics Synthesizer HDTV video mode synchronisation parameters
 */
static struct gs_sync_param vm_to_sp_hdtv(
	const struct fb_videomode *vm, const struct gs_synch_gen sg)
{
	const u32 spml  = sg.spml;
	const u32 t1248 = sg.t1248;
	const u32 lc    = sg.lc;
	const u32 rc    = sg.rc;
	const u32 vc    = vm->yres <= 576 ? 1 : 0;
	const u32 hadj  = spml / 2;
	const u32 vhp   = (vm->vmode & FB_VMODE_INTERLACED) ? 0 : 1;
	const u32 hb    = vm->xres * spml * 3 / 5;

	return (struct gs_sync_param) {
		.smode1 = {
			.vhp    = vhp, .vcksel = vc, .slck2 =     1, .nvck = 1,
			.clksel =   1, .pevs   =  0, .pehs  =     0, .pvs  = 0,
			.phs    =   0, .gcont  =  0, .spml  =  spml, .pck2 = 0,
			.xpck   =   0, .sint   =  1, .prst  =     0, .ex   = 0,
			.cmod   =   0, .slck   =  0, .t1248 = t1248,
			.lc     =  lc, .rc     = rc
		},
		.smode2 = {
			.intm = (vm->vmode & FB_VMODE_INTERLACED) ? 1 : 0
		},
		.srfsh = {
			.rfsh = gs_rfsh_from_synch_gen(sg)
		},
		.synch1 = {
			.hs   = vm->hsync_len * spml,
			.hsvs = (vm->left_margin + vm->xres +
				 vm->right_margin - vm->hsync_len) * spml / 2,
			.hseq = vm->hsync_len * spml,
			.hbp  = vm->left_margin * spml - hadj,
			.hfp  = vm->right_margin * spml + hadj
		},
		.synch2 = {
			.hb = hb,
			.hf = vm->xres * spml - hb
		},
		.syncv = {
			.vs   = vm->vsync_len,
			.vdp  = vm->yres,
			.vbpe = 0,
			.vbp  = vm->upper_margin,
			.vfpe = 0,
			.vfp  = vm->lower_margin
		},
		.display = {
			.dh   = vm->yres - 1,
			.dw   = vm->xres * spml - 1,
			.magv = 0,
			.magh = spml - 1,
			.dy   = vm->vsync_len + vm->upper_margin - 1,
			.dx   = (vm->hsync_len + vm->left_margin) * spml - 1 - hadj
		}
	};
}

/**
 * vm_to_sp_vesa - VESA computer display mode synch parameters
 * @vm: video mode object to compute synchronisation parameters for
 * @sg: Graphics Synthesizer SMODE1 register video clock fields
 *
 * Some of the numeric register field constants come from fixed VESA video
 * modes made by Sony. The main complication is that these values are the
 * basis to compute arbitrary top, bottom, left and right display margin
 * (border) settings. This is useful to for example precisely center the
 * image for a given analogue video display.
 *
 * The VESA modes are designed to work with the synch-on-green (SOG) VGA cable
 * that Sony distributed with the Linux kit for the PlayStation 2. Modern
 * computer displays typically do not support synch-on-green, so an adapter
 * is most likely necessary for these modes.
 *
 * Return: Graphics Synthesizer VESA display mode synchronisation parameters
 */
static struct gs_sync_param vm_to_sp_vesa(
	const struct fb_videomode *vm, const struct gs_synch_gen sg)
{
	const u32 spml  = sg.spml;
	const u32 t1248 = sg.t1248;
	const u32 lc    = sg.lc;
	const u32 rc    = sg.rc;
	const u32 hadj  = spml / 2;
	const u32 vhp   = (vm->vmode & FB_VMODE_INTERLACED) ? 0 : 1;
	const u32 hb    = vm->xres * spml * 3 / 5;

	return (struct gs_sync_param) {
		.smode1 = {
			.vhp    = vhp, .vcksel =  0, .slck2 =     1, .nvck = 1,
			.clksel =   1, .pevs   =  0, .pehs  =     0, .pvs  = 0,
			.phs    =   0, .gcont  =  0, .spml  =  spml, .pck2 = 0,
			.xpck   =   0, .sint   =  1, .prst  =     0, .ex   = 0,
			.cmod   =   0, .slck   =  0, .t1248 = t1248,
			.lc     =  lc, .rc     = rc
		},
		.smode2 = {
			.intm = (vm->vmode & FB_VMODE_INTERLACED) ? 1 : 0
		},
		.srfsh = {
			.rfsh = gs_rfsh_from_synch_gen(sg)
		},
		.synch1 = {
			.hs   = vm->hsync_len * spml,
			.hsvs = (vm->left_margin + vm->xres +
				 vm->right_margin - vm->hsync_len) * spml / 2,
			.hseq = vm->hsync_len * spml,
			.hbp  = vm->left_margin * spml - hadj,
			.hfp  = vm->right_margin * spml + hadj
		},
		.synch2 = {
			.hb = hb,
			.hf = vm->xres * spml - hb
		},
		.syncv = {
			.vs   = vm->vsync_len,
			.vdp  = vm->yres,
			.vbpe = 0,
			.vbp  = vm->upper_margin,
			.vfpe = 0,
			.vfp  = vm->lower_margin
		},
		.display = {
			.dh   = vm->yres - 1,
			.dw   = vm->xres * spml - 1,
			.magv = 0,
			.magh = spml - 1,
			.dy   = vm->vsync_len + vm->upper_margin - 1,
			.dx   = (vm->hsync_len + vm->left_margin) * spml - 1 - hadj
		}
	};
}

static struct gs_sync_param vm_to_sp_for_synch_gen(
	const struct fb_videomode *vm, const struct gs_synch_gen sg)
{
	const bool bc = vm->sync & FB_SYNC_BROADCAST;
	const bool il = vm->vmode & FB_VMODE_INTERLACED;
	struct gs_sync_param sp =
		vm->yres <= 288 &&       bc ? vm_to_sp_sdtv(vm) :
		vm->yres <= 576 && il && bc ? vm_to_sp_sdtv(vm) :
					 bc ? vm_to_sp_hdtv(vm, sg) :
					      vm_to_sp_vesa(vm, sg);

	sp.smode1.gcont = gs_gcont_ycrcb;
	sp.smode1.sint = 1;
	sp.smode1.prst = 0;

	return sp;
}

static struct gs_sync_param vm_to_sp(const struct fb_videomode *vm)
{
	return vm_to_sp_for_synch_gen(vm, gs_synch_gen_for_vck(vm->pixclock));
}

static int ps2fb_set_par(const struct fb_videomode *vm)
{
	const struct gs_sync_param sp = vm_to_sp(vm);
	struct gs_smode1 smode1 = sp.smode1;

	gs_write_smode1(smode1);
	gs_write_smode2(sp.smode2);
	gs_write_srfsh(sp.srfsh);
	gs_write_synch1(sp.synch1);
	gs_write_synch2(sp.synch2);
	gs_write_syncv(sp.syncv);

	gs_write_display1(sp.display);

	GS_WRITE_PMODE(
		.en1 = 1,
		.crtmd = 1
	);

	smode1.prst = 1;
	gs_write_smode1(smode1);

	udelay(2500);

	smode1.sint = 0;
	smode1.prst = 0;
	gs_write_smode1(smode1);

	return 0;
}

static const struct drm_crtc_funcs gs_crtc_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
#if 0
	.enable_vblank		= drm_simple_kms_crtc_enable_vblank,
	.disable_vblank		= drm_simple_kms_crtc_disable_vblank,
#endif
};

static void gs_atomic_flush(struct drm_crtc *crtc,
	struct drm_crtc_state *old_crtc_state)
{
	const struct drm_crtc_state *state = crtc->state;
	const struct drm_display_mode *mode = &state->mode;
	struct gs_device *gs = container_of(crtc, struct gs_device, crtc);
	struct fb_var_screeninfo *var = &gs->info->var;

	const struct fb_videomode vm = {
		.name = mode->name,
		.refresh = mode->vrefresh,
		.xres = mode->crtc_hdisplay,
		.yres = mode->crtc_vdisplay,
		.left_margin = mode->crtc_htotal - mode->crtc_hsync_end,
		.right_margin = mode->crtc_hsync_start - mode->crtc_hdisplay, // FIXME: .hsync_len?
		.upper_margin = mode->crtc_vtotal - mode->crtc_vsync_end,
		.lower_margin = mode->crtc_vsync_start - mode->crtc_vdisplay,
		.hsync_len = mode->crtc_hsync_end - mode->crtc_hsync_start, // FIXME: .right_margin?
		.vsync_len = mode->crtc_vsync_end - mode->crtc_vsync_start,
		.pixclock = DIV_ROUND_CLOSEST_ULL(1000000000ull, mode->crtc_clock),
		.sync = mode->flags & DRM_MODE_FLAG_BCAST ? FB_SYNC_BROADCAST : 0,
		.vmode = mode->flags & DRM_MODE_FLAG_INTERLACE ? FB_VMODE_INTERLACED : 0,
	};

	fb_videomode_to_var(var, &vm);

	printk("drm: %s name %s %u %u %u %u %u %u %u %u %u\n",
		__func__,
		vm.name,
		vm.xres,
		vm.yres,
		vm.pixclock,
		vm.left_margin,
		vm.right_margin,
		vm.upper_margin,
		vm.lower_margin,
		vm.hsync_len,
		vm.vsync_len);

	ps2fb_set_par(&vm);
}

static enum drm_mode_status gs_mode_valid(struct drm_crtc *crtc,
	const struct drm_display_mode *mode)
{
	printk("drm: %s name %s\n", __func__, mode->name);

	return MODE_OK;		/* FIXME */
}

static int gs_atomic_check(struct drm_crtc *crtc,
				     struct drm_crtc_state *state)
{
#if 1
	printk("drm: %s\n", __func__);

	return 0;
#else
	bool has_primary = state->plane_mask &
			   drm_plane_mask(crtc->primary);

	/* We always want to have an active plane with an active CRTC */
	if (has_primary != state->enable)
		return -EINVAL;

	return drm_atomic_add_affected_planes(state->state, crtc);
#endif
}

static void gs_atomic_enable(struct drm_crtc *crtc,
				       struct drm_crtc_state *old_state)
{
#if 1
	printk("drm: %s\n", __func__);
#else
	struct drm_plane *plane;
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->enable)
		return;

	plane = &pipe->plane;
	pipe->funcs->enable(pipe, crtc->state, plane->state);
#endif
}

static void gs_atomic_disable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_state)
{
#if 1
	printk("drm: %s\n", __func__);
#else
	struct drm_simple_display_pipe *pipe;

	pipe = container_of(crtc, struct drm_simple_display_pipe, crtc);
	if (!pipe->funcs || !pipe->funcs->disable)
		return;

	pipe->funcs->disable(pipe);
#endif
}

static const struct drm_crtc_helper_funcs gs_crtc_helper_funcs = {
	.atomic_flush	= gs_atomic_flush,
	.mode_valid	= gs_mode_valid,
	.atomic_check	= gs_atomic_check,
	.atomic_enable	= gs_atomic_enable,
	.atomic_disable	= gs_atomic_disable,
};

int gs_crtc_init(struct drm_crtc *crtc,
	struct drm_plane *plane,
	struct drm_device *dev)
{
	int err;

	printk("drm: %s\n", __func__);

	drm_crtc_helper_add(crtc, &gs_crtc_helper_funcs);
	err = drm_crtc_init_with_planes(dev, crtc,
		plane, NULL, &gs_crtc_funcs, NULL);
	if (err)
		goto err_crtc_init_with_planes;

	return 0;

err_crtc_init_with_planes:
	return err;
}
