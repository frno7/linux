/*
 *  PlayStation 2 Sysconf
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

#ifndef __ASM_PS2_SYSCONF_H
#define __ASM_PS2_SYSCONF_H

struct ps2_sysconf {
    short timezone;
    u_char aspect;
    u_char datenotation;
    u_char language;
    u_char spdif;
    u_char summertime;
    u_char timenotation;
    u_char video;
};

#define PS2SYSCONF_GETLINUXCONF		_IOR ('s', 0, struct ps2_sysconf)
#define PS2SYSCONF_SETLINUXCONF		_IOW ('s', 1, struct ps2_sysconf)

#ifdef __KERNEL__
extern struct ps2_sysconf *ps2_sysconf;
#endif

#endif /* __ASM_PS2_SYSCONF_H */
