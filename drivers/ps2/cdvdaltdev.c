/*
 *  PlayStation 2 CD/DVD driver
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

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include "cdvd.h"

static loff_t
cdvd_file__llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t
cdvd_file__read(struct file *filp, char *buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t
cdvd_file__write(struct file *filp, const char *buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static long
cdvd_file__ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ps2cdvd_common_ioctl(cmd, arg);
}

static int
cdvd_file__open(struct inode *inode, struct file *filp)
{

	return 0;
}

static int
cdvd_file__release(struct inode *inode, struct file *filp)
{

	return 0;
}

struct file_operations ps2cdvd_altdev_fops = {
	owner:	THIS_MODULE,
	llseek:	cdvd_file__llseek,
	read:	cdvd_file__read,
	write:	cdvd_file__write,
	unlocked_ioctl:	cdvd_file__ioctl,
	open:	cdvd_file__open,
	release:cdvd_file__release,
};
