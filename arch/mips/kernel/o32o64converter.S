/*
 *  Conversion between 64-bit and 32-bit native system calls.
 *  Convert syscalls from MIPS ABI o64 to o32.
 *
 *  Copyright (C) 2012-2013 Juergen Urban
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

#include <asm/stackframe.h>

.text
.global syso64_lseek
.set push
.set arch=r5900

syso64_lseek:
	sd	ra, PT_R2(sp)		/* misuse PT_R2 to save return address. */
	jal	sysn32_lseek

	ld	ra, PT_R2(sp)		/* Restore return address. */
	/* o32 returns 64 bit value in v0 and v1. Convert it to a 64
	 * bit value in register v0.
	 */
	pextlw	v0, v1, v0
	jr	ra

.set pop
