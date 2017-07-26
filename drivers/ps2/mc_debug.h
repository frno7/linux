/*
 *  PlayStation 2 Memory Card driver
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

#ifndef PS2MC_DEBUG_H
#define PS2MC_DEBUG_H

#ifdef PS2MC_DEBUG

#define DBG_POLLING	(1<< 0)
#define DBG_INFO	(1<< 1)
#define DBG_DIRCACHE	(1<< 2)
#define DBG_PATHCHECK	(1<< 3)
#define DBG_DEV		(1<< 4)
#define DBG_FILESEM	(1<< 5)
#define DBG_LOG_LEVEL	KERN_CRIT

#define DPRINT(mask, fmt, args...) \
	do { \
		if ((ps2mc_debug & (mask)) == (mask)) \
			printk(DBG_LOG_LEVEL "ps2mc: " fmt, ## args); \
	} while (0)
#define DPRINTK(mask, fmt, args...) \
	do { \
		if ((ps2mc_debug & (mask)) == (mask)) \
			printk(fmt, ## args); \
	} while (0)
#else
#define DPRINT(mask, fmt, args...) do {} while (0)
#define DPRINTK(mask, fmt, args...) do {} while (0)
#endif

#endif /* PS2MC_DEBUG_H */
