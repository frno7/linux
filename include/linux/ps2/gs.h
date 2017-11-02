#ifndef __PS2_GS_H
#define __PS2_GS_H
/* Copyright 2010 Mega Man */
/* TBD: Unfinished state. Rework code. */

#include <asm/types.h>

/* crtmode : mode */
#define PS2_GS_VESA	0
#define PS2_GS_DTV	1
#define PS2_GS_NTSC	2
#define PS2_GS_PAL	3

/* crtmode : res (NTSC, PAL) */
#define PS2_GS_NOINTERLACE	0
#define PS2_GS_INTERLACE	1
#define PS2_GS_FIELD		0x00000
#define PS2_GS_FRAME		0x10000

/* crtmode : res (VESA) */
#define PS2_GS_640x480		0
#define PS2_GS_800x600		1
#define PS2_GS_1024x768		2
#define PS2_GS_1280x1024	3
#define PS2_GS_60Hz		0x0100
#define PS2_GS_75Hz		0x0200

/* crtmode : res (DTV) */
#define PS2_GS_480P	0
#define PS2_GS_1080I	1
#define PS2_GS_720P	2

/* GS pixel format */
#define PS2_GS_PSMCT32		0
#define PS2_GS_PSMCT24		1
#define PS2_GS_PSMCT16		2
#define PS2_GS_PSMCT16S		10
#define PS2_GS_PSMT8		19
#define PS2_GS_PSMT4		20
#define PS2_GS_PSMT8H		27
#define PS2_GS_PSMT4HL		36
#define PS2_GS_PSMT4HH		44
#define PS2_GS_PSMZ32		48
#define PS2_GS_PSMZ24		49
#define PS2_GS_PSMZ16		50
#define PS2_GS_PSMZ16S		58

/* GS registers */
#define PS2_GS_PRIM		0x00
#define PS2_GS_RGBAQ		0x01
#define PS2_GS_ST		0x02
#define PS2_GS_UV		0x03
#define PS2_GS_XYZF2		0x04
#define PS2_GS_XYZ2		0x05
#define PS2_GS_TEX0_1		0x06
#define PS2_GS_TEX0_2		0x07
#define PS2_GS_CLAMP_1		0x08
#define PS2_GS_CLAMP_2		0x09
#define PS2_GS_FOG		0x0a
#define PS2_GS_XYZF3		0x0c
#define PS2_GS_XYZ3		0x0d
#define PS2_GS_TEX1_1		0x14
#define PS2_GS_TEX1_2		0x15
#define PS2_GS_TEX2_1		0x16
#define PS2_GS_TEX2_2		0x17
#define PS2_GS_XYOFFSET_1	0x18
#define PS2_GS_XYOFFSET_2	0x19
#define PS2_GS_PRMODECONT	0x1a
#define PS2_GS_PRMODE		0x1b
#define PS2_GS_TEXCLUT		0x1c
#define PS2_GS_SCANMSK		0x22
#define PS2_GS_MIPTBP1_1	0x34
#define PS2_GS_MIPTBP1_2	0x35
#define PS2_GS_MIPTBP2_1	0x36
#define PS2_GS_MIPTBP2_2	0x37
#define PS2_GS_TEXA		0x3b
#define PS2_GS_FOGCOL		0x3d
#define PS2_GS_TEXFLUSH		0x3f
#define PS2_GS_SCISSOR_1	0x40
#define PS2_GS_SCISSOR_2	0x41
#define PS2_GS_ALPHA_1		0x42
#define PS2_GS_ALPHA_2		0x43
#define PS2_GS_DIMX		0x44
#define PS2_GS_DTHE		0x45
#define PS2_GS_COLCLAMP		0x46
#define PS2_GS_TEST_1		0x47
#define PS2_GS_TEST_2		0x48
#define PS2_GS_PABE		0x49
#define PS2_GS_FBA_1		0x4a
#define PS2_GS_FBA_2		0x4b
#define PS2_GS_FRAME_1		0x4c
#define PS2_GS_FRAME_2		0x4d
#define PS2_GS_ZBUF_1		0x4e
#define PS2_GS_ZBUF_2		0x4f
#define PS2_GS_BITBLTBUF	0x50
#define PS2_GS_TRXPOS		0x51
#define PS2_GS_TRXREG		0x52
#define PS2_GS_TRXDIR		0x53
#define PS2_GS_HWREG		0x54
#define PS2_GS_SIGNAL		0x60
#define PS2_GS_FINISH		0x61
#define PS2_GS_LABEL		0x62
#define PS2_GS_NOP		0x7f

/* GS register setting utils */
#define PS2_GS_SETREG_ALPHA_1	PS2_GS_SET_ALPHA
#define PS2_GS_SETREG_ALPHA_2	PS2_GS_SET_ALPHA
#define PS2_GS_SETREG_ALPHA(a, b, c, d, fix) \
	((__u64)(a)       | ((__u64)(b) << 2)     | ((__u64)(c) << 4) | \
	((__u64)(d) << 6) | ((__u64)(fix) << 32))

#define PS2_GS_SETREG_BITBLTBUF(sbp, sbw, spsm, dbp, dbw, dpsm) \
	((__u64)(sbp)         | ((__u64)(sbw) << 16) | \
	((__u64)(spsm) << 24) | ((__u64)(dbp) << 32) | \
	((__u64)(dbw) << 48)  | ((__u64)(dpsm) << 56))

#define PS2_GS_SETREG_CLAMP_1	PS2_GS_SET_CLAMP
#define PS2_GS_SETREG_CLAMP_2	PS2_GS_SET_CLAMP
#define PS2_GS_SETREG_CLAMP(wms, wmt, minu, maxu, minv, maxv) \
	((__u64)(wms)         | ((__u64)(wmt) << 2) | \
	((__u64)(minu) << 4)  | ((__u64)(maxu) << 14) | \
	((__u64)(minv) << 24) | ((__u64)(maxv) << 34))

#define PS2_GS_SETREG_COLCLAMP(clamp) ((__u64)(clamp))

#define PS2_GS_SETREG_DIMX(dm00, dm01, dm02, dm03, dm10, dm11, dm12, dm13, \
			dm20, dm21, dm22, dm23, dm30, dm31, dm32, dm33) \
	((__u64)(dm00)        | ((__u64)(dm01) << 4)  | \
	((__u64)(dm02) << 8)  | ((__u64)(dm03) << 12) | \
	((__u64)(dm10) << 16) | ((__u64)(dm11) << 20) | \
	((__u64)(dm12) << 24) | ((__u64)(dm13) << 28) | \
	((__u64)(dm20) << 32) | ((__u64)(dm21) << 36) | \
	((__u64)(dm22) << 40) | ((__u64)(dm23) << 44) | \
	((__u64)(dm30) << 48) | ((__u64)(dm31) << 52) | \
	((__u64)(dm32) << 56) | ((__u64)(dm33) << 60))

#define PS2_GS_SETREG_DTHE(dthe) ((__u64)(dthe))

#define PS2_GS_SETREG_FBA_1	PS2_GS_SETREG_FBA
#define PS2_GS_SETREG_FBA_2	PS2_GS_SETREG_FBA
#define PS2_GS_SETREG_FBA(fba) ((__u64)(fba))

#define PS2_GS_SETREG_FOG(f) ((__u64)(f) << 56)

#define PS2_GS_SETREG_FOGCOL(fcr, fcg, fcb) \
	((__u64)(fcr) | ((__u64)(fcg) << 8) | ((__u64)(fcb) << 16))

#define PS2_GS_SETREG_FRAME_1	PS2_GS_SETREG_FRAME
#define PS2_GS_SETREG_FRAME_2	PS2_GS_SETREG_FRAME
#define PS2_GS_SETREG_FRAME(fbp, fbw, psm, fbmask) \
	((__u64)(fbp)        | ((__u64)(fbw) << 16) | \
	((__u64)(psm) << 24) | ((__u64)(fbmask) << 32))

#define PS2_GS_SETREG_LABEL(id, idmsk) \
	((__u64)(id) | ((__u64)(idmsk) << 32))

#define PS2_GS_SETREG_MIPTBP1_1	PS2_GS_SETREG_MIPTBP1
#define PS2_GS_SETREG_MIPTBP1_2	PS2_GS_SETREG_MIPTBP1
#define PS2_GS_SETREG_MIPTBP1(tbp1, tbw1, tbp2, tbw2, tbp3, tbw3) \
	((__u64)(tbp1)        | ((__u64)(tbw1) << 14) | \
	((__u64)(tbp2) << 20) | ((__u64)(tbw2) << 34) | \
	((__u64)(tbp3) << 40) | ((__u64)(tbw3) << 54))

#define PS2_GS_SETREG_MIPTBP2_1	PS2_GS_SETREG_MIPTBP2
#define PS2_GS_SETREG_MIPTBP2_2	PS2_GS_SETREG_MIPTBP2
#define PS2_GS_SETREG_MIPTBP2(tbp4, tbw4, tbp5, tbw5, tbp6, tbw6) \
	((__u64)(tbp4)        | ((__u64)(tbw4) << 14) | \
	((__u64)(tbp5) << 20) | ((__u64)(tbw5) << 34) | \
	((__u64)(tbp6) << 40) | ((__u64)(tbw6) << 54))

#define PS2_GS_SETREG_PABE(pabe) ((__u64)(pabe))

#define PS2_GS_SETREG_PRIM(prim, iip, tme, fge, abe, aa1, fst, ctxt, fix) \
	((__u64)(prim)      | ((__u64)(iip) << 3)  | ((__u64)(tme) << 4) | \
	((__u64)(fge) << 5) | ((__u64)(abe) << 6)  | ((__u64)(aa1) << 7) | \
	((__u64)(fst) << 8) | ((__u64)(ctxt) << 9) | ((__u64)(fix) << 10))

#define PS2_GS_SETREG_PRMODE(iip, tme, fge, abe, aa1, fst, ctxt, fix) \
	(((__u64)(iip) << 3) | ((__u64)(tme) << 4)  | \
	 ((__u64)(fge) << 5) | ((__u64)(abe) << 6)  | ((__u64)(aa1) << 7) | \
	 ((__u64)(fst) << 8) | ((__u64)(ctxt) << 9) | ((__u64)(fix) << 10))

#define PS2_GS_SETREG_PRMODECONT(ac) ((__u64)(ac))

#define PS2_GS_SETREG_RGBAQ(r, g, b, a, q) \
	((__u64)(r)        | ((__u64)(g) << 8) | ((__u64)(b) << 16) | \
	((__u64)(a) << 24) | ((__u64)(q) << 32))

#define PS2_GS_SETREG_SCANMSK(msk) ((__u64)(msk))

#define PS2_GS_SETREG_SCISSOR_1	PS2_GS_SETREG_SCISSOR
#define PS2_GS_SETREG_SCISSOR_2	PS2_GS_SETREG_SCISSOR
#define PS2_GS_SETREG_SCISSOR(scax0, scax1, scay0, scay1) \
	((__u64)(scax0)        | ((__u64)(scax1) << 16) | \
	((__u64)(scay0) << 32) | ((__u64)(scay1) << 48))

#define PS2_GS_SETREG_SIGNAL(id, idmsk) \
	((__u64)(id) | ((__u64)(idmsk) << 32))

#define PS2_GS_SETREG_ST(s, t) ((__u64)(s) |  ((__u64)(t) << 32))

#define PS2_GS_SETREG_TEST_1 PS2_GS_SETREG_TEST
#define PS2_GS_SETREG_TEST_2 PS2_GS_SETREG_TEST
#define PS2_GS_SETREG_TEST(ate, atst, aref, afail, date, datm, zte, ztst) \
	((__u64)(ate)         | ((__u64)(atst) << 1) | \
	((__u64)(aref) << 4)  | ((__u64)(afail) << 12) | \
	((__u64)(date) << 14) | ((__u64)(datm) << 15) | \
	((__u64)(zte) << 16)  | ((__u64)(ztst) << 17))

#define PS2_GS_SETREG_TEX0_1	PS2_GS_SETREG_TEX0
#define PS2_GS_SETREG_TEX0_2	PS2_GS_SETREG_TEX0
#define PS2_GS_SETREG_TEX0(tbp, tbw, psm, tw, th, tcc, tfx, \
			   cbp, cpsm, csm, csa, cld) \
	((__u64)(tbp)         | ((__u64)(tbw) << 14) | \
	((__u64)(psm) << 20)  | ((__u64)(tw) << 26) | \
	((__u64)(th) << 30)   | ((__u64)(tcc) << 34) | \
	((__u64)(tfx) << 35)  | ((__u64)(cbp) << 37) | \
	((__u64)(cpsm) << 51) | ((__u64)(csm) << 55) | \
	((__u64)(csa) << 56)  | ((__u64)(cld) << 61))

#define PS2_GS_SETREG_TEX1_1	PS2_GS_SETREG_TEX1
#define PS2_GS_SETREG_TEX1_2	PS2_GS_SETREG_TEX1
#define PS2_GS_SETREG_TEX1(lcm, mxl, mmag, mmin, mtba, l, k) \
	((__u64)(lcm)        | ((__u64)(mxl) << 2)  | \
	((__u64)(mmag) << 5) | ((__u64)(mmin) << 6) | \
	((__u64)(mtba) << 9) | ((__u64)(l) << 19) | \
	((__u64)(k) << 32))

#define PS2_GS_SETREG_TEX2_1	PS2_GS_SETREG_TEX2
#define PS2_GS_SETREG_TEX2_2	PS2_GS_SETREG_TEX2
#define PS2_GS_SETREG_TEX2(psm, cbp, cpsm, csm, csa, cld) \
	(((__u64)(psm) << 20) | ((__u64)(cbp) << 37) | \
	((__u64)(cpsm) << 51) | ((__u64)(csm) << 55) | \
	((__u64)(csa) << 56)  | ((__u64)(cld) << 61))

#define PS2_GS_SETREG_TEXA(ta0, aem, ta1) \
	((__u64)(ta0) | ((__u64)(aem) << 15) | ((__u64)(ta1) << 32))

#define PS2_GS_SETREG_TEXCLUT(cbw, cou, cov) \
	((__u64)(cbw) | ((__u64)(cou) << 6) | ((__u64)(cov) << 12))

#define PS2_GS_SETREG_TRXDIR(xdr) ((__u64)(xdr))

#define PS2_GS_SETREG_TRXPOS(ssax, ssay, dsax, dsay, dir) \
	((__u64)(ssax)        | ((__u64)(ssay) << 16) | \
	((__u64)(dsax) << 32) | ((__u64)(dsay) << 48) | \
	((__u64)(dir) << 59))

#define PS2_GS_SETREG_TRXREG(rrw, rrh) \
	((__u64)(rrw) | ((__u64)(rrh) << 32))

#define PS2_GS_SETREG_UV(u, v) ((__u64)(u) | ((__u64)(v) << 16))

#define PS2_GS_SETREG_XYOFFSET_1	PS2_GS_SETREG_XYOFFSET
#define PS2_GS_SETREG_XYOFFSET_2	PS2_GS_SETREG_XYOFFSET
#define PS2_GS_SETREG_XYOFFSET(ofx, ofy) ((__u64)(ofx) | ((__u64)(ofy) << 32))

#define PS2_GS_SETREG_XYZ3 PS2_GS_SETREG_XYZ
#define PS2_GS_SETREG_XYZ2 PS2_GS_SETREG_XYZ
#define PS2_GS_SETREG_XYZ(x, y, z) \
	((__u64)(x) | ((__u64)(y) << 16) | ((__u64)(z) << 32))

#define PS2_GS_SETREG_XYZF3 PS2_GS_SETREG_XYZF
#define PS2_GS_SETREG_XYZF2 PS2_GS_SETREG_XYZF
#define PS2_GS_SETREG_XYZF(x, y, z, f) \
	((__u64)(x) | ((__u64)(y) << 16) | ((__u64)(z) << 32) | \
	((__u64)(f) << 56))

#define PS2_GS_SETREG_ZBUF_1	PS2_GS_SETREG_ZBUF
#define PS2_GS_SETREG_ZBUF_2	PS2_GS_SETREG_ZBUF
#define PS2_GS_SETREG_ZBUF(zbp, psm, zmsk) \
	((__u64)(zbp) | ((__u64)(psm) << 24) | \
	((__u64)(zmsk) << 32))


/* GS special registers */
#define PS2_GSSREG_PMODE	0x00
#define PS2_GSSREG_SMODE1	0x01
#define PS2_GSSREG_SMODE2	0x02
#define PS2_GSSREG_SRFSH	0x03
#define PS2_GSSREG_SYNCH1	0x04
#define PS2_GSSREG_SYNCH2	0x05
#define PS2_GSSREG_SYNCV	0x06
#define PS2_GSSREG_DISPFB1	0x07
#define PS2_GSSREG_DISPLAY1	0x08
#define PS2_GSSREG_DISPFB2	0x09
#define PS2_GSSREG_DISPLAY2	0x0a
#define PS2_GSSREG_EXTBUF	0x0b
#define PS2_GSSREG_EXTDATA	0x0c
#define PS2_GSSREG_EXTWRITE	0x0d
#define PS2_GSSREG_BGCOLOR	0x0e
#define PS2_GSSREG_CSR		0x40
#define PS2_GSSREG_IMR		0x41
#define PS2_GSSREG_BUSDIR	0x44
#define PS2_GSSREG_SIGLBLID	0x48
#define PS2_GSSREG_SYSCNT	0x4f

/* GS register bit assign/define */
#define PS2_GS_CLEAR_GSREG(p)	*(__u64 *)(p) = 0
/* ALPHA */
typedef struct {
	__u64 A:      2 __attribute__((packed));
	__u64 B:      2 __attribute__((packed));
	__u64 C:      2 __attribute__((packed));
	__u64 D:      2 __attribute__((packed));
	__u64 pad8:  24 __attribute__((packed));
	__u64 FIX:    8 __attribute__((packed));
	__u64 pad40: 24 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_alpha;
#define PS2_GS_ALPHA_A_CS	0
#define PS2_GS_ALPHA_A_CD	1
#define PS2_GS_ALPHA_A_ZERO	2
#define PS2_GS_ALPHA_B_CS	0
#define PS2_GS_ALPHA_B_CD	1
#define PS2_GS_ALPHA_B_ZERO	2
#define PS2_GS_ALPHA_C_AS	0
#define PS2_GS_ALPHA_C_AD	1
#define PS2_GS_ALPHA_C_FIX	2
#define PS2_GS_ALPHA_D_CS	0
#define PS2_GS_ALPHA_D_CD	1
#define PS2_GS_ALPHA_D_ZERO	2

/* BITBLTBUF */
/** use ioctl PS2IOC_{LOAD,SAVE}IMAGE for HOST<->LOCAL xfer **/
typedef struct {
	__u64 SBP:   14 __attribute__((packed));
	__u64 pad14:  2 __attribute__((packed));
	__u64 SBW:    6 __attribute__((packed));
	__u64 pad22:  2 __attribute__((packed));
	__u64 SPSM:   6 __attribute__((packed));
	__u64 pad30:  2 __attribute__((packed));
	__u64 DBP:   14 __attribute__((packed));
	__u64 pad46:  2 __attribute__((packed));
	__u64 DBW:    6 __attribute__((packed));
	__u64 pad54:  2 __attribute__((packed));
	__u64 DPSM:   6 __attribute__((packed));
	__u64 pad62:  2 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_bitbltbuf;

/* CLAMP */
typedef struct {
	__u64 WMS:    2 __attribute__((packed));
	__u64 WMT:    2 __attribute__((packed));
	__u64 MINU:  10 __attribute__((packed));
	__u64 MAXU:  10 __attribute__((packed));
	__u64 MINV:  10 __attribute__((packed));
	__u64 MAXV:  10 __attribute__((packed));
	__u64 pad44: 20 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_clamp;
#define PS2_GS_CLAMP_REPEAT		0
#define PS2_GS_CLAMP_CLAMP		1
#define PS2_GS_CLAMP_REGION_CLAMP	2
#define PS2_GS_CLAMP_REGION_REPEAT	3

/* COLCLAMP */
typedef struct {
	__u64 CLAMP:  1 __attribute__((packed));
	__u64 pad01: 63 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_colclamp;
#define PS2_GS_COLCLAMP_MASK		0
#define PS2_GS_COLCLAMP_CLAMP		1


/* DIMX */
typedef struct {
	__u64 DM00:  3 __attribute__((packed));
	__u64 pad03: 1 __attribute__((packed));
	__u64 DM01:  3 __attribute__((packed));
	__u64 pad07: 1 __attribute__((packed));
	__u64 DM02:  3 __attribute__((packed));
	__u64 pad11: 1 __attribute__((packed));
	__u64 DM03:  3 __attribute__((packed));
	__u64 pad15: 1 __attribute__((packed));
	__u64 DM10:  3 __attribute__((packed));
	__u64 pad19: 1 __attribute__((packed));
	__u64 DM11:  3 __attribute__((packed));
	__u64 pad23: 1 __attribute__((packed));
	__u64 DM12:  3 __attribute__((packed));
	__u64 pad27: 1 __attribute__((packed));
	__u64 DM13:  3 __attribute__((packed));
	__u64 pad31: 1 __attribute__((packed));
	__u64 DM20:  3 __attribute__((packed));
	__u64 pad35: 1 __attribute__((packed));
	__u64 DM21:  3 __attribute__((packed));
	__u64 pad39: 1 __attribute__((packed));
	__u64 DM22:  3 __attribute__((packed));
	__u64 pad43: 1 __attribute__((packed));
	__u64 DM23:  3 __attribute__((packed));
	__u64 pad47: 1 __attribute__((packed));
	__u64 DM30:  3 __attribute__((packed));
	__u64 pad51: 1 __attribute__((packed));
	__u64 DM31:  3 __attribute__((packed));
	__u64 pad55: 1 __attribute__((packed));
	__u64 DM32:  3 __attribute__((packed));
	__u64 pad59: 1 __attribute__((packed));
	__u64 DM33:  3 __attribute__((packed));
	__u64 pad63: 1 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_dimx;

/* DTHE */
typedef struct {
	__u64 DTHE:   1 __attribute__((packed));
	__u64 pad01: 63 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_dthe;
#define PS2_GS_DTHE_OFF		0
#define PS2_GS_DTHE_ON		1

/* FBA */
typedef struct {
	__u64 FBA:   1 __attribute__((packed));
	__u64 pad01: 63 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_fba;

/* FINISH */
typedef struct {
	__u64 pad00: 64 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_finish;

/* FOG */
typedef struct {
	__u64 pad00: 56 __attribute__((packed));
	__u64 F:      8 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_fog;

/* FOGCOL */
typedef struct {
	__u64 FCR:    8 __attribute__((packed));
	__u64 FCG:    8 __attribute__((packed));
	__u64 FCB:    8 __attribute__((packed));
	__u64 pad24: 40 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_fogcol;

/* FRAME */
typedef struct {
	__u64 FBP:    9 __attribute__((packed));
	__u64 pad09:  7 __attribute__((packed));
	__u64 FBW:    6 __attribute__((packed));
	__u64 pad22:  2 __attribute__((packed));
	__u64 PSM:    6 __attribute__((packed));
	__u64 pad30:  2 __attribute__((packed));
	__u64 FBMSK: 32 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_frame;

/* HWREG */
/** use ioctl PS2IOC_{LOAD,SAVE}IMAGE **/
typedef struct {
	__u64 DATA: 64 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_hwreg;

/* LABEL */
typedef struct {
	__u64 ID:    32 __attribute__((packed));
	__u64 IDMSK: 32 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_label;

/* MIPTBP1 */
typedef struct {
	__u64 TBP1:  14 __attribute__((packed));
	__u64 TBW1:   6 __attribute__((packed));
	__u64 TBP2:  14 __attribute__((packed));
	__u64 TBW2:   6 __attribute__((packed));
	__u64 TBP3:  14 __attribute__((packed));
	__u64 TBW3:   6 __attribute__((packed));
	__u64 pad60:  4 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_miptbp1;

/* MIPTBP2 */
typedef struct {
	__u64 TBP4:  14 __attribute__((packed));
	__u64 TBW4:   6 __attribute__((packed));
	__u64 TBP5:  14 __attribute__((packed));
	__u64 TBW5:   6 __attribute__((packed));
	__u64 TBP6:  14 __attribute__((packed));
	__u64 TBW6:   6 __attribute__((packed));
	__u64 pad60:  4 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_miptbp2;

/* PABE */
typedef struct {
	__u64 PABE:   1 __attribute__((packed));
	__u64 pad01: 63 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_pabe;
#define PS2_GS_PABE_OFF		0
#define PS2_GS_PABE_ON		1

/* PRIM */
typedef struct {
	__u64 PRIM:   3 __attribute__((packed));
	__u64 IIP:    1 __attribute__((packed));
	__u64 TME:    1 __attribute__((packed));
	__u64 FGE:    1 __attribute__((packed));
	__u64 ABE:    1 __attribute__((packed));
	__u64 AA1:    1 __attribute__((packed));
	__u64 FST:    1 __attribute__((packed));
	__u64 CTXT:   1 __attribute__((packed));
	__u64 FIX:    1 __attribute__((packed));
	__u64 pad11: 53 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_prim;
#define PS2_GS_PRIM_PRIM_POINT		0
#define PS2_GS_PRIM_PRIM_LINE		1
#define PS2_GS_PRIM_PRIM_LINESTRIP	2
#define PS2_GS_PRIM_PRIM_TRIANGLE	3
#define PS2_GS_PRIM_PRIM_TRISTRIP	4
#define PS2_GS_PRIM_PRIM_TRIFAN		5
#define PS2_GS_PRIM_PRIM_SPRITE		6
#define PS2_GS_PRIM_IIP_FLAT		0
#define PS2_GS_PRIM_IIP_GOURAUD		1
#define PS2_GS_PRIM_TME_OFF		0
#define PS2_GS_PRIM_TME_ON		1
#define PS2_GS_PRIM_FGE_OFF		0
#define PS2_GS_PRIM_FGE_ON		1
#define PS2_GS_PRIM_ABE_OFF		0
#define PS2_GS_PRIM_ABE_ON		1
#define PS2_GS_PRIM_AA1_OFF		0
#define PS2_GS_PRIM_AA1_ON		1
#define PS2_GS_PRIM_FST_STQ		0
#define PS2_GS_PRIM_FST_UV		1
#define PS2_GS_PRIM_CTXT_CONTEXT1	0
#define PS2_GS_PRIM_CTXT_CONTEXT2	1
#define PS2_GS_PRIM_FIX_NOFIXDDA	0
#define PS2_GS_PRIM_FIX_FIXDDA		1

/* PRMODE */
typedef struct {
	__u64 pad00:  3 __attribute__((packed));
	__u64 IIP:    1 __attribute__((packed));
	__u64 TME:    1 __attribute__((packed));
	__u64 FGE:    1 __attribute__((packed));
	__u64 ABE:    1 __attribute__((packed));
	__u64 AA1:    1 __attribute__((packed));
	__u64 FST:    1 __attribute__((packed));
	__u64 CTXT:   1 __attribute__((packed));
	__u64 FIX:    1 __attribute__((packed));
	__u64 pad11: 53 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_prmode;
/* use PRIM defines */

/* PRMODECONT */
typedef struct {
	__u64 AC:     1 __attribute__((packed));
	__u64 pad01: 63 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_prmodecont;
#define PS2_GS_PRMODECONT_REFPRMODE	0
#define PS2_GS_PRMODECONT_REFPRIM	1

/* RGBAQ */
typedef struct {
	__u64 R: 8 __attribute__((packed));
	__u64 G: 8 __attribute__((packed));
	__u64 B: 8 __attribute__((packed));
	__u64 A: 8 __attribute__((packed));
	float Q    __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_rgbaq;

/* SCANMSK */
typedef struct {
	__u64 MSK:    2 __attribute__((packed));
	__u64 pad02: 62 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_scanmsk;
#define PS2_GS_SCANMSK_NOMASK		0
#define PS2_GS_SCANMSK_MASKEVEN		2
#define PS2_GS_SCANMSK_MASKODD		3

/* SCISSOR */
typedef struct {
	__u64 SCAX0: 11 __attribute__((packed));
	__u64 pad11:  5 __attribute__((packed));
	__u64 SCAX1: 11 __attribute__((packed));
	__u64 pad27:  5 __attribute__((packed));
	__u64 SCAY0: 11 __attribute__((packed));
	__u64 pad43:  5 __attribute__((packed));
	__u64 SCAY1: 11 __attribute__((packed));
	__u64 pad59:  5 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_scissor;

/* SIGNAL */
typedef struct {
	__u64 ID:    32 __attribute__((packed));
	__u64 IDMSK: 32 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_signal;

/* ST */
typedef struct {
	float S __attribute__((packed));
	float T __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_st;

/* TEST */
typedef struct {
	__u64 ATE:    1 __attribute__((packed));
	__u64 ATST:   3 __attribute__((packed));
	__u64 AREF:   8 __attribute__((packed));
	__u64 AFAIL:  2 __attribute__((packed));
	__u64 DATE:   1 __attribute__((packed));
	__u64 DATM:   1 __attribute__((packed));
	__u64 ZTE:    1 __attribute__((packed));
	__u64 ZTST:   2 __attribute__((packed));
	__u64 pad19: 45 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_test;
#define PS2_GS_TEST_ATE_OFF		0
#define PS2_GS_TEST_ATE_ON		1
#define PS2_GS_TEST_ATST_NEVER		0
#define PS2_GS_TEST_ATST_ALWAYS		1
#define PS2_GS_TEST_ATST_LESS		2
#define PS2_GS_TEST_ATST_LEQUAL		3
#define PS2_GS_TEST_ATST_EQUAL		4
#define PS2_GS_TEST_ATST_GEQUAL		5
#define PS2_GS_TEST_ATST_GREATER	6
#define PS2_GS_TEST_ATST_NOTEQUAL	7
#define PS2_GS_TEST_AFAIL_KEEP		0
#define PS2_GS_TEST_AFAIL_FB_ONLY	1
#define PS2_GS_TEST_AFAIL_ZB_ONLY	2
#define PS2_GS_TEST_AFAIL_RGB_ONLY	3
#define PS2_GS_TEST_DATE_OFF		0
#define PS2_GS_TEST_DATE_ON		1
#define PS2_GS_TEST_DATM_PASS0		0
#define PS2_GS_TEST_DATM_PASS1		1
#define PS2_GS_TEST_ZTE_OFF		0
#define PS2_GS_TEST_ZTE_ON		1
#define PS2_GS_TEST_ZTST_NEVER		0
#define PS2_GS_TEST_ZTST_ALWAYS		1
#define PS2_GS_TEST_ZTST_GEQUAL		2
#define PS2_GS_TEST_ZTST_GREATER	3
#define PS2_GS_ZNEVER		PS2_GS_TEST_ZTST_NEVER
#define PS2_GS_ZALWAYS		PS2_GS_TEST_ZTST_ALWAYS
#define PS2_GS_ZGEQUAL		PS2_GS_TEST_ZTST_GEQUAL
#define PS2_GS_ZGREATER		PS2_GS_TEST_ZTST_GREATER

/* TEX0 */
typedef struct {
	__u64 TBP0: 14 __attribute__((packed));
	__u64 TBW:   6 __attribute__((packed));
	__u64 PSM:   6 __attribute__((packed));
	__u64 TW:    4 __attribute__((packed));
	__u64 TH:    4 __attribute__((packed));
	__u64 TCC:   1 __attribute__((packed));
	__u64 TFX:   2 __attribute__((packed));
	__u64 CBP:  14 __attribute__((packed));
	__u64 CPSM:  4 __attribute__((packed));
	__u64 CSM:   1 __attribute__((packed));
	__u64 CSA:   5 __attribute__((packed));
	__u64 CLD:   3 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_tex0;
#define PS2_GS_TEX_TCC_RGB			0
#define PS2_GS_TEX_TCC_RGBA			1
#define PS2_GS_TEX_TFX_MODULATE			0
#define PS2_GS_TEX_TFX_DECAL			1
#define PS2_GS_TEX_TFX_HIGHLIGHT		2
#define PS2_GS_TEX_TFX_HIGHLIGHT2		3
#define PS2_GS_TEX_CSM_CSM1			0
#define PS2_GS_TEX_CSM_CSM2			1
#define PS2_GS_TEX_CLD_NOUPDATE			0
#define PS2_GS_TEX_CLD_LOAD			1
#define PS2_GS_TEX_CLD_LOAD_COPY0		2
#define PS2_GS_TEX_CLD_LOAD_COPY1		3
#define PS2_GS_TEX_CLD_TEST0_LOAD_COPY0		4
#define PS2_GS_TEX_CLD_TEST1_LOAD_COPY1		5

/* TEX1 */
typedef struct {
	__u64 LCM:    1 __attribute__((packed));
	__u64 pad01:  1 __attribute__((packed));
	__u64 MXL:    3 __attribute__((packed));
	__u64 MMAG:   1 __attribute__((packed));
	__u64 MMIN:   3 __attribute__((packed));
	__u64 MTBA:   1 __attribute__((packed));
	__u64 pad10:  9 __attribute__((packed));
	__u64 L:      2 __attribute__((packed));
	__u64 pad21: 11 __attribute__((packed));
	__u64 K:     12 __attribute__((packed));
	__u64 pad44: 20 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_tex1;
#define PS2_GS_TEX1_LCM_CALC				0
#define PS2_GS_TEX1_LCM_K				1
#define PS2_GS_TEX1_MMAG_NEAREST			0
#define PS2_GS_TEX1_MMAG_LINEAR				1
#define PS2_GS_TEX1_MMIN_NEAREST			0
#define PS2_GS_TEX1_MMIN_LINEAR				1
#define PS2_GS_TEX1_MMIN_NEAREST_MIPMAP_NEAREST		2
#define PS2_GS_TEX1_MMIN_NEAREST_MIPMAP_LINEAR		3
#define PS2_GS_TEX1_MMIN_LINEAR_MIPMAP_NEAREST		4
#define PS2_GS_TEX1_MMIN_LINEAR_MIPMAP_LINEAR		5
#define PS2_GS_TEX1_MTBA_NOAUTO				0
#define PS2_GS_TEX1_MTBA_AUTO				1

/* TEX2 */
typedef struct {
	__u64 pad00: 20 __attribute__((packed));
	__u64 PSM:    6 __attribute__((packed));
	__u64 pad26: 11 __attribute__((packed));
	__u64 CBP:   14 __attribute__((packed));
	__u64 CPSM:   4 __attribute__((packed));
	__u64 CSM:    1 __attribute__((packed));
	__u64 CSA:    5 __attribute__((packed));
	__u64 CLD:    3 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_tex2;
/* use TEX0 defines */

/* TEXA */
typedef struct {
	__u64 TA0:    8 __attribute__((packed));
	__u64 pad08:  7 __attribute__((packed));
	__u64 AEM:    1 __attribute__((packed));
	__u64 pad16: 16 __attribute__((packed));
	__u64 TA1:    8 __attribute__((packed));
	__u64 pad40: 24 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_texa;
#define PS2_GS_TEXA_AEM_NORMAL		0
#define PS2_GS_TEXA_AEM_BLACKTHRU	1

/* TEXCLUT */
typedef struct {
	__u64 CBW:    6 __attribute__((packed));
	__u64 COU:    6 __attribute__((packed));
	__u64 COV:   10 __attribute__((packed));
	__u64 pad22: 42 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_texclut;

/* TEXFLUSH */
typedef struct {
	__u64 pad00: 64 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_texflush;

/* TRXDIR */
typedef struct {
	__u64 XDR:    2 __attribute__((packed));
	__u64 pad02: 62 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_trxdir;
#define PS2_GS_TRXDIR_HOST_TO_LOCAL	0
#define PS2_GS_TRXDIR_LOCAL_TO_HOST	1
#define PS2_GS_TRXDIR_LOCAL_TO_LOCAL	2

/* TRXPOS */
typedef struct {
	__u64 SSAX:  11 __attribute__((packed));
	__u64 pad11:  5 __attribute__((packed));
	__u64 SSAY:  11 __attribute__((packed));
	__u64 pad27:  5 __attribute__((packed));
	__u64 DSAX:  11 __attribute__((packed));
	__u64 pad43:  5 __attribute__((packed));
	__u64 DSAY:  11 __attribute__((packed));
	__u64 DIR:    2 __attribute__((packed));
	__u64 pad61:  3 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_trxpos;
#define PS2_GS_TRXPOS_DIR_LR_UD		0
#define PS2_GS_TRXPOS_DIR_LR_DU		1
#define PS2_GS_TRXPOS_DIR_RL_UD		2
#define PS2_GS_TRXPOS_DIR_RL_DU		3

/* TRXREG */
typedef struct {
	__u64 RRW:   12 __attribute__((packed));
	__u64 pad12: 20 __attribute__((packed));
	__u64 RRH:   12 __attribute__((packed));
	__u64 pad44: 20 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_trxreg;

/* UV */
typedef struct {
	__u64 U:     14 __attribute__((packed));
	__u64 pad14:  2 __attribute__((packed));
	__u64 V:     14 __attribute__((packed));
	__u64 pad30: 34 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_uv;

/* XYOFFSET */
typedef struct {
	__u64 OFX:   16 __attribute__((packed));
	__u64 pad16: 16 __attribute__((packed));
	__u64 OFY:   16 __attribute__((packed));
	__u64 pad48: 16 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_xyoffset;

/* XYZ2/3 */
typedef struct {
	__u64 X: 16 __attribute__((packed));
	__u64 Y: 16 __attribute__((packed));
	__u64 Z: 32 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_xyz;

/* XYZF2/3 */
typedef struct {
	__u64 X: 16 __attribute__((packed));
	__u64 Y: 16 __attribute__((packed));
	__u64 Z: 24 __attribute__((packed));
	__u64 F:  8 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_xyzf;

/* ZBUF */
typedef struct {
	__u64 ZBP:    9 __attribute__((packed));
	__u64 pad09: 15 __attribute__((packed));
	__u64 PSM:    4 __attribute__((packed));
	__u64 pad28:  4 __attribute__((packed));
	__u64 ZMSK:   1 __attribute__((packed));
	__u64 pad33: 31 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_zbuf;
#define PS2_GS_ZBUF_ZMSK_NOMASK	0
#define PS2_GS_ZBUF_ZMSK_MASK	1

/* GS special registers */
/* BGCOLOR */
typedef struct {
	__u64 R:      8 __attribute__((packed));
	__u64 G:      8 __attribute__((packed));
	__u64 B:      8 __attribute__((packed));
	__u64 pad24: 40 __attribute__((packed));
} __attribute__((packed)) ps2_gssreg_bgcolor;

/* BUSDIR */
/** ioctl, PS2IOC_{LOAD,SAVE}IMAGE **/
/** set always HOST_TO_LOCAL(0) **/

/* CSR */
/** see ps2gs_{en,jp}.txt **/
typedef struct {
	__u64 SIGNAL:      1 __attribute__((packed)); /* ro */
	__u64 FINISH:      1 __attribute__((packed)); /* ro */
	__u64 HSINT:       1 __attribute__((packed)); /* ro */
	__u64 VSINT:       1 __attribute__((packed)); /* ro */
	__u64 reserved04:  3 __attribute__((packed)); /* ro */
	__u64 pad07:       1 __attribute__((packed));
	__u64 FLUSH:       1 __attribute__((packed)); /* rw */
	__u64 RESET:       1 __attribute__((packed)); /* N/A */
	__u64 pad10:       2 __attribute__((packed));
	__u64 NFIELD:      1 __attribute__((packed)); /* ro */
	__u64 FIELD:       1 __attribute__((packed)); /* ro */
	__u64 FIFO:        2 __attribute__((packed)); /* ro */
	__u64 REV:         8 __attribute__((packed)); /* ro */
	__u64 ID:          8 __attribute__((packed)); /* ro */
	__u64 pad32:      32 __attribute__((packed));
} __attribute__((packed)) ps2_gssreg_csr;
#define PS2_GS_CSR_FLUSH		1
#define PS2_GS_CSR_FIELD_EVEN		0
#define PS2_GS_CSR_FIELD_ODD		1
#define PS2_GS_CSR_FIFO_HALFFULL	0
#define PS2_GS_CSR_FIFO_EMPTY		1
#define PS2_GS_CSR_FIFO_ALMOSTFULL	2

/* DISPFB1/2 */
/** see ps2gs_{en,jp}.txt **/
typedef struct {
	__u64 FBP:    9 __attribute__((packed));
	__u64 FBW:    6 __attribute__((packed));
	__u64 PSM:    5 __attribute__((packed));
	__u64 pad20: 12 __attribute__((packed));
	__u64 DBX:   11 __attribute__((packed));
	__u64 DBY:   11 __attribute__((packed));
	__u64 pad54: 10 __attribute__((packed));
} __attribute__((packed)) ps2_gssreg_dispfb;

/* DISPLAY1/2 */
/** see ps2gs_{en,jp}.txt **/
typedef struct {
	__u64 DX:    12 __attribute__((packed));
	__u64 DY:    11 __attribute__((packed));
	__u64 MAGH:   4 __attribute__((packed));
	__u64 MAGV:   2 __attribute__((packed));
	__u64 pad29:  3 __attribute__((packed));
	__u64 DW:    12 __attribute__((packed));
	__u64 DH:    11 __attribute__((packed));
	__u64 pad55:  9 __attribute__((packed));
} __attribute__((packed)) ps2_gssreg_display;

/* EXTBUF */
typedef struct {
	__u64 EXBP:  14 __attribute__((packed));
	__u64 EXBW:   6 __attribute__((packed));
	__u64 FBIN:   2 __attribute__((packed));
	__u64 WFFMD:  1 __attribute__((packed));
	__u64 EMODA:  2 __attribute__((packed));
	__u64 EMODC:  2 __attribute__((packed));
	__u64 pad27:  5 __attribute__((packed));
	__u64 WDX:   11 __attribute__((packed));
	__u64 WDY:   11 __attribute__((packed));
	__u64 pad54: 10 __attribute__((packed));
} __attribute__((packed)) ps2_gssreg_extbuf;
#define PS2_GS_EXTBUF_FBIN_OUT1		0
#define PS2_GS_EXTBUF_FBIN_OUT2		1
#define PS2_GS_EXTBUF_WFFMD_FIELD	0
#define PS2_GS_EXTBUF_WFFMD_FRAME	1
#define PS2_GS_EXTBUF_EMODA_THURU	0
#define PS2_GS_EXTBUF_EMODA_Y		1
#define PS2_GS_EXTBUF_EMODA_Y2		2
#define PS2_GS_EXTBUF_EMODA_ZERO	3
#define PS2_GS_EXTBUF_EMODC_THURU	0
#define PS2_GS_EXTBUF_EMODC_MONO	1
#define PS2_GS_EXTBUF_EMODC_YCbCr	2
#define PS2_GS_EXTBUF_EMODC_ALPHA	3

/* EXTDATA */
typedef struct {
	__u64 SX:    12 __attribute__((packed));
	__u64 SY:    11 __attribute__((packed));
	__u64 SMPH:   4 __attribute__((packed));
	__u64 SMPV:   2 __attribute__((packed));
	__u64 pad29:  3 __attribute__((packed));
	__u64 WW:    12 __attribute__((packed));
	__u64 WH:    11 __attribute__((packed));
	__u64 pad55:  9 __attribute__((packed));
} __attribute__((packed)) ps2_gssreg_extdata;

/* EXTWRITE */
typedef struct {
	__u64 EXTWRITE:  1 __attribute__((packed));
	__u64 pad01: 63 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_extwrite;
#define PS2_GS_EXTWRITE_STOP	0
#define PS2_GS_EXTWRITE_START	1

/* IMR */
/** see ps2event_{en,jp}.txt **/
typedef struct {
	__u64 pad00:      8 __attribute__((packed));
	__u64 SIGMSK:     1 __attribute__((packed)); /* ro */
	__u64 FINISHMSK:  1 __attribute__((packed)); /* ro */
	__u64 HSMSK:      1 __attribute__((packed)); /* ro */
	__u64 VSMSK:      1 __attribute__((packed)); /* ro */
	__u64 reserve12:  1 __attribute__((packed)); /* ro */
	__u64 reserve13:  1 __attribute__((packed)); /* ro */
	__u64 reserve14:  1 __attribute__((packed)); /* ro */
	__u64 pad15:     49 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_imr;

/* PMODE */
/** see ps2gs_{en,jp}.txt **/
typedef struct {
	__u64 EN1:        1 __attribute__((packed));
	__u64 EN2:        1 __attribute__((packed));
	__u64 CRTMD:      3 __attribute__((packed));
	__u64 MMOD:       1 __attribute__((packed));
	__u64 AMOD:       1 __attribute__((packed));
	__u64 SLBG:       1 __attribute__((packed));
	__u64 ALP:        8 __attribute__((packed));
	__u64 reserve16: 17 __attribute__((packed));
	__u64 pad33:     31 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_pmode;
#define PS2_GS_PMODE_EN_OFF		0
#define PS2_GS_PMODE_EN_ON		1
#define PS2_GS_PMODE_MMOD_PORT1		0
#define PS2_GS_PMODE_MMOD_ALP		1
#define PS2_GS_PMODE_AMOD_PORT1		0
#define PS2_GS_PMODE_AMOD_PORT2		1
#define PS2_GS_PMODE_SLBG_BLEND2	0
#define PS2_GS_PMODE_SLBG_BLENDBG	1

/* SIGLBLID */
typedef struct {
	__u64 SIGID: 32 __attribute__((packed));
	__u64 LBLID: 32 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_siglblid;

/* SMODE2 */
/** see ps2gs_{en,jp}.txt **/
typedef struct {
	__u64 INT:    1 __attribute__((packed));
	__u64 FFMD:   1 __attribute__((packed));
	__u64 DPMS:   2 __attribute__((packed));
	__u64 pad04: 60 __attribute__((packed));
} __attribute__((packed)) ps2_gsreg_smode2;
#define PS2_GS_SMODE2_INT_NOINTERLACE	0
#define PS2_GS_SMODE2_INT_INTERLACE	1
#define PS2_GS_SMODE2_FFMD_FIELD	0
#define PS2_GS_SMODE2_FFMD_FRAME	1
#define PS2_GS_SMODE2_DPMS_ON		0
#define PS2_GS_SMODE2_DPMS_STANDBY	1
#define PS2_GS_SMODE2_DPMS_SUSPEND	2
#define PS2_GS_SMODE2_DPMS_OFF		3

#endif /* __PS2_GS_H */
