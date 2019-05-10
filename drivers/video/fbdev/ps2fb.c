// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 frame buffer driver
 *
 * Copyright (C) 2019 Fredrik Noring
 *
 * This frame buffer supports the frame buffer console. Its main limitation
 * is the lack of mmap, since the Graphics Synthesizer has local frame buffer
 * memory that is not directly accessible from the main bus.
 *
 * All frame buffer transmissions are done by DMA via GIF PATH3.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <asm/io.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gif.h>
#include <uapi/asm/gs.h>

#define DEVICE_NAME "ps2fb"

#define PALETTE_SIZE 256
#define PALETTE_BLOCK_COUNT 1	/* One block is used for the indexed colors */

#define GIF_PACKAGE_TAG(package) ((package)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_REG(package) ((package)++)->gif.reg = (struct gif_data_reg)
#define GIF_PACKAGE_AD(package)  ((package)++)->gif.packed.ad = (struct gif_packed_ad)
#define DMA_PACKAGE_TAG(package) ((package)++)->dma = (struct dma_tag)

/* Module parameters */
static char *mode_option;

union package {
	union gif_data gif;
	struct dma_tag dma;
};

/**
 * struct tile_texture - texture representing a tile
 * @tbp: texture base pointer
 * @u: texel u coordinate (x coordinate)
 * @v: texel v coordinate (y coordinate)
 */
struct tile_texture {
	u32 tbp;
	u32 u;
	u32 v;
};

/**
 * struct console_buffer - console buffer
 * @block_count: number of frame buffer blocks
 * @bg: background color index
 * @fg: foreground color index
 * @tile: tile dimensions
 * @tile.width: width in pixels
 * @tile.height: height in pixels
 * @tile.width2: least width in pixels, power of 2
 * @tile.height2: least height in pixels, power of 2
 * @tile.block: tiles are stored as textures in the PSMT4 pixel storage format
 * 	with both cols and rows as powers of 2
 * @tile.block.cols: tile columns per GS block
 * @tile.block.rows: tile rows per GS block
 */
struct console_buffer {
	u32 block_count;

	u32 bg;
	u32 fg;

	struct cb_tile {
		u32 width;
		u32 height;

		u32 width2;
		u32 height2;

		struct {
			u32 cols;
			u32 rows;
		} block;
	} tile;
};

struct ps2fb_par {
	spinlock_t lock;

	struct console_buffer cb;

	struct {
		size_t capacity;
		union package *buffer;
	} package;
};

struct gs_sync_param {
	struct gs_smode1 smode1;
	struct gs_smode2 smode2;
	struct gs_srfsh srfsh;
	struct gs_synch1 synch1;
	struct gs_synch2 synch2;
	struct gs_syncv syncv;
	struct gs_display display;
};

/* Pixel width to FBW (frame buffer width) */
static u32 var_to_fbw(const struct fb_var_screeninfo *var)
{
	/*
	 * Round up to nearest GS_FB_PAGE_WIDTH (64 px) since there are
	 * valid resolutions such as 720 px that do not divide 64 properly.
	 */
	return (var->xres_virtual + GS_FB_PAGE_WIDTH - 1) / GS_FB_PAGE_WIDTH;
}

static enum gs_psm var_to_psm(const struct fb_var_screeninfo *var,
	const struct fb_info *info)
{
	if (var->bits_per_pixel == 1)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 16)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 32)
		return gs_psm_ct32;

	fb_warn_once(info, "%s: Unsupported bits per pixel %u\n",
		__func__, var->bits_per_pixel);
	return gs_psm_ct32;
}

static u32 var_to_block_count(const struct fb_info *info)
{
	const struct fb_var_screeninfo *var = &info->var;
	const enum gs_psm psm = var_to_psm(var, info);
	const u32 fbw = var_to_fbw(var);

	if (psm == gs_psm_ct16)
		return gs_psm_ct16_block_count(fbw, var->yres_virtual);
	if (psm == gs_psm_ct32)
		return gs_psm_ct32_block_count(fbw, var->yres_virtual);

	fb_warn_once(info, "%s: Unsupported pixel storage format %u\n",
		__func__, psm);
	return 0;
}

static u32 color_base_pointer(const struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;

	return par->cb.block_count;
}

static u32 texture_least_power_of_2(u32 x)
{
	return max(1 << get_count_order(x), 8);
}

static struct cb_tile cb_tile(u32 width, u32 height)
{
	const u32 width2 = texture_least_power_of_2(width);
	const u32 height2 = texture_least_power_of_2(height);

	return (struct cb_tile) {
		.width = width,
		.height = height,

		.width2 = width2,
		.height2 = height2,

		.block = {
			.cols = GS_PSMT4_BLOCK_WIDTH / width2,
			.rows = GS_PSMT4_BLOCK_HEIGHT / height2,
		},
	};
}

/* Returns the size of the frame buffer in bytes. */
static u32 framebuffer_size(const u32 xres_virtual, const u32 yres_virtual,
      const u32 bits_per_pixel)
{
	return (xres_virtual * yres_virtual * bits_per_pixel) / 8;
}

static int ps2fb_cb_get_tilemax(struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;
	const u32 block_tile_count =
		par->cb.tile.block.cols *
		par->cb.tile.block.rows;
	const s32 blocks_available =
		GS_BLOCK_COUNT - par->cb.block_count - PALETTE_BLOCK_COUNT;

	return blocks_available > 0 ? blocks_available * block_tile_count : 0;
}

static bool bits_per_pixel_fits(const u32 xres_virtual, const u32 yres_virtual,
      const int bits_per_pixel, const size_t buffer_size)
{
	return framebuffer_size(xres_virtual, yres_virtual,
		bits_per_pixel) <= buffer_size;
}

static int default_bits_per_pixel(
	const u32 xres_virtual, const u32 yres_virtual,
	const size_t buffer_size)
{
	return bits_per_pixel_fits(xres_virtual, yres_virtual,
		32, buffer_size) ? 32 : 16;
}

static bool filled_var_videomode(const struct fb_var_screeninfo *var)
{
	return var->xres > 0 && var->hsync_len > 0 &&
	       var->yres > 0 && var->vsync_len > 0 && var->pixclock > 0;
}

static int ps2fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	/* Check whether video mode defaults are needed. */
	if (!filled_var_videomode(var)) {
		const struct fb_videomode *vm =
			fb_find_best_mode(var, &info->modelist);

		if (!vm)
			return -EINVAL;

		fb_videomode_to_var(var, vm);
	}

        /* GS video register resolution is limited to 2048. */
        if (var->xres < 1 || 2048 < var->xres ||
            var->yres < 1 || 2048 < var->yres)
		return -EINVAL;

	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->xoffset = 0;
	var->yoffset = 0;

        /* Check bits per pixel. */
        if (!var->bits_per_pixel)
		var->bits_per_pixel = default_bits_per_pixel(
                      var->xres_virtual, var->yres_virtual, info->fix.smem_len);
	else if (var->bits_per_pixel != 16 &&
		 var->bits_per_pixel != 32)
		return -EINVAL;
        if (!bits_per_pixel_fits(var->xres_virtual, var->yres_virtual,
                 var->bits_per_pixel, info->fix.smem_len))
		var->bits_per_pixel = default_bits_per_pixel(
                      var->xres_virtual, var->yres_virtual, info->fix.smem_len);
        if (!bits_per_pixel_fits(var->xres_virtual, var->yres_virtual,
                 var->bits_per_pixel, info->fix.smem_len))
		return -ENOMEM;
	if (var->bits_per_pixel == 16) {
		var->red    = (struct fb_bitfield){ .offset =  0, .length = 5 };
		var->green  = (struct fb_bitfield){ .offset =  5, .length = 5 };
		var->blue   = (struct fb_bitfield){ .offset = 10, .length = 5 };
		var->transp = (struct fb_bitfield){ .offset = 15, .length = 1 };
	} else if (var->bits_per_pixel == 32) {
		var->red    = (struct fb_bitfield){ .offset =  0, .length = 8 };
		var->green  = (struct fb_bitfield){ .offset =  8, .length = 8 };
		var->blue   = (struct fb_bitfield){ .offset = 16, .length = 8 };
		var->transp = (struct fb_bitfield){ .offset = 24, .length = 8 };
	} else
		return -EINVAL;		/* Unsupported bits per pixel. */

        /* Screen rotations are not supported. */
	if (var->rotate)
		return -EINVAL;

        return 0;
}

static int ps2fb_cb_check_var(
	struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&par->lock, flags);
	err = ps2fb_check_var(var, info);
	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		if (info->tileops->fb_get_tilemax(info) < 256)
			err = -ENOMEM;

	return err;
}

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

static u32 refresh_for_var(const struct fb_var_screeninfo *var)
{
	const u32 htotal = var->hsync_len +
		var->left_margin + var->xres + var->right_margin;
	const u32 vtotal = var->vsync_len +
		var->upper_margin + var->yres + var->lower_margin;
	const u32 ptotal = htotal * vtotal;

	return DIV_ROUND_CLOSEST_ULL(DIV_ROUND_CLOSEST_ULL(
		1000000000000ull * ((var->vmode & FB_VMODE_INTERLACED) ? 2 : 1),
		var->pixclock), ptotal);
}

static int ps2fb_set_par(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	const struct fb_var_screeninfo *var = &info->var;
	const struct fb_videomode *mm = fb_match_mode(var, &info->modelist);
	const struct fb_videomode vm = (struct fb_videomode) {
		.refresh      = refresh_for_var(var),
		.xres         = var->xres,
		.yres         = var->yres,
		.pixclock     = var->pixclock,
		.left_margin  = var->left_margin,
		.right_margin = var->right_margin,
		.upper_margin = var->upper_margin,
		.lower_margin = var->lower_margin,
		.hsync_len    = var->hsync_len,
		.vsync_len    = var->vsync_len,
		.sync         = var->sync,
		.vmode        = var->vmode,
		.flag         = mm != NULL ? mm->flag : 0
	};
	const struct gs_sync_param sp = vm_to_sp(&vm);
	struct gs_smode1 smode1 = sp.smode1;

	par->mode = vm;

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
	info->fix.ywrapstep = 1;
	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;

	gs_write_smode1(smode1);
	gs_write_smode2(sp.smode2);
	gs_write_srfsh(sp.srfsh);
	gs_write_synch1(sp.synch1);
	gs_write_synch2(sp.synch2);
	gs_write_syncv(sp.syncv);
	gs_write_display1(sp.display);

	GS_WRITE_DISPFB1(
		.fbw = var_to_fbw(var),
		.psm = var_to_psm(var, info),
		.dbx = var->xoffset,
		.dby = var->yoffset,
	);

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

static int ps2fb_cb_set_par(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&par->lock, flags);

	err = ps2fb_set_par(info);
	if (!err)
		par->cb.block_count = var_to_block_count(info);

	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		fb_info(info, "%d tiles maximum for %ux%u font\n",
			info->tileops->fb_get_tilemax(info),
			par->cb.tile.width, par->cb.tile.height);

	return err;
}

static u32 block_dimensions(u32 dim, u32 alignment)
{
	u32 mask = 0;
	u32 d;

	for (d = 1; d <= dim; d++)
		if (d % alignment == 0)
			mask |= 1 << (d - 1);

	return mask;
}

static int init_console_buffer(struct platform_device *pdev,
	struct fb_info *info)
{
	static struct fb_ops fbops = {
		.owner		= THIS_MODULE,
		.fb_set_par	= ps2fb_cb_set_par,
		.fb_check_var	= ps2fb_cb_check_var,
	};

	static struct fb_tile_ops tileops = {
		.fb_get_tilemax = ps2fb_cb_get_tilemax
	};

	struct ps2fb_par *par = info->par;

	fb_info(info, "Graphics Synthesizer console frame buffer device\n");

	info->screen_size = 0;
	info->screen_base = NULL;	/* mmap is unsupported by hardware */

	info->fix.smem_start = 0;	/* The GS framebuffer is local memory */
	info->fix.smem_len = GS_MEMORY_SIZE;

	info->fbops = &fbops;
	info->flags = FBINFO_DEFAULT |
		      FBINFO_READS_FAST;

	info->flags |= FBINFO_MISC_TILEBLITTING;
	info->tileops = &tileops;

	/*
	 * BITBLTBUF for pixel format CT32 requires divisibility by 2,
	 * and CT16 requires divisibility by 4. So 4 is a safe choice.
	 */
	info->pixmap.blit_x = block_dimensions(GS_PSMT4_BLOCK_WIDTH, 4);
	info->pixmap.blit_y = block_dimensions(GS_PSMT4_BLOCK_HEIGHT, 1);

	/* 8x8 default font tile size for fb_get_tilemax */
	par->cb.tile = cb_tile(8, 8);

	return 0;
}

static int ps2fb_probe(struct platform_device *pdev)
{
	struct ps2fb_par *par;
	struct fb_info *info;
	int err;

	info = framebuffer_alloc(sizeof(*par), &pdev->dev);
	if (info == NULL) {
		dev_err(&pdev->dev, "framebuffer_alloc failed\n");
		err = -ENOMEM;
		goto err_framebuffer_alloc;
	}

	par = info->par;

	spin_lock_init(&par->lock);

	par->package.buffer = (union package *)__get_free_page(GFP_DMA);
	if (!par->package.buffer) {
		dev_err(&pdev->dev, "Failed to allocate package buffer\n");
		err = -ENOMEM;
		goto err_package_buffer;
	}
	par->package.capacity = PAGE_SIZE / sizeof(union package);

	strlcpy(info->fix.id, "PS2 GS", ARRAY_SIZE(info->fix.id));
	info->fix.accel = FB_ACCEL_PLAYSTATION_2;

	err = init_console_buffer(pdev, info);
	if (err < 0)
		goto err_init_buffer;

	info->mode = &par->mode;

	if (register_framebuffer(info) < 0) {
		fb_err(info, "register_framebuffer failed\n");
		err = -EINVAL;
		goto err_register_framebuffer;
	}

	platform_set_drvdata(pdev, info);

	return 0;

err_register_framebuffer:
err_init_buffer:
	free_page((unsigned long)par->package.buffer);
err_package_buffer:
	framebuffer_release(info);
err_framebuffer_alloc:
	return err;
}

static int ps2fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct ps2fb_par *par = info->par;
	int err = 0;

	if (info != NULL) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

		framebuffer_release(info);
	}

	if (!gif_wait()) {
		fb_err(info, "Failed to complete GIF DMA transfer\n");
		err = -EBUSY;
	}
	free_page((unsigned long)par->package.buffer);

	return err;
}

static struct platform_driver ps2fb_driver = {
	.probe		= ps2fb_probe,
	.remove		= ps2fb_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

static struct platform_device *ps2fb_device;

static int __init ps2fb_init(void)
{
	int err;

#ifndef MODULE
	char *options = NULL;
	char *this_opt;

	if (fb_get_options(DEVICE_NAME, &options))
		return -ENODEV;
	if (!options || !*options)
		goto no_options;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "mode_option:", 12))
			mode_option = &this_opt[12];
		else if ('0' <= this_opt[0] && this_opt[0] <= '9')
			mode_option = this_opt;
		else
			pr_warn(DEVICE_NAME ": Unrecognized option \"%s\"\n",
				this_opt);
	}

no_options:
#endif /* !MODULE */

	/* Default to a suitable PAL or NTSC broadcast mode. */
	if (!mode_option)
		mode_option = gs_region_pal() ? "576x460i@50" : "576x384i@60";

	ps2fb_device = platform_device_alloc("ps2fb", 0);
	if (!ps2fb_device)
		return -ENOMEM;

	err = platform_device_add(ps2fb_device);
	if (err < 0) {
		platform_device_put(ps2fb_device);
		return err;
	}

	return platform_driver_register(&ps2fb_driver);
}

static void __exit ps2fb_exit(void)
{
	platform_driver_unregister(&ps2fb_driver);
	platform_device_unregister(ps2fb_device);
}

module_init(ps2fb_init);
module_exit(ps2fb_exit);

module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option,
	"Specify initial video mode as \"<xres>x<yres>[-<bpp>][@<refresh>]\"");

MODULE_DESCRIPTION("PlayStation 2 frame buffer driver");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
