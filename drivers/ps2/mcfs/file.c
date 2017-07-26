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

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>

#include "mcfs.h"
#include "mcfs_debug.h"

#define PS2MCFS_FILEBUFSIZE	1024
#define ALIGN2(a, n)	((__typeof__(a))(((unsigned long)(a) + (n) - 1) / (n) * (n)))
#define MIN(a, b)	((a) < (b) ? (a) : (b))

char *ps2mcfs_filebuf;
void *dmabuf;

static ssize_t ps2mcfs_read(struct file *, char *, size_t, loff_t *);
static ssize_t ps2mcfs_write(struct file *, const char *, size_t, loff_t *);
static void ps2mcfs_truncate(struct inode *);
static int ps2mcfs_open(struct inode *, struct file *);
static int ps2mcfs_release(struct inode *, struct file *);
static int ps2mcfs_readpage(struct file *, struct page *);
static int ps2mcfs_writepage(struct page *page, struct writeback_control *wbc);
static int ps2mcfs_get_block(struct inode *inode, sector_t iblock,
			struct buffer_head *bh_result, int create);
static sector_t ps2mcfs_bmap(struct address_space *address_space, sector_t sectort);

struct file_operations ps2mcfs_file_operations = {
	read:		ps2mcfs_read,
	write:		ps2mcfs_write,
	mmap:		generic_file_mmap,
	open:		ps2mcfs_open,
	release:	ps2mcfs_release,
};

struct inode_operations ps2mcfs_file_inode_operations = {
	truncate:		ps2mcfs_truncate,
	setattr:		ps2mcfs_setattr,
};

struct address_space_operations ps2mcfs_addrspace_operations = {
	readpage:		ps2mcfs_readpage,
	writepage:		ps2mcfs_writepage,
	sync_page:		block_sync_page,
	bmap:			ps2mcfs_bmap,
};

int
ps2mcfs_init_filebuf()
{
	int dtabsz = ps2mc_getdtablesize();

	TRACE("ps2mcfs_init_filebuf()\n");
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_init_filebuf");

	dmabuf = kmalloc(PS2MCFS_FILEBUFSIZE * dtabsz + 64, GFP_KERNEL);
	if (dmabuf == NULL)
		return -ENOMEM;
	ps2mcfs_filebuf = ALIGN2(dmabuf, 64);

	return (0);
}

int
ps2mcfs_exit_filebuf()
{

	TRACE("ps2mcfs_exit_filebuf()\n");
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_exit_filebuf");
	if (dmabuf != NULL)
		kfree(dmabuf);
	dmabuf = NULL;

	return (0);
}

#define READ_MODE	0
#define WRITE_MODE	1
#define USER_COPY	2

static ssize_t
ps2mcfs_rw(struct inode *inode, char *buf, size_t nbytes, loff_t *ppos, int mode)
{
	struct ps2mcfs_dirent *de;
	int fd = -1;
	int res, nreads, pos;
	char *filebuf;

	de = inode->i_private;
	if (de == NULL)
		return -EIO;

	if (de->flags & PS2MCFS_DIRENT_INVALID)
		return -EIO;

	/*
	 * get full-path-name and open file
	 */
	res = ps2mcfs_get_fd(de, (mode & WRITE_MODE) ? O_WRONLY : O_RDONLY);
	if (res < 0)
		goto out;
	fd = res;

	/*
	 * XXX, It asummes that (0 <= fd) and (fd < descriptor table size)
	 */
	filebuf = &ps2mcfs_filebuf[PS2MCFS_FILEBUFSIZE * fd];

	/*
	 * seek
	 */
	if (mode & WRITE_MODE) {
		if (inode->i_size < *ppos)
			pos = inode->i_size;
		else
			pos = *ppos;
	} else {
			pos = *ppos;
	}
	if ((res = ps2mc_lseek(fd, pos, 0 /* SEEK_SET */)) < 0)
		goto out;
	if (res != pos) {
		res = -EIO;
		goto out;
	}
	if ((mode & WRITE_MODE) && inode->i_size < *ppos) {
		int pad = *ppos - inode->i_size;
		memset(filebuf, 0, MIN(pad, PS2MCFS_FILEBUFSIZE));
		while (0 < pad) {
			int n = MIN(pad, PS2MCFS_FILEBUFSIZE);
			res = ps2mc_write(fd, filebuf, n);
			if (res <= 0) /* error or EOF */
				goto out;
			pad -= res;
			inode->i_size += res;
		}
	}

	/*
	 * read/write
	 */
	nreads = 0;
	res = 0;
	while (0 < nbytes) {
		int n = MIN(nbytes, PS2MCFS_FILEBUFSIZE);
		if (mode & WRITE_MODE) {
			/* write */
			if (mode & USER_COPY) {
				if (copy_from_user(filebuf, buf, n)) {
					res = -EFAULT;
					goto out;
				}
			} else {
				memcpy(filebuf, buf, n);
			}
			res = ps2mc_write(fd, filebuf, n);
			if (res <= 0) /* error or EOF */
				break;
		} else {
			/* read */
			res = ps2mc_read(fd, filebuf, n);
			if (res <= 0) /* error or EOF */
				break;
			if (mode & USER_COPY) {
				if (copy_to_user(buf, filebuf, res)) {
					res = -EFAULT;
					goto out;
				}
			} else {
				memcpy(buf, filebuf, res);
			}
		}
		nreads += res;
		buf += res;
		nbytes -= res;
		*ppos += res;
	}
	res = (nreads == 0) ? res : nreads;

 out:
	TRACE("ps2mcfs_rw(): res=%d\n", res);

	return (res);
}

int
ps2mcfs_create(struct ps2mcfs_dirent *de)
{
	int res;
	const char *path;

	/* need the lock to call get_fd() */
	res = ps2sif_lock_interruptible(ps2mcfs_lock, "mcfs_read");
	if (res < 0)
		return (res);

	path = ps2mcfs_get_path(de);
	TRACE("ps2mcfs_create(%s)\n", path);
	ps2mcfs_put_path(de, path);

	if (0 <= (res = ps2mcfs_get_fd(de, O_RDWR | O_CREAT))) {
		res = 0; /* succeeded */
	}
	ps2sif_unlock(ps2mcfs_lock);

	return (res);
}

static ssize_t
ps2mcfs_read(struct file *filp, char * buf, size_t count, loff_t *ppos)
{
	int res;
	TRACE("ps2mcfs_read(filp=%p, pos=%ld)\n", filp, (long)*ppos);

	res = ps2sif_lock_interruptible(ps2mcfs_lock, "mcfs_read");
	if (res < 0)
		return (res);

	res = ps2mcfs_rw(filp->f_dentry->d_inode, buf,
			 count, ppos, READ_MODE|USER_COPY);
	ps2sif_unlock_interruptible(ps2mcfs_lock);

	return (res);
}

static ssize_t
ps2mcfs_write(struct file *filp, const char * buf, size_t count, loff_t *ppos)
{
	int res;
	struct inode *inode = filp->f_dentry->d_inode;

	TRACE("ps2mcfs_write(filp=%p, pos=%ld)\n", filp, (long)*ppos);
	res = ps2sif_lock_interruptible(ps2mcfs_lock, "mcfs_write");
	if (res < 0)
		return (res);

	res = ps2mcfs_rw(filp->f_dentry->d_inode, (char*)buf,
			 count, ppos, WRITE_MODE|USER_COPY);
	if (res < 0)
		goto out;

	if (inode->i_size < *ppos)
		inode->i_size = *ppos; /* file size was extended */
	inode->i_mtime = CURRENT_TIME;
 out:
	ps2sif_unlock_interruptible(ps2mcfs_lock);

	return (res);
}

static int
ps2mcfs_open(struct inode *inode, struct file *filp)
{
	int res;
	struct ps2mcfs_dirent *de = inode->i_private;
	const char *path;

	path = ps2mcfs_get_path(de);
	TRACE("ps2mcfs_open(%s)\n", path);
	if (*path == '\0')
		return -ENAMETOOLONG; /* path name might be too long */
	ps2mcfs_put_path(de, path);

	res = ps2sif_lock_interruptible(ps2mcfs_lock, "mcfs_read");
	if (res < 0)
		return (res);
	ps2mcfs_ref_dirent(de);
	ps2sif_unlock_interruptible(ps2mcfs_lock);

	return (0);
}

static int
ps2mcfs_release(struct inode *inode, struct file *filp)
{
	int res;
	struct ps2mcfs_dirent *de = inode->i_private;
	const char *path;

	path = ps2mcfs_get_path(de);
	TRACE("ps2mcfs_release(%s)\n", path);
	ps2mcfs_put_path(de, path);

	res = ps2sif_lock_interruptible(ps2mcfs_lock, "mcfs_read");
	if (res < 0)
		return (res);
	ps2mcfs_free_fd(de);
	ps2mcfs_unref_dirent(de);
	ps2sif_unlock_interruptible(ps2mcfs_lock);

	return (0);
}

static int
ps2mcfs_readpage(struct file *filp, struct page *page)
{
	return block_read_full_page(page, ps2mcfs_get_block);
}

static sector_t ps2mcfs_bmap(struct address_space *address_space, sector_t sectort)
{
	return generic_block_bmap(address_space, sectort, ps2mcfs_get_block);
}

static int ps2mcfs_get_block(struct inode *inode, sector_t block,
			struct buffer_head *bh, int create)
{
	struct ps2mcfs_dirent *de;
	int block_shift;
	long sector;

	ps2sif_lock(ps2mcfs_lock, "mcfs_bmap");
	de = inode->i_private;
	block_shift = de->root->block_shift;
	if ((1 << PS2MCFS_SECTOR_BITS) <= (block << block_shift)) {
		printk("ps2mcfs_bmap(): block number is too big\n");
		ps2sif_unlock(ps2mcfs_lock);
		return -EINVAL;
	}

	if (de->flags & PS2MCFS_DIRENT_INVALID) {
		ps2sif_unlock(ps2mcfs_lock);
		return -EINVAL;
	}

	sector = ((de->no << PS2MCFS_SECTOR_BITS) >> block_shift) | block;
	de->flags |= PS2MCFS_DIRENT_BMAPPED;

#ifdef PS2MCFS_DEBUG
	{
	const char *path;
	path = ps2mcfs_get_path(de);
	DPRINT(DBG_BLOCKRW, "ps2mcfs_bmap(%s): block=%llx -> %lx\n",
	       path, block, sector);
	ps2mcfs_put_path(de, path);
	}
#endif
	ps2sif_unlock(ps2mcfs_lock);

	bh->b_bdev = inode->i_bdev;
	bh->b_blocknr = sector;
	bh->b_state |= (1 << BH_Mapped);

	return (0);
}

static int
ps2mcfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ps2mcfs_get_block, wbc);
}

int
ps2mcfs_blkrw(int rw, int sector, void *buffer, int nsectors)
{
	struct ps2mcfs_dirent *de;
	int dno, res;
	loff_t pos;

	dno = ((sector >> PS2MCFS_DIRENT_SHIFT) & PS2MCFS_DIRENT_MASK);
	sector = ((sector >> PS2MCFS_SECTOR_SHIFT) & PS2MCFS_SECTOR_MASK);
        DPRINT(DBG_BLOCKRW, "ps2mcfs: %s dirent=%d sect=%x, len=%d, addr=%p\n",
	       rw ? "write" : "read", dno, sector, nsectors, buffer);

	ps2sif_lock(ps2mcfs_lock, "mcfs_bmap");
	if ((de = ps2mcfs_find_dirent_no(dno)) == NULL ||
	    de->inode == NULL) {
		res = -ENOENT;
		goto out;
	}

	if (de->inode->i_size < (sector + nsectors) * 512) {
		res = -EIO;
		goto out;
	}

	pos = sector * 512;
	res = ps2mcfs_rw(de->inode, buffer, nsectors * 512, &pos,
			 rw ? WRITE_MODE : READ_MODE);

	res = (res == nsectors * 512 ? 0 : -EIO);
 out:
	ps2sif_unlock(ps2mcfs_lock);

	return (res);
}

static void
ps2mcfs_truncate(struct inode *inode)
{
	struct ps2mcfs_dirent *de;
	const char *path;
	struct ps2mc_dirent mcdirent;

	ps2sif_lock(ps2mcfs_lock, "truncate");
	de = inode->i_private;
	path = ps2mcfs_get_path(de);
	if (*path == '\0')
		return; /* path name might be too long */

	TRACE("ps2mcfs_truncate(%s): size=%lld\n", path, inode->i_size);

	if (de->flags & PS2MCFS_DIRENT_INVALID)
		goto out;

	/*
	 * the size must be zero.
	 * please see ps2mcfs_notify_change() in inode.c.
	 */
	if (inode->i_size != 0)
		goto out;

	/*
	 * save mode and time of creation 
	 */
	if (ps2mc_getdir(de->root->portslot, path, &mcdirent) < 0)
		goto out;

	/*
	 * remove the entry
	 */
	if (ps2mc_delete(de->root->portslot, path) < 0)
		goto out;

	/*
	 * create new one
	 */
	if (ps2mcfs_create(de) < 0)
		goto out;

	/*
	 * restore mode and time of creation 
	 */
	if (ps2mc_setdir(de->root->portslot, path,
			   PS2MC_SETDIR_CTIME, &mcdirent) < 0)
		goto out;

 out:
	ps2mcfs_put_path(de, path);
	ps2sif_unlock(ps2mcfs_lock);

	return;
}
