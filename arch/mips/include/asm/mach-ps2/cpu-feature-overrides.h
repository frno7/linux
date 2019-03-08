/*
 * PlayStation 2 CPU features
 *
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
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
#define cpu_has_dsp			0
#define cpu_has_userlocal		0
#define cpu_has_64bit_addresses		0
#define cpu_has_64bit   		1
#define cpu_has_64bit_gp_regs		0
#define cpu_has_64bit_zero_reg		0
#define cpu_vmbits			31
#define cpu_has_clo_clz			0
#define cpu_has_ejtag			0
#define cpu_has_ic_fills_f_dc		0
#define cpu_has_inclusive_pcaches	0
#define cpu_has_fpu 			0
#define cpu_has_toshiba_mmi		1

#endif /* __ASM_MACH_PS2_CPU_FEATURE_OVERRIDES_H */
