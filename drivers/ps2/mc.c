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

#include <linux/rtc.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/ps2/mcio.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>

#include <asm/time.h>
#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sifutil.h>
#include <asm/mach-ps2/siflock.h>

#include "mc.h"
#include "mccall.h"

//#define PS2MC_DEBUG
#include "mcpriv.h"
#include "mc_debug.h"

/*
 * macro defines
 */
#define ALIGN2(a, n)	((__typeof__(a))(((unsigned long)(a) + (n) - 1) / (n) * (n)))

/*
 * data types
 */

/*
 * function prototypes
 */
/* mktime is defined in arch/mips/ps2/kernel/time.c */
unsigned long mktime(unsigned int, unsigned int, unsigned int, unsigned int,
		     unsigned int, unsigned int);

/*
 * variables
 */
int ps2mc_debug = 0;
ps2sif_lock_t *ps2mc_lock;	/* the lock which is need to invoke RPC */
static unsigned char *dmabuf = NULL;
McDirEntry *dirbuf;
char *ps2mc_rwbuf;
int ps2mc_basedir_len;
struct semaphore ps2mc_filesem;
struct semaphore ps2mc_waitsem;

/* card status wacthing thread stuff */
static struct completion thread_comp;
static struct task_struct *thread_task = NULL;
static struct list_head listeners;
static volatile int timer_flag = 0;
atomic_t ps2mc_cardgens[PS2MC_NPORTS][PS2MC_NSLOTS];

static char *ps2mc_type_names[] = {
[PS2MC_TYPE_EMPTY]		= "empty",
[PS2MC_TYPE_PS1]		= "PS1 memory card",
[PS2MC_TYPE_PS2]		= "PS2 memory card",
[PS2MC_TYPE_POCKETSTATION]	= "Pocket Station",
};

/*
 * export symbols
 */
EXPORT_SYMBOL(ps2mc_add_listener);
EXPORT_SYMBOL(ps2mc_del_listener);
EXPORT_SYMBOL(ps2mc_getinfo);
EXPORT_SYMBOL(ps2mc_readdir);
EXPORT_SYMBOL(ps2mc_getdir);
EXPORT_SYMBOL(ps2mc_setdir);
EXPORT_SYMBOL(ps2mc_mkdir);
EXPORT_SYMBOL(ps2mc_rename);
EXPORT_SYMBOL(ps2mc_delete);
EXPORT_SYMBOL(ps2mc_getdtablesize);
EXPORT_SYMBOL(ps2mc_close);
EXPORT_SYMBOL(ps2mc_lseek);
EXPORT_SYMBOL(ps2mc_open);
EXPORT_SYMBOL(ps2mc_read);
EXPORT_SYMBOL(ps2mc_write);
EXPORT_SYMBOL(ps2mc_checkdev);
EXPORT_SYMBOL(ps2mc_blkrw_hook);
EXPORT_SYMBOL(ps2mc_terminate_name);
EXPORT_SYMBOL(ps2mc_add_disks);
EXPORT_SYMBOL(ps2mc_del_disks);

module_param(ps2mc_debug, int, 0);

/*
 * function bodies
 */
char*
ps2mc_terminate_name(char buf[PS2MC_NAME_MAX+1], const char *name, int namelen)
{

	memcpy(buf, name, MIN(namelen, PS2MC_NAME_MAX));
	buf[MIN(namelen, PS2MC_NAME_MAX)] = '\0';

	return (buf);
}

/*
 * format memory card
 */
int
ps2mc_format(int portslot)
{
	int res, result;

	if ((res = ps2sif_lock_interruptible(ps2mc_lock, "mc format")) < 0) {
		return (res);
	}

	res = ps2mclib_Format(PS2MC_PORT(portslot), PS2MC_SLOT(portslot), &result);
	if (res != 0 || result != 0) {
		/* error */
		printk("ps2mclib_Format() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "format(): card%d%d result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

out:
	ps2mc_dircache_invalidate(portslot);
	ps2sif_unlock(ps2mc_lock);

	ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);

	return (res);
}

/*
 * unformat memory card
 */
int
ps2mc_unformat(int portslot)
{
	int res, result;

	if ((res = ps2sif_lock_interruptible(ps2mc_lock, "mc format")) < 0) {
		return (res);
	}

	res = ps2mclib_Unformat(PS2MC_PORT(portslot), PS2MC_SLOT(portslot), &result);
	if (res != 0 || result != 0) {
		/* error */
		printk("ps2mclib_Unformat() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "unformat(): card%d%d result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

out:
	ps2mc_dircache_invalidate(portslot);
	ps2sif_unlock(ps2mc_lock);

	ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);

	return (res);
}

/*
 * get memory card info
 */
int
ps2mc_getinfo(int portslot, struct ps2mc_cardinfo *info)
{
	int res;
	int result, type, free, format;

	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);

	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
		return (-ENODEV);

	res = ps2mc_getinfo_sub(portslot, &result, &type, &free, &format);
	if (res < 0)
		return (res);

	memset(info, 0, sizeof(struct ps2mc_cardinfo));
	info->type = PS2MC_TYPE_EMPTY;

	switch (result) {
	case 0:
	case -1:
	case -2:
		/* succeeded normaly */
		break;
	default:
		return (0);
	}

	info->type = type;
	info->freeblocks = free;
	info->formatted = format;
	info->generation = atomic_read(&ps2mc_cardgens[port][slot]);
	if (type == PS2MC_TYPE_PS2) {
		info->blocksize = 1024;		/* XXX, I donna */
		info->totalblocks = 1024 * 8;	/* XXX, 8MB */
	}
	if (type == PS2MC_TYPE_PS1) {
		info->blocksize = 1024 * 8;
		info->totalblocks = 16;	     /* 16 blocks of 8KB (128 KB) */
	}

	return (0);
}

int
ps2mc_checkdev(dev_t dev)
{
	int portslot = MINOR(dev);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);

	if (MAJOR(dev) != PS2MC_MAJOR)
		return (-ENODEV);

	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
		return (-ENODEV);

	return (0);
}

int
ps2mc_getinfo_sub(int portslot, int *result, int *type, int *free, int *format)
{
	int res;
	int port = PS2MC_PORT(portslot), slot = PS2MC_SLOT(portslot);

	if ((res = ps2sif_lock_interruptible(ps2mc_lock, "mc get info")) < 0) {
		return (res);
	}

	res = 0;
	if (ps2mclib_GetInfo(port, slot, type, free, format, result) != 0) {
		/* error */
		printk("ps2mclib_GetInfo() failed\n");
		res = -EIO;
	}

	DPRINT(DBG_POLLING,
	       "getinfo(): card%d%d result=%d type=%d format=%d\n",
	       port, slot,
	       result != NULL ? *result : 0,
	       type != NULL ? *type : 0,
	       format != NULL ? *format : 0);

	if (res == 0 && (*result == -1 || *result == -2)) {
		/* card was replaced */
		atomic_inc(&ps2mc_cardgens[port][slot]);
	}

	ps2sif_unlock(ps2mc_lock);

	return (res);
}

/*
 * make directory
 */
int
ps2mc_mkdir(int portslot, const char *path)
{
	int res, result;

	/*
	 * XXX, recent PS2 Runtime library does not allow to
	 * make a subdirectory.
	 */
	if (ps2mc_basedir_len != 0 && path[ps2mc_basedir_len + 1] != '\0')
		return (-EPERM);

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc mkdir");
	if (res < 0)
		return (res);

	/* invalidate directory cache */
	ps2mc_dircache_invalidate(portslot);

	res = 0;
	if (ps2mclib_Mkdir(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			   (char*)path, &result) != 0) {
		/* error */
		printk("ps2mclib_Mkdir() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "mkdir(%s): card%d%d result=%d\n",
	       path, PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

	switch (result) {
	case 0:		res = 0;	break;
	case -2:	res = -EINVAL;	break;	/* not formatted */
	case -3:	res = -ENOSPC;	break;	/* no space left on device */
	case -4:	res = -ENOENT;	break;	/* no such file or directory */
	default:	res = -EIO;	break;
	}

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}


/*
 * rename directory entry
 */
int
ps2mc_rename(int portslot, const char *path, char *newname)
{
	int res, result;

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc rename");
	if (res < 0)
		return (res);

	res = 0;
	if (ps2mclib_Rename(PS2MC_PORT(portslot), PS2MC_SLOT(portslot), (char*)path, newname, &result) != 0) {
		/* error */
		printk("ps2mclib_Rename() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "rename(): result=%d\n", result);

	switch (result) {
	case 0: /* succeeded */
		res = 0;
		ps2mc_dircache_invalidate(portslot);
		break;
	case -4: /* File not found */
		res = -ENOENT;
		break;
	default:
		res = -EIO;
		break;
	}

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}

/*
 * delete directory or file
 */
int
ps2mc_delete(int portslot, const char *path)
{
	int res, result;

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc delete");
	if (res < 0)
		return (res);

	/* invalidate directory cache */
	ps2mc_dircache_invalidate(portslot);

	res = 0;
	if (ps2mclib_Delete(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			    (char*)path, &result) != 0) {
		/* error */
		printk("ps2mclib_Delete() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_INFO, "delete(%s): card%d%d result=%d\n",
	       path, PS2MC_PORT(portslot), PS2MC_SLOT(portslot), result);

	switch (result) {
	case 0:		res = 0;	break;
	case -2:	res = -EINVAL;	break;	/* not formatted */
	case -4:	res = -ENOENT;	break;	/* no such file or directory */
	case -5:	res = -EBUSY;	break;	/* device or resource busy */
	case -6:	res = -ENOTEMPTY;break;	/* directory not empty */
	default:	res = -EIO;	break;
	}

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}

/*
 * delete directory and all files which belong to the directory
 */
int
ps2mc_delete_all(int portslot, const char *path)
{
	struct ps2mc_dirent dirent;
	int res;
	char name[PS2MC_NAME_MAX+1];
	char path2[PS2MC_PATH_MAX+1];

	DPRINT(DBG_INFO, "delete all(): card%d%d %s\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path);

	res = ps2mc_getdir_sub(portslot, path, 0, 1, &dirent);
	if (res < 0)
		return (res);
	if (res == 0)
		return (-ENOENT);

	if (S_ISDIR(dirent.mode)) {
		while (0 < ps2mc_readdir(portslot, path, 2, &dirent, 1)) {
		    ps2mc_terminate_name(name, dirent.name, dirent.namelen);
		    sprintf(path2, "%s/%s", path, name);
		    if ((res = ps2mc_delete(portslot, path2)) != 0)
			return (res);
		}
	}

	return ps2mc_delete(portslot, path);
}

int
ps2mc_getdir(int portslot, const char *path, struct ps2mc_dirent *buf)
{
	int count, res, entspace;

	DPRINT(DBG_INFO, "getdir(): card%d%d %s\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path);

	if (strcmp(path, "/") == 0) {
		/*
		 * PS2 memory card has no entry of root directry itself.
		 */
		buf->name[0] = '/';
		buf->namelen = 1;
		buf->mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUGO; /* 0777 */
		buf->mtime = get_seconds(); //CURRENT_TIME;
		buf->ctime = get_seconds(); //CURRENT_TIME;
	} else {
		if ((res = ps2mc_getdir_sub(portslot, path, 0, 1, buf)) <= 0)
			return (res);
	}

	if (S_ISDIR(buf->mode)) {
		count = 0;
		for ( ; ; ) {
			struct ps2mc_dirent tmpbuf;
			res = ps2mc_readdir(portslot, path, count, &tmpbuf, 1);
			if (res < 0)
				return (res); /* error */ 
			if (res == 0)
				break; /* no more entries */
			/* read an entry successfully */
			count++;
		}
		res = ps2sif_lock_interruptible(ps2mc_lock, "mc getentspace");
		if (res < 0)
			return (res);
		res = ps2mclib_GetEntSpace(PS2MC_PORT(portslot),
					   PS2MC_SLOT(portslot),
					   (char*)path, &entspace);
		ps2sif_unlock(ps2mc_lock);
		if (res < 0)
			return -EIO;
		count += entspace;
		buf->size = ALIGN2(count, 2) * 512;
	}

	return (1); /* succeeded */
}

/*
 * get directory infomation
 *
 * return value:
 *   < 0 error
 *   0   no more entries
 *   0 < succeeded
 *       if return value equals maxent, there might be more entries.
 */
int
ps2mc_getdir_sub(int portslot, const char *path, int mode, int maxent,
		 struct ps2mc_dirent *buf)
{
	int i, res, result;

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc format");
	if (res < 0)
		return (res);

	res = 0;
	ps2mc_dircache_invalidate_next_pos(portslot);
	if (ps2mclib_GetDir(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			    (char*)path, mode, maxent, dirbuf, &result) != 0) {
		/* error */
		printk("ps2mclib_GetDir() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_DIRCACHE, "getdir_sub(): card%d%d %s result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path, result);
	if (result < 0) {
		res = -EIO;
		goto out;
	}
	res = result;

	/*
	 * convert from ps2mclib_TblGetDir into ps2mc_dirent
	 */
	for (i = 0; i < res; i++) {
		/* name */
		memcpy(buf[i].name, dirbuf[i].EntryName, sizeof(buf[i].name));
		buf[i].namelen = MIN(sizeof(buf[i].name),
				     strlen(dirbuf[i].EntryName));

		/* mode */
		if (dirbuf[i].AttrFile & McFileAttrSubdir)
			buf[i].mode = S_IFDIR;
		else
			buf[i].mode = S_IFREG;
		if (dirbuf[i].AttrFile & McFileAttrReadable)
			buf[i].mode |= S_IRUGO;
		if (dirbuf[i].AttrFile & McFileAttrWriteable)
			buf[i].mode |= S_IWUGO;
		if (dirbuf[i].AttrFile & McFileAttrExecutable)
			buf[i].mode |= S_IXUGO;

		/* size */
		buf[i].size = dirbuf[i].FileSizeByte;

		/* create time */
		buf[i].ctime = mktime(dirbuf[i]._Create.Year,
				      dirbuf[i]._Create.Month,
				      dirbuf[i]._Create.Day,
				      dirbuf[i]._Create.Hour,
				      dirbuf[i]._Create.Min,
				      dirbuf[i]._Create.Sec) - McTZONE;

		/* modify time */
		buf[i].mtime = mktime(dirbuf[i]._Modify.Year,
				      dirbuf[i]._Modify.Month,
				      dirbuf[i]._Modify.Day,
				      dirbuf[i]._Modify.Hour,
				      dirbuf[i]._Modify.Min,
				      dirbuf[i]._Modify.Sec) - McTZONE;
	}

#ifdef PS2MC_DEBUG
	for (i = 0; i < res; i++) {
	    char name[PS2MC_NAME_MAX+1];
	    ps2mc_terminate_name(name, buf[i].name, buf[i].namelen);
	    DPRINT(DBG_DIRCACHE, "%3d: %04x %ld %ld %ld %s\n", i,
		   buf[i].mode,
		   buf[i].ctime,
		   buf[i].mtime,
		   buf[i].size,
		   name);
	}
#endif /* PS2MC_DEBUG */

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}

int
ps2mc_setdir(int portslot, const char *path, int flags,
	     struct ps2mc_dirent *buf)
{
	int res, result;
	unsigned valid = 0;

	if (strcmp(path, "/") == 0) {
		/*
		 * PS2 memory card has no entry of root directry itself.
		 * Just ignore.
		 */
		return (0);
	}

	res = ps2sif_lock_interruptible(ps2mc_lock, "mc format");
	if (res < 0)
		return (res);

	if (flags & PS2MC_SETDIR_MODE) {
		int rwx;

		/*
		 * first, you should retrieve current mode of the entry.
		 */
		res = ps2mclib_GetDir(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
				  (char*)path, 0, 1, dirbuf, &result);
		if (res != 0 || result < 0) {
			/* error */
			printk("setdir: ps2mclib_GetDir() failed\n");
			res = -EIO;
			goto out;
		}

		/* fix the mode bits in buf-> */
		rwx = (buf->mode & 0700) >> 6;
		buf->mode = (buf->mode & ~0777) |
				(rwx << 6) | (rwx << 3) | (rwx << 0);
		buf->mode &= (S_IFDIR | S_IFREG | S_IRUGO | S_IWUGO | S_IXUGO);

		dirbuf[0].AttrFile &= ~(McFileAttrReadable |
					McFileAttrWriteable |
					McFileAttrExecutable);
		if (buf->mode & S_IRUSR)
			dirbuf->AttrFile |= McFileAttrReadable;
		if (buf->mode & S_IWUSR)
			dirbuf->AttrFile |= McFileAttrWriteable;
		if (buf->mode & S_IXUSR)
			dirbuf->AttrFile |= McFileAttrExecutable;
		valid |= McFileInfoAttr;
	}

	if (flags & PS2MC_SETDIR_CTIME) { /* create time */
		struct rtc_time date;

		//to_tm(buf->ctime + McTZONE, &date);
		rtc_time_to_tm(buf->ctime + McTZONE, &date);
		dirbuf[0]._Create.Year = date.tm_year;
		dirbuf[0]._Create.Month = date.tm_mon;
		dirbuf[0]._Create.Day = date.tm_mday;
		dirbuf[0]._Create.Hour = date.tm_hour;
		dirbuf[0]._Create.Min = date.tm_min;
		dirbuf[0]._Create.Sec = date.tm_sec;
		valid |= McFileInfoCreate;
	}

	if (flags & PS2MC_SETDIR_MTIME) { /* modify time */
		struct rtc_time date;

		//to_tm(buf->mtime + McTZONE, &date);
		rtc_time_to_tm(buf->mtime + McTZONE, &date);
		dirbuf[0]._Modify.Year = date.tm_year;
		dirbuf[0]._Modify.Month = date.tm_mon;
		dirbuf[0]._Modify.Day = date.tm_mday;
		dirbuf[0]._Modify.Hour = date.tm_hour;
		dirbuf[0]._Modify.Min = date.tm_min;
		dirbuf[0]._Modify.Sec = date.tm_sec;
		valid |= McFileInfoModify;
	}

	res = ps2mclib_SetFileInfo(PS2MC_PORT(portslot), PS2MC_SLOT(portslot),
			       (char*)path, (char*)dirbuf, valid, &result);
	if (res != 0 || (result < 0 && result != -4)) {
		/* error */
		printk("ps2mclib_SetDir() failed\n");
		res = -EIO;
		goto out;
	}

	DPRINT(DBG_DIRCACHE, "setdir(): card%d%d %s result=%d\n",
	       PS2MC_PORT(portslot), PS2MC_SLOT(portslot), path, result);

 out:
	ps2sif_unlock(ps2mc_lock);

	return (res);
}

void
ps2mc_add_listener(struct ps2mc_listener *listener)
{
	ps2sif_lock(ps2mc_lock, "ps2mc add listener");
	list_add(&listener->link, &listeners);
	ps2sif_unlock(ps2mc_lock);
}

void
ps2mc_del_listener(struct ps2mc_listener *listener)
{
	ps2sif_lock(ps2mc_lock, "ps2mc del listener");
	list_del(&listener->link);
	ps2sif_unlock(ps2mc_lock);
}

void
ps2mc_set_state(int portslot, int state)
{
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);
	struct list_head *p;
	static int cardstates[PS2MC_NPORTS][PS2MC_NSLOTS];

	ps2sif_lock(ps2mc_lock, "ps2mc set state");
	if (cardstates[port][slot] != state) {
		DPRINT(DBG_INFO, "card%d%d: %s -> %s\n",
		       port, slot,
		       ps2mc_type_names[cardstates[port][slot]],
		       ps2mc_type_names[state]);

		ps2mc_dircache_invalidate(PS2MC_PORTSLOT(port, slot));

		/*
		 * notify all listeners
		 */
		for (p = listeners.next; p != &listeners; p = p->next) {
			struct ps2mc_listener *listener;
			listener = list_entry(p, struct ps2mc_listener,link);
			if (listener->func != NULL)
				(*listener->func)(listener->ctx,
						  PS2MC_PORTSLOT(port, slot),
						  cardstates[port][slot],
						  state);
		}
		cardstates[port][slot] = state;
	}
	ps2sif_unlock(ps2mc_lock);
}

static void
ps2mc_check(void)
{
	int res;
	int port, slot;
	int result, type, gen;
	static int gens[PS2MC_NPORTS][PS2MC_NSLOTS];
	struct block_device *bdev;

	for (port = 0; port < PS2MC_NPORTS; port++) {
	  for (slot = 0; slot < PS2MC_NSLOTS; slot++) {
	    int portslot = PS2MC_PORTSLOT(port, slot);
	    res = ps2mc_getinfo_sub(portslot, &result, &type, NULL, NULL);
	    if (res < 0 || result < -2) {
	      /* error */
	      ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);
	    } else {
	      gen = atomic_read(&ps2mc_cardgens[port][slot]);
	      if (gens[port][slot] != gen) {
		ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);
		gens[port][slot] = gen;
		ps2mc_signal_change(port, slot);
		
		bdev = bdget(MKDEV(PS2MC_MAJOR, portslot));
		if (bdev) {
			invalidate_bdev(bdev);
			bdput(bdev);
		}
	      }

	      if (result == 0 || result == -1)
		      ps2mc_set_state(portslot, type);
	    }
	  }
	}
}

static void
ps2mc_timer(unsigned long arg)
{
	timer_flag = 1;
	up(&ps2mc_waitsem);
}

static int
ps2mc_settimer(int timeout)
{
	int rv;
	struct timer_list timer;

	init_timer_on_stack(&timer);
	timer.function = (void(*)(u_long))ps2mc_timer;
	timer.expires = jiffies + timeout;
	add_timer(&timer);
	rv = down_interruptible(&ps2mc_waitsem);
	del_timer(&timer);
	return rv;
}

static int
ps2mc_thread(void *arg)
{
	sigset_t blocked, oldset;

	DPRINT(DBG_INFO, "start thread\n");

	/* get rid of all our resources related to user space */
	daemonize("ps2mc_thread");

	siginitsetinv(&blocked, sigmask(SIGKILL)|sigmask(SIGINT)|sigmask(SIGTERM));
	sigprocmask(SIG_SETMASK, &blocked, &oldset);

	thread_task = current;
	complete(&thread_comp); /* notify that we are ready */

	/*
	 * loop
	 */
	while(1) {
		if (timer_flag) {
			ps2mc_check();
			timer_flag = 0;
		}
		if (ps2mc_settimer(PS2MC_CHECK_INTERVAL) != 0) {
			/* Interrupted by signal. */
			break;
		}
		ps2mc_process_request();

		if (signal_pending(current) )
			break;
	}

	printk("ps2mc_thread: exit thread\n");
	DPRINT(DBG_INFO, "exit thread\n");

	thread_task = NULL;
	complete(&thread_comp); /* notify that we've exited */

	return (0);
}

int __init
ps2mc_init(void)
{

	ps2mc_basedir_len = strlen(PS2MC_BASEDIR);
	init_MUTEX(&ps2mc_filesem);
	sema_init(&ps2mc_filesem, ps2mc_getdtablesize());
	init_completion(&thread_comp);
	init_MUTEX(&ps2mc_waitsem);

	printk("PlayStation 2 Memory Card driver\n");

	/*
	 * initialize lock
	 */
	if ((ps2mc_lock = ps2sif_getlock(PS2LOCK_MC)) == NULL) {
		printk(KERN_ERR "ps2mc: Can't get lock\n");
		return (-1);
	}

	if (ps2sif_lock_interruptible(ps2mc_lock, "mc init") < 0)
		return (-1);

	/*
	 * allocate DMA buffers
	 */
	if ((dmabuf = kmalloc(1500 + PS2MC_RWBUFSIZE, GFP_KERNEL)) == NULL) {
		ps2sif_unlock(ps2mc_lock);
		return (-1);
	}
	PS2SIF_ALLOC_BEGIN(dmabuf, 1500 + PS2MC_RWBUFSIZE);
	PS2SIF_ALLOC(dirbuf, sizeof(McDirEntry) * PS2MC_DIRCACHESIZE, 64);
	PS2SIF_ALLOC(ps2mc_rwbuf, PS2MC_RWBUFSIZE, 64);
	PS2SIF_ALLOC_END("memory card\n");

	/*
	 * initialize event lister list
	 */
	INIT_LIST_HEAD(&listeners);

	/*
	 * initialize IOP access library
	 */
	if (ps2mclib_Init() < 0) {
		printk(KERN_CRIT "ps2mc: can't initialize memory card system\n");
		ps2sif_unlock(ps2mc_lock);
		kfree(dmabuf);
		return -1;
	}

	/*
	 * register block device entry
	 */
	if (ps2mc_devinit() < 0) {
		printk(KERN_CRIT "ps2mc: can't register memory card system\n");
		ps2sif_unlock(ps2mc_lock);
		kfree(dmabuf);
		return -1;
	}

	/*
	 * create and start thread
	 */
	kernel_thread(ps2mc_thread, NULL, 0);
 	//kthread_run(ps2mc_thread, NULL, "ps2mc_thread");
	wait_for_completion(&thread_comp);	/* wait the thread ready */
	ps2sif_unlock(ps2mc_lock);
	return 0;
}

void
ps2mc_cleanup(void)
{

	/*
	 * stop the thread
	 */
	if (thread_task != NULL) {
            send_sig(SIGKILL, thread_task, 1);
	    wait_for_completion(&thread_comp);	/* wait the thread exit */
	}

	/*
	 * unregister block device entry
	 */
	ps2mc_devexit();

//	ps2mclib_Exit();

	/* free DMA buffer */
	if (dmabuf != NULL) {
		kfree(dmabuf);
		dmabuf = NULL;
	}
}

module_init(ps2mc_init);
module_exit(ps2mc_cleanup);

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 memory card driver");
MODULE_LICENSE("GPL");
