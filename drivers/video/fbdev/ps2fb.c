// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 frame buffer
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 *
 * This frame buffer supports the frame buffer console. Its main limitation
 * is the lack of mmap, since the Graphics Synthesizer has local frame buffer
 * memory that is not directly accessible from the main bus.
 *
 * All frame buffer transmissions are done by DMA via GIF PATH3.
 *
 * FIXME: Vsync CB too
 * FIXME: Buffer requests
 * FIXME: Special buffer for interrupts
 * FIXME: Avoid generating DMAC interrupts?
 * FIXME: Config option to disable virtual buffer
 * FIXME: Optimize common 1 bpp 8 width cases etc.
 * FIXME: Ensure at most one module instance is loaded
 * FIXME: Allow modeline pixel adjustments (kernel parameter)
 * FIXME: int (*fb_setcmap)(struct fb_cmap *cmap, struct fb_info *info);
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
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

#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/ps2.h>

#include <uapi/asm/gif.h>
#include <uapi/asm/gs.h>

#define DEVICE_NAME "ps2fb"

#define PALETTE_SIZE 256
#define PALETTE_BLOCK_COUNT 1	/* One block is used for the indexed colors */

#define D2_CHCR		0x1000a000	/* Channel 2 control */
#define D2_MADR		0x1000a010	/* Channel 2 memory address */
#define D2_TADR		0x1000a030	/* Channel 2 memory address */
#define D2_QWC		0x1000a020	/* Channel 2 quad word count */

#define GIF_PACKAGE_TAG(package) ((package)++)->gif.tag = (struct gif_tag)
#define GIF_PACKAGE_REG(package) ((package)++)->gif.reg = (struct gif_data_reg)
#define GIF_PACKAGE_AD(package)  ((package)++)->gif.packed.ad = (struct gif_packed_ad)
#define DMA_PACKAGE_TAG(package) ((package)++)->dma = (struct dma_tag)

/* Module parameters */
#if 0
static char *mode_option = "640x512i@50";
static char *mode_adjust = "";
#else
static char *mode_option = "1920x1080p@50";
static char *mode_adjust = "+13+0";
#endif	/* FIXME */
static unsigned int vfb_ram = 0;

union package {
	union gif_data gif;
	struct dma_tag dma;
};

struct tile_texture {
	u32 tbp;	/* Texture base pointer */
	u32 u;		/* Texel u coordinate (x coordinate) */
	u32 v;		/* Texel v coordinate (y coordinate) */
};

struct ps2fb_par {
	spinlock_t lock;

	struct fb_videomode mode;
	struct gs_rgba32 pseudo_palette[PALETTE_SIZE];

	/* Console buffer */
	struct {
		u32 block_count;	/* Number of frame buffer blocks */

		u32 bg;			/* Background color index */
		u32 fg;			/* Foreground color index */

		struct {
			u32 width;	/* Width in pixels, power of 2 */
			u32 height;	/* Height in pixels, power of 2 */

			/*
			 * Tiles are stored as textures in the PSMT4 pixel
			 * storage format. Both cols and rows are powers of 2.
			 */
			struct {
				u32 cols;	/* Tile columns per GS block */
				u32 rows;	/* Tile rows per GS block */
			} block;
		} tile;
	} cb;

	struct fb_vblank vblank;
	wait_queue_head_t vblank_queue;

	unsigned long dma_buffer;

	size_t package_capacity;
	union package *package_buffer;
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

struct synch_gen {
	u32 spml : 4;
	u32 t1248 : 2;
	u32 lc : 7;
	u32 rc : 3;
};

static const struct fb_videomode standard_modes[] = {
	/* PAL */
	{ "256p", 50, 640, 256, 74074, 100, 61, 34, 22, 63, 2,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "288p", 50, 720, 288, 74074, 70, 11, 19, 3, 63, 3,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "512i", 50, 640, 512, 74074, 100, 61, 67, 41, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "576i", 50, 720, 576, 74074, 70, 11, 39, 5, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "576p", 50, 720, 576, 37037, 70, 11, 39, 5, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "720p", 50, 1280, 720, 13468, 220, 400, 19, 6, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1080i", 50, 1920, 1080, 13468, 148, 484, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "1080p", 50, 1920, 1080, 6734, 148, 484, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },

	/* FIXME: Center display on nearest resolution */
	{ "460i", 50, 576, 460, 74074, 142, 83, 97, 63, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "460p", 50, 576, 460, 37037, 142, 83, 97, 63, 63, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "644p", 50, 1124, 644, 13468, 298, 478, 57, 44, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "964i", 50, 1688, 964, 13468, 264, 600, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "964p", 50, 1688, 964, 6734, 264, 600, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },

	/* NTSC */
	{ "224p", 60, 640, 224, 74074, 95, 60, 22, 14, 63, 3,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "240p", 60, 720, 240, 74074, 58, 17, 15, 5, 63, 3,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "448i", 60, 640, 448, 74074, 95, 60, 44, 27, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "480i", 60, 720, 480, 74074, 58, 17, 30, 9, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "480p", 60, 720, 480, 37037, 58, 17, 30, 9, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "720p", 60, 1280, 720, 13481, 220, 70, 19, 6, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },
	{ "1080i", 60, 1920, 1080, 13481, 148, 44, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED, FB_MODE_IS_STANDARD },
	{ "1080p", 60, 1920, 1080, 6741, 148, 44, 36, 4, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED, FB_MODE_IS_STANDARD },

	/* FIXME: Center display on nearest resolution */
	{ "384i", 60, 576, 384, 74074, 130, 89, 78, 57, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "384p", 60, 576, 384, 37037, 130, 89, 78, 57, 63, 6,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "644p", 60, 1124, 644, 13481, 298, 148, 57, 44, 80, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },
	{ "964i", 60, 1688, 964, 13481, 264, 160, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_INTERLACED },
	{ "964p", 60, 1688, 964, 6741, 264, 160, 94, 62, 88, 5,
	  FB_SYNC_BROADCAST, FB_VMODE_NONINTERLACED },

	/* VESA */
	{ "vesa-1a", 60, 640, 480, 39682,  48, 16, 33, 10, 96, 2,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-1c", 75, 640, 480, 31746, 120, 16, 16, 1, 64, 3,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-2b", 60, 800, 600, 25000, 88, 40, 23, 1, 128, 4,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-2d", 75, 800, 600, 20202, 160, 16, 21, 1, 80, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-3b", 60, 1024, 768, 15384, 160, 24, 29, 3, 136, 6,
	  0, FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-3d", 75, 1024, 768, 12690, 176, 16, 28, 1, 96, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-4a", 60, 1280, 1024, 9259, 248, 48, 38, 1, 112, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA },
	{ "vesa-4b", 75, 1280, 1024, 7407, 248, 16, 38, 1, 144, 3,
	  FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	  FB_VMODE_NONINTERLACED, FB_MODE_IS_VESA }
};

static struct gs_rgbaq console_pseudo_palette(
	const struct ps2fb_par *par, const u32 regno)
{
	const struct gs_rgba32 color = regno < PALETTE_SIZE ?
		par->pseudo_palette[regno] : (struct gs_rgba32) { };

	return (struct gs_rgbaq) {
		.r = color.r,
		.g = color.g,
		.b = color.b,
		.a = (color.a + 1) / 2	/* 0x80 = GS_ALPHA_ONE = 1.0 */
	};
}

/* Pixel width to FBW (frame buffer width) */
static u32 var_to_fbw(const struct fb_var_screeninfo *var)
{
	/*
	 * Round up to nearest GS_FB_PAGE_WIDTH (64 px) since there are
	 * valid resolutions such as 720 px that do not divide 64 properly.
	 */
	return (var->xres_virtual + GS_FB_PAGE_WIDTH - 1) / GS_FB_PAGE_WIDTH;
}

static enum gs_pixel_format bpp_to_psm(const u32 bits_per_pixel)
{
	if (bits_per_pixel == 1)
		return gs_psmct16;
	if (bits_per_pixel == 16)
		return gs_psmct16;
	if (bits_per_pixel == 32)
		return gs_psmct32;

	BUG();
}

static enum gs_pixel_format var_to_psm(const struct fb_var_screeninfo *var)
{
	return bpp_to_psm(var->bits_per_pixel);
}

static u32 var_to_block_count(const struct fb_var_screeninfo *var)
{
	const enum gs_pixel_format psm = var_to_psm(var);
	const u32 fbw = var_to_fbw(var);

	if (psm == gs_psmct16)
		return gs_psmct16_block_count(fbw, var->yres_virtual);
	if (psm == gs_psmct32)
		return gs_psmct32_block_count(fbw, var->yres_virtual);

	BUG();
}

static u32 var_to_block_address(const struct fb_var_screeninfo *var,
	const u32 block_index)
{
	const enum gs_pixel_format psm = var_to_psm(var);
	const u32 fbw = var_to_fbw(var);

	if (psm == gs_psmct16)
		return gs_psmct16_block_address(fbw, block_index);
	if (psm == gs_psmct32)
		return gs_psmct32_block_address(fbw, block_index);

	BUG();
}

static u32 color_base_pointer(const struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;

	return par->cb.block_count;
}

static u32 texture_base_pointer(const struct fb_info *info,
	const u32 block_index)
{
	const struct ps2fb_par *par = info->par;

	return var_to_block_address(&info->var,
		par->cb.block_count + PALETTE_BLOCK_COUNT + block_index);
}

/* Returns texture base pointer and texel coordinates given a tile index. */
static struct tile_texture texture_for_tile(const struct fb_info *info,
	const u32 tile_index)
{
	const struct ps2fb_par *par = info->par;

	const u32 texture_tile_count =
		par->cb.tile.block.cols * par->cb.tile.block.rows;
	const u32 block_tile = tile_index / texture_tile_count;
	const u32 texture_tile = tile_index % texture_tile_count;
	const u32 block_address = texture_base_pointer(info, block_tile);

	const u32 row = texture_tile / par->cb.tile.block.cols;
	const u32 col = texture_tile % par->cb.tile.block.cols;

	return (struct tile_texture) {
		.tbp	= block_address,
		.u	= col * par->cb.tile.width,
		.v	= row * par->cb.tile.height
	};
}

/* Check BITBLTBUF hardware restrictions given a pixel width. */
static bool valid_bitbltbuf_width(int width, enum gs_pixel_format psm)
{
	if (width < 1)
		return 0;
	if (psm == gs_psmct32)
		return (width & 1) == 0;
	if (psm == gs_psmct16)
		return (width & 3) == 0;

	BUG();
}

/* Returns the size of the frame buffer in bytes. */
static u32 framebuffer_size(const u32 xres_virtual, const u32 yres_virtual,
      const u32 bits_per_pixel)
{
	return (xres_virtual * yres_virtual * bits_per_pixel) / 8;
}

struct environment {
	u32 xres;
	u32 yres;
	u32 fbw;
	enum gs_pixel_format psm;
	u32 fbp;
};

static struct environment var_to_env(const struct fb_var_screeninfo *var)
{
	return (struct environment) {
		.xres = var->xres,
		.yres = var->yres,
		.fbw  = var_to_fbw(var),
		.psm  = bpp_to_psm(var->bits_per_pixel)
	};
}

static size_t package_environment(union package *package,
	const struct environment env)
{
	union package * const base_package = package;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 11
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_frame_1,
		.data.frame_1 = {
			.fbw = env.fbw,
			.fbp = env.fbp,
			.psm = env.psm
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_xyoffset_1,
		.data.xyoffset_1 = {
			.ofx = 0,
			.ofy = 0,
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_scissor_1,
		.data.scissor_1 = {
			.scax0 = 0, .scax1 = env.xres,
			.scay0 = 0, .scay1 = env.yres
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_scanmsk,
		.data.scanmsk = {
			.msk = gs_scanmsk_normal
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_prmode,
		.data.prmode = { }	/* Reset PRMODE to a known value */
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_prmodecont,
		.data.prmodecont = {
			.ac = 1
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_test_1,
		.data.test_1 = {
			.zte  = gs_depth_test_on,	/* Must always be ON */
			.ztst = gs_depth_pass		/* Emulate OFF */
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_texa,
		.data.texa = {
			.ta0 = GS_ALPHA_ONE,
			.aem = gs_aem_normal,
			.ta1 = GS_ALPHA_ONE
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_tex1_1,
		.data.tex1 = {
			.lcm = gs_lcm_fixed,
			.mmag = gs_lod_nearest,
			.mmin = gs_lod_nearest,
			.k = 0
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_zbuf_1,
		.data.zbuf = {
			.zmsk = gs_zbuf_off
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_dthe,
		.data.dthe = {
			.dthe = gs_dthe_off
		}
	};

	return package - base_package;
}

void write_cb_environment(struct fb_info *info)
{
        if (gif_ready()) {
		struct ps2fb_par *par = info->par;
		union package * const base_package = par->package_buffer;
		union package *package = base_package;

		package += package_environment(package, var_to_env(&info->var));

		gif_write(&base_package->gif, package - base_package);
	}
}

static size_t package_sprite(union package *package,
	const struct fb_fillrect *region, const struct gs_rgbaq rbaq)
{
	union package * const base_package = package;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_reglist_mode,
		.reg0 = gif_reg_prim,
		.reg1 = gif_reg_rgbaq,
		.reg2 = gif_reg_xyz2,
		.reg3 = gif_reg_xyz2,
		.nreg = 4,
		.nloop = 1,
		.eop = 1
	};
	GIF_PACKAGE_REG(package) {
		.lo.prim = { .prim = gs_sprite },
		.hi.rgbaq = rbaq
        };
	GIF_PACKAGE_REG(package) {
		.lo.xyz2 = {
			.x = gs_fbcs_to_pcs(region->dx),
			.y = gs_fbcs_to_pcs(region->dy)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(region->dx + region->width),
			.y = gs_fbcs_to_pcs(region->dy + region->height)
		}
        };

	return package - base_package;
}

static size_t package_copyarea(union package *package,
	const struct fb_copyarea *area, const struct fb_var_screeninfo *var)
{
	union package * const base_package = package;
	const int psm = bpp_to_psm(var->bits_per_pixel);
	const int fbw = var_to_fbw(var);

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = {
			.spsm = psm, .sbw = fbw,
			.dpsm = psm, .dbw = fbw
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = {
			.ssax = area->sx, .ssay = area->sy,
			.dsax = area->dx, .dsay = area->dy,
			.dir  = area->dy < area->sy ||
				(area->dy == area->sy && area->dx < area->sx) ?
				gs_trxpos_dir_ul_lr : gs_trxpos_dir_lr_ul
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = {
			.rrw = area->width,
			.rrh = area->height
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_local_to_local }
	};

	return package - base_package;
}

int dma_write(struct dma_tag *tags, size_t count)
{
	/* Check if previous transmission finished. */
	if ((inl(D2_CHCR) & 0x100) != 0)
		return -EBUSY;

	if (count > 0) {
		const size_t size = count * sizeof(*tags);
		const dma_addr_t tadr =
			dma_map_single(NULL, tags, size, DMA_TO_DEVICE);

		outl(tadr, D2_TADR);
		outl(0, D2_QWC);
		outl(CHCR_SENDC, D2_CHCR);

		dma_unmap_single(NULL, tadr, size, DMA_TO_DEVICE); /* FIXME: At end? */
	}

	return 0;
}
EXPORT_SYMBOL(dma_write);	/* FIXME */

static size_t package_bitbltbuf(union package *package,
	struct fb_info *info, const int dy, const int sy, const int h)
{
	const union package * const base_package = package;
	const size_t image_qwc = gif_quadword_count(info->fix.line_length * h);
	const int psm = bpp_to_psm(info->var.bits_per_pixel);
	const int fbw = var_to_fbw(&info->var);
	const int fbp = 0;

	DMA_PACKAGE_TAG(package) {
		.id = dma_tag_id_cnt,
		.qwc = 6	/* 6 GIF quadwords follow */
	};

	/*
	 * Use BITBLTBUF to copy image data. BITBLTBUF requires TRXPOS,
	 * TRXREG and TRXDIR (which starts the transmission) followed by
	 * the image data.
	 */
	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = { .dpsm = psm, .dbw = fbw, .dbp = fbp }
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = { .dsax = 0, .dsay = dy }
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = { .rrw = info->var.xres, .rrh = h }
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_host_to_local }
	};

	GIF_PACKAGE_TAG(package) {
		.flg = gif_image_mode,
		.nloop = image_qwc,
		.eop = 1
	};

	DMA_PACKAGE_TAG(package) {
		.addr = info->fix.smem_start + sy * info->fix.line_length,
		.id = dma_tag_id_ref,
		.qwc = image_qwc
	};

	return package - base_package;
}

static size_t synch_screen_section(union package *package,
	struct fb_info *info, const int ydst, const int ysrc, const int h)
{
	const union package * const base_package = package;

	/*
	 * The size of the image for BITBLTBUF is limited by the GIF TAG
	 * NLOOP (15 bits) and the DMA TAG QWC (16 bits) quadword (16 bytes)
	 * counters. Compute the maximum number of lines to transfer in one
	 * GIF DMA package. Use NLOOP as the limit since it's the smallest.
	 */
	const int dmax = (16 * GIF_TAG_NLOOP_MAX) / info->fix.line_length;
	const int d = min(h, dmax);
	int y;

	for (y = 0; y < h; y += d)
		package += package_bitbltbuf(package, info,
			ydst + y, ysrc + y, min(d, h - y));

	return package - base_package;
}

static void synch_screen(struct fb_info *info)
{
        if (gif_ready()) {
		struct ps2fb_par *par = info->par;
		union package * const base_package = par->package_buffer;
		union package *package = base_package;
		const int yo = info->var.yoffset % info->var.yres_virtual;
		const int h0 = min_t(int,
			info->var.yres_virtual - yo, info->var.yres);
		const int h1 = info->var.yres - h0;

		package += synch_screen_section(package, info, 0, yo, h0);
		package += synch_screen_section(package, info, h0, 0, h1);

		/* FIXME: Restriction p. 57? */
		DMA_PACKAGE_TAG(package) {
			.id = dma_tag_id_cnt,
			.qwc = 0
		};

		DMA_PACKAGE_TAG(package) {
			.id = dma_tag_id_end,
			.qwc = 0
		};

		dma_write(&base_package->dma, package - base_package);
	}
}

void ps2fb_cb_fillrect(struct fb_info *info, const struct fb_fillrect *region)
{
	struct ps2fb_par *par = info->par;
	const struct gs_rgbaq color = console_pseudo_palette(par, region->color);
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (region->width < 1 || region->height < 1)
		return;
	if (region->rop != ROP_COPY)
		return;		/* ROP_XOR isn't handled. */

	spin_lock_irqsave(&par->lock, flags);

        if (gif_ready()) {
		union package * const base_package = par->package_buffer;
		union package *package = base_package;

		package += package_sprite(package, region, color);

		gif_write(&base_package->gif, package - base_package);
	}

	spin_unlock_irqrestore(&par->lock, flags);
}

void ps2fb_cb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	const enum gs_pixel_format psm = bpp_to_psm(info->var.bits_per_pixel);
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;
	if (area->width < 1 || area->height < 1)
		return;
	if (!valid_bitbltbuf_width(area->width, psm))
		return;      /* Some widths cannot be copied with BITBLTBUF */

	spin_lock_irqsave(&par->lock, flags);

        if (gif_ready()) {
		union package * const base_package = par->package_buffer;
		union package *package = base_package;

		package += package_copyarea(package, area, &info->var);

		gif_write(&base_package->gif, package - base_package);
	}

	spin_unlock_irqrestore(&par->lock, flags);
}

static size_t package_point_tag(struct ps2fb_par *par,
	union package * const base_package, union package *package,
	const u32 area, const u32 i)
{
	union package * const tag_package = package;
	const u32 remaining = area - i;
	u32 capacity;

	/* Prepare for the first pixel. */
	if (i == 0) {
		GIF_PACKAGE_TAG(package) {
			.flg = gif_reglist_mode,
			.reg0 = gif_reg_prim,
			.nreg = 1,
			.nloop = 1
		};
		GIF_PACKAGE_REG(package) {
			.lo.prim = { .prim = gs_point }
		};
	}

	capacity = par->package_capacity - (package - base_package) - 1;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_reglist_mode,
		.reg0 = gif_reg_rgbaq,
		.reg1 = gif_reg_xyz2,
		.nreg = 2,
		.nloop = min(remaining, capacity),
		.eop = (remaining <= capacity)
	};

	return package - tag_package;
}

static u32 pixel(const struct fb_image * const image, const int x, const int y)
{
	if (image->depth == 1)
		return (image->data[y*(image->width >> 3) + (x >> 3)] &
			(0x80 >> (x & 0x7))) ?
			image->fg_color : image->bg_color;

	if (image->depth == 8)
		return image->data[y*image->width + x];

	return 0;	/* FIXME: Other pixel depths */
}

static void ps2fb_cb_imageblit(struct fb_info *info,
	const struct fb_image *image)
{
	struct ps2fb_par *par = info->par;
	const u32 area = image->width * image->height;
	union package * const base_package = par->package_buffer;
	union package *package = base_package;
	unsigned long flags;
	u32 i, x, y;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	spin_lock_irqsave(&par->lock, flags);

        if (!gif_ready())
		goto timeout;

	for (y = i = 0; y < image->height; y++)
	for (x = 0; x < image->width; x++, i++) {
		const struct gs_rgbaq rgbaq =
			console_pseudo_palette(par, pixel(image, x, y));

		if (package - base_package == par->package_capacity) {
			gif_write(&base_package->gif, package - base_package);

			if (!gif_ready())
				goto timeout;

			package = base_package;
		}

		if (package == base_package)
			package += package_point_tag(par,
				base_package, package, area, i);

		GIF_PACKAGE_REG(package) {
			.lo.rgbaq = rgbaq,
			.hi.xyz2 = {
				.x = gs_fbcs_to_pcs(image->dx + x),
				.y = gs_fbcs_to_pcs(image->dy + y)
			},
		};
	}

	gif_write(&base_package->gif, package - base_package);

timeout:
	spin_unlock_irqrestore(&par->lock, flags);
}

static void ps2fb_cb_texflush(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	union package * const base_package = par->package_buffer;
	union package *package = base_package;
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	spin_lock_irqsave(&par->lock, flags);

        if (!gif_ready())
		goto timeout;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 1
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_texflush
	};

	gif_write(&base_package->gif, package - base_package);

timeout:
	spin_unlock_irqrestore(&par->lock, flags);
}

static size_t package_psmt4_texture(union package *package,
	const struct fb_image *image)
{
	union package * const base_package = package;
	const u32 texels_per_quadword = 32;	/* PSMT4 are 4 bit texels */
	u32 x, y;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_image_mode,
		.nloop = image->width * image->height / texels_per_quadword,
		.eop = 1
	};

	for (y = 0; y < image->height; y++)
	for (x = 0; x < image->width; x += 2) {
		const int p0 = pixel(image, x + 0, y);
		const int p1 = pixel(image, x + 1, y);
		const int i = 4*y + x/2;

		package[i/16].gif.image[i%16] = (p1 ? 0x10 : 0) | (p0 ? 0x01 : 0);
	}

	package += 2;	/* FIXME: Support other font sizes */

	return package - base_package;
}

static void write_cb_tile(struct fb_info *info, const int tile_index,
	const struct fb_image *image)
{
	struct ps2fb_par *par = info->par;
	const struct tile_texture tt = texture_for_tile(info, tile_index);
	union package * const base_package = par->package_buffer;
	union package *package = base_package;

        if (!gif_ready())
		return;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = {
			.dpsm = gs_psmt4,
			.dbw = GS_PSMT4_BLOCK_WIDTH / 64,
			.dbp = tt.tbp
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = {
			.dsax = tt.u,
			.dsay = tt.v
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = {
			.rrw = image->width,
			.rrh = image->height
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_host_to_local }
	};

	package += package_psmt4_texture(package, image);

	gif_write(&base_package->gif, package - base_package);
}

static void ps2fb_cb_settile(struct fb_info *info, struct fb_tilemap *map)
{
	struct ps2fb_par *par = info->par;
	const u8 *font = map->data;
	int i;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	if (!map->data) {
		// FIXME: WARN_ON_ONCE(map->data == NULL);
		return;	/* FIXME: Why is fb_settile called with NULL? */
	}

	if (map->width != 8 || map->height != 8 ||		/* FIXME */
	    map->depth != 1 || map->length != 256) {
		fb_err(info, "Unsupported font parameters: width %d height %d depth %d length %d\n",
		       map->width, map->height, map->depth, map->length);
		return;
	}

	par->cb.tile.width = map->width;
	par->cb.tile.height = map->height;
	par->cb.tile.block.cols = GS_PSMT4_BLOCK_WIDTH / map->width;
	par->cb.tile.block.rows = GS_PSMT4_BLOCK_HEIGHT / map->height;

	for (i = 0; i < map->length; i++) {
		const struct fb_image image = {
			.dx = map->width * i,		/* FIXME: 2048 limit */
			.dy = 0,			/* FIXME */
			.width = map->width,
			.height = map->height,
			.fg_color = 1,
			.bg_color = 0,
			.depth = 1,
			.data = &font[i * map->width * map->height / 8],
			/* FIXME: struct fb_cmap cmap */
		};
		unsigned long flags;

		spin_lock_irqsave(&par->lock, flags);
		write_cb_tile(info, i, &image);
		spin_unlock_irqrestore(&par->lock, flags);
	}

	ps2fb_cb_texflush(info);
}

static size_t package_palette(struct fb_info *info,
	union package *package, const int bg, const int fg)
{
	struct ps2fb_par *par = info->par;
	union package * const base_package = par->package_buffer;
	const struct gs_rgbaq bg_rgbaq = console_pseudo_palette(par, bg);
	const struct gs_rgbaq fg_rgbaq = console_pseudo_palette(par, fg);

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 4
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_bitbltbuf,
		.data.bitbltbuf = {
			.dpsm = gs_psmct32,
			.dbw = 1,	/* Palette is one block wide */
			.dbp = color_base_pointer(info)
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxpos,
		.data.trxpos = {
			.dsax = 0,
			.dsay = 0
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxreg,
		.data.trxreg = {
			.rrw = 2,	/* Background and foreground color */
			.rrh = 1
		}
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_trxdir,
		.data.trxdir = { .xdir = gs_trxdir_host_to_local }
	};

	GIF_PACKAGE_TAG(package) {
		.flg = gif_image_mode,
		.nloop = 1,
		.eop = 1
	};
	package->gif.rgba32[0] = (struct gs_rgba32) {
		.r = bg_rgbaq.r,
		.g = bg_rgbaq.g,
		.b = bg_rgbaq.b,
		.a = bg_rgbaq.a
	};
	package->gif.rgba32[1] = (struct gs_rgba32) {
		.r = fg_rgbaq.r,
		.g = fg_rgbaq.g,
		.b = fg_rgbaq.b,
		.a = fg_rgbaq.a
	};
	package++;

	GIF_PACKAGE_TAG(package) {
		.flg = gif_packed_mode,
		.reg0 = gif_reg_ad,
		.nreg = 1,
		.nloop = 1
	};
	GIF_PACKAGE_AD(package) {
		.addr = gs_addr_texflush
	};

	return package - base_package;
}

static void write_tilefill(struct fb_info *info, const struct fb_tilerect rect)
{
	const struct tile_texture tt = texture_for_tile(info, rect.index);
	struct ps2fb_par *par = info->par;
	union package * const base_package = par->package_buffer;
	union package *package = base_package;
	const u32 cbp = color_base_pointer(info);
	const u32 tw = par->cb.tile.width;
	const u32 th = par->cb.tile.height;
	const u32 dsax = tw * rect.sx;
	const u32 dsay = th * rect.sy;
	const u32 rrw = tw * rect.width;
	const u32 rrh = th * rect.height;

	/* Determine whether background or foreground color needs update. */
	const bool cld = (par->cb.bg != rect.bg || par->cb.fg != rect.fg);

        if (!gif_ready())
		return;

	if (cld) {
		package += package_palette(info, package, rect.bg, rect.fg);
		par->cb.bg = rect.bg;
		par->cb.fg = rect.fg;
	}

	GIF_PACKAGE_TAG(package) {
		.flg = gif_reglist_mode,
		.reg0 = gif_reg_prim,
		.reg1 = gif_reg_nop,
		.reg2 = gif_reg_tex0_1,
		.reg3 = gif_reg_clamp_1,
		.reg4 = gif_reg_uv,
		.reg5 = gif_reg_xyz2,
		.reg6 = gif_reg_uv,
		.reg7 = gif_reg_xyz2,
		.nreg = 8,
		.nloop = 1,
		.eop = 1
	};
	GIF_PACKAGE_REG(package) {
		.lo.prim = {
			.prim = gs_sprite,
			.tme = gs_texturing_on,
			.fst = gs_texturing_uv
		}
	};
	GIF_PACKAGE_REG(package) {
		.lo.tex0 = {
			.tbp0 = tt.tbp,
			.tbw = GS_PSMT4_BLOCK_WIDTH / 64,
			.psm = gs_psmt4,
			.tw = 5,	/* 2^5 = 32 texels wide PSMT4 block */
			.th = 4,	/* 2^4 = 16 texels high PSMT4 block */
			.tcc = gs_tcc_rgba,
			.tfx = gs_tfx_decal,
			.cbp = cbp,
			.cpsm = gs_psmct32,
			.csm = gs_csm1,
			.cld = cld ? 1 : 0
		},
		.hi.clamp_1 = {
			.wms = gs_clamp_region_repeat,
			.wmt = gs_clamp_region_repeat,
			.minu = tw - 1,	/* Mask, tw is always a power of 2 */
			.maxu = tt.u,
			.minv = th - 1,	/* Mask, th is always a power of 2 */
			.maxv = tt.v
		}
	};
	GIF_PACKAGE_REG(package) {
		.lo.uv = {
			.u = gs_pxcs_to_tcs(tt.u),
			.v = gs_pxcs_to_tcs(tt.v)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(dsax),
			.y = gs_fbcs_to_pcs(dsay)
		}
	};
	GIF_PACKAGE_REG(package) {
		.lo.uv = {
			.u = gs_pxcs_to_tcs(tt.u + rrw),
			.v = gs_pxcs_to_tcs(tt.v + rrh)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(dsax + rrw),
			.y = gs_fbcs_to_pcs(dsay + rrh)
		}
	};

	gif_write(&base_package->gif, package - base_package);
}

static void ps2fb_cb_tilecopy(struct fb_info *info, struct fb_tilearea *area)
{
	const struct ps2fb_par *par = info->par;
	const u32 tw = par->cb.tile.width;
	const u32 th = par->cb.tile.height;
	const struct fb_copyarea a = {
		.dx	= tw * area->dx,
		.dy	= th * area->dy,
		.width	= tw * area->width,
		.height	= th * area->height,
		.sx	= tw * area->sx,
		.sy	= th * area->sy
	};

	ps2fb_cb_copyarea(info, &a);
}

static void ps2fb_cb_tilefill(struct fb_info *info, struct fb_tilerect *rect)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	spin_lock_irqsave(&par->lock, flags);

	write_tilefill(info, *rect);

	spin_unlock_irqrestore(&par->lock, flags);
}

static void ps2fb_cb_tileblit(struct fb_info *info, struct fb_tileblit *blit)
{
	struct ps2fb_par *par = info->par;
	int i = 0, dx, dy;

	if (info->state != FBINFO_STATE_RUNNING)
		return;

	for (dy = 0; i < blit->length && dy < blit->height; dy++)
	for (dx = 0; i < blit->length && dx < blit->width; dx++, i++) {
		unsigned long flags;

		spin_lock_irqsave(&par->lock, flags);

		write_tilefill(info, (struct fb_tilerect) {
			.sx = blit->sx + dx,
			.sy = blit->sy + dy,
			.width = 1,
			.height = 1,
			.index = blit->indices[i],
			.fg = blit->fg,
			.bg = blit->bg
		}); /* FIXME: Optimise this */

		spin_unlock_irqrestore(&par->lock, flags);
	}
}

static void ps2fb_cb_tilecursor(struct fb_info *info, struct fb_tilecursor *cursor)
{
	/* FIXME */
}

static int ps2fb_cb_get_tilemax(struct fb_info *info)
{
	const struct ps2fb_par *par = info->par;
	const u32 block_tile_count =
		par->cb.tile.block.cols *
		par->cb.tile.block.rows;
	const s32 blocks_free =
		GS_BLOCK_COUNT - par->cb.block_count - PALETTE_BLOCK_COUNT;

	return blocks_free >= 0 ? blocks_free * block_tile_count : 0;
}

static struct fb_tile_ops ps2_tile_ops = {
	.fb_settile	= ps2fb_cb_settile,
	.fb_tilecopy	= ps2fb_cb_tilecopy,
	.fb_tilefill    = ps2fb_cb_tilefill,
	.fb_tileblit    = ps2fb_cb_tileblit,
	.fb_tilecursor  = ps2fb_cb_tilecursor,
	.fb_get_tilemax = ps2fb_cb_get_tilemax
};

static void invalidate_palette(struct ps2fb_par *par)
{
	par->cb.bg = ~0;
	par->cb.fg = ~0;
}

static int ps2fb_setcolreg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *info)
{
	const struct gs_rgba32 color = {
		.r = red    >> 8,
		.g = green  >> 8,
		.b = blue   >> 8,
		.a = transp >> 8
	};
	struct ps2fb_par *par = info->par;
	unsigned long flags;

	if (regno >= PALETTE_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&par->lock, flags);

	par->pseudo_palette[regno] = color;
	invalidate_palette(par);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static void write_cb_pan_display(const struct fb_var_screeninfo *var)
{
	const struct gs_display display = gs_read_display1();
	const int psm = bpp_to_psm(var->bits_per_pixel);
	const int fbw = var_to_fbw(var);
	const int yo = var->yoffset % var->yres_virtual;
	const int dh1 = min_t(int, var->yres_virtual - yo, var->yres);
	const int dh2 = var->yres - dh1;

	gs_write_display1((struct gs_display) {
		.dh = dh1 - 1,
		.dw = display.dw,
		.magv = display.magv,
		.magh = display.magh,
		.dy = display.dy,
		.dx = display.dx,
	});

	gs_write_display2((struct gs_display) {
		.dh = dh2 - 1,
		.dw = display.dw,
		.magv = display.magv,
		.magh = display.magh,
		.dy = display.dy + dh1,
		.dx = display.dx,
	});

	gs_write_dispfb1((struct gs_dispfb12) {
		.fbw = fbw,
		.psm = psm,
		.dbx = var->xoffset,
		.dby = yo
	});

	gs_write_dispfb2((struct gs_dispfb12) {
		.fbw = fbw,
		.psm = psm,
		.dbx = var->xoffset,
		.dby = 0,
	});

	gs_write_pmode((struct gs_pmode) {
		.en1 = 1,
		.en2 = dh2 ? 1 : 0,
		.crtmd = 1
	});
}

static bool changed_cb_pan_display(const struct fb_var_screeninfo *var)
{
	const struct gs_dispfb12 dspfb12 = gs_read_dispfb1();
	const int yo = var->yoffset % var->yres_virtual;

	return dspfb12.dbx != var->xoffset || dspfb12.dby != yo;
}

static int ps2fb_vb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&par->lock, flags);

	if (var->xoffset) {
		err = -EINVAL;
		goto out;
	}

out:
	spin_unlock_irqrestore(&par->lock, flags);

	return err;
}

static int ps2fb_cb_pan_display(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	return var->xoffset + var->xres <= var->xres_virtual ? 0 : -EINVAL;
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

        /* Check virtual resolution. */
        if (var->xres_virtual < var->xres)
           var->xres_virtual = var->xres;
        if (var->yres_virtual < var->yres)
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
		BUG();

        /* Screen rotations are not supported. */
	if (var->rotate)
		return -EINVAL;

	/* FIXME: var->greyscale */
	/* FIXME: var->colorspace */
	/* FIXME: vm->flag? */

        return 0;
}

static int ps2fb_vb_check_var(
	struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&par->lock, flags);

	err = ps2fb_check_var(var, info);
	if (err < 0)
		goto out;

	/*
	 * The virtual frame buffer is transferred to GS local memory using
	 * DMA, which only supports interleave mode for the scratchpad memory.
	 * Hence the virtual frame buffer cannot be wider than the screen.
	 */
        if (var->xres_virtual != var->xres)
		err = -EINVAL;

out:
	spin_unlock_irqrestore(&par->lock, flags);

	return err;
}

static int ps2fb_cb_check_var(
	struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	/* FIXME: Only depth = 1 */

	spin_lock_irqsave(&par->lock, flags);
	err = ps2fb_check_var(var, info);
	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		if (info->tileops->fb_get_tilemax(info) < 256)
			err = -ENOMEM;

	return err;
}

static u32 div_round_ps(u32 a, u32 b)
{
	return DIV_ROUND_CLOSEST_ULL(a * 1000000000000ll, b);
}

static u32 vck_to_pixclock(const u32 vck, const u32 spml)
{
	return div_round_ps(spml, vck);
}

u32 gs_video_clock(const u32 t1248, const u32 lc, const u32 rc)
{
	return (13500000 * lc) / ((t1248 + 1) * rc);
}

u32 gs_video_clock_for_smode1(const struct gs_smode1 smode1)
{
	return gs_video_clock(smode1.t1248, smode1.lc, smode1.rc);
}

static u32 pck_to_rfsh(const u32 pck)
{
	return pck < 20000000 ? 8 :
	       pck < 70000000 ? 4 : 2;
}

static enum gs_cmod vm_to_cmod(const struct fb_videomode *vm)
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
		       gs_cmod_default;

	return gs_cmod_default; /* FIXME: gs_cmod_vesa? */
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
	const struct fb_videomode *vm, const struct synch_gen sg)
{
	const u32 spml  = sg.spml;
	const u32 t1248 = sg.t1248;
	const u32 lc    = sg.lc;
	const u32 rc    = sg.rc;
        const u32 vc    = vm->yres <= 576 ? 1 : 0;
	const u32 vck   = gs_video_clock(t1248, lc, rc);
	const u32 pck   = vck / spml;
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
			.rfsh = pck_to_rfsh(pck)
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
	const struct fb_videomode *vm, const struct synch_gen sg)
{
	const u32 spml  = sg.spml;
	const u32 t1248 = sg.t1248;
	const u32 lc    = sg.lc;
	const u32 rc    = sg.rc;
	const u32 vck   = gs_video_clock(t1248, lc, rc);
	const u32 pck   = vck / spml;
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
			.rfsh = pck_to_rfsh(pck)
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
	const struct fb_videomode *vm, const struct synch_gen sg)
{
	const bool bc = vm->sync & FB_SYNC_BROADCAST;
	const bool il = vm->vmode & FB_VMODE_INTERLACED;

	return vm->yres <  480 &&       bc ? vm_to_sp_sdtv(vm) :
	       vm->yres <= 576 && il && bc ? vm_to_sp_sdtv(vm) :
				        bc ? vm_to_sp_hdtv(vm, sg) :
				             vm_to_sp_vesa(vm, sg);
}

static struct gs_sync_param vm_to_sp(const struct fb_videomode *vm)
{
	static const struct synch_gen preferred[] = {
		{ .spml = 2, .t1248 = 1, .lc = 15, .rc = 2 }, /*  50.625 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 32, .rc = 4 }, /*  54.000 MHz */
		{ .spml = 4, .t1248 = 1, .lc = 32, .rc = 4 }, /*  54.000 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 28, .rc = 3 }, /*  63.000 MHz */
		{ .spml = 1, .t1248 = 1, .lc = 22, .rc = 2 }, /*  74.250 MHz */
		{ .spml = 1, .t1248 = 1, .lc = 35, .rc = 3 }, /*  78.750 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 71, .rc = 6 }, /*  79.875 MHz */
		{ .spml = 2, .t1248 = 1, .lc = 44, .rc = 3 }, /*  99.000 MHz */
		{ .spml = 1, .t1248 = 0, .lc =  8, .rc = 1 }, /* 108.000 MHz */
		{ .spml = 2, .t1248 = 0, .lc = 58, .rc = 6 }, /* 130.500 MHz */
		{ .spml = 1, .t1248 = 0, .lc = 10, .rc = 1 }, /* 135.000 MHz */
		{ .spml = 1, .t1248 = 1, .lc = 22, .rc = 1 }  /* 148.500 MHz */
	};

	struct gs_sync_param sp = { };
	struct synch_gen sg = { };
	u32 spml, t1248, lc, rc;
	int best = -1;
	int diff, i;

	for (i = 0; i < ARRAY_SIZE(preferred); i++) {
		spml  = preferred[i].spml;
		t1248 = preferred[i].t1248;
		lc    = preferred[i].lc;
		rc    = preferred[i].rc;

		diff = abs(vm->pixclock -
			vck_to_pixclock(gs_video_clock(t1248, lc, rc), spml));

		if (best == -1 || diff < best) {
			best = diff;
			sg = (struct synch_gen) {
				.spml = spml, .t1248 = t1248, .lc = lc, .rc = rc
			};
		}
	}

	for (spml  = 1; spml  <   5; spml++)
	for (t1248 = 0; t1248 <   2; t1248++)
	for (lc    = 1; lc    < 128; lc++)
	for (rc    = 1; rc    <   7; rc++) {
		diff = abs(vm->pixclock -
			vck_to_pixclock(gs_video_clock(t1248, lc, rc), spml));

		if (best == -1 || diff < best) {
			best = diff;
			sg = (struct synch_gen) {
				.spml = spml, .t1248 = t1248, .lc = lc, .rc = rc
			};
		}
	}

	sp = vm_to_sp_for_synch_gen(vm, sg);
	sp.smode1.gcont = 1;	/* For YCrCb output */
	sp.smode1.sint = 1;
	sp.smode1.prst = 0;

	return sp;
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

struct margin_adjustment {
	int dx;
	int dy;
};

static struct margin_adjustment margin_adjustment(void)
{
	char sx = '+', sy = '+';
	int dx = 0, dy = 0;

	bool valid =
		sscanf(mode_adjust, "%c%d%c%d", &sx, &dx, &sy, &dy) == 4 &&
		(sx == '-' || sx == '+') &&
		(sy == '-' || sy == '+');

	printk("margin_adjustment %d %d '%s' %d\n", dx, dy, mode_adjust, valid);	// FIXME

	if (!valid)
		return (struct margin_adjustment) { };

	return (struct margin_adjustment) {
		.dx = (sx == '-' ? -dx : dx),
		.dy = (sy == '-' ? -dy : dy)
	};
}

static int ps2fb_set_par(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	const struct margin_adjustment adjust = margin_adjustment();
	const struct fb_var_screeninfo *var = &info->var;
	const struct fb_videomode *mm = fb_match_mode(var, &info->modelist);
	const struct fb_videomode vm = (struct fb_videomode) {
		.refresh      = refresh_for_var(var),
		.xres         = var->xres,
		.yres         = var->yres,
		.pixclock     = var->pixclock,
		.left_margin  = var->left_margin  + adjust.dx,
		.right_margin = var->right_margin - adjust.dx,
		.upper_margin = var->upper_margin + adjust.dy,
		.lower_margin = var->lower_margin - adjust.dy,
		.hsync_len    = var->hsync_len,
		.vsync_len    = var->vsync_len,
		.sync         = var->sync,
		.vmode        = var->vmode,
		.flag         = mm != NULL ? mm->flag : 0
	};
	const struct gs_sync_param sp = vm_to_sp(&vm);
	struct gs_smode1 smode1 = sp.smode1;

	par->mode = vm;
	invalidate_palette(par);

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.xpanstep = var->xres_virtual > var->xres ? 1 : 0;
	info->fix.ypanstep = var->yres_virtual > var->yres ? 1 : 0;
	info->fix.ywrapstep = 1;
	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;

	gs_write_smode1(smode1);
	gs_write_smode2(sp.smode2);
	gs_write_srfsh(sp.srfsh);
	gs_write_synch1(sp.synch1);
	gs_write_synch2(sp.synch2);
	gs_write_syncv(sp.syncv);
	gs_write_display1(sp.display);

	gs_write_dispfb1((struct gs_dispfb12) {
		.fbw = var_to_fbw(var),
		.psm = bpp_to_psm(var->bits_per_pixel),
		.dbx = var->xoffset,
		.dby = var->yoffset,
	});

	gs_write_pmode((struct gs_pmode) {
		.en1 = 1,
		.crtmd = 1
	});

	smode1.prst = 1;
	gs_write_smode1(smode1);
	udelay(2500);			/* 2.5 ms FIXME: spinlock */

	smode1.sint = 0;
	smode1.prst = 0;
	gs_write_smode1(smode1);

	return 0;
}

static int ps2fb_vb_set_par(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&par->lock, flags);

	err = ps2fb_set_par(info);

	spin_unlock_irqrestore(&par->lock, flags);

	return err;
}

static int ps2fb_cb_set_par(struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&par->lock, flags);

	err = ps2fb_set_par(info);
	if (!err) {
		par->cb.block_count = var_to_block_count(&info->var);

		write_cb_environment(info);
		write_cb_pan_display(&info->var);
	}

	spin_unlock_irqrestore(&par->lock, flags);

	if (!err && info->tileops)
		fb_info(info, "%d tiles maximum for %ux%u font\n",
			info->tileops->fb_get_tilemax(info),
			par->cb.tile.width, par->cb.tile.height);

	return err;
}

static int ps2fb_blank(int blank, struct fb_info *info)
{
	struct ps2fb_par *par = info->par;
	struct gs_smode2 smode2;
	unsigned long flags;

	spin_lock_irqsave(&par->lock, flags);

	smode2 = gs_read_smode2();
	smode2.dpms = blank == FB_BLANK_POWERDOWN     ? gs_vesa_dpms_off :
		      blank == FB_BLANK_NORMAL        ? gs_vesa_dpms_standby :
		      blank == FB_BLANK_VSYNC_SUSPEND ? gs_vesa_dpms_suspend :
		      blank == FB_BLANK_HSYNC_SUSPEND ? gs_vesa_dpms_suspend :
							gs_vesa_dpms_on;
	gs_write_smode2(smode2);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

static u32 ps2fb_vblank_count(struct ps2fb_par *par)
{
	unsigned long flags;
	u32 count;

	spin_lock_irqsave(&par->lock, flags);
	count = par->vblank.count;
	spin_unlock_irqrestore(&par->lock, flags);

	return count;
}

static int ps2fb_wait_for_vsync(struct ps2fb_par *par, const u32 crt)
{
	const u32 count = ps2fb_vblank_count(par);

	if (!wait_event_timeout(par->vblank_queue,
		count != ps2fb_vblank_count(par), msecs_to_jiffies(100)))
		return -EIO;

	return 0;
}

static int ps2fb_vb_ioctl(struct fb_info *info, unsigned int cmd,
	unsigned long arg)
{
	struct ps2fb_par *par = info->par;
	void __user *argp = (void __user *)arg;
	int ret = -EFAULT;

	switch (cmd) {
	case FBIOGET_VBLANK:
	{
		struct fb_vblank vblank;
		unsigned long flags;

		spin_lock_irqsave(&par->lock, flags);
		vblank = par->vblank;
		spin_unlock_irqrestore(&par->lock, flags);

		if (copy_to_user(argp, &vblank, sizeof(vblank)))
			break;

		ret = 0;
		break;
	}

	case FBIO_WAITFORVSYNC:
	{
		u32 crt;	/* FIXME: Use for what? */

		if (get_user(crt, (u32 __user *) arg))
			break;

		ret = ps2fb_wait_for_vsync(par, crt);
		break;
	}

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

static void fill_modes(struct device *dev, struct list_head *head)
{
	int i;

	INIT_LIST_HEAD(head);

	for (i = 0; i < ARRAY_SIZE(standard_modes); i++)
		if (fb_add_videomode(&standard_modes[i], head) < 0)
			dev_err(dev, "fb_add_videomode failed\n");
}

static irqreturn_t ps2fb_vb_vsync_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct fb_info *info = platform_get_drvdata(pdev);

	if (info != NULL) {	/* FIXME: Enable vblank later? */
		struct ps2fb_par *par = info->par;
		unsigned long flags;

		if (irq == IRQ_GS_VSYNC) {
			spin_lock_irqsave(&par->lock, flags);

			par->vblank.count++;

			synch_screen(info);

			wake_up(&par->vblank_queue);

			spin_unlock_irqrestore(&par->lock, flags);
		}
	}

	return IRQ_HANDLED;	/* FIXME: Indicate possibly not handled? */
}

static irqreturn_t ps2fb_cb_vsync_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct fb_info *info = platform_get_drvdata(pdev);

	if (info != NULL) {	/* FIXME: Enable vblank later? */
		struct ps2fb_par *par = info->par;
		unsigned long flags;

		if (irq == IRQ_GS_VSYNC) {
			spin_lock_irqsave(&par->lock, flags);

			par->vblank.count++;

			if (changed_cb_pan_display(&info->var))
				write_cb_pan_display(&info->var);

			wake_up(&par->vblank_queue);

			spin_unlock_irqrestore(&par->lock, flags);
		}
	}

	return IRQ_HANDLED;	/* FIXME: Indicate possibly not handled? */
}

static int init_virtual_buffer(struct platform_device *pdev,
	struct fb_info *info)
{
	static struct fb_ops fbops = {
		.owner		= THIS_MODULE,
		.fb_blank	= ps2fb_blank,
		.fb_setcolreg	= ps2fb_setcolreg,
		.fb_set_par	= ps2fb_vb_set_par,
		.fb_check_var	= ps2fb_vb_check_var,
		.fb_pan_display	= ps2fb_vb_pan_display,
		.fb_ioctl	= ps2fb_vb_ioctl,
		.fb_fillrect	= sys_fillrect,
		.fb_copyarea	= sys_copyarea,
		.fb_imageblit	= sys_imageblit,
	};

	/*
	 * Frame buffer memory does not necessarily need to be physically
	 * continous, as long as (a) it's not swapped out and (b) scatter-
	 * gather DMA is used to assemble the pages.
	 */
	const int order = get_order(1024 * vfb_ram);
	const size_t screen_size = (1 << order) * PAGE_SIZE; /* FIXME: Helper function? */
	const unsigned long screen_buffer =
		__get_free_pages(GFP_KERNEL | GFP_DMA | __GFP_ZERO, order);
	struct ps2fb_par *par = info->par;
	int err;

	fb_info(info, "Graphics Synthesizer virtual frame buffer device\n");

	if (!screen_buffer) {
		dev_err(&pdev->dev, "__get_free_pages %d (%zu bytes) failed\n",
			order, screen_size);
		err = -ENOMEM;
		goto err_page_alloc;
	}

	info->screen_size = screen_size;
	info->screen_base = (char __iomem *)screen_buffer;

	info->fix.smem_len = info->screen_size;
	info->fix.smem_start = __pa(screen_buffer);

	info->fbops = &fbops;
	info->flags = FBINFO_DEFAULT |
		      FBINFO_VIRTFB |
		      FBINFO_HWACCEL_YPAN |
		      FBINFO_HWACCEL_YWRAP |
		      FBINFO_PARTIAL_PAN_OK |
		      FBINFO_READS_FAST;

	info->pseudo_palette = par->pseudo_palette;

	par->vblank.flags = FB_VBLANK_HAVE_VCOUNT | FB_VBLANK_HAVE_VSYNC;
	err = devm_request_irq(&pdev->dev, IRQ_GS_VSYNC, ps2fb_vb_vsync_interrupt,
		IRQF_SHARED, pdev->name, pdev);
	if (err < 0) {
		dev_err(&pdev->dev, "devm_request_irq failed %d\n", err);
		goto err_vsync_irq;
	}

	return 0;

err_vsync_irq:
	free_pages(screen_buffer, order);
err_page_alloc:
	return err;
}

static int init_console_buffer(struct platform_device *pdev,
	struct fb_info *info)
{
	static struct fb_ops fbops = {
		.owner		= THIS_MODULE,
		.fb_blank	= ps2fb_blank,
		.fb_setcolreg	= ps2fb_setcolreg,
		.fb_set_par	= ps2fb_cb_set_par,
		.fb_check_var	= ps2fb_cb_check_var,
		.fb_pan_display	= ps2fb_cb_pan_display,
		.fb_fillrect	= ps2fb_cb_fillrect,
		.fb_copyarea	= ps2fb_cb_copyarea,
		.fb_imageblit	= ps2fb_cb_imageblit,
	};
	struct ps2fb_par *par = info->par;
	int err;

	fb_info(info, "Graphics Synthesizer console frame buffer device\n");

	info->screen_size = 0;
	info->screen_base = NULL;	/* Mmap is unsupported by hardware */

	info->fix.smem_start = 0;	/* The GS framebuffer is local memory */
	info->fix.smem_len = GS_MEMORY_SIZE;

	info->fbops = &fbops;
	info->flags = FBINFO_DEFAULT |
		      FBINFO_HWACCEL_COPYAREA |
		      FBINFO_HWACCEL_FILLRECT |
		      FBINFO_HWACCEL_IMAGEBLIT |
		      FBINFO_HWACCEL_XPAN |
		      FBINFO_HWACCEL_YPAN |
		      FBINFO_HWACCEL_YWRAP |
		      FBINFO_PARTIAL_PAN_OK |
		      FBINFO_READS_FAST;

	info->flags |= FBINFO_MISC_TILEBLITTING;
	info->tileops = &ps2_tile_ops;

	/* Support for 8x8, 8x16, 16x8 and 16x16 tiles */
	info->pixmap.blit_x = (1 << (8 - 1)) | (1 << (16 - 1));
	info->pixmap.blit_y = (1 << (8 - 1)) | (1 << (16 - 1));

	info->pseudo_palette = par->pseudo_palette;

	par->vblank.flags = FB_VBLANK_HAVE_VCOUNT | FB_VBLANK_HAVE_VSYNC;
	err = devm_request_irq(&pdev->dev, IRQ_GS_VSYNC, ps2fb_cb_vsync_interrupt,
		IRQF_SHARED, pdev->name, pdev);
	if (err < 0) {
		dev_err(&pdev->dev, "devm_request_irq failed %d\n", err);
		goto err_vsync_irq;
	}

	/* 8x8 default font tile size for fb_get_tilemax */
	par->cb.tile.width = 8;
	par->cb.tile.height = 8;
	par->cb.tile.block.cols = GS_PSMT4_BLOCK_WIDTH / par->cb.tile.width;
	par->cb.tile.block.rows = GS_PSMT4_BLOCK_HEIGHT / par->cb.tile.height;

	return 0;

err_vsync_irq:
	return err;
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
	init_waitqueue_head(&par->vblank_queue);

	par->dma_buffer = __get_free_page(GFP_DMA);
	if (!par->dma_buffer) {
		dev_err(&pdev->dev, "Failed to allocate package buffer\n");
		err = -ENOMEM;
		goto err_package_buffer;
	}

	par->package_buffer = (union package *)par->dma_buffer;
	par->package_capacity = PAGE_SIZE / sizeof(union package);

	fill_modes(&pdev->dev, &info->modelist);

	strlcpy(info->fix.id, "PS2 GS", ARRAY_SIZE(info->fix.id));
	info->fix.accel = FB_ACCEL_PLAYSTATION_2;

	if (vfb_ram > 0)
		err = init_virtual_buffer(pdev, info);
	else
		err = init_console_buffer(pdev, info);
	if (err < 0)
		goto err_init_buffer;

        info->var = (struct fb_var_screeninfo) { };
	printk("ps2fb_probe mode_option '%s' mode_adjust '%s'\n", mode_option, mode_adjust);	// FIXME
	if (!fb_find_mode(&info->var, info, mode_option,
			standard_modes, ARRAY_SIZE(standard_modes), NULL, 32)) {
		dev_err(&pdev->dev, "Failed to find video mode \"%s\"\n",
			mode_option);
		err = -EINVAL;
		goto err_find_mode;
	}

	info->mode = &par->mode;

#if 0	/* FIXME */
        err = info->fbops->fb_check_var(&info->var, info);
        if (err < 0) {
		dev_err(&pdev->dev, "Failed to check initial video mode\n");
		goto err_check_var;
	}
        err = info->fbops->fb_set_par(info);
        if (err < 0) {
		dev_err(&pdev->dev, "Failed to set initial video mode\n");
		goto err_set_par;
	}
#endif

	if (fb_alloc_cmap(&info->cmap, PALETTE_SIZE, 0) < 0) {
		dev_err(&pdev->dev, "fb_alloc_cmap failed\n");
		err = -ENOMEM;
		goto err_alloc_cmap;
	}
	fb_set_cmap(&info->cmap, info);

	disable_irq(IRQ_DMAC_2);	/* FIXME: Why is this needed? */

	platform_set_drvdata(pdev, info);

	if (register_framebuffer(info) < 0) {
		dev_err(&pdev->dev, "register_framebuffer failed\n");
		err = -EINVAL;
		goto err_register_framebuffer;
	}

	/* Clear the mode adjustment after setting the initial mode */
	mode_adjust = "";

	return 0;

err_init_buffer:
	unregister_framebuffer(info);
err_register_framebuffer:
	fb_dealloc_cmap(&info->cmap);
#if 0	/* FIXME */
err_set_par:
err_check_var:
#endif
	fb_dealloc_cmap(&info->cmap);

err_alloc_cmap:
	free_page(par->dma_buffer);

err_find_mode:
err_package_buffer:
	framebuffer_release(info);

err_framebuffer_alloc:
	return err;
}

static int ps2fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct ps2fb_par *par = info->par;

	/* FIXME: Ackowledge DMAC_2 to clear? */
	enable_irq(IRQ_DMAC_2);		/* FIXME: Why is this needed? */

	if (info != NULL) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);

#if 0	/* FIXME */
		if (info->screen_base)
			iounmap(info->screen_base);
#endif
		if (info->fix.smem_start)
			free_pages((unsigned long)__va(info->fix.smem_start),
				get_order(info->fix.smem_len));

		framebuffer_release(info);
	}

	free_page(par->dma_buffer);

	return 0;
}

static struct platform_driver ps2fb_driver = {
	.probe		= ps2fb_probe,
	.remove		= ps2fb_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

static int __init ps2fb_init(void)
{
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

		if (!strncmp(this_opt, "vfb_ram:", 8))
			vfb_ram = simple_strtoul(this_opt + 8, NULL, 0);
		else if (!strncmp(this_opt, "mode_option:", 12))
			mode_option = &this_opt[12];
		else if ('0' <= this_opt[0] && this_opt[0] <= '9')
			mode_option = this_opt;
		else if (!strncmp(this_opt, "mode_adjust:", 12))
			mode_adjust = &this_opt[12];
		else
			pr_warn(DEVICE_NAME ": Unrecognized option \"%s\"\n",
				this_opt);
	}

no_options:
#endif /* !MODULE */

	return platform_driver_register(&ps2fb_driver);
}

static void __exit ps2fb_exit(void)
{
	platform_driver_unregister(&ps2fb_driver);
}

module_init(ps2fb_init);
module_exit(ps2fb_exit);

module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option,
	"Specify initial video mode as \"<xres>x<yres>[-<bpp>][@<refresh>]\"");

/*
 * Analogue devices are frequently a few pixels off. Use this mode_adjust
 * option to make necessary device dependent adjustments to the built-in modes.
 */
module_param(mode_adjust, charp, 0);
MODULE_PARM_DESC(mode_adjust,	/* FIXME: mode_margin? */
	"Adjust initial video mode as \"<-|+><dx><-|+><dy>\"");

module_param(vfb_ram, uint, 0);
MODULE_PARM_DESC(vfb_ram,
	"Enable the virtual framebuffer with this amount of memory [KiB]");

MODULE_LICENSE("GPL");
