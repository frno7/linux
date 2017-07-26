/*
 *  Context switch on MIPS r5900. Copied from r4k_switch.c.
 *
 *  Copyright (C) 2010-2013 Juergen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <asm/asm.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

#include <asm/asmmacro.h>

/*
 * Offset to the current process status flags, the first 32 bytes of the
 * stack are not used.
 */
#define ST_OFF (_THREAD_SIZE - 32 - PT_SIZE + PT_STATUS)

/*
 * FPU context is saved iff the process has used it's FPU in the current
 * time slice as indicated by _TIF_USEDFPU.  In any case, the CU1 bit for user
 * space STATUS register should be 0, so that a process *always* starts its
 * userland with FPU disabled after each context switch.
 *
 * FPU will be enabled as soon as the process accesses FPU again, through
 * do_cpu() trap.
 */

/*
 * task_struct *resume(task_struct *prev, task_struct *next,
 *                     struct thread_info *next_ti)
 */
	.align	5
	LEAF(resume)
	sync.p
	mfc0	t1, CP0_STATUS
	LONGI_S	t1, THREAD_STATUS(a0)
	cpu_save_nonscratch a0
	LONGD_S	ra, THREAD_REG31(a0)

	/*
	 * check if we need to save FPU registers
	 */
	PTR_L	t3, TASK_THREAD_INFO(a0)
	LONGI_L	t0, TI_FLAGS(t3)
	li	t1, _TIF_USEDFPU
	and	t2, t0, t1
	beqz	t2, 1f
	nor	t1, zero, t1

	and	t0, t0, t1
	LONGI_S	t0, TI_FLAGS(t3)

	/*
	 * clear saved user stack CU1 bit
	 */
	LONGI_L	t0, ST_OFF(t3)
	li	t1, ~ST0_CU1
	and	t0, t0, t1
	LONGI_S	t0, ST_OFF(t3)

	fpu_save_double a0 t0 t1		# c0_status passed in t0
						# clobbers t1
1:

	/*
	 * The order of restoring the registers takes care of the race
	 * updating $28, $29 and kernelsp without disabling ints.
	 */
	move	$28, a2
	cpu_restore_nonscratch a1

	PTR_ADDU	t0, $28, _THREAD_SIZE - 32
	set_saved_sp	t0, t1, t2
#ifdef CONFIG_MIPS_MT_SMTC
	/* Read-modify-writes of Status must be atomic on a VPE */
	sync.p
	mfc0	t2, CP0_TCSTATUS
	ori	t1, t2, TCSTATUS_IXMT
	mtc0	t1, CP0_TCSTATUS
	sync.p
	andi	t2, t2, TCSTATUS_IXMT
	_ehb
	DMT	8				# dmt	t0
	move	t1,ra
	jal	mips_ihb
	move	ra,t1
#endif /* CONFIG_MIPS_MT_SMTC */
	sync.p
	mfc0	t1, CP0_STATUS		/* Do we really need this? */
	li	a3, 0xff01
	and	t1, a3
	LONGI_L	a2, THREAD_STATUS(a1)
	nor	a3, $0, a3
	and	a2, a3
	or	a2, t1
	mtc0	a2, CP0_STATUS
	sync.p
#ifdef CONFIG_MIPS_MT_SMTC
	_ehb
	andi	t0, t0, VPECONTROL_TE
	beqz	t0, 1f
	emt
1:
	sync.p
	mfc0	t1, CP0_TCSTATUS
	xori	t1, t1, TCSTATUS_IXMT
	or	t1, t1, t2
	mtc0	t1, CP0_TCSTATUS
	sync.p
	_ehb
#endif /* CONFIG_MIPS_MT_SMTC */
	move	v0, a0
	jr	ra
	END(resume)

/*
 * Save a thread's fp context.
 */
LEAF(_save_fp)
#ifdef CONFIG_64BIT
	sync.p
	mfc0	t0, CP0_STATUS
#endif
	fpu_save_double a0 t0 t1		# clobbers t1
	jr	ra
	END(_save_fp)

/*
 * Restore a thread's fp context.
 */
LEAF(_restore_fp)
#ifdef CONFIG_64BIT
	sync.p
	mfc0	t0, CP0_STATUS
#endif
	fpu_restore_double a0 t0 t1		# clobbers t1
	jr	ra
	END(_restore_fp)

/*
 * Load the FPU with signalling NANS.  This bit pattern we're using has
 * the property that no matter whether considered as single or as double
 * precision represents signaling NANS.
 *
 * We initialize fcr31 to rounding to nearest, no exceptions.
 */

#define FPU_DEFAULT  0x00000000

LEAF(_init_fpu)
#ifdef CONFIG_MIPS_MT_SMTC
	/* Rather than manipulate per-VPE Status, set per-TC bit in TCStatus */
	sync.p
	mfc0	t0, CP0_TCSTATUS
	/* Bit position is the same for Status, TCStatus */
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_TCSTATUS
	sync.p
#else /* Normal MIPS CU1 enable */
	sync.p
	mfc0	t0, CP0_STATUS
	li	t1, ST0_CU1
	or	t0, t1
	mtc0	t0, CP0_STATUS
	sync.p
#endif /* CONFIG_MIPS_MT_SMTC */
	enable_fpu_hazard

	li	t1, FPU_DEFAULT
	ctc1	t1, fcr31

	li	t1, -1				# SNaN

	mtc1	t1, $f0
	mtc1	t1, $f1
	mtc1	t1, $f2
	mtc1	t1, $f3
	mtc1	t1, $f4
	mtc1	t1, $f5
	mtc1	t1, $f6
	mtc1	t1, $f7
	mtc1	t1, $f8
	mtc1	t1, $f9
	mtc1	t1, $f10
	mtc1	t1, $f11
	mtc1	t1, $f12
	mtc1	t1, $f13
	mtc1	t1, $f14
	mtc1	t1, $f15
	mtc1	t1, $f16
	mtc1	t1, $f17
	mtc1	t1, $f18
	mtc1	t1, $f19
	mtc1	t1, $f20
	mtc1	t1, $f21
	mtc1	t1, $f22
	mtc1	t1, $f23
	mtc1	t1, $f24
	mtc1	t1, $f25
	mtc1	t1, $f26
	mtc1	t1, $f27
	mtc1	t1, $f28
	mtc1	t1, $f29
	mtc1	t1, $f30
	mtc1	t1, $f31
	jr	ra
	END(_init_fpu)
