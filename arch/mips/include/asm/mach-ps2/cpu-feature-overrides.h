/*
 *  PlayStation 2 CPU features
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
#ifndef __ASM_MACH_PS2_CPU_FEATURE_OVERRIDES_H
#define __ASM_MACH_PS2_CPU_FEATURE_OVERRIDES_H

#define cpu_has_llsc			0
#define cpu_has_4k_cache		1
#define cpu_has_divec			1
#define cpu_has_4kex			1
#define cpu_has_counter			1
#define cpu_has_cache_cdex_p		0
#define cpu_has_cache_cdex_s		0
#define cpu_has_mcheck			0
#define cpu_has_nofpuex			1
#define cpu_has_mipsmt			0
#define cpu_has_vce			0
#define cpu_has_dsp			1
#define cpu_has_userlocal		0
#define cpu_has_64bit_addresses		0
#define cpu_has_64bit   		1
#ifdef CONFIG_R5900_128BIT_SUPPORT
#define cpu_has_64bit_gp_regs		1
#define cpu_has_64bit_zero_reg		1
#else
#define cpu_has_64bit_gp_regs		0
#define cpu_has_64bit_zero_reg		0
#endif
#define cpu_vmbits			31
#define cpu_has_clo_clz			0
#define cpu_has_ejtag			0
#define cpu_has_ic_fills_f_dc		0
#define cpu_has_inclusive_pcaches	0
/* TBD: Currently there is no newer GCC which creates compatible FPU code.
 * Only GCC 2.95 creates compatible FPU code.
 * So FPU is always emulated and disabled here.
 */
#if 1
/* For ABI n32 the 64 bit CPU must be emulated. The 32 bit CPU can't be used.
 * The r5900 FPU doesn't comply with IEEE 794 which is expected by
 * most programs.
 * Tasks with TIF_32BIT_REGS set, are compiled with r5900 FPU support.
 */
#define cpu_has_fpu (test_thread_flag(TIF_R5900FPU))
#else
#define cpu_has_fpu 			0
#endif

#endif
