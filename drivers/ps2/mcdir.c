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

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <linux/ps2/mcio.h>
#include <linux/smp_lock.h>

#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/sifdefs.h>

#include "mc.h"
#include "mccall.h"

//#define PS2MC_DEBUG
#include "mcpriv.h"
#include "mc_debug.h"

/*
 * macro defines
 */

/*
 * variables
 */
/* directory info cache stuff */
static char dircache_path[McMaxPathLen + 1];
static int dircache_pos;
static int dircache_next_pos;
static int dircache_count = -1;
static int dircache_portslot;
static struct ps2mc_dirent dircache[PS2MC_DIRCACHESIZE];

void
ps2mc_dircache_invalidate(int portslot)
{
	ps2sif_assertlock(ps2mc_lock, "ps2mc_dircache_invalidate");
	if (dircache_portslot == portslot)
		dircache_count = -1;
}

void
ps2mc_dircache_invalidate_next_pos(int portslot)
{
	ps2sif_assertlock(ps2mc_lock, "ps2mc_dircache_invalidate_next_pos");
	if (dircache_portslot == portslot)
		dircache_next_pos = 0;
}

#define cache_isvalid()	(dircache_count != -1)

/*
 * read single directory entry
 *
 * return value:
 *   < 0 error
 *   0   no more entries
 *   0 < succeeded
 *       if return value equals maxent, there might be more entries.
 */
static int
ps2mc_readdir_sub(int portslot, const char *path, int pos,
		  struct ps2mc_dirent *buf)
{
	int res;
	int curpos;
	char tmp_path[McMaxPathLen + 1];

	/*
	 * get the lock
	 */
	res = ps2sif_lock_interruptible(ps2mc_lock, "mc format");
	if (res < 0) {
		DPRINTK(DBG_DIRCACHE, " can't get the lock\n");
		return (res);
	}

	DPRINT(DBG_DIRCACHE,
	       "readdir card%02x %s pos=%d ",
	       portslot, path, pos);
	if (cache_isvalid())
		DPRINTK(DBG_DIRCACHE, "(cache %s pos=%d count=%d)",
			dircache_path, dircache_pos, dircache_count);
	else
		DPRINTK(DBG_DIRCACHE, "(cache invalid)");

	/*
	 * check the cache
	 */
	if (!cache_isvalid() ||
	    dircache_portslot != portslot ||
	    strcmp(dircache_path, path) != 0) {
		ps2mc_dircache_invalidate(dircache_portslot);
		dircache_portslot = portslot;
		strcpy(dircache_path, path);
	}

	/*
	 * Does the cache contains what we want?
	 */
	if (cache_isvalid() &&
	    dircache_pos <= pos && pos < dircache_pos + dircache_count) {
		DPRINTK(DBG_DIRCACHE, " cache hit\n");
		goto out;
	}

	/*
	 * are there more entries in the directory?
	 */
	if (cache_isvalid() &&
	    dircache_pos + dircache_count <= pos) {
		if (dircache_count < PS2MC_DIRCACHESIZE) {
			DPRINTK(DBG_DIRCACHE, " no more entry(%d entries)\n",
				dircache_pos + dircache_count);
			ps2sif_unlock(ps2mc_lock);
			return (0);	/* no more entries */
		} else {
			DPRINTK(DBG_DIRCACHE,
				" read more entries continuously...\n");
			curpos = dircache_next_pos;
		}
	} else {
		DPRINTK(DBG_DIRCACHE, " read entries from start\n");
		curpos = 0;
	}

	if (McMaxPathLen <= strlen(dircache_path) + 2) {
		printk("ps2mc: path name is too long\n");
		return (-ENOENT);
	}
	if (strcmp(dircache_path, "/") == 0)
		strcpy(tmp_path, "/*");
	else
		sprintf(tmp_path, "%s/*", dircache_path);

	for ( ; ; ) {
		/*
		 * read entries
		 */
		res = ps2mc_getdir_sub(dircache_portslot, tmp_path,
				       curpos == 0 ? 0 : 1,
				       PS2MC_DIRCACHESIZE, dircache);
		if (res < 0) {
			ps2mc_dircache_invalidate(dircache_portslot);
			ps2sif_unlock(ps2mc_lock);
			return (res);	/* error */
		}
		if (res == 0) {
			/* save previous cache contents */
			ps2sif_unlock(ps2mc_lock);
			return (0);	/* no more directory entries */
		}
		dircache_pos = curpos;
		dircache_count = res;	/* now the cache is valid */
		curpos += res;
		dircache_next_pos = curpos;
		if (dircache_pos <= pos &&
		    pos < dircache_pos + dircache_count)
			break;		/* succeeded */
		if (res < PS2MC_DIRCACHESIZE) {
			ps2sif_unlock(ps2mc_lock);
			return (0);	/* no more directory entries */
		}
	}

 out:
	memcpy(buf, &dircache[pos - dircache_pos],
	       sizeof(struct ps2mc_dirent));
	ps2sif_unlock(ps2mc_lock);

	return (1);	/* succeeded */
}

/*
 * read directory infomation
 *
 * return value:
 *   < 0 error
 *   0   no more entries
 *   0 < succeeded
 *       if return value equals maxent, there might be more entries.
 */
int
ps2mc_readdir(int portslot, const char *path, int pos,
	      struct ps2mc_dirent *buf, int maxent)
{
	int res, count;

	count = 0;
	while (count < maxent) {
		res = ps2mc_readdir_sub(portslot, path, pos++, &buf[count]);
		if (res <= 0)
			return count ? count : res;
		count++;
	}

	return (count);
}

