/*
 *  PlayStation 2 Bootinfo
 *
 *  Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
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

#ifndef __ASM_PS2_BOOTINFO_H
#define __ASM_PS2_BOOTINFO_H

#include <asm/mach-ps2/sysconf.h>

#define PS2_BOOTINFO_MAGIC	0x50324c42	/* "P2LB" */
#define PS2_BOOTINFO_OLDADDR	0x01fff000
#define PS2_BOOTINFO_MACHTYPE_PS2	0
#define PS2_BOOTINFO_MACHTYPE_T10K	1

struct ps2_rtc {
    u_char padding_1;
    u_char sec;
    u_char min;
    u_char hour;
    u_char padding_2;
    u_char day;
    u_char mon;
    u_char year;
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
#define PS2_BOOTINFO_OLDSIZE	((uintptr_t)(&((struct ps2_bootinfo*)0)->magic))

extern struct ps2_bootinfo *ps2_bootinfo;

#endif /* __ASM_PS2_BOOTINFO_H */
