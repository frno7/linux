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

#include <linux/genhd.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/ps2/mcio.h>
#include <linux/major.h>
#include <linux/vmalloc.h>

#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/siflock.h>

#include "mc.h"
#include "mccall.h"

#define PS2MC_DEBUG
#include "mcpriv.h"
#include "mc_debug.h"

/*
 * macro defines
 */
#define MIN(a, b)	((a) < (b) ? (a) : (b))

/*
 * block device stuffs
 */
#define MAJOR_NR PS2MC_MAJOR
#define DEVICE_NAME "ps2mc"
#define DEVICE_REQUEST do_ps2mc_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)
#define DEVICE_NO_RANDOM
#define PS2MC_MINORS	4
#define KERNEL_SECTOR_SIZE      512 
#include <linux/blkdev.h>


/*
 * data types
 */

/*
 * function prototypes
 */
static int ps2mc_devioctl(struct block_device *, fmode_t mode, unsigned cmd, unsigned long arg);
static int ps2mc_devopen(struct block_device *, fmode_t mode);
static int ps2mc_devrelease(struct gendisk *gd, fmode_t mode);
static int ps2mc_devcheck(struct gendisk *gd);
static void do_ps2mc_request(struct request_queue *);
static int hardsect_size = 512;
static int ps2mc_revalidate(struct gendisk *gd);

struct request_queue *req_que_ex;

struct ps2mc_dev {
	int size;                       /* Device size in sectors */
	u8 *data;                       /* The data array */
	short media_change;             /* Flag a media change? */
	spinlock_t spinlock;         	/* For mutual exclusion */
	ps2sif_lock_t *ps2mc_lock;
	struct request_queue *req_que;	/* The device request queue */
	struct gendisk *gd;             /* The gendisk structure */
	struct timer_list timer;        /* For simulated media changes */
	atomic_t opened;
};

static struct ps2mc_dev *ps2mc_devices = NULL;

struct block_device_operations ps2mc_bdops = {
	.owner=			THIS_MODULE,
	.open=			ps2mc_devopen,
	.release=		ps2mc_devrelease,
	.ioctl=			ps2mc_devioctl,
	.media_changed=		ps2mc_devcheck,
	.revalidate_disk=	ps2mc_revalidate,
};

int (*ps2mc_blkrw_hook)(int, int, void*, int);

/*
 * function bodies
 */

void ps2mc_signal_change(int port, int slot)
{
	ps2mc_devices[port * PS2MC_NSLOTS + slot].media_change = 1;
}

int setup_ps2mc_dev(struct ps2mc_dev *device_info, int minor_n, int i)
{
	memset (device_info, 0, sizeof (struct ps2mc_dev));
	device_info->size = 8 * hardsect_size;
	device_info->data = vmalloc(device_info->size);
	if (device_info->data == NULL) {
		printk (KERN_NOTICE "vmalloc failure.\n");
		return -ENOMEM;
	}
	spin_lock_init(&device_info->spinlock);

	device_info->gd = alloc_disk(1);
	if (! device_info->gd) {
		printk (KERN_ERR "alloc_disk failure\n");
		vfree(device_info->data);
		device_info->data = NULL;
		return -ENOMEM;
	}
	device_info->gd->major = PS2MC_MAJOR;
  	device_info->gd->first_minor = minor_n;
  	device_info->gd->fops = &ps2mc_bdops;
	device_info->gd->minors = PS2MC_MINORS; 
	device_info->req_que = blk_init_queue(do_ps2mc_request, &device_info->spinlock);
	if (device_info->req_que == NULL) {
		printk (KERN_ERR "queue failure\n");
		put_disk(device_info->gd);
		device_info->gd = NULL;
		vfree(device_info->data);
		device_info->data = NULL;
		return -ENOMEM;
	}
	device_info->req_que->queuedata = device_info;
	blk_queue_logical_block_size(device_info->req_que, hardsect_size);
  	device_info->gd->queue = device_info->req_que;
  	device_info->gd->private_data = device_info;
	sprintf(device_info->gd->disk_name, "ps2mc%d0", i);
	device_info->gd->flags = GENHD_FL_REMOVABLE;
	set_capacity(device_info->gd, (8 * (hardsect_size / KERNEL_SECTOR_SIZE)));
	return 0;
}

int ps2mc_devinit(void)
{
	int i, port, slot, res;
	/*
	 * register block device entry
	 */
	if ((res = register_blkdev(PS2MC_MAJOR, "ps2mc")) < 0) {
		printk(KERN_ERR "Unable to get major %d for PS2 Memory Card\n",
		       PS2MC_MAJOR);
		return -EINVAL;
	}

	ps2mc_devices = kzalloc(PS2MC_NPORTS * PS2MC_NSLOTS * sizeof(struct ps2mc_dev), GFP_KERNEL);
	if (ps2mc_devices == NULL) {
		ps2mc_devexit();
		return -ENOMEM;
	}
	i = 0;
	for (port = 0; port < PS2MC_NPORTS; port++) {
	  	for (slot = 0; slot < PS2MC_NSLOTS; slot++) {
			int portslot = PS2MC_PORTSLOT(port, slot);
			int rv;

			rv = setup_ps2mc_dev(ps2mc_devices + i, portslot, i);
			if (rv != 0) {
				printk(KERN_ERR "ps2mc: Failed to initialize port%d%d\n", port, slot);
				return rv;
			}
			
			i++;
		}
	}
	return (0);
}

void ps2mc_add_disks(void)
{
	int i, port, slot;

	i = 0;
	for (port = 0; port < PS2MC_NPORTS; port++) {
	  	for (slot = 0; slot < PS2MC_NSLOTS; slot++) {
			/* Add disks, this will already call file operations. */
			printk("ps2mc: Adding MC port %d slot %d.\n", port, slot);
			add_disk(ps2mc_devices[i].gd);
			i++;
		}
	}
	printk("ps2mc: Finished adding MC ports.\n");
}

void ps2mc_del_disks(void)
{
	int i, port, slot;

	i = 0;
	for (port = 0; port < PS2MC_NPORTS; port++) {
	  	for (slot = 0; slot < PS2MC_NSLOTS; slot++) {
			/* Remove disks. */
			del_gendisk(ps2mc_devices[i].gd);
			i++;
		}
	}
}

int
ps2mc_devexit(void)
{
	/*
	 * unregister block device entry
	 */
	unregister_blkdev(PS2MC_MAJOR, "ps2mc");

	return (0);
}


static int
ps2mc_devioctl(struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg)
{
	dev_t devno = disk_devt(bdev->bd_disk);
	int portslot = MINOR(devno);
	int n, res, fd;
	int port, slot;
	struct ps2mc_cardinfo info;
	struct ps2mc_arg cmdarg;
	char path[PS2MC_NAME_MAX+1];

	port = PS2MC_PORT(portslot);
	slot = PS2MC_SLOT(portslot);
	switch (cmd) {
	case PS2MC_IOCGETINFO: {
		struct ps2mc_dev *dev_i = bdev->bd_disk->private_data;

		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=GETINFO\n",
		       port, slot);
		if ((res = ps2mc_getinfo(portslot, &info)) != 0)
			return (res);
		info.busy = atomic_read(&dev_i->opened);
		return copy_to_user((void *)arg, &info, sizeof(info)) ? -EFAULT : 0;
	}

	case PS2MC_IOCFORMAT:
		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=FORMAT\n",
		       port, slot);
		return ps2mc_format(portslot);

	case PS2MC_IOCSOFTFORMAT:
		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=SOFTFORMAT\n",
		       port, slot);
		if (ps2mc_basedir_len == 0)
			return (0);

		sprintf(path, "/%s", PS2MC_BASEDIR);
		if ((res = ps2mc_delete_all(portslot, path)) != 0 &&
		    res != -ENOENT) {
			return (res);
		}
		if ((res = ps2mc_mkdir(portslot, path)) != 0)
			return (res);
		return (0);

	case PS2MC_IOCUNFORMAT:
		DPRINT(DBG_DEV, "device ioctl: card%x%x cmd=UNFORMAT\n",
		       port, slot);
		return ps2mc_unformat(portslot);

	case PS2MC_IOCWRITE:
	case PS2MC_IOCREAD:
		/* get arguments */
		if (copy_from_user(&cmdarg, (void *)arg, sizeof(cmdarg)))
			return -EFAULT;
		sprintf(path, "%s%s", ps2mc_basedir_len ? "/" : "",
			PS2MC_BASEDIR);
		n = strlen(path);
		if (PS2MC_NAME_MAX < cmdarg.pathlen + n)
			return -ENAMETOOLONG;
		if (copy_from_user(&path[n], cmdarg.path, cmdarg.pathlen))
			return -EFAULT;
		path[cmdarg.pathlen + n] = '\0';

		DPRINT(DBG_DEV,
		       "device ioctl: card%x%x cmd=%s path=%s pos=%d\n",
		       port, slot, cmd== PS2MC_IOCWRITE ? "WRITE" : "READ",
		       path, cmdarg.pos);

		res = ps2sif_lock_interruptible(ps2mc_lock, "mc call");
		if (res < 0)
			return (res);
		fd = ps2mc_open(portslot, path, cmdarg.mode);
		/*
		 * Invalidate directory cache because 
		 * the file might be created.
		 */
		ps2mc_dircache_invalidate(portslot);
		ps2sif_unlock(ps2mc_lock);

		if (fd < 0)
			return (fd);

		if ((res = ps2mc_lseek(fd, cmdarg.pos, 0 /* SEEK_SET */)) < 0)
			goto rw_out;

		res = 0;
		while (0 < cmdarg.count) {
			n = MIN(cmdarg.count, PS2MC_RWBUFSIZE);
			if (cmd== PS2MC_IOCWRITE) {
			    if (copy_from_user(ps2mc_rwbuf, cmdarg.data, n)) {
				res = res ? res : -EFAULT;
				goto rw_out;
			    }
			    if ((n = ps2mc_write(fd, ps2mc_rwbuf, n)) <= 0) {
				res = res ? res : n;
				goto rw_out;
			    }
			} else {
			    if ((n = ps2mc_read(fd, ps2mc_rwbuf, n)) <= 0) {
				res = res ? res : n;
				goto rw_out;
			    }
			    if (copy_to_user(cmdarg.data, ps2mc_rwbuf, n)) {
				res = res ? res : -EFAULT;
				goto rw_out;
			    }
			}
			cmdarg.data += n;
			cmdarg.count -= n;
			res += n;
		}
	rw_out:
		ps2mc_close(fd);
		return (res);

	case PS2MC_IOCNOTIFY:
		ps2mc_set_state(portslot, PS2MC_TYPE_EMPTY);
		return (0);

	case PS2MC_CALL:
		res = ps2sif_lock_interruptible(ps2mc_lock, "mc call");
		if (res < 0)
			return (res);
		res = sbios_rpc(SBR_MC_CALL, (void*)arg, &n);
		ps2sif_unlock(ps2mc_lock);

		return ((res < 0) ? -EBUSY : 0);

	}

	return -EINVAL;
}

static int
ps2mc_devopen(struct block_device *bdev, fmode_t mode)
{
	struct ps2mc_dev *dev_i = bdev->bd_disk->private_data;
	dev_t devno = disk_devt(bdev->bd_disk);
	int portslot = MINOR(devno);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);

	check_disk_change(bdev);
	DPRINT(DBG_DEV, "device open: card%d%d\n", port, slot);

	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot) {
		return (-ENODEV);
	}
	atomic_inc(&dev_i->opened);
	return (0);
}

static int
ps2mc_devrelease(struct gendisk *gd, fmode_t mode)
{
	struct ps2mc_dev *dev_i = gd->private_data;
	dev_t devno = disk_devt(gd);
	int portslot = MINOR(devno);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);
	DPRINT(DBG_DEV, "device release: card%d%d\n", port, slot);
	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
	{
		printk(KERN_ERR "device LOST!!!%d%d\n", port, slot);
		return (-ENODEV);
	}
	atomic_dec(&dev_i->opened);
	return (0);
}

static void
do_ps2mc_request(struct request_queue *req_que_ex)
{
	up(&ps2mc_waitsem);
}

int ps2mc_blk_request(struct request *req)
{
	int cmd_f;
	dev_t dev;
	struct gendisk *gd;
	struct ps2mc_dev *device;
	char *cmd = NULL;

	if (!blk_fs_request(req)) {
		return -EIO;
	}

	cmd_f = rq_data_dir(req);
	switch(cmd_f)
	{
		case READ:
			cmd = "read";
			break;
		case WRITE:
			cmd = "write";
			break;
		default:
			printk(KERN_ERR "ps2mc: unknown command: %u \n", cmd_f);
			return -EIO;
	}

	gd = req->rq_disk;
	device = gd->private_data;
	dev = disk_devt(gd);
	if (atomic_read(&device->opened) == 0) {
		DPRINT(DBG_DEV, "%s sect=%llx, len=%u, addr=%p\n",
	       cmd, blk_rq_pos(req),
	       blk_rq_cur_sectors(req),
	       req->buffer);
		return -ENXIO;
	}
	if (ps2mc_blkrw_hook){
		int res;

		res = (*ps2mc_blkrw_hook)(req->cmd_flags == 0 ? 0 : 1,
			blk_rq_pos(req),
			req->buffer,
			blk_rq_cur_sectors(req));
		DPRINT(DBG_DEV, "ps2mc_blk_request: ps2mc_blkrw_hook res = %d\n", res);
		return res;
	} else {
		printk(KERN_ERR "ps2mc: ps2mcfs is not initialized.\n");
		return -EIO;
	}
}

void ps2mc_process_request(void)
{
	struct request_queue *rq;
	struct request *req;
	int res, port, slot, i;

	i = 0;
	req = NULL;
	for (port = 0; port < PS2MC_NPORTS; port++) {
		for (slot = 0; slot < PS2MC_NSLOTS; slot++) {
			/* Get queue for current MC port. */
			rq = (ps2mc_devices + i)->req_que;

			for (;;) {
				if (req == NULL) {
					spin_lock_irq(rq->queue_lock);
					req = blk_fetch_request(rq);
					spin_unlock_irq(rq->queue_lock);
				}

				if (req != NULL) {
					/* Make block transfer. */
					res = ps2mc_blk_request(req);
	
					spin_lock_irq(rq->queue_lock);
					if (!__blk_end_request_cur(req, res)) {
						req = NULL;
					}
					spin_unlock_irq(rq->queue_lock);
					if (req != NULL) {
						/* Failed to end request, try later again. */
						set_current_state(TASK_INTERRUPTIBLE);
						schedule();
					}
				} else {
					/* All requests processed for current MC port. */
					break;
				}
			}
			i++;
		}
	} 
}

static int 
ps2mc_devcheck(struct gendisk *gd)
{
	dev_t devno = disk_devt(gd);
	int portslot = MINOR(devno);
	int port = PS2MC_PORT(portslot);
	int slot = PS2MC_SLOT(portslot);
	int gen;
	static int gens[PS2MC_NPORTS][PS2MC_NSLOTS];
	if (port < 0 || PS2MC_NPORTS <= port ||
	    slot < 0 || PS2MC_NSLOTS <= slot)
	return (0);

	gen = atomic_read(&ps2mc_cardgens[port][slot]);
	if (gens[port][slot] != gen) {
		DPRINT(DBG_DEV, "card%d%d was changed\n", port, slot);
		gens[port][slot] = gen;
		return (1);
	}

	return (0);
}

int ps2mc_revalidate(struct gendisk *gd)
{
    struct ps2mc_dev *dev = gd->private_data;
    
    if (dev->media_change) {
        dev->media_change = 0;
        memset(dev->data, 0, dev->size);
    }
    return 0;
}
