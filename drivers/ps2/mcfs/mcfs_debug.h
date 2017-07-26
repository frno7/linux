/*
 *  PlayStation 2 Memory Card File System driver
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

#define PS2MCFS_DEBUG
#ifdef PS2MCFS_DEBUG

extern unsigned long ps2mcfs_debug;

#define DBG_INFO	(1<< 0)
#define DBG_TRACE	(1<< 1)
#define DBG_PATHCACHE	(1<< 2)
#define DBG_FILECACHE	(1<< 3)
#define DBG_DEBUGHOOK	(1<< 4)
#define DBG_READPAGE	(1<< 5)
#define DBG_BLOCKRW	(1<< 6)
#define DBG_LOCK	(1<< 7)
#define DBG_LOG		(1<<31)
#define DBG_LOG_LEVEL	KERN_CRIT
/*
#define DEBUGLOG(fmt, args...)		debuglog(NULL, fmt, ## args)
*/
#define DEBUGLOG(fmt, args...)		do {} while (0)
#define DPRINT(mask, fmt, args...) \
	do { \
		if ((ps2mcfs_debug & (mask)) == (mask)) { \
			if (ps2mcfs_debug & DBG_LOG) \
				DEBUGLOG(fmt, ## args); \
			else \
				printk(DBG_LOG_LEVEL "ps2mcfs: " fmt, ## args); \
		} \
	} while (0)
#define DPRINTK(mask, fmt, args...) \
	do { \
		if ((ps2mcfs_debug & (mask)) == (mask)) { \
			if (ps2mcfs_debug & DBG_LOG) \
				DEBUGLOG(fmt, ## args); \
			else \
				printk(fmt, ## args); \
		} \
	} while (0)
#else
#define DPRINT(mask, fmt, args...) do {} while (0)
#define DPRINTK(mask, fmt, args...) do {} while (0)
#endif

#define TRACE(fmt, args...) DPRINT(DBG_TRACE, fmt, ## args)
