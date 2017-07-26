// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer interface (GIF)
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#ifndef _UAPI_MIPS_ASM_GIF_H
#define _UAPI_MIPS_ASM_GIF_H

#include <asm/bitfield.h>

#include "gs.h"

#define GIF_SIZE 16	/* 128 bits */	/* FIXME: GIF_TAG_SIZE */
#define GIF_ALIGN(x) (((x) + GIF_SIZE-1) & ~(size_t)(GIF_SIZE-1))
#define gif_quadword_count(x) (((x) + GIF_SIZE-1) >> 4)

/*
 * When transferring data via PATH1 and PATH3 in parallel, do not apply the
 * A+D packing format to the data on PATH3. This may cause the GS to hang.
 */
enum gif_tag_reg {
	gif_reg_prim,        gif_reg_rgbaq,   gif_reg_st,     gif_reg_uv,
	gif_reg_xyzf2,       gif_reg_xyz2,    gif_reg_tex0_1, gif_reg_tex0_2,
	gif_reg_clamp_1,     gif_reg_clamp_2, gif_reg_fog,    /* Reserved */
	gif_reg_xyzf3 = 0xc, gif_reg_xyz3,    gif_reg_ad,     gif_reg_nop
};

enum gif_tag_flg {
	gif_packed_mode, gif_reglist_mode, gif_image_mode, gif_disabled_mode
};

#define GIF_TAG_NLOOP_MAX 0x7fff	/* The NLOOP field is 15 bits */

/*
 * The REGS, NREG, PRIM and PRE fields of the GIF tag are ignored in IMAGE mode.
 */
struct gif_tag {
	__BITFIELD_FIELD(__u64 reg15 : 4,
	__BITFIELD_FIELD(__u64 reg14 : 4,
	__BITFIELD_FIELD(__u64 reg13 : 4,
	__BITFIELD_FIELD(__u64 reg12 : 4,
	__BITFIELD_FIELD(__u64 reg11 : 4,
	__BITFIELD_FIELD(__u64 reg10 : 4,
	__BITFIELD_FIELD(__u64 reg9 : 4,
	__BITFIELD_FIELD(__u64 reg8 : 4,
	__BITFIELD_FIELD(__u64 reg7 : 4,
	__BITFIELD_FIELD(__u64 reg6 : 4,
	__BITFIELD_FIELD(__u64 reg5 : 4,
	__BITFIELD_FIELD(__u64 reg4 : 4,
	__BITFIELD_FIELD(__u64 reg3 : 4,
	__BITFIELD_FIELD(__u64 reg2 : 4,
	__BITFIELD_FIELD(__u64 reg1 : 4,
	__BITFIELD_FIELD(__u64 reg0 : 4,	/* First register descriptor */
	__BITFIELD_FIELD(__u64 nreg : 4,	/* 0 means 16 registers */
	__BITFIELD_FIELD(__u64 flg : 2,		/* Data format mode */
	__BITFIELD_FIELD(__u64 prim : 11,	/* PRIM register */
	__BITFIELD_FIELD(__u64 pre : 1,		/* Enable PRIM field */
	__BITFIELD_FIELD(__u64 : 30,
	__BITFIELD_FIELD(__u64 eop : 1,		/* End of packet */
	__BITFIELD_FIELD(__u64 nloop : 15,
	;)))))))))))))))))))))))
} __attribute__((aligned(GIF_SIZE)));

union gif_reg {
	struct gs_bitbltbuf bitbltbuf;
	struct gs_clamp clamp_1;
	struct gs_clamp clamp_2;
	struct gs_dthe dthe;
	struct gs_frame_12 frame_1;
	struct gs_frame_12 frame_2;
	struct gs_prim prim;
	struct gs_prmode prmode;
	struct gs_prmodecont prmodecont;
	struct gs_rgbaq rgbaq;
	struct gs_scanmsk_12 scanmsk;
	struct gs_scissor_12 scissor_1;
	struct gs_scissor_12 scissor_2;
	struct gs_test_12 test_1;
	struct gs_test_12 test_2;
	struct gs_texa texa;
	struct gs_tex0 tex0;
	struct gs_tex1 tex1;
	struct gs_tex2 tex2;
	struct gs_trxdir trxdir;
	struct gs_trxpos trxpos;
	struct gs_trxreg trxreg;
	struct gs_uv uv;
	struct gs_xyz23 xyz2;
	struct gs_xyz23 xyz3;
	struct gs_xyoffset_12 xyoffset_1;
	struct gs_xyoffset_12 xyoffset_2;
	struct gs_zbuf zbuf;
};

struct gif_packed_ad {
	union gif_reg data;			/* Register data */
	__BITFIELD_FIELD(__u64 : 56,
	__BITFIELD_FIELD(__u64 addr : 8,	/* Register address */
	;))
};

struct gif_data_reg {
	union gif_reg lo;
	union gif_reg hi;
};

union gif_data {
	struct gif_tag tag;
	union {
		struct gif_packed_ad ad;
	} packed;
        struct gif_data_reg reg;
	struct gs_rgba32 rgba32[4];
	__u8 image[16];	/* All pixel formats pack data without padding. */
	__u64 doubleword[2];
	__u32 word[4];
	__u16 halfword[8];
	__u8 byte[16];
} __attribute__((aligned(GIF_SIZE)));

/*
 * Writing 1 to PSE temporarily stops GIF transfers and makes it possible
 * to read GIF registers for debugging. Writing 0 to PSE resumes transfers.
 */
struct gif_ctrl {
	__BITFIELD_FIELD(__u32 : 28,
	__BITFIELD_FIELD(__u32 pse : 1,		/* Temporary transfer stop */
	__BITFIELD_FIELD(__u32 : 2,
	__BITFIELD_FIELD(__u32 rst : 1,		/* GIF reset */
	;))))
};

/* FIXME: Move from UAPI */

void gif_writel_ctrl(u32 value);
void gif_write_ctrl(struct gif_ctrl value);

void gif_writel_mode(u32 value);

u32 gif_readl_stat(void);

void gif_reset(void);

void gif_stop(void);
void gif_resume(void);

bool gif_ready(void);
void gif_read(union gif_data *base_package, size_t package_count);
void gif_write(union gif_data *base_package, size_t package_count);

#endif /* _UAPI_MIPS_ASM_GIF_H */
