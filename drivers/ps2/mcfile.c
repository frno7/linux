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
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <linux/ps2/mcio.h>
#include <linux/smp_lock.h>

#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/siflock.h>

#include "mc.h"
#include "mccall.h"

//#define PS2MC_DEBUG
#include "mcpriv.h"
#include "mc_debug.h"

#define MAXFILEDESC	32

/*
 * macro defines
 */

/*
 * variables
 */
static int openedfd = 0;

int
ps2mc_getdtablesize()
{
	return (MIN(McMaxFileDiscr, 32));
}

int
ps2mc_open(int portslot, const char *path, int mode)
{
	int res, result;
	int iopflags;

	switch (mode & O_ACCMODE) {
	case O_RDONLY:
		iopflags = McRDONLY;
		break;
	case O_WRONLY:
		iopflags = McWRONLY;
		break;
	case O_RDWR:
		iopflags = McRDWR;
		break;
	default:
		return -EINVAL;
	}
	if (mode & O_CREAT)
		iopflags |= McCREAT;

	if ((res = down_interruptible(&ps2mc_filesem)) < 0)
		return (res);
	res = ps2sif_lock_interruptible(ps2mc_lock, "mc open");
	if (res < 0) {
		up(&ps2mc_filesem);
		return (res);
	}
	if (mode & O_CREAT) {
		/*
		 * This might create a new entry, thereby,
		 * invalidate directory cache.
		 */
		ps2mc_dircache_invalidate(portslot);
	}

	res = 0;
	if (ps2mclib_Open(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
		      (char *)path, iopflags, &result) != 0) {
		/* error */
		printk("ps2mclib_Open() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "open(): card%d%d %s result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path, result);

	if (0 <= result) {
		/* succeeded */
		res = result;
	} else {
		switch (result) {
		case -7: /* Too many open files */
			res = -EMFILE;
			break;
		default:
			res = -EIO;
			break;
		}
	}

 out:
	if (res < 0) {
		ps2sif_unlock(ps2mc_lock);
		up(&ps2mc_filesem);
	} else {
		if (MAXFILEDESC <= res) {
			printk(KERN_CRIT "ps2mc: ERROR: unexpected fd=%d\n",
			       res);
			ps2sif_unlock(ps2mc_lock);
			up(&ps2mc_filesem);
		} else {
			openedfd |= (1 << res);
			ps2sif_unlock(ps2mc_lock);
		}
	}
	return (res);
}

int
ps2mc_close(int fd)
{
	int res, result;

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc close");
	if (res < 0)
		return (res);

	if (fd < 0 || MAXFILEDESC <= fd || !(openedfd & (1 << fd))) {
		ps2sif_unlock(ps2mc_lock);
		return -EBADF;
	}

	res = 0;
	if (ps2mclib_Close(fd, &result) != 0) {
		/* error */
		printk("ps2mclib_Close() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "close(): result=%d\n", result);

	switch (result) {
	case 0: /* succeeded */
		break;
	case 4: /* Bad file number */
		res = -EBADF;
		break;
	default:
		res = -EIO;
		break;
	}

 out:
	openedfd &= ~(1 << fd);
	ps2sif_unlock(ps2mc_lock);
	up(&ps2mc_filesem);

	return (res);
}

off_t
ps2mc_lseek(int fd, off_t offset, int whence)
{
	int res, result;

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc lseek");
	if (res < 0)
		return (res);

	res = 0;
	if (ps2mclib_Seek(fd, offset, whence, &result) != 0) {
		/* error */
		printk("ps2mclib_Seek() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "lseek(): result=%d\n", result);

	if (0 <= result) {
		/* succeeded */
		res = result;
	} else {
		switch (result) {
		case 4: /* Bad file number */
			res = -EBADF;
			break;
		default:
			res = -EIO;
			break;
		}
	}

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}

ssize_t
ps2mc_write(int fd, const void *buf, size_t size)
{
	int res, result;

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc write");
	if (res < 0)
		return (res);

	res = 0;
	if (ps2mclib_Write(fd, (char*)buf, size, &result) != 0) {
		/* error */
		printk("ps2mclib_Write() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "write(): result=%d\n", result);

	if (0 <= result) {
		/* succeeded */
		res = result;
	} else {
		switch (result) {
		case -4: /* Bad file number */
			res = -EBADF;
			break;
		case -5: /* Operation not permitted */
			res = -EPERM;
			break;
		default:
			res = -EIO;
			break;
		}
	}

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}

ssize_t
ps2mc_read(int fd, const void *buf, size_t size)
{
	int res, result;

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc read");
	if (res < 0)
		return (res);

	res = 0;
	if (ps2mclib_Read(fd, (char*)buf, size, &result) != 0) {
		/* error */
		printk("ps2mclib_Read() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "read(): result=%d\n", result);

	if (0 <= result) {
		/* succeeded */
		res = result;
	} else {
		switch (result) {
		case -4: /* Bad file number */
			res = -EBADF;
			break;
		case -5: /* Operation not permitted */
			res = -EPERM;
			break;
		default:
			res = -EIO;
			break;
		}
	}

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}
