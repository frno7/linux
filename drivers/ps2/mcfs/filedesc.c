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

#include <asm/bitops.h>
#include <asm/uaccess.h>

#include "mcfs.h"
#include "mcfs_debug.h"

static struct ps2mcfs_filedesc {
	struct ps2mcfs_dirent *dirent;
	struct list_head link;
	int fd;
	int rwmode;
	int expire_time;
} *items;

static struct list_head lru;

static void __free_fd(struct ps2mcfs_dirent *);

/*
 * Insert a new entry at tail of the specified list
 */
static __inline__ void list_addtail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

int
ps2mcfs_init_fdcache()
{
	int i;
	int dtabsz = ps2mc_getdtablesize();

	ps2sif_assertlock(ps2mcfs_lock, "mcfs_init_fdcache");
#if defined(PS2MCFS_DEBUG) && defined(CONFIG_T10000_DEBUG_HOOK)
	if (ps2mcfs_debug & DBG_DEBUGHOOK) {
		static void dump(void);
		extern void (*ps2_debug_hook[0x80])(int c);
		ps2_debug_hook['D'] = (void(*)(int))dump;
	}
#endif

	items = kmalloc(sizeof(struct ps2mcfs_filedesc) * dtabsz, GFP_KERNEL);
	if (items == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&lru);
	for (i = 0; i < dtabsz; i++) {
		items[i].dirent = NULL;
		items[i].fd = -1;
		list_add(&items[i].link, &lru);
	}

	return (0);
}

int
ps2mcfs_exit_fdcache()
{
	static struct list_head *p;

	ps2sif_assertlock(ps2mcfs_lock, "mcfs_exit_fdcache");
	if (items != NULL)
		kfree(items);
	items = NULL;

#if defined(PS2MCFS_DEBUG) && defined(CONFIG_T10000_DEBUG_HOOK)
	if (ps2mcfs_debug & DBG_DEBUGHOOK) {
		static void dump(void);
		extern void (*ps2_debug_hook[0x80])(int c);
		ps2_debug_hook['D'] = (void(*)(int))NULL;
	}
#endif
	for (p = lru.next; p != &lru; p = p->next) {
		struct ps2mcfs_filedesc *ent;

		ent = list_entry(p, struct ps2mcfs_filedesc, link);
		if (ent->dirent)
			__free_fd(ent->dirent);
	}

	return (0);
}


#if defined(PS2MCFS_DEBUG) && defined(CONFIG_T10000_DEBUG_HOOK)
static void dump(void)
{
	struct list_head *p;
	const char *path;

	for (p = lru.next; p != &lru; p = p->next) {
		struct ps2mcfs_filedesc *ent;

		ent = list_entry(p, struct ps2mcfs_filedesc, link);
		if (ent->dirent)
			path = ps2mcfs_get_path(ent->dirent);
		else
			path = "-";
		printk(" %d: '%s' fd=%d\n", ent - items, path, ent->fd);
		if (ent->dirent)
			ps2mcfs_put_path(ent->dirent, path);
	}
}
#endif

int
ps2mcfs_get_fd(struct ps2mcfs_dirent *dirent, int rwmode)
{
	int res;
	struct ps2mcfs_filedesc *ent;
#ifdef PS2MCFS_DEBUG
	const char *path;

	ps2sif_assertlock(ps2mcfs_lock, "mcfs_get_fd");
	path = ps2mcfs_get_path(dirent);
	if (*path == '\0')
		return -ENAMETOOLONG; /* path name might be too long */
#endif

	ent = dirent->fd;
	if (ent != NULL) {
		if (ent->rwmode == rwmode) {
			list_del(&ent->link);
			list_add(&ent->link, &lru);
			ent->expire_time = PS2MCFS_FD_EXPIRE_TIME;
			DPRINT(DBG_FILECACHE, "get_fd: %s\n", path);
			res = ent->fd;
			goto out;
		}
		__free_fd(dirent);
	}

	ent = list_entry(lru.prev, struct ps2mcfs_filedesc, link);
	if (ent->dirent != NULL)
		__free_fd(ent->dirent);
	list_del(&ent->link);
	list_add(&ent->link, &lru);
	ent->expire_time = PS2MCFS_FD_EXPIRE_TIME;
	dirent->fd = ent;
	ent->dirent = dirent;
	ent->rwmode = rwmode;
	DPRINT(DBG_FILECACHE, "get_fd: ps2mc_open(%s)\n", path);
	ent->fd = ps2mc_open(dirent->root->portslot, path, rwmode);
	res = ent->fd;
	if (ent->fd < 0)
		__free_fd(dirent);
 out:
	DPRINT(DBG_FILECACHE, "get_fd: fd=%d %s\n", ent->fd, path);
	ps2mcfs_put_path(dirent, path);

	return (res);
}

void
ps2mcfs_free_fd(struct ps2mcfs_dirent *dirent)
{
	ps2sif_lock(ps2mcfs_lock, "mcfs_free_fd");
	__free_fd(dirent);
	ps2sif_unlock(ps2mcfs_lock);
}

static void
__free_fd(struct ps2mcfs_dirent *dirent)
{
	struct ps2mcfs_filedesc *ent;

	if (dirent->fd == NULL) {
		return;
	}
	ent = dirent->fd;
	dirent->fd = NULL;
#ifdef PS2MCFS_DEBUG
	{
		const char *path;
		
		path = ps2mcfs_get_path(dirent);
		DPRINT(DBG_FILECACHE, "ps2mcfs_free_fd: %s(fd=%d)\n",
		       path, ent->fd);
		if (0 <= ent->fd)
			DPRINT(DBG_FILECACHE,
			       "free_fd: ps2mc_close(%s)\n", path);
		ps2mcfs_put_path(dirent, path);
	}
#endif
	if (0 <= ent->fd)
		ps2mc_close(ent->fd);
	ent->dirent = NULL;
	ent->fd = -1;
	list_del(&ent->link);
	list_addtail(&ent->link, &lru);
}

/*
 * ps2mcfs_check_fd() is called from daemon thread (ps2mcfs_thread)
 * periodically.
 */
void
ps2mcfs_check_fd()
{
	struct list_head *p;

	ps2sif_assertlock(ps2mcfs_lock, "mcfs_check_fd");
	for (p = lru.next; p != &lru; p = p->next) {
		struct ps2mcfs_filedesc *ent;

		ent = list_entry(p, struct ps2mcfs_filedesc, link);
		if (ent->dirent) {
			ent->expire_time -= PS2MCFS_CHECK_INTERVAL;
			if (ent->expire_time < 0) {
#ifdef PS2MCFS_DEBUG
				const char *path = NULL;
				path = ps2mcfs_get_path(ent->dirent);
				DPRINT(DBG_FILECACHE,
				       "check_fd: expire '%s'\n", path);
				ps2mcfs_put_path(ent->dirent, path);
#endif
				__free_fd(ent->dirent);
			}

		}
	}
}
