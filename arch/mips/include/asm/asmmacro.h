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
	.endm
#endif /* CONFIG_MIPS_MT_SMTC */

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
