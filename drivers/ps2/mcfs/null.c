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
#include <linux/fs.h>

#include "mcfs.h"
#include "mcfs_debug.h"

static struct dentry *ps2mcfs_null_lookup(struct inode *, struct dentry *, struct nameidata *);

struct file_operations ps2mcfs_null_operations = {
	/* NULL */
};

struct inode_operations ps2mcfs_null_inode_operations = {
	lookup:			ps2mcfs_null_lookup,
};

static struct dentry *
ps2mcfs_null_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *name_i_data)
{
	return ERR_PTR(-ENOENT);
}
