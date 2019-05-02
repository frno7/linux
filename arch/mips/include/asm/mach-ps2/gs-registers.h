// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 privileged Graphics Synthesizer (GS) registers
 *
 * Copyright (C) 2019 Fredrik Noring
 *
 * All privileged GS registers are write-only except CSR (system status)
 * and SIGLBLID (signal and label id). Reading write-only registers is
 * emulated by shadow registers in memory. Reading unwritten registers
 * is not permitted. Predicate functions indicate whether registers are
 * readable.
 */

#ifndef __ASM_PS2_GS_REGISTERS_H
#define __ASM_PS2_GS_REGISTERS_H

#include <asm/types.h>

/* Privileged GS registers must be accessed using LD/SD instructions. */

#define GS_PMODE	0x12000000  /* (WO) PCRTC mode setting */
#define GS_SMODE1	0x12000010  /* (WO) Sync */
#define GS_SMODE2	0x12000020  /* (WO) Sync */
#define GS_SRFSH	0x12000030  /* (WO) DRAM refresh */
#define GS_SYNCH1	0x12000040  /* (WO) Sync */
#define GS_SYNCH2	0x12000050  /* (WO) Sync */
#define GS_SYNCV	0x12000060  /* (WO) Sync */
#define GS_DISPFB1	0x12000070  /* (WO) Rectangle read output circuit 1 */
#define GS_DISPLAY1	0x12000080  /* (WO) Rectangle read output circuit 1 */
#define GS_DISPFB2	0x12000090  /* (WO) Rectangle read output circuit 2 */
#define GS_DISPLAY2	0x120000a0  /* (WO) Rectangle read output circuit 2 */
#define GS_EXTBUF	0x120000b0  /* (WO) Feedback write buffer */
#define GS_EXTDATA	0x120000c0  /* (WO) Feedback write setting */
#define GS_EXTWRITE	0x120000d0  /* (WO) Feedback write function control */
#define GS_BGCOLOR	0x120000e0  /* (WO) Background color setting */
#define GS_CSR		0x12001000  /* (RW) System status */
#define GS_IMR		0x12001010  /* (WO) Interrupt mask control */
#define GS_BUSDIR	0x12001040  /* (WO) Host interface bus switching */
#define GS_SIGLBLID	0x12001080  /* (RW) Signal and label id */

enum gs_pmode_mmod {
	gs_mmod_circuit1,
	gs_mmod_alp
};

enum gs_pmode_amod {
	gs_amod_circuit1,
	gs_amod_circuit2
};

enum gs_pmode_slbg {
	gs_slbg_circuit2,
	gs_slbg_bgcolor
};

struct gs_pmode {
	u64 en1 : 1;		/* Enable read circuit 1 */
	u64 en2 : 1;		/* Enable read circuit 2 */
	u64 crtmd : 3;		/* CRT output switching (always 001) */
	u64 mmod : 1;		/* Alpha blending value */
	u64 amod : 1;		/* OUT1 alpha output */
	u64 slbg : 1;		/* Alpha blending method */
	u64 alp : 8;		/* Fixed alpha (0xff = 1.0) */
	u64 zero : 1;		/* Must be zero */
	u64 : 47;
};

enum gs_smode1_cmod {
	gs_cmod_vesa,		/* VESA */
	/* Reserved */
	gs_cmod_ntsc = 2,	/* NTSC broadcast */
	gs_cmod_pal		/* PAL broadcast */
};

enum gs_smode1_gcont {
	gs_gcont_rgbyc,		/* Output RGBYc */
	gs_gcont_ycrcb		/* Output YCrCb */
};

/* The video clock is VCK = (13500000 * LC) / ((T1248 + 1) * SPML * RC). */
struct gs_smode1 {
	u64 rc : 3;		/* PLL reference divider */
	u64 lc : 7;		/* PLL loop divider */
	u64 t1248 : 2;		/* PLL output divider */
	u64 slck : 1;		/* FIXME */
	u64 cmod : 2;		/* Display mode (PAL, NTSC or VESA) */
	u64 ex : 1;		/* FIXME */
	u64 prst : 1;		/* PLL reset */
	u64 sint : 1;		/* PLL (phase-locked loop) */
	u64 xpck : 1;		/* FIXME */
	u64 pck2 : 2;		/* FIXME */
	u64 spml : 4;		/* FIXME */
	u64 gcont : 1;		/* Select RGBYC or YCrCb */
	u64 phs : 1;		/* HSync output */
	u64 pvs : 1;		/* VSync output */
	u64 pehs : 1;		/* FIXME */
	u64 pevs : 1;		/* FIXME */
	u64 clksel : 2;		/* FIXME */
	u64 nvck : 1;		/* FIXME */
	u64 slck2 : 1;		/* FIXME */
	u64 vcksel : 2;		/* FIXME */
	u64 vhp : 1;		/* FIXME */
	u64 : 27;
};

enum gs_smode2_dpms {
	gs_dpms_on,
	gs_dpms_standby,
	gs_dpms_suspend,
	gs_dpms_off
};

enum gs_smode2_ffmd {
	gs_ffmd_field,
	gs_ffmd_frame
};

enum gs_smode2_intm {
	gs_intm_progressive,
	gs_intm_interlace
};

/*
 * In FIELD mode every other line is read. In FRAME mode every line is read.
 *
 * FIELD: 0, 2, 4, ... / 1, 3, 5, ...
 * FRAME: 1, 2, 3, 4, 5, ...
 */
struct gs_smode2 {
	u64 intm : 1;		/* Enable interlace (= 1) */
	u64 ffmd : 1;		/* FIELD (= 0) or FRAME (= 1) */
	u64 dpms : 2;		/* VESA DPMS mode */
	u64 : 60;
};

struct gs_srfsh {
	u64 rfsh : 4;		/* DRAM refresh FIXME: Size? */
	u64 : 60;
};

struct gs_synch1 {
	u64 hfp : 11;		/* Horizontal front porch */
	u64 hbp : 11;		/* Horizontal back porch */
	u64 hseq : 10;		/* FIXME */
	u64 hsvs : 11;		/* FIXME */
	u64 hs : 21;		/* FIXME: Size? */
};

struct gs_synch2 {
	u64 hf : 11;
	u64 hb : 11;
	u64 : 42;
};

/*
 * VS   : Halflines with VSYNC
 * VDP  : Halflines with with video data
 * VBPE : Halflines without colorburst after VS ("back porch")
 * VBP  : Halflines with colorburst after VBPE ("back porch")
 * VFPE : Halflines without colorburst after VFP ("front porch")
 * VFP  : Halflines with colorburst after videodata ("front porch")
 */
struct gs_syncv {
	u64 vfp : 10;		/* Vertical front porch */
	u64 vfpe : 10;
	u64 vbp : 12;		/* Vertical back porch */
	u64 vbpe : 10;
	u64 vdp : 11;
	u64 vs : 11;
};

struct gs_dispfb {
	u64 fbp : 9;		/* Base pointer address/2048 */
	u64 fbw : 6;		/* Buffer width/64 */
	u64 psm : 5;		/* Pixel storage format */
	u64 : 12;
	u64 dbx : 11;		/* Upper left x position */
	u64 dby : 11;		/* Upper left y position */
	u64 : 10;
};

/* Magnifications are factor-1, so 0 is 1x, 1 is 2x, 2 is 3x, etc. */
struct gs_display {
	u64 dx : 12;		/* Display x position (VCK) */
	u64 dy : 11;		/* Display y position (px) */
	u64 magh : 4;		/* Horizontal magnification */
	u64 magv : 5;		/* Vertical magnification */
	u64 dw : 12;		/* Display area width-1 (VCK) */
	u64 dh : 11;		/* Display area height-1 (px) */
	u64 : 9;
};

enum gs_extbuf_fbin {
	gs_fbin_out1,
	gs_fbin_out2
};

enum gs_extbuf_wffmd {
	gs_wffmd_field,		/* Written to every other raster */
	gs_wffmd_frame		/* Written to every raster */
};

enum gs_extbuf_emoda {
	gs_emoda_alpha,		/* Input alpha is written as it is */
	gs_emoda_y,
	gs_emoda_yhalf,
	gs_emoda_zero
};

enum gs_extbuf_emodc {
	gs_emodc_rgb,
	gs_emodc_y,
	gs_emodc_ycbcr,
	gs_emodc_alpha
};

struct gs_extbuf {
	u64 exbp : 14;		/* Buffer base pointer/64 */
	u64 exbw : 6;		/* Width of buffer/64 */
	u64 fbin : 2;		/* Selection of input source */
	u64 wffmd : 1;		/* Interlace mode */
	u64 emoda : 2;		/* Processing of input alpha */
	u64 emodc : 2;		/* Processing of input color */
	u64 : 5;
	u64 wdx : 11;		/* Upper left x position */
	u64 wdy : 11;		/* Upper left y position */
	u64 : 10;
};

struct gs_extdata {
	u64 sx : 12;		/* Upper left x position (VCK) */
	u64 sy : 11;		/* Upper left y position (VCK) */
	u64 smph : 4;		/* Horizontal sampling rate interval (VCK) */
	u64 smpv : 2;		/* Vertical sampling rate interval (VCK)*/
	u64 : 3;
	u64 ww : 12;		/* Rectangular area width-1 */
	u64 wh : 11;		/* Rectangular area height-1 */
	u64 : 9;
};

enum gs_extwrite_write {
	gs_write_complete_current,
	gs_write_start_next
};

struct gs_extwrite {
	u64 write : 1;		/* Enable feedback write */
	u64 : 63;
};

struct gs_bgcolor {
	u64 r : 8;		/* Red background color */
	u64 g : 8;		/* Green background color */
	u64 b : 8;		/* Blue background color */
	u64 : 40;
};

enum gs_csr_fifo {
	gs_fifo_neither,	/* Neither empty nor almost full */
	gs_fifo_empty,
	gs_fifo_almost_full
};

enum gs_csr_field {
	gs_field_even,
	gs_field_odd
};

struct gs_csr {
	u64 signal : 1;		/* SIGNAL event control */
	u64 finish : 1;		/* FINISH event control */
	u64 hsint : 1;		/* HSync interrupt control */
	u64 vsint : 1;		/* VSync interrupt control */
	u64 edwint : 1;		/* Rectangular area write termination
				   interrupt control */
	u64 zero : 2;		/* Must be zero */
	u64 : 1;
	u64 flush : 1;		/* Drawing suspend and FIFO clear
				   (enabled during data write) */
	u64 reset : 1;		/* GS reset (enabled during data write) */
	u64 : 2;
	u64 nfield : 1;		/* VSync sampled FIELD */
	u64 field : 1;		/* Field display currently */
	u64 fifo : 2;		/* Host interface FIFO status */
	u64 rev : 8;		/* GS revision (hex) */
	u64 id : 8;		/* GS id (hex) */
	u64 : 32;
};

struct gs_imr {
	u64 : 8;
	u64 sigmsk : 1;		/* SIGNAL event interrupt mask */
	u64 finishmsk : 1;	/* FINISH event interrupt mask */
	u64 hsmsk : 1;		/* HSync interrupt mask */
	u64 vsmsk : 1;		/* VSync interrupt mask */
	u64 edwmsk : 1;		/* Rectangular area write termination
				   interrupt mask */
	u64 ones : 2;		/* Should be set to all ones (= 3) */
	u64 : 49;
};

enum gs_busdir_dir {
	gs_dir_host_to_local,
	gs_dir_local_to_host
};

struct gs_busdir {
	u64 dir : 1;		/* Host to local direction, or vice versa */
	u64 : 63;
};

struct gs_siglblid {
	u64 sigid : 32;		/* Id value set by SIGNAL register */
	u64 lblid : 32;		/* Id value set by LABEL register */
};

#define GS_DECLARE_VALID_REG(reg)			\
	bool gs_valid_##reg(void)

#define GS_DECLARE_RQ_REG(reg)				\
	u64 gs_readq_##reg(void)

#define GS_DECLARE_WQ_REG(reg)				\
	void gs_writeq_##reg(u64 value)

#define GS_DECLARE_RS_REG(reg, str)			\
	struct gs_##str gs_read_##reg(void)

#define GS_DECLARE_WS_REG(reg, str)			\
	void gs_write_##reg(struct gs_##str value)

#define GS_DECLARE_RW_REG(reg, str)			\
	GS_DECLARE_VALID_REG(reg);			\
	GS_DECLARE_RQ_REG(reg);				\
	GS_DECLARE_WQ_REG(reg);				\
	GS_DECLARE_RS_REG(reg, str);			\
	GS_DECLARE_WS_REG(reg, str)

/*
 * All registers follow the same pattern. For example, the CSR register has
 * the following functions:
 *
 *	bool gs_valid_csr(void);
 *	u64 gs_readq_csr(void);
 *	void gs_writeq_csr(u64 value);
 *
 *	struct gs_csr gs_read_csr(void);
 *	void gs_write_csr(struct gs_csr value);
 *
 * gs_valid_csr indicates whether CSR is readable, which is always true,
 * since CSR is read-write in hardware. The IMR register however is write-
 * only in hardware, so its shadow register is only valid and readable once
 * it is written at least once. Reading nonvalid registers is not permitted.
 */
GS_DECLARE_RW_REG(pmode,    pmode);
GS_DECLARE_RW_REG(smode1,   smode1);
GS_DECLARE_RW_REG(smode2,   smode2);
GS_DECLARE_RW_REG(srfsh,    srfsh);
GS_DECLARE_RW_REG(synch1,   synch1);
GS_DECLARE_RW_REG(synch2,   synch2);
GS_DECLARE_RW_REG(syncv,    syncv);
GS_DECLARE_RW_REG(dispfb1 , dispfb);
GS_DECLARE_RW_REG(display1, display);
GS_DECLARE_RW_REG(dispfb2,  dispfb);
GS_DECLARE_RW_REG(display2, display);
GS_DECLARE_RW_REG(extbuf,   extbuf);
GS_DECLARE_RW_REG(extdata,  extdata);
GS_DECLARE_RW_REG(extwrite, extwrite);
GS_DECLARE_RW_REG(bgcolor,  bgcolor);
GS_DECLARE_RW_REG(csr,      csr);
GS_DECLARE_RW_REG(imr,      imr);
GS_DECLARE_RW_REG(busdir,   busdir);
GS_DECLARE_RW_REG(siglblid, siglblid);

#define GS_WS_REG(reg, str, ...)			\
	gs_write_##reg((struct gs_##str) { __VA_ARGS__ })

/*
 * These macros simplifies register writing further, allowing named
 * fields in statements such as GS_WRITE_CSR( .flush = 1 ).
 */
#define GS_WRITE_PMODE(...)    GS_WS_REG(pmode,    pmode,   __VA_ARGS__)
#define GS_WRITE_SMODE1(...)   GS_WS_REG(smode1,   smode1,  __VA_ARGS__)
#define GS_WRITE_SMODE2(...)   GS_WS_REG(smode2,   smode2,  __VA_ARGS__)
#define GS_WRITE_SRFSH(...)    GS_WS_REG(srfsh,    srfsh,   __VA_ARGS__)
#define GS_WRITE_SYNCH1(...)   GS_WS_REG(synch1,   synch1,  __VA_ARGS__)
#define GS_WRITE_SYNCH2(...)   GS_WS_REG(synch2,   synch2,  __VA_ARGS__)
#define GS_WRITE_SYNCV(...)    GS_WS_REG(syncv,    syncv,   __VA_ARGS__)
#define GS_WRITE_DISPLAY1(...) GS_WS_REG(display1, display, __VA_ARGS__)
#define GS_WRITE_DISPLAY2(...) GS_WS_REG(display2, display, __VA_ARGS__)
#define GS_WRITE_DISPFB1(...)  GS_WS_REG(dispfb1,  dispfb,  __VA_ARGS__)
#define GS_WRITE_DISPFB2(...)  GS_WS_REG(dispfb2,  dispfb,  __VA_ARGS__)
#define GS_WRITE_CSR(...)      GS_WS_REG(csr,      csr,     __VA_ARGS__)
#define GS_WRITE_BUSDIR(...)   GS_WS_REG(busdir,   busdir,  __VA_ARGS__)

/* Exclusive or (XOR) value with the IMR register, and return the result. */
u64 gs_xorq_imr(u64 value);

#endif /* __ASM_PS2_GS_REGISTERS_H */
