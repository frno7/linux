/*
 *  PlayStation 2 Emotion Engine
 *
 *  Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
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

#ifndef __ASM_PS2_EEDEV_H
#define __ASM_PS2_EEDEV_H

#include <asm/types.h>
#include <asm/io.h>

#define ALIGN16(x)	(((unsigned long)(x) + 15) & ~15)
#define PACK32(x, y)	((x) | ((y) << 16))
#define PACK64(x, y)	((u64)(x) | ((u64)(y) << 32))

#define GSFB_SIZE		(4 * 1024 * 1024)
/* The scratchpad has no physical address, ise the following value to detect
 * the scratchpad in the TLB handlers.
 * The TLB handler expects that the highest bit is set.
 */
#define SPR_PHYS_ADDR		0x80000000
/* EntryLo0 flag to use scratchpad instead or normal RAM. */
#define SCRATCHPAD_RAM		0x80000000
/* Size of scratchpad memory. */
#define SPR_SIZE		16384

/* register defines */

#define IPUREG_CMD		0x10002000
#define IPUREG_CTRL		0x10002010
#define IPUREG_BP		0x10002020
#define IPUREG_TOP		0x10002030

#define GIFREG_BASE		0x10003000
#define GIFREG(x)		(inl(GIFREG_BASE + ((x) << 4)))
#define SET_GIFREG(x, val)	(outl(val, GIFREG_BASE + ((x) << 4)))
#define VIF0REG_BASE		0x10003800
#define VIF0REG(x)		(inl(VIF0REG_BASE + ((x) << 4)))
#define SET_VIF0REG(x, val)		(outl(val, VIF0REG_BASE + ((x) << 4)))
#define VIF1REG_BASE		0x10003c00
#define VIF1REG(x)		(inl(VIF1REG_BASE + ((x) << 4)))
#define SET_VIF1REG(x, val)	(outl(val, VIF1REG_BASE + ((x) << 4)))
#define VIFnREG(n, x)		\
	(inl(VIF0REG_BASE + ((n) * 0x0400) + ((x) << 4)))
#define SET_VIFnREG(n, x, val)		\
	(outl(val, VIF0REG_BASE + ((n) * 0x0400) + ((x) << 4)))

#define VIF0_FIFO		0x10004000
#define VIF1_FIFO		0x10005000
#define GIF_FIFO		0x10006000
#define IPU_O_FIFO		0x10007000
#define IPU_I_FIFO		0x10007010

#define GSSREG_BASE1		0x12000000
#define GSSREG_BASE2		0x12001000
#define GSSREG1(x)		(GSSREG_BASE1 + ((x) << 4))
#define GSSREG2(x)		(GSSREG_BASE2 + (((x) & 0x0f) << 4))

/* inline assembler functions */

union _dword {
        __u64 di;
        struct {
#ifdef CONFIG_CPU_LITTLE_ENDIAN
                __u32   lo, hi;
#else
                __u32   hi, lo;
#endif
        } si;
};

/* TBD: Use offical I/O functions of kernel instead. */
static inline void move_quad(unsigned long dest, unsigned long src)
{
    __asm__ __volatile__(
	"	.set	push\n"
	"	.set	arch=r5900\n"
	"	move	$8,%1\n"
	"	lq     $9,($8)\n"
	"	move	$8,%0\n"
	"	sq     $9,($8)\n"
	"	.set	pop"
	: : "r" (dest), "r" (src) : "$8", "$9" );
}

static inline void dummy_read_quad(unsigned long addr)
{
    __asm__ __volatile__(
	"	.set	push\n"
	"	.set	arch=r5900\n"
	"	lq	$9,(%0)\n"
	"	.set	pop"
	: : "r" (addr) : "$9" );
}

#endif /* __ASM_PS2_EEDEV_H */
