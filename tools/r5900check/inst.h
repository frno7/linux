// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 * Format of an instruction in memory.
 *
 * Copyright (C) 1996, 2000 by Ralf Baechle
 * Copyright (C) 2006 by Thiemo Seufer
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */

#ifndef R5900CHECK_INST_H
#define R5900CHECK_INST_H

#include "types.h"

/*
 * Major opcodes; before MIPS IV cop1x was called cop3.
 */
enum major_op {
	spec_op, bcond_op, j_op, jal_op,
	beq_op, bne_op, blez_op, bgtz_op,
	addi_op, pop10_op = addi_op, addiu_op, slti_op, sltiu_op,
	andi_op, ori_op, xori_op, lui_op,
	cop0_op, cop1_op, cop2_op, cop1x_op,
	beql_op, bnel_op, blezl_op, bgtzl_op,
	daddi_op, pop30_op = daddi_op, daddiu_op, ldl_op, ldr_op,
	spec2_op, jalx_op, mdmx_op, msa_op = mdmx_op, spec3_op,
	lb_op, lh_op, lwl_op, lw_op,
	lbu_op, lhu_op, lwr_op, lwu_op,
	sb_op, sh_op, swl_op, sw_op,
	sdl_op, sdr_op, swr_op, cache_op,
	ll_op, lwc1_op, lwc2_op, bc6_op = lwc2_op, pref_op,
	lld_op, ldc1_op, ldc2_op, pop66_op = ldc2_op, ld_op,
	sc_op, swc1_op, swc2_op, balc6_op = swc2_op, major_3b_op,
	scd_op, sdc1_op, sdc2_op, pop76_op = sdc2_op, sd_op
};

/*
 * func field of spec opcode.
 */
enum spec_op {
	sll_op, movc_op, srl_op, sra_op,
	sllv_op, pmon_op, srlv_op, srav_op,
	jr_op, jalr_op, movz_op, movn_op,
	syscall_op, break_op, spim_op, sync_op,
	mfhi_op, mthi_op, mflo_op, mtlo_op,
	dsllv_op, spec2_unused_op, dsrlv_op, dsrav_op,
	mult_op, multu_op, div_op, divu_op,
	dmult_op, dmultu_op, ddiv_op, ddivu_op,
	add_op, addu_op, sub_op, subu_op,
	and_op, or_op, xor_op, nor_op,
	spec3_unused_op, spec4_unused_op, slt_op, sltu_op,
	dadd_op, daddu_op, dsub_op, dsubu_op,
	tge_op, tgeu_op, tlt_op, tltu_op,
	teq_op, seleqz_op, tne_op, selnez_op,
	dsll_op, spec5_unused_op, dsrl_op, dsra_op,
	dsll32_op, spec6_unused_op, dsrl32_op, dsra32_op
};

/*
 * func field of spec2 opcode.
 */
enum spec2_op {
	madd_op, maddu_op, mul_op, spec2_3_unused_op,
	msub_op, msubu_op, /* more unused ops */
	clz_op = 0x20, clo_op,
	dclz_op = 0x24, dclo_op,
	sdbpp_op = 0x3f
};

/*
 * func field of spec3 opcode.
 */
enum spec3_op {
	ext_op, dextm_op, dextu_op, dext_op,
	ins_op, dinsm_op, dinsu_op, dins_op,
	yield_op  = 0x09, lx_op     = 0x0a,
	lwle_op   = 0x19, lwre_op   = 0x1a,
	cachee_op = 0x1b, sbe_op    = 0x1c,
	she_op    = 0x1d, sce_op    = 0x1e,
	swe_op    = 0x1f, bshfl_op  = 0x20,
	swle_op   = 0x21, swre_op   = 0x22,
	prefe_op  = 0x23, dbshfl_op = 0x24,
	cache6_op = 0x25, sc6_op    = 0x26,
	scd6_op   = 0x27, lbue_op   = 0x28,
	lhue_op   = 0x29, lbe_op    = 0x2c,
	lhe_op    = 0x2d, lle_op    = 0x2e,
	lwe_op    = 0x2f, pref6_op  = 0x35,
	ll6_op    = 0x36, lld6_op   = 0x37,
	rdhwr_op  = 0x3b
};

/*
 * Bits 10-6 minor opcode for r6 spec mult/div encodings
 */
enum mult_op {
	mult_mult_op = 0x0,
	mult_mul_op = 0x2,
	mult_muh_op = 0x3,
};
enum multu_op {
	multu_multu_op = 0x0,
	multu_mulu_op = 0x2,
	multu_muhu_op = 0x3,
};
enum div_op {
	div_div_op = 0x0,
	div_div6_op = 0x2,
	div_mod_op = 0x3,
};
enum divu_op {
	divu_divu_op = 0x0,
	divu_divu6_op = 0x2,
	divu_modu_op = 0x3,
};
enum dmult_op {
	dmult_dmult_op = 0x0,
	dmult_dmul_op = 0x2,
	dmult_dmuh_op = 0x3,
};
enum dmultu_op {
	dmultu_dmultu_op = 0x0,
	dmultu_dmulu_op = 0x2,
	dmultu_dmuhu_op = 0x3,
};
enum ddiv_op {
	ddiv_ddiv_op = 0x0,
	ddiv_ddiv6_op = 0x2,
	ddiv_dmod_op = 0x3,
};
enum ddivu_op {
	ddivu_ddivu_op = 0x0,
	ddivu_ddivu6_op = 0x2,
	ddivu_dmodu_op = 0x3,
};

/*
 * rt field of bcond opcodes.
 */
enum rt_op {
	bltz_op, bgez_op, bltzl_op, bgezl_op,
	spimi_op, unused_rt_op_0x05, unused_rt_op_0x06, unused_rt_op_0x07,
	tgei_op, tgeiu_op, tlti_op, tltiu_op,
	teqi_op, unused_0x0d_rt_op, tnei_op, unused_0x0f_rt_op,
	bltzal_op, bgezal_op, bltzall_op, bgezall_op,
	rt_op_0x14, rt_op_0x15, rt_op_0x16, rt_op_0x17,
	rt_op_0x18, rt_op_0x19, rt_op_0x1a, rt_op_0x1b,
	bposge32_op, rt_op_0x1d, rt_op_0x1e, synci_op
};

/*
 * rs field of cop opcodes.
 */
enum cop_op {
	mfc_op	      = 0x00, dmfc_op	    = 0x01,
	cfc_op	      = 0x02, mfhc0_op	    = 0x02,
	mfhc_op       = 0x03, mtc_op	    = 0x04,
	dmtc_op	      = 0x05, ctc_op	    = 0x06,
	mthc0_op      = 0x06, mthc_op	    = 0x07,
	bc_op	      = 0x08, bc1eqz_op     = 0x09,
	mfmc0_op      = 0x0b, bc1nez_op     = 0x0d,
	wrpgpr_op     = 0x0e, cop_op	    = 0x10,
	copm_op	      = 0x18
};

/*
 * rt field of cop.bc_op opcodes
 */
enum bcop_op {
	bcf_op, bct_op, bcfl_op, bctl_op
};

/*
 * func field of cop0 coi opcodes.
 */
enum cop0_coi_func {
	tlbr_op	      = 0x01, tlbwi_op	    = 0x02,
	tlbwr_op      = 0x06, tlbp_op	    = 0x08,
	rfe_op	      = 0x10, eret_op	    = 0x18,
	wait_op       = 0x20, hypcall_op    = 0x28
};

/*
 * func field of cop0 com opcodes.
 */
enum cop0_com_func {
	tlbr1_op      = 0x01, tlbw_op	    = 0x02,
	tlbp1_op      = 0x08, dctr_op	    = 0x09,
	dctw_op	      = 0x0a
};

/*
 * fmt field of cop1 opcodes.
 */
enum cop1_fmt {
	s_fmt, d_fmt, e_fmt, q_fmt,
	w_fmt, l_fmt
};

/*
 * func field of cop1 instructions using d, s or w format.
 */
enum cop1_sdw_func {
	fadd_op	     =	0x00, fsub_op	   =  0x01,
	fmul_op	     =	0x02, fdiv_op	   =  0x03,
	fsqrt_op     =	0x04, fabs_op	   =  0x05,
	fmov_op	     =	0x06, fneg_op	   =  0x07,
	froundl_op   =	0x08, ftruncl_op   =  0x09,
	fceill_op    =	0x0a, ffloorl_op   =  0x0b,
	fround_op    =	0x0c, ftrunc_op	   =  0x0d,
	fceil_op     =	0x0e, ffloor_op	   =  0x0f,
	fsel_op      =  0x10,
	fmovc_op     =	0x11, fmovz_op	   =  0x12,
	fmovn_op     =	0x13, fseleqz_op   =  0x14,
	frecip_op    =  0x15, frsqrt_op    =  0x16,
	fselnez_op   =  0x17, fmaddf_op    =  0x18,
	fmsubf_op    =  0x19, frint_op     =  0x1a,
	fclass_op    =  0x1b, fmin_op      =  0x1c,
	fmina_op     =  0x1d, fmax_op      =  0x1e,
	fmaxa_op     =  0x1f, fcvts_op     =  0x20,
	fcvtd_op     =	0x21, fcvte_op	   =  0x22,
	fcvtw_op     =	0x24, fcvtl_op	   =  0x25,
	fcmp_op	     =	0x30
};

/*
 * func field of cop1x opcodes (MIPS IV).
 */
enum cop1x_func {
	lwxc1_op     =	0x00, ldxc1_op	   =  0x01,
	swxc1_op     =  0x08, sdxc1_op	   =  0x09,
	pfetch_op    =	0x0f, madd_s_op	   =  0x20,
	madd_d_op    =	0x21, madd_e_op	   =  0x22,
	msub_s_op    =	0x28, msub_d_op	   =  0x29,
	msub_e_op    =	0x2a, nmadd_s_op   =  0x30,
	nmadd_d_op   =	0x31, nmadd_e_op   =  0x32,
	nmsub_s_op   =	0x38, nmsub_d_op   =  0x39,
	nmsub_e_op   =	0x3a
};

/*
 * func field for mad opcodes (MIPS IV).
 */
enum mad_func {
	madd_fp_op	= 0x08, msub_fp_op	= 0x0a,
	nmadd_fp_op	= 0x0c, nmsub_fp_op	= 0x0e
};

struct j_format {
	__BITFIELD_FIELD(u32 opcode : 6, /* Jump format */
	__BITFIELD_FIELD(u32 target : 26,
	;))
};

struct i_format {			/* signed immediate format */
	__BITFIELD_FIELD(u32 opcode : 6,
	__BITFIELD_FIELD(u32 rs : 5,
	__BITFIELD_FIELD(u32 rt : 5,
	__BITFIELD_FIELD(s32 simmediate : 16,
	;))))
};

struct u_format {			/* unsigned immediate format */
	__BITFIELD_FIELD(u32 opcode : 6,
	__BITFIELD_FIELD(u32 rs : 5,
	__BITFIELD_FIELD(u32 rt : 5,
	__BITFIELD_FIELD(u32 uimmediate : 16,
	;))))
};

struct r_format {			/* Register format */
	__BITFIELD_FIELD(u32 opcode : 6,
	__BITFIELD_FIELD(u32 rs : 5,
	__BITFIELD_FIELD(u32 rt : 5,
	__BITFIELD_FIELD(u32 rd : 5,
	__BITFIELD_FIELD(u32 re : 5,
	__BITFIELD_FIELD(u32 func : 6,
	;))))))
};

struct c0r_format {			/* C0 register format */
	__BITFIELD_FIELD(u32 opcode : 6,
	__BITFIELD_FIELD(u32 rs : 5,
	__BITFIELD_FIELD(u32 rt : 5,
	__BITFIELD_FIELD(u32 rd : 5,
	__BITFIELD_FIELD(u32 z: 8,
	__BITFIELD_FIELD(u32 sel : 3,
	;))))))
};

struct mfmc0_format {			/* MFMC0 register format */
	__BITFIELD_FIELD(u32 opcode : 6,
	__BITFIELD_FIELD(u32 rs : 5,
	__BITFIELD_FIELD(u32 rt : 5,
	__BITFIELD_FIELD(u32 rd : 5,
	__BITFIELD_FIELD(u32 re : 5,
	__BITFIELD_FIELD(u32 sc : 1,
	__BITFIELD_FIELD(u32 : 2,
	__BITFIELD_FIELD(u32 sel : 3,
	;))))))))
};

struct co_format {			/* C0 CO format */
	__BITFIELD_FIELD(u32 opcode : 6,
	__BITFIELD_FIELD(u32 co : 1,
	__BITFIELD_FIELD(u32 code : 19,
	__BITFIELD_FIELD(u32 func : 6,
	;))))
};

struct b_format {			/* BREAK and SYSCALL */
	__BITFIELD_FIELD(u32 opcode : 6,
	__BITFIELD_FIELD(u32 code : 20,
	__BITFIELD_FIELD(u32 func : 6,
	;)))
};

union mips_instruction {
	u32 word;
	u16 halfword[2];
	u8 byte[4];
	struct j_format j_format;
	struct i_format i_format;
	struct u_format u_format;
	struct r_format r_format;
	struct c0r_format c0r_format;
	struct mfmc0_format mfmc0_format;
	struct co_format co_format;
	struct b_format b_format;
};

#endif /* R5900CHECK_INST_H */
