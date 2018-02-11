/*
 * PlayStation 2 Bootinfo
 *
 * Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_BOOTINFO_H
#define __ASM_PS2_BOOTINFO_H

#include <linux/types.h>

#include <asm/mach-ps2/sysconf.h>

#define PS2_BOOTINFO_MACHTYPE_PS2	0
#define PS2_BOOTINFO_MACHTYPE_T10K	1

struct ps2_rtc {
	__u8 padding1;
	__u8 sec;
	__u8 min;
	__u8 hour;
	__u8 padding2;
	__u8 day;
	__u8 mon;
	__u8 year;
};

struct ps2_bootinfo {
    __u32		pccard_type;
    __u32		opt_string;
    __u32		reserved0;
    __u32		reserved1;
    struct ps2_rtc	boot_time;
    __u32		mach_type;
    __u32		pcic_type;
    struct ps2_sysconf	sysconf;
    __u32		magic;
    __s32		size;
    __u32		sbios_base;
    __u32		maxmem;
    __u32		stringsize;
    char		*stringdata;
    char		*ver_vm;
    char		*ver_rb;
    char		*ver_model;
    char		*ver_ps1drv_rom;
    char		*ver_ps1drv_hdd;
    char		*ver_ps1drv_path;
    char		*ver_dvd_id;
    char		*ver_dvd_rom;
    char		*ver_dvd_hdd;
    char		*ver_dvd_path;
};
#define PS2_BOOTINFO_SIZE	((uintptr_t)(&((struct ps2_bootinfo*)0)->magic))

extern struct ps2_bootinfo *ps2_bootinfo;

#endif /* __ASM_PS2_BOOTINFO_H */
