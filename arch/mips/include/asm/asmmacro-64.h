/*
 * asmmacro.h: Assembler macros to make things easier to read.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1998, 1999 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_ASMMACRO_64_H
#define _ASM_ASMMACRO_64_H

#include <asm/asm-offsets.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>

	.macro	cpu_save_nonscratch thread
	LONGD_S	s0, THREAD_REG16(\thread)
	LONGD_S	s1, THREAD_REG17(\thread)
	LONGD_S	s2, THREAD_REG18(\thread)
	LONGD_S	s3, THREAD_REG19(\thread)
	LONGD_S	s4, THREAD_REG20(\thread)
	LONGD_S	s5, THREAD_REG21(\thread)
	LONGD_S	s6, THREAD_REG22(\thread)
	LONGD_S	s7, THREAD_REG23(\thread)
	LONGD_S	sp, THREAD_REG29(\thread)
	LONGD_S	fp, THREAD_REG30(\thread)
	.endm

	.macro	cpu_restore_nonscratch thread
	LONGD_L	s0, THREAD_REG16(\thread)
	LONGD_L	s1, THREAD_REG17(\thread)
	LONGD_L	s2, THREAD_REG18(\thread)
	LONGD_L	s3, THREAD_REG19(\thread)
	LONGD_L	s4, THREAD_REG20(\thread)
	LONGD_L	s5, THREAD_REG21(\thread)
	LONGD_L	s6, THREAD_REG22(\thread)
	LONGD_L	s7, THREAD_REG23(\thread)
	LONGD_L	sp, THREAD_REG29(\thread)
	LONGD_L	fp, THREAD_REG30(\thread)
	LONGD_L	ra, THREAD_REG31(\thread)
	.endm

#endif /* _ASM_ASMMACRO_64_H */
