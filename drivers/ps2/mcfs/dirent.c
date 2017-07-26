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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>

#include "mcfs.h"
#include "mcfs_debug.h"

#define ARRAYSIZEOF(a)	(sizeof(a)/sizeof(*(a)))
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define HASH(a)		(0)

static struct list_head free_dirents;
static struct list_head active_dirents[1];
static struct ps2mcfs_dirent *all_dirents[PS2MCFS_MAX_DIRENTS];
static int nfreeents = 0;
static int nents = 0;

static void ps2mcfs_clean_dirent(struct ps2mcfs_dirent *de);

static unsigned long get_next_ino(void)
{
	static unsigned long ino = 0;

	return (++ino & ~(1<<31));
}

unsigned long ps2mcfs_pseudo_ino(void)
{
	static unsigned long pino = 0;

	return (++pino | (1<<31));
}

int ps2mcfs_is_pseudo_ino(unsigned long ino)
{
	return (ino & (1<<31));
}

static int ps2mcfs_compare_name(struct ps2mcfs_dirent *de, const char *name,
			      int namelen)
{
	if (de->namelen != namelen)
		return (1);	/* XXX, fix me !*/
	return (strncmp(de->name, name, namelen));
}

int ps2mcfs_init_dirent(void)
{
	int i;

	ps2sif_assertlock(ps2mcfs_lock, "mcfs_init_dirent");
	INIT_LIST_HEAD(&free_dirents);
	for (i = 0; i < ARRAYSIZEOF(active_dirents); i++)
		INIT_LIST_HEAD(&active_dirents[i]);

	return (0);
}

struct ps2mcfs_dirent* ps2mcfs_alloc_dirent(struct ps2mcfs_dirent *parent,
					    const char *name, int namelen)
{
	struct ps2mcfs_dirent *newent = NULL;
	char buf[PS2MC_NAME_MAX + 1];

	ps2mc_terminate_name(buf, name, namelen);
	TRACE("ps2mcfs_alloc_dirent(%s)\n", buf);
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_alloc_dirent");
	if (list_empty(&free_dirents)) {
		if (PS2MCFS_MAX_DIRENTS <= nents) {
			printk(KERN_CRIT "ps2mcfs: too many directory entries\n");
			newent = NULL;
			goto out;
		}
		newent = kmalloc(sizeof(struct ps2mcfs_dirent), GFP_KERNEL);
		if (!newent)
			goto out;
		all_dirents[nents] = newent;
		memset(newent, 0, sizeof(struct ps2mcfs_dirent));
		newent->no = nents;
		newent->ino = get_next_ino();
		nents++;
	} else {
		newent = list_entry(free_dirents.next, struct ps2mcfs_dirent, 
				    next);
		list_del(&newent->next);
		nfreeents--;
	}
	ps2mcfs_clean_dirent(newent);
	if ((newent->parent = parent) != NULL) {
		ps2mcfs_ref_dirent(parent);
		list_add(&newent->next, &parent->sub);
		newent->root = parent->root;
	}
	newent->namelen = MIN(namelen, PS2MC_NAME_MAX);
	memcpy(newent->name, name, newent->namelen);

	list_add(&newent->hashlink, &active_dirents[HASH(newent->ino)]);
 out:

	return (newent);
}

struct ps2mcfs_dirent* ps2mcfs_find_dirent(struct ps2mcfs_dirent *parent,
					   const char *name, int namelen)
{
	struct list_head *tmp;
	char buf[PS2MC_NAME_MAX + 1];

	ps2mc_terminate_name(buf, name, namelen);
	TRACE("ps2mcfs_find_dirent(%s)\n", buf);
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_find_dirent");

	for (tmp = parent->sub.next; tmp != &parent->sub; tmp = tmp->next) {
		struct ps2mcfs_dirent *de;
		de = list_entry(tmp, struct ps2mcfs_dirent, next);
		if (ps2mcfs_compare_name(de, name, namelen) == 0)
			return (de);
	}

	return (NULL);
}

struct ps2mcfs_dirent* ps2mcfs_find_dirent_ino(unsigned long ino)
{
	struct list_head *tmp;

	TRACE("ps2mcfs_find_dirent_ino(%ld)\n", ino);
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_find_dirent_ino");

	for (tmp = active_dirents[HASH(newent->ino)].next;
	     tmp != &active_dirents[HASH(newent->ino)]; tmp = tmp->next) {
		struct ps2mcfs_dirent *de;
		de = list_entry(tmp, struct ps2mcfs_dirent, hashlink);
		if (de->ino == ino)
			return de;
	}

	return (NULL);
}

struct ps2mcfs_dirent* ps2mcfs_find_dirent_no(int no)
{
	TRACE("ps2mcfs_find_dirent_no(%d)\n", no);
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_find_dirent_no");
	if (no < 0 || nents <= no)
		return (NULL);

	return (all_dirents[no]);
}

static void ps2mcfs_clean_dirent(struct ps2mcfs_dirent *de)
{
	de->parent = NULL;
	INIT_LIST_HEAD(&de->next);
	INIT_LIST_HEAD(&de->sub);
	INIT_LIST_HEAD(&de->hashlink);
	de->flags = 0;
	de->inode = NULL;
	de->root = NULL;
	de->refcount = 0;
}

void ps2mcfs_ref_dirent(struct ps2mcfs_dirent *de)
{
	char buf[PS2MC_NAME_MAX + 1];

	ps2mc_terminate_name(buf, de->name, de->namelen);
	TRACE("ps2mcfs_ref_dirent(%s): refcount=%d++\n", buf, de->refcount);
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_ref_dirent");
	de->refcount++;
}

void ps2mcfs_unref_dirent(struct ps2mcfs_dirent *de)
{
	char buf[PS2MC_NAME_MAX + 1];

	ps2mc_terminate_name(buf, de->name, de->namelen);
	TRACE("ps2mcfs_unref_dirent(%s): refcount=%d--\n", buf, de->refcount);
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_unref_dirent");
	if (--de->refcount == 0)
		ps2mcfs_free_dirent(de);
}

void ps2mcfs_free_dirent(struct ps2mcfs_dirent *de)
{
	char buf[PS2MC_NAME_MAX + 1];

	ps2mc_terminate_name(buf, de->name, de->namelen);
	TRACE("ps2mcfs_free_dirent(%s) entries=%d(%d)\n",
	      buf, nents, nfreeents + 1);
	ps2sif_assertlock(ps2mcfs_lock, "mcfs_free_dirent");

	list_del(&de->next);
	list_del(&de->hashlink);

	if (de->flags & PS2MCFS_DIRENT_BMAPPED)
		invalidate_bdev(de->inode->i_bdev);

	/*
	 * free file descriptor cache entry
	 */
	ps2mcfs_free_fd(de);

	/*
	 * free path name cache entry
	 */
	ps2mcfs_free_path(de);

	/*
	 * decrement parent's reference count
	 * (it might free the parent entry)
	 */
	if (de->parent)
		ps2mcfs_unref_dirent(de->parent);

	/*
	 * link with free list
	 */
	list_add(&de->next, &free_dirents);
	nfreeents++;
}

void
ps2mcfs_invalidate_dirents(struct ps2mcfs_root *root)
{
	int i;
	struct list_head *p;
	struct ps2mcfs_dirent *de;
	struct inode *inode;
	const char *path;

	TRACE("ps2mcfs_invalidate_dirents(card%02x)\n", root->portslot);

	if (ps2sif_lock_interruptible(ps2mcfs_lock, "mcfs_invalidate_dirents") < 0)
		return;
	for (i = 0; i < ARRAYSIZEOF(active_dirents); i++) {
		for (p = active_dirents[i].next;
		     p != &active_dirents[i]; p = p->next) {

			de = list_entry(p, struct ps2mcfs_dirent, hashlink);
			if (de->root != root)
				continue;

			path = ps2mcfs_get_path(de);
			DPRINT(DBG_INFO, "  invalidate: %s\n", path);
			ps2mcfs_put_path(de, path);

			de->flags |= PS2MCFS_DIRENT_INVALID;
			if (de->flags & PS2MCFS_DIRENT_BMAPPED) {
				de->flags &= ~PS2MCFS_DIRENT_BMAPPED;

				/*
				 * XXX,
				 * You can't call ps2mcfs_unref_dirent(de)
				 * because it possibly free the node.
				 * But I think it might not happen...
				 */
				de->refcount--;
				if (de->refcount == 0)
					printk("ps2mcfs: dirent refcount=0\n");
			}

			if ((inode = de->inode) != NULL) {
				/*inode->i_op = &ps2mcfs_null_inode_operations;
				 */
				inode->i_nlink = 0;
			}
		}
	}
	ps2sif_unlock(ps2mcfs_lock);
}
