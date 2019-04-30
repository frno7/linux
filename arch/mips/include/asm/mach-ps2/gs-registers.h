// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 privileged Graphics Synthesizer (GS) registers
 *
 * Copyright (C) 2019 Fredrik Noring
 */

/**
 * DOC: Privileged Graphics Synthesizer (GS) registers
 *
 * All privileged GS registers are write-only except CSR (system status)
 * and SIGLBLID (signal and label id). Reading write-only registers is
 * emulated by shadow registers in memory. Reading unwritten registers
 * is not permitted. Predicate functions indicate whether registers are
 * readable.
 */

#ifndef __ASM_MACH_PS2_GS_REGISTERS_H
#define __ASM_MACH_PS2_GS_REGISTERS_H

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

/**
 * enum gs_pmode_mmod - &gs_pmode.mmod alpha blending value
 * @gs_mmod_circuit1: FIXME
 * @gs_mmod_alp: FIXME
 */
enum gs_pmode_mmod {
	gs_mmod_circuit1,
	gs_mmod_alp
};

/**
 * enum gs_pmode_amod - &gs_pmode.amod OUT1 alpha output
 * @gs_amod_circuit1: FIXME
 * @gs_amod_circuit2: FIXME
 */
enum gs_pmode_amod {
	gs_amod_circuit1,
	gs_amod_circuit2
};

/**
 * enum gs_pmode_slbg - &gs_pmode.slbg alpha blending method
 * @gs_slbg_circuit2: FIXME
 * @gs_slbg_bgcolor: FIXME
 */
enum gs_pmode_slbg {
	gs_slbg_circuit2,
	gs_slbg_bgcolor
};

/**
 * struct gs_pmode - PMODE privileged Graphics Synthesizer register
 * @en1: enable read circuit 1
 * @en2: enable read circuit 2
 * @crtmd: CRT output switching (always 001)
 * @mmod: alpha blending value
 * @amod: OUT1 alpha output
 * @slbg: alpha blending method
 * @alp: fixed alpha (0xff = 1.0)
 * @zero: must be zero
 */
struct gs_pmode {
	u64 en1 : 1;
	u64 en2 : 1;
	u64 crtmd : 3;
	u64 mmod : 1;
	u64 amod : 1;
	u64 slbg : 1;
	u64 alp : 8;
	u64 zero : 1;
	u64 : 47;
};

/**
 * enum gs_smode1_cmod - &gs_smode1.cmod value
 * @gs_cmod_vesa: VESA
 * @gs_cmod_ntsc: NTSC broadcast
 * @gs_cmod_pal: PAL broadcast
 */
enum gs_smode1_cmod {
	gs_cmod_vesa,
	/* Reserved */
	gs_cmod_ntsc = 2,
	gs_cmod_pal
};

/**
 * enum gs_smode1_gcont - &gs_smode1.gcont value
 * @gs_gcont_rgbyc: Output RGBYc
 * @gs_gcont_ycrcb: Output YCrCb
 */
enum gs_smode1_gcont {
	gs_gcont_rgbyc,
	gs_gcont_ycrcb
};

/**
 * struct gs_smode1 - SMODE1 privileged Graphics Synthesizer register
 * @rc: PLL reference divider
 * @lc: PLL loop divider
 * @t1248: PLL output divider
 * @slck: FIXME
 * @cmod: @enum gs_smode1_cmod display mode (PAL, NTSC or VESA)
 * @ex: FIXME
 * @prst: PLL reset
 * @sint: PLL (phase-locked loop)
 * @xpck: FIXME
 * @pck2: FIXME
 * @spml: sub-pixel magnification level
 * @gcont: @enum gs_smode1_gcont select RGBYC or YCrCb
 * @phs: HSync output
 * @pvs: VSync output
 * @pehs: FIXME
 * @pevs: FIXME
 * @clksel: FIXME
 * @nvck: FIXME
 * @slck2: FIXME
 * @vcksel: FIXME
 * @vhp: 0 = interlace mode and 1 = progressive mode
 *
 * The video clock VCK = (13500000 * @lc) / ((@t1248 + 1) * @spml * @rc).
 */
struct gs_smode1 {
	u64 rc : 3;
	u64 lc : 7;
	u64 t1248 : 2;
	u64 slck : 1;
	u64 cmod : 2;
	u64 ex : 1;
	u64 prst : 1;
	u64 sint : 1;
	u64 xpck : 1;
	u64 pck2 : 2;
	u64 spml : 4;
	u64 gcont : 1;
	u64 phs : 1;
	u64 pvs : 1;
	u64 pehs : 1;
	u64 pevs : 1;
	u64 clksel : 2;
	u64 nvck : 1;
	u64 slck2 : 1;
	u64 vcksel : 2;
	u64 vhp : 1;
	u64 : 27;
};

/**
 * enum gs_smode2_intm - &gs_smode2.intm interlace mode
 * @gs_intm_progressive: progressive (noninterlace) mode
 * @gs_intm_interlace: interlace mode
 */
enum gs_smode2_intm {
	gs_intm_progressive,
	gs_intm_interlace
};

/**
 * enum gs_smode2_ffmd - &gs_smode2.ffmd FIELD or FRAME mode
 * @gs_ffmd_field:
 * @gs_ffmd_frame:
 *
 * In FIELD mode every other line is read: 0, 2, 4, ... / 1, 3, 5, ...
 *
 * In FRAME mode every line is read: 1, 2, 3, 4, 5, ...
 */
enum gs_smode2_ffmd {
	gs_ffmd_field,
	gs_ffmd_frame
};

/**
 * enum gs_smode2_dpms - &gs_smode2.dpms VESA display power management
 *      signaling (DPMS) levels
 * @gs_dpms_on: in use
 * @gs_dpms_standby: blanked, low power
 * @gs_dpms_suspend: blanked, lower power
 * @gs_dpms_off: shut off, awaiting activity
 */
enum gs_smode2_dpms {
	gs_dpms_on,
	gs_dpms_standby,
	gs_dpms_suspend,
	gs_dpms_off
};

/**
 * struct gs_smode2 - SMODE2 privileged Graphics Synthesizer register
 * @intm: &enum gs_smode2_intm progressive or interlace mode
 * @ffmd: &enum gs_smode2_ffmd FIELD or FRAME mode
 * @dpms: &enum gs_smode2_dpms VESA display power management signaling (DPMS)
 *      level
 */
struct gs_smode2 {
	u64 intm : 1;
	u64 ffmd : 1;
	u64 dpms : 2;
	u64 : 60;
};

/**
 * struct gs_srfsh - DRAM refresh privileged Graphics Synthesizer register
 * @rfsh: FIXME
 */
struct gs_srfsh {
	u64 rfsh : 4;		/* FIXME: Number of bits? */
	u64 : 60;
};

/**
 * struct gs_synch1 - SYNCH1 privileged Graphics Synthesizer register
 * @hfp: horizontal front porch
 * @hbp: horizontal back porch
 * @hseq: FIXME
 * @hsvs: FIXME
 * @hs: FIXME
 */
struct gs_synch1 {
	u64 hfp : 11;
	u64 hbp : 11;
	u64 hseq : 10;
	u64 hsvs : 11;
	u64 hs : 21;		/* FIXME: Number of bits? */
};

/**
 * struct gs_synch2 - SYNCH2 privileged Graphics Synthesizer register
 * @hf: FIXME
 * @hb: FIXME
 */
struct gs_synch2 {
	u64 hf : 11;
	u64 hb : 11;
	u64 : 42;
};

/**
 * struct gs_syncv - SYNCHV privileged Graphics Synthesizer register
 * @vfp: vertical front porch, halflines with color burst after video data
 * @vfpe: halflines without color burst after @vfp
 * @vbp: vertical back porch, halflines with color burst after @vbpe
 * @vbpe: halflines without color burst after @vs
 * @vdp: halflines with with video data
 * @vs: halflines with VSYNC
 */
struct gs_syncv {
	u64 vfp : 10;
	u64 vfpe : 10;
	u64 vbp : 12;
	u64 vbpe : 10;
	u64 vdp : 11;
	u64 vs : 11;
};

/**
 * struct gs_dispfb - DISPFB privileged Graphics Synthesizer register
 * @fbp: base pointer address/2048
 * @fbw: buffer width/64
 * @psm: pixel storage format FIXME
 * @dbx: upper left x position
 * @dby: upper left y position
 */
struct gs_dispfb {
	u64 fbp : 9;
	u64 fbw : 6;
	u64 psm : 5;
	u64 : 12;
	u64 dbx : 11;
	u64 dby : 11;
	u64 : 10;
};

/**
 * struct gs_display - DISPLAY privileged Graphics Synthesizer register
 * @dx: display x position (VCK)
 * @dy: display y position (px)
 * @magh: horizontal magnification
 * @magv: vertical magnification
 * @dw: display area width-1 (VCK)
 * @dh: display area height-1 (px)
 *
 * @magh and @magv are factor-1, so 0 is 1x, 1 is 2x, 2 is 3x, etc.
 */
struct gs_display {
	u64 dx : 12;
	u64 dy : 11;
	u64 magh : 4;
	u64 magv : 5;
	u64 dw : 12;
	u64 dh : 11;
	u64 : 9;
};

/**
 * enum gs_extbuf_fbin - &&gs_extbuf.fbin selection of input source
 * @gs_fbin_out1: FIXME
 * @gs_fbin_out2: FIXME
 */
enum gs_extbuf_fbin {
	gs_fbin_out1,
	gs_fbin_out2
};

/*
 * enum gs_extbuf_wffmd - &gs_extbuf.wffmd interlace mode
 * @gs_wffmd_field: write to every other raster
 * @gs_wffmd_frame: write to every raster
 */
enum gs_extbuf_wffmd {
	gs_wffmd_field,
	gs_wffmd_frame
};

/**
 * enum gs_extbuf_emoda - &gs_extbuf.emoda processing of input alpha
 * @gs_emoda_alpha: input alpha is written as it is
 * @gs_emoda_y: FIXME
 * @gs_emoda_yhalf: FIXME
 * @gs_emoda_zero: FIXME
 */
enum gs_extbuf_emoda {
	gs_emoda_alpha,
	gs_emoda_y,
	gs_emoda_yhalf,
	gs_emoda_zero
};

/**
 * enum gs_extbuf_emodc - &gs_extbuf.emodc processing of input color
 * @gs_emodc_rgb: FIXME
 * @gs_emodc_y: FIXME
 * @gs_emodc_ycbcr: FIXME
 * @gs_emodc_alpha: FIXME
 */
enum gs_extbuf_emodc {
	gs_emodc_rgb,
	gs_emodc_y,
	gs_emodc_ycbcr,
	gs_emodc_alpha
};

/**
 * struct gs_extbuf - EXTBUF privileged Graphics Synthesizer register
 * @exbp: buffer base pointer/64
 * @exbw: width of buffer/64
 * @fbin: @enum gs_extbuf_fbin selection of input source
 * @wffmd: @enum gs_extbuf_wffmd interlace mode
 * @emoda: @enum gs_extbuf_emoda processing of input alpha
 * @emodc: @enum gs_extbuf_emodc processing of input color
 * @wdx: upper left x position
 * @wdy: upper left y position
 */
struct gs_extbuf {
	u64 exbp : 14;
	u64 exbw : 6;
	u64 fbin : 2;
	u64 wffmd : 1;
	u64 emoda : 2;
	u64 emodc : 2;
	u64 : 5;
	u64 wdx : 11;
	u64 wdy : 11;
	u64 : 10;
};

/**
 * struct gs_extdata - EXTDATA privileged Graphics Synthesizer register
 * @sx: upper left x position (VCK)
 * @sy: upper left y position (VCK)
 * @smph: horizontal sampling rate interval (VCK)
 * @smpv: vertical sampling rate interval (VCK)
 * @ww: rectangular area width-1
 * @wh: rectangular area height-1
 */
struct gs_extdata {
	u64 sx : 12;
	u64 sy : 11;
	u64 smph : 4;
	u64 smpv : 2;
	u64 : 3;
	u64 ww : 12;
	u64 wh : 11;
	u64 : 9;
};

/**
 * enum gs_extwrite_write - &gs_extwrite.write enable feedback write
 * @gs_write_complete_current: FIXME
 * @gs_write_start_next: FIXME
 */
enum gs_extwrite_write {
	gs_write_complete_current,
	gs_write_start_next
};

/**
 * struct gs_extwrite - EXTWRITE privileged Graphics Synthesizer register
 * @write: &enum gs_extwrite_write enable feedback write
 */
struct gs_extwrite {
	u64 write : 1;
	u64 : 63;
};

/**
 * struct gs_bgcolor - BGCOLOR privileged Graphics Synthesizer register
 * @r: red background color
 * @g: green background color
 * @b: blue background color
 */
struct gs_bgcolor {
	u64 r : 8;
	u64 g : 8;
	u64 b : 8;
	u64 : 40;
};

/**
 * enum gs_csr_fifo - &gs_csr.fifo host interface FIFO status
 * @gs_fifo_neither: neither empty nor almost full
 * @gs_fifo_empty: FIXME
 * @gs_fifo_almost_full: FIXME
 */
enum gs_csr_fifo {
	gs_fifo_neither,
	gs_fifo_empty,
	gs_fifo_almost_full
};

/**
 * enum gs_csr_field - &gs_csr.field field display currently FIXME
 * @gs_field_even: FIXME
 * @gs_field_odd: FIXME
 */
enum gs_csr_field {
	gs_field_even,
	gs_field_odd
};

/**
 * struct gs_csr - CSR privileged Graphics Synthesizer register FIXME
 * @signal: SIGNAL event control
 * @finish: FINISH event control
 * @hsint: HSync interrupt control
 * @vsint: VSync interrupt control
 * @edwint: rectangular area write termination interrupt control
 * @zero: must be zero
 * @flush: drawing suspend and FIFO clear (enabled during data write)
 * @reset: Graphics Synthesizer reset (enabled during data write)
 * @nfield: VSync sampled FIELD
 * @field: &enum gs_csr_field field display currently
 * @fifo: &enum gs_csr_fifo host interface FIFO status
 * @rev: Graphics Synthesizer revision (hex)
 * @id: Graphics Synthesizer id (hex)
 */
struct gs_csr {
	u64 signal : 1;
	u64 finish : 1;
	u64 hsint : 1;
	u64 vsint : 1;
	u64 edwint : 1;
	u64 zero : 2;
	u64 : 1;
	u64 flush : 1;
	u64 reset : 1;
	u64 : 2;
	u64 nfield : 1;
	u64 field : 1;
	u64 fifo : 2;
	u64 rev : 8;
	u64 id : 8;
	u64 : 32;
};

/**
 * struct gs_imr - IMR privileged Graphics Synthesizer register FIXME
 * @sigmsk: SIGNAL event interrupt mask
 * @finishmsk: FINISH event interrupt mask
 * @hsmsk: HSync interrupt mask
 * @vsmsk: VSync interrupt mask
 * @edwmsk: rectangular area write termination interrupt mask
 * @ones: should be set to all ones (= 3)
 */
struct gs_imr {
	u64 : 8;
	u64 sigmsk : 1;
	u64 finishmsk : 1;
	u64 hsmsk : 1;
	u64 vsmsk : 1;
	u64 edwmsk : 1;
	u64 ones : 2;
	u64 : 49;
};

/**
 * enum gs_busdir_dir - &gs_busdir.dir host to local direction, or vice versa
 * @gs_dir_host_to_local: host to local bus transfer direction
 * @gs_dir_local_to_host: local to host bus transfer direction
 */
enum gs_busdir_dir {
	gs_dir_host_to_local,
	gs_dir_local_to_host
};

/**
 * struct gs_busdir - BUSDIR privileged Graphics Synthesizer register FIXME
 * @dir: &enum gs_busdir_dir host to local direction, or vice versa
 */
struct gs_busdir {
	u64 dir : 1;
	u64 : 63;
};

/**
 * struct gs_siglblid - SIGLBLID privileged Graphics Synthesizer register FIXME
 * @sigid: id value set by SIGNAL register
 * @lblid: id value set by LABEL register
 */
struct gs_siglblid {
	u64 sigid : 32;
	u64 lblid : 32;
};

#define GS_DECLARE_VALID_REG(reg)			\
	bool gs_valid_##reg(void)

#define GS_DECLARE_WQ_REG(reg)				\
	void gs_writeq_##reg(u64 value)

#define GS_DECLARE_RW_REG(reg)				\
	GS_DECLARE_VALID_REG(reg);			\
	GS_DECLARE_WQ_REG(reg)

/**
 * DOC:
 *
 * All privileged Graphics Synthesizer register functions follow the same
 * pattern. For example, the CSR register has the following functions::
 *
 *	bool gs_valid_csr(void);
 *	void gs_writeq_csr(u64 value);
 *
 * gs_valid_csr() indicates whether CSR is readable, which is always true,
 * since CSR is read-write in hardware. The IMR register however is write-
 * only in hardware, so its shadow register is only valid and readable once
 * it is written at least once. Reading nonvalid registers is not permitted.
 *
 * The following registers have functions: PMODE, SMODE1, SMODE2, SRFSH,
 * SYNCH1, SYNCH2, SYNCV, DISPFB1 , DISPLAY1, DISPFB2, DISPLAY2, EXTBUF,
 * EXTDATA, EXTWRITE, BGCOLOR, CSR, IMR, BUSDIR and SIGLBLID.
 */
GS_DECLARE_RW_REG(pmode);
GS_DECLARE_RW_REG(smode1);
GS_DECLARE_RW_REG(smode2);
GS_DECLARE_RW_REG(srfsh);
GS_DECLARE_RW_REG(synch1);
GS_DECLARE_RW_REG(synch2);
GS_DECLARE_RW_REG(syncv);
GS_DECLARE_RW_REG(dispfb1 );
GS_DECLARE_RW_REG(display1);
GS_DECLARE_RW_REG(dispfb2);
GS_DECLARE_RW_REG(display2);
GS_DECLARE_RW_REG(extbuf);
GS_DECLARE_RW_REG(extdata);
GS_DECLARE_RW_REG(extwrite);
GS_DECLARE_RW_REG(bgcolor);
GS_DECLARE_RW_REG(csr);
GS_DECLARE_RW_REG(imr);
GS_DECLARE_RW_REG(busdir);
GS_DECLARE_RW_REG(siglblid);

#endif /* __ASM_MACH_PS2_GS_REGISTERS_H */
