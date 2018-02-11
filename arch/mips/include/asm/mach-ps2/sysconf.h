/*
 * PlayStation 2 Sysconf
 *
 * Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef __ASM_PS2_SYSCONF_H
#define __ASM_PS2_SYSCONF_H

#include <linux/types.h>

struct ps2_sysconf {
    __s16 timezone;
    __u8 aspect;
    __u8 datenotation;
    __u8 language;
    __u8 spdif;
    __u8 summertime;
    __u8 timenotation;
    __u8 video;
};

#define PS2SYSCONF_GETLINUXCONF		_IOR ('s', 0, struct ps2_sysconf)
#define PS2SYSCONF_SETLINUXCONF		_IOW ('s', 1, struct ps2_sysconf)

#ifdef __KERNEL__
extern struct ps2_sysconf *ps2_sysconf;
#endif

#endif /* __ASM_PS2_SYSCONF_H */
