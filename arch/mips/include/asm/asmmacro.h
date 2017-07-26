/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Ralf Baechle
 */
#ifndef _ASM_ASMMACRO_H
#define _ASM_ASMMACRO_H

#include <asm/hazards.h>
#include <asm/asm-offsets.h>

#if defined(CONFIG_32BIT) || defined(CONFIG_CPU_R5900)
#include <asm/asmmacro-32.h>
#endif
#if defined(CONFIG_64BIT) && !defined(CONFIG_CPU_R5900)
#include <asm/asmmacro-64.h>
#endif
#ifdef CONFIG_MIPS_MT_SMTC
#include <asm/mipsmtregs.h>
#endif

#ifdef CONFIG_MIPS_MT_SMTC
	.macro	local_irq_enable reg=t0
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	mfc0	\reg, CP0_TCSTATUS
	ori	\reg, \reg, TCSTATUS_IXMT
	xori	\reg, \reg, TCSTATUS_IXMT
	mtc0	\reg, CP0_TCSTATUS
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	_ehb
	.endm

	.macro	local_irq_disable reg=t0
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	mfc0	\reg, CP0_TCSTATUS
	ori	\reg, \reg, TCSTATUS_IXMT
	mtc0	\reg, CP0_TCSTATUS
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	_ehb
	.endm
#elif defined(CONFIG_CPU_MIPSR2)
	.macro	local_irq_enable reg=t0
	ei
	irq_enable_hazard
	.endm

	.macro	local_irq_disable reg=t0
	di
	irq_disable_hazard
	.endm
#else
	.macro	local_irq_enable reg=t0
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	mfc0	\reg, CP0_STATUS
	ori	\reg, \reg, 1
	mtc0	\reg, CP0_STATUS
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	irq_enable_hazard
	.endm

	.macro	local_irq_disable reg=t0
#ifdef CONFIG_PREEMPT
	lw      \reg, TI_PRE_COUNT($28)
	addi    \reg, \reg, 1
	sw      \reg, TI_PRE_COUNT($28)
#endif
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	mfc0	\reg, CP0_STATUS
	ori	\reg, \reg, 1
	xori	\reg, \reg, 1
	mtc0	\reg, CP0_STATUS
#ifdef CONFIG_CPU_R5900
	sync.p
#endif
	irq_disable_hazard
#ifdef CONFIG_PREEMPT
	lw      \reg, TI_PRE_COUNT($28)
	addi    \reg, \reg, -1
	sw      \reg, TI_PRE_COUNT($28)
#endif
	.endm
#endif /* CONFIG_MIPS_MT_SMTC */

	.macro	fpu_save_16even thread tmp=t0
	cfc1	\tmp, fcr31
	sdc1	$f0,  THREAD_FPR0(\thread)
	sdc1	$f2,  THREAD_FPR2(\thread)
	sdc1	$f4,  THREAD_FPR4(\thread)
	sdc1	$f6,  THREAD_FPR6(\thread)
	sdc1	$f8,  THREAD_FPR8(\thread)
	sdc1	$f10, THREAD_FPR10(\thread)
	sdc1	$f12, THREAD_FPR12(\thread)
	sdc1	$f14, THREAD_FPR14(\thread)
	sdc1	$f16, THREAD_FPR16(\thread)
	sdc1	$f18, THREAD_FPR18(\thread)
	sdc1	$f20, THREAD_FPR20(\thread)
	sdc1	$f22, THREAD_FPR22(\thread)
	sdc1	$f24, THREAD_FPR24(\thread)
	sdc1	$f26, THREAD_FPR26(\thread)
	sdc1	$f28, THREAD_FPR28(\thread)
	sdc1	$f30, THREAD_FPR30(\thread)
	sw	\tmp, THREAD_FCR31(\thread)
	.endm

	.macro	fpu_save_16odd thread
	.set	push
	.set	mips64r2
	sdc1	$f1,  THREAD_FPR1(\thread)
	sdc1	$f3,  THREAD_FPR3(\thread)
	sdc1	$f5,  THREAD_FPR5(\thread)
	sdc1	$f7,  THREAD_FPR7(\thread)
	sdc1	$f9,  THREAD_FPR9(\thread)
	sdc1	$f11, THREAD_FPR11(\thread)
	sdc1	$f13, THREAD_FPR13(\thread)
	sdc1	$f15, THREAD_FPR15(\thread)
	sdc1	$f17, THREAD_FPR17(\thread)
	sdc1	$f19, THREAD_FPR19(\thread)
	sdc1	$f21, THREAD_FPR21(\thread)
	sdc1	$f23, THREAD_FPR23(\thread)
	sdc1	$f25, THREAD_FPR25(\thread)
	sdc1	$f27, THREAD_FPR27(\thread)
	sdc1	$f29, THREAD_FPR29(\thread)
	sdc1	$f31, THREAD_FPR31(\thread)
	.set	pop
	.endm

#ifdef CONFIG_CPU_R5900
	/* Kernel expects that floating point registers are saved as 64-bit
	 * with the sdc1 instruction, but this is not working with R5900.
	 * The 64-bit write is simulated as two 32-bit writes.
	 */
	.macro fpu_save_double thread status tmp1=t0
	cfc1	\tmp1,  fcr31
	swc1	$f0,  THREAD_FPR0(\thread)
	swc1	$f1,  (THREAD_FPR0 + 4)(\thread)
	swc1	$f2,  THREAD_FPR2(\thread)
	swc1	$f3,  (THREAD_FPR2 + 4)(\thread)
	swc1	$f4,  THREAD_FPR4(\thread)
	swc1	$f5,  (THREAD_FPR4 + 4)(\thread)
	swc1	$f6,  THREAD_FPR6(\thread)
	swc1	$f7,  (THREAD_FPR6 + 4)(\thread)
	swc1	$f8,  THREAD_FPR8(\thread)
	swc1	$f9,  (THREAD_FPR8 + 4)(\thread)
	swc1	$f10, THREAD_FPR10(\thread)
	swc1	$f11, (THREAD_FPR10 + 4)(\thread)
	swc1	$f12, THREAD_FPR12(\thread)
	swc1	$f13, (THREAD_FPR12 + 4)(\thread)
	swc1	$f14, THREAD_FPR14(\thread)
	swc1	$f15, (THREAD_FPR14 + 4)(\thread)
	swc1	$f16, THREAD_FPR16(\thread)
	swc1	$f17, (THREAD_FPR16 + 4)(\thread)
	swc1	$f18, THREAD_FPR18(\thread)
	swc1	$f19, (THREAD_FPR18 + 4)(\thread)
	swc1	$f20, THREAD_FPR20(\thread)
	swc1	$f21, (THREAD_FPR20 + 4)(\thread)
	swc1	$f22, THREAD_FPR22(\thread)
	swc1	$f23, (THREAD_FPR22 + 4)(\thread)
	swc1	$f24, THREAD_FPR24(\thread)
	swc1	$f25, (THREAD_FPR24 + 4)(\thread)
	swc1	$f26, THREAD_FPR26(\thread)
	swc1	$f27, (THREAD_FPR26 + 4)(\thread)
	swc1	$f28, THREAD_FPR28(\thread)
	swc1	$f29, (THREAD_FPR28 + 4)(\thread)
	swc1	$f30, THREAD_FPR30(\thread)
	swc1	$f31, (THREAD_FPR30 + 4)(\thread)
	sw	\tmp1, THREAD_FCR31(\thread)
	.endm
#else
	.macro	fpu_save_double thread status tmp
#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	sll	\tmp, \status, 5
	bgez	\tmp, 10f
	fpu_save_16odd \thread
10:
#endif
	fpu_save_16even \thread \tmp
	.endm
#endif

	.macro	fpu_restore_16even thread tmp=t0
	lw	\tmp, THREAD_FCR31(\thread)
	ldc1	$f0,  THREAD_FPR0(\thread)
	ldc1	$f2,  THREAD_FPR2(\thread)
	ldc1	$f4,  THREAD_FPR4(\thread)
	ldc1	$f6,  THREAD_FPR6(\thread)
	ldc1	$f8,  THREAD_FPR8(\thread)
	ldc1	$f10, THREAD_FPR10(\thread)
	ldc1	$f12, THREAD_FPR12(\thread)
	ldc1	$f14, THREAD_FPR14(\thread)
	ldc1	$f16, THREAD_FPR16(\thread)
	ldc1	$f18, THREAD_FPR18(\thread)
	ldc1	$f20, THREAD_FPR20(\thread)
	ldc1	$f22, THREAD_FPR22(\thread)
	ldc1	$f24, THREAD_FPR24(\thread)
	ldc1	$f26, THREAD_FPR26(\thread)
	ldc1	$f28, THREAD_FPR28(\thread)
	ldc1	$f30, THREAD_FPR30(\thread)
	ctc1	\tmp, fcr31
	.endm

	.macro	fpu_restore_16odd thread
	.set	push
	.set	mips64r2
	ldc1	$f1,  THREAD_FPR1(\thread)
	ldc1	$f3,  THREAD_FPR3(\thread)
	ldc1	$f5,  THREAD_FPR5(\thread)
	ldc1	$f7,  THREAD_FPR7(\thread)
	ldc1	$f9,  THREAD_FPR9(\thread)
	ldc1	$f11, THREAD_FPR11(\thread)
	ldc1	$f13, THREAD_FPR13(\thread)
	ldc1	$f15, THREAD_FPR15(\thread)
	ldc1	$f17, THREAD_FPR17(\thread)
	ldc1	$f19, THREAD_FPR19(\thread)
	ldc1	$f21, THREAD_FPR21(\thread)
	ldc1	$f23, THREAD_FPR23(\thread)
	ldc1	$f25, THREAD_FPR25(\thread)
	ldc1	$f27, THREAD_FPR27(\thread)
	ldc1	$f29, THREAD_FPR29(\thread)
	ldc1	$f31, THREAD_FPR31(\thread)
	.set	pop
	.endm

#ifdef CONFIG_CPU_R5900
	/* Kernel expects that floating point registers are read as 64-bit
	 * with the ldc1 instruction, but this is not working with R5900.
	 * The 64-bit read is simulated as two 32-bit reads.
	 */
	.macro	fpu_restore_double thread status tmp=t0
	lw	\tmp, THREAD_FCR31(\thread)
	lwc1	$f0,  THREAD_FPR0(\thread)
	lwc1	$f1,  (THREAD_FPR0 + 4)(\thread)
	lwc1	$f2,  THREAD_FPR2(\thread)
	lwc1	$f3,  (THREAD_FPR2 + 4)(\thread)
	lwc1	$f4,  THREAD_FPR4(\thread)
	lwc1	$f5,  (THREAD_FPR4 + 4)(\thread)
	lwc1	$f6,  THREAD_FPR6(\thread)
	lwc1	$f7,  (THREAD_FPR6 + 4)(\thread)
	lwc1	$f8,  THREAD_FPR8(\thread)
	lwc1	$f9,  (THREAD_FPR8 + 4)(\thread)
	lwc1	$f10, THREAD_FPR10(\thread)
	lwc1	$f11, (THREAD_FPR10 + 4)(\thread)
	lwc1	$f12, THREAD_FPR12(\thread)
	lwc1	$f13, (THREAD_FPR12 + 4)(\thread)
	lwc1	$f14, THREAD_FPR14(\thread)
	lwc1	$f15, (THREAD_FPR14 + 4)(\thread)
	lwc1	$f16, THREAD_FPR16(\thread)
	lwc1	$f17, (THREAD_FPR16 + 4)(\thread)
	lwc1	$f18, THREAD_FPR18(\thread)
	lwc1	$f19, (THREAD_FPR18 + 4)(\thread)
	lwc1	$f20, THREAD_FPR20(\thread)
	lwc1	$f21, (THREAD_FPR20 + 4)(\thread)
	lwc1	$f22, THREAD_FPR22(\thread)
	lwc1	$f23, (THREAD_FPR22 + 4)(\thread)
	lwc1	$f24, THREAD_FPR24(\thread)
	lwc1	$f25, (THREAD_FPR24 + 4)(\thread)
	lwc1	$f26, THREAD_FPR26(\thread)
	lwc1	$f27, (THREAD_FPR26 + 4)(\thread)
	lwc1	$f28, THREAD_FPR28(\thread)
	lwc1	$f29, (THREAD_FPR28 + 4)(\thread)
	lwc1	$f30, THREAD_FPR30(\thread)
	lwc1	$f31, (THREAD_FPR30 + 4)(\thread)
	ctc1	\tmp, fcr31
	.endm
#else
	.macro	fpu_restore_double thread status tmp
#if defined(CONFIG_64BIT) || defined(CONFIG_CPU_MIPS32_R2)
	sll	\tmp, \status, 5
	bgez	\tmp, 10f				# 16 register mode?

	fpu_restore_16odd \thread
10:
#endif
	fpu_restore_16even \thread \tmp
	.endm
#endif

/*
 * Temporary until all gas have MT ASE support
 */
	.macro	DMT	reg=0
	.word	0x41600bc1 | (\reg << 16)
	.endm

	.macro	EMT	reg=0
	.word	0x41600be1 | (\reg << 16)
	.endm

	.macro	DVPE	reg=0
	.word	0x41600001 | (\reg << 16)
	.endm

	.macro	EVPE	reg=0
	.word	0x41600021 | (\reg << 16)
	.endm

	.macro	MFTR	rt=0, rd=0, u=0, sel=0
	 .word	0x41000000 | (\rt << 16) | (\rd << 11) | (\u << 5) | (\sel)
	.endm

	.macro	MTTR	rt=0, rd=0, u=0, sel=0
	 .word	0x41800000 | (\rt << 16) | (\rd << 11) | (\u << 5) | (\sel)
	.endm

#endif /* _ASM_ASMMACRO_H */
