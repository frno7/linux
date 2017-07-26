/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 99, 2001 Ralf Baechle
 * Copyright (C) 1994, 1995, 1996 Paul M. Antoine.
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2007  Maciej W. Rozycki
 */
#ifndef _ASM_STACKFRAME_H
#define _ASM_STACKFRAME_H

#include <linux/threads.h>

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
#define STATMASK 0x3f
#else
#define STATMASK 0x1f
#endif

		.macro	SAVE_AT
		.set	push
		.set	noat
		LONGD_S	$1, PT_R1(sp)
		.set	pop
		.endm

		.macro	SAVE_TEMP
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		mflhxu	v1
		LONGH_S	v1, PT_LO(sp)
		mflhxu	v1
		LONGH_S	v1, PT_HI(sp)
		mflhxu	v1
		LONGD_S	v1, PT_ACX(sp)
#else
		mfhi	v1
#endif
#ifdef CONFIG_32BIT
		LONGD_S	$8, PT_R8(sp)
		LONGD_S	$9, PT_R9(sp)
#endif
		LONGD_S	$10, PT_R10(sp)
		LONGD_S	$11, PT_R11(sp)
		LONGD_S	$12, PT_R12(sp)
#ifndef CONFIG_CPU_HAS_SMARTMIPS
		LONGH_S	v1, PT_HI(sp)
		mflo	v1
#endif
		LONGD_S	$13, PT_R13(sp)
		LONGD_S	$14, PT_R14(sp)
		LONGD_S	$15, PT_R15(sp)
		LONGD_S	$24, PT_R24(sp)
#ifndef CONFIG_CPU_HAS_SMARTMIPS
		LONGH_S	v1, PT_LO(sp)
#endif
#ifdef CONFIG_CPU_CAVIUM_OCTEON
		/*
		 * The Octeon multiplier state is affected by general
		 * multiply instructions. It must be saved before and
		 * kernel code might corrupt it
		 */
		jal     octeon_mult_save
#endif
		.endm

		.macro	SAVE_STATIC
		LONGD_S	$16, PT_R16(sp)
		LONGD_S	$17, PT_R17(sp)
		LONGD_S	$18, PT_R18(sp)
		LONGD_S	$19, PT_R19(sp)
		LONGD_S	$20, PT_R20(sp)
		LONGD_S	$21, PT_R21(sp)
		LONGD_S	$22, PT_R22(sp)
		LONGD_S	$23, PT_R23(sp)
		LONGD_S	$30, PT_R30(sp)
		.endm

#ifdef CONFIG_SMP
		.macro	get_saved_sp	/* SMP variation */
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		ASM_CPUID_MFC0	k0, ASM_SMP_CPUID_REG
#if defined(CONFIG_32BIT) || defined(KBUILD_64BIT_SYM32)
		lui	k1, %hi(kernelsp)
#else
		lui	k1, %highest(kernelsp)
		daddiu	k1, %higher(kernelsp)
		dsll	k1, 16
		daddiu	k1, %hi(kernelsp)
		dsll	k1, 16
#endif
		LONGI_SRL	k0, SMP_CPUID_PTRSHIFT
		LONGI_ADDU	k1, k0
		LONGI_L	k1, %lo(kernelsp)(k1)
		.endm

		.macro	set_saved_sp stackp temp temp2
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		ASM_CPUID_MFC0	\temp, ASM_SMP_CPUID_REG
		LONGI_SRL	\temp, SMP_CPUID_PTRSHIFT
		LONGI_S	\stackp, kernelsp(\temp)
		.endm
#else /* !CONFIG_SMP */
		.macro	get_saved_sp	/* Uniprocessor variation */
#ifdef CONFIG_CPU_JUMP_WORKAROUNDS
		/*
		 * Clear BTB (branch target buffer), forbid RAS (return address
		 * stack) to workaround the Out-of-order Issue in Loongson2F
		 * via its diagnostic register.
		 */
		move	k0, ra
		jal	1f
		 nop
1:		jal	1f
		 nop
1:		jal	1f
		 nop
1:		jal	1f
		 nop
1:		move	ra, k0
		li	k0, 3
		mtc0	k0, $22
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
#endif /* CONFIG_CPU_JUMP_WORKAROUNDS */
#if defined(CONFIG_32BIT) || defined(KBUILD_64BIT_SYM32)
		lui	k1, %hi(kernelsp)
#else
		lui	k1, %highest(kernelsp)
		daddiu	k1, %higher(kernelsp)
		dsll	k1, k1, 16
		daddiu	k1, %hi(kernelsp)
		dsll	k1, k1, 16
#endif
		LONGI_L	k1, %lo(kernelsp)(k1)
		.endm

		.macro	set_saved_sp stackp temp temp2
		LONGI_S	\stackp, kernelsp
		.endm
#endif

		.macro	SAVE_SOME
		.set	push
		.set	noat
		.set	reorder
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	k0, CP0_STATUS
		sll	k0, 3		/* extract cu0 bit */
		.set	noreorder
		bltz	k0, 8f
		 move	k1, sp
		.set	reorder
		/* Called from user mode, new stack. */
		get_saved_sp
8:
#ifdef CONFIG_R5900_128BIT_SUPPORT
		/* Align stack to 16 byte. */
		ori		k1, k1, 16 - 1
		xori	k1, k1, 16 - 1
#endif
#ifndef CONFIG_CPU_DADDI_WORKAROUNDS
		move	k0, sp
		PTR_SUBU sp, k1, PT_SIZE
#else
		.set	at=k0
		PTR_SUBU k1, PT_SIZE
		.set	noat
		move	k0, sp
		move	sp, k1
#endif
		LONGD_S	k0, PT_R29(sp)
		LONGD_S	$3, PT_R3(sp)
		/*
		 * You might think that you don't need to save $0,
		 * but the FPU emulator and gdb remote debug stub
		 * need it to operate correctly
		 */
		LONGD_S	$0, PT_R0(sp)
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	v1, CP0_STATUS
		LONGD_S	$2, PT_R2(sp)
		LONGI_S	v1, PT_STATUS(sp)
		LONGD_S	$4, PT_R4(sp)
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	v1, CP0_CAUSE
		LONGD_S	$5, PT_R5(sp)
		LONGI_S	v1, PT_CAUSE(sp)
		LONGD_S	$6, PT_R6(sp)
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		MFC0	v1, CP0_EPC
		LONGD_S	$7, PT_R7(sp)
#ifdef CONFIG_64BIT
		LONGD_S	$8, PT_R8(sp)
		LONGD_S	$9, PT_R9(sp)
#endif
		LONGI_S	v1, PT_EPC(sp)
		LONGD_S	$25, PT_R25(sp)
		LONGD_S	$28, PT_R28(sp)
		LONGD_S	$31, PT_R31(sp)
		ori	$28, sp, _THREAD_MASK
		xori	$28, _THREAD_MASK
#ifdef CONFIG_CPU_CAVIUM_OCTEON
		.set    mips64
		pref    0, 0($28)       /* Prefetch the current pointer */
#endif
		.set	pop
		.endm

		.macro	SAVE_ALL
		SAVE_SOME
		SAVE_AT
		SAVE_TEMP
		SAVE_STATIC
		.endm

		.macro	RESTORE_AT
		.set	push
		.set	noat
		LONGD_L	$1,  PT_R1(sp)
		.set	pop
		.endm

		.macro	RESTORE_TEMP
#ifdef CONFIG_CPU_CAVIUM_OCTEON
		/* Restore the Octeon multiplier state */
		jal	octeon_mult_restore
#endif
#ifdef CONFIG_CPU_HAS_SMARTMIPS
		LONGD_L	$24, PT_ACX(sp)
		mtlhx	$24
		LONGH_L	$24, PT_HI(sp)
		mtlhx	$24
		LONGH_L	$24, PT_LO(sp)
		mtlhx	$24
#else
		LONGH_L	$24, PT_LO(sp)
		mtlo	$24
		LONGH_L	$24, PT_HI(sp)
		mthi	$24
#endif
#ifdef CONFIG_32BIT
		LONGD_L	$8, PT_R8(sp)
		LONGD_L	$9, PT_R9(sp)
#endif
		LONGD_L	$10, PT_R10(sp)
		LONGD_L	$11, PT_R11(sp)
		LONGD_L	$12, PT_R12(sp)
		LONGD_L	$13, PT_R13(sp)
		LONGD_L	$14, PT_R14(sp)
		LONGD_L	$15, PT_R15(sp)
		LONGD_L	$24, PT_R24(sp)
		.endm

		.macro	RESTORE_STATIC
		LONGD_L	$16, PT_R16(sp)
		LONGD_L	$17, PT_R17(sp)
		LONGD_L	$18, PT_R18(sp)
		LONGD_L	$19, PT_R19(sp)
		LONGD_L	$20, PT_R20(sp)
		LONGD_L	$21, PT_R21(sp)
		LONGD_L	$22, PT_R22(sp)
		LONGD_L	$23, PT_R23(sp)
		LONGD_L	$30, PT_R30(sp)
		.endm

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

		.macro	RESTORE_SOME
		.set	push
		.set	reorder
		.set	noat
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	a0, CP0_STATUS
		li	v1, 0xff00
		ori	a0, STATMASK
		xori	a0, STATMASK
		mtc0	a0, CP0_STATUS
		and	a0, v1
		LONGI_L	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, a0
		mtc0	v0, CP0_STATUS
		LONGD_L	$31, PT_R31(sp)
		LONGD_L	$28, PT_R28(sp)
		LONGD_L	$25, PT_R25(sp)
		LONGD_L	$7,  PT_R7(sp)
		LONGD_L	$6,  PT_R6(sp)
		LONGD_L	$5,  PT_R5(sp)
		LONGD_L	$4,  PT_R4(sp)
		LONGD_L	$3,  PT_R3(sp)
		LONGD_L	$2,  PT_R2(sp)
		.set	pop
		.endm

		.macro	RESTORE_SP_AND_RET
		.set	push
		.set	noreorder
		LONGI_L	k0, PT_EPC(sp)
		LONGD_L	sp, PT_R29(sp)
		jr	k0
		 rfe
		.set	pop
		.endm

#else
		.macro	RESTORE_SOME
		.set	push
		.set	reorder
		.set	noat
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	a0, CP0_STATUS
		ori	a0, STATMASK
		xori	a0, STATMASK
		mtc0	a0, CP0_STATUS
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		li	v1, 0xff00
		and	a0, v1
		LONGI_L	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, a0
		mtc0	v0, CP0_STATUS
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		LONGI_L	v1, PT_EPC(sp)
		MTC0	v1, CP0_EPC
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		LONGD_L	$31, PT_R31(sp)
		LONGD_L	$28, PT_R28(sp)
		LONGD_L	$25, PT_R25(sp)
#ifdef CONFIG_64BIT
		LONGD_L	$8, PT_R8(sp)
		LONGD_L	$9, PT_R9(sp)
#endif
		LONGD_L	$7,  PT_R7(sp)
		LONGD_L	$6,  PT_R6(sp)
		LONGD_L	$5,  PT_R5(sp)
		LONGD_L	$4,  PT_R4(sp)
		LONGD_L	$3,  PT_R3(sp)
		LONGD_L	$2,  PT_R2(sp)
		.set	pop
		.endm

		.macro	RESTORE_SP_AND_RET
		LONGD_L	sp, PT_R29(sp)
		.set	push
		.set	mips3
		eret
		.set	pop
		.endm

#endif

		.macro	RESTORE_SP
		LONGD_L	sp, PT_R29(sp)
		.endm

		.macro	RESTORE_ALL
		RESTORE_TEMP
		RESTORE_STATIC
		RESTORE_AT
		RESTORE_SOME
		RESTORE_SP
		.endm

		.macro	RESTORE_ALL_AND_RET
		RESTORE_TEMP
		RESTORE_STATIC
		RESTORE_AT
		RESTORE_SOME
		RESTORE_SP_AND_RET
		.endm

/*
 * Move to kernel mode and disable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	CLI
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | STATMASK
		or	t0, t1
		xori	t0, STATMASK
		mtc0	t0, CP0_STATUS
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		irq_disable_hazard
		.endm

/*
 * Move to kernel mode and enable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	STI
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | STATMASK
		or	t0, t1
		xori	t0, STATMASK & ~1
		mtc0	t0, CP0_STATUS
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		irq_enable_hazard
		.endm

/*
 * Just move to kernel mode and leave interrupts as they are.  Note
 * for the R3000 this means copying the previous enable from IEp.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	KMODE
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | (STATMASK & ~1)
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
		andi	t2, t0, ST0_IEP
		srl	t2, 2
		or	t0, t2
#endif
		or	t0, t1
		xori	t0, STATMASK & ~1
		mtc0	t0, CP0_STATUS
#ifdef CONFIG_CPU_R5900
		sync.p
#endif
		irq_disable_hazard
		.endm

#endif /* _ASM_STACKFRAME_H */
