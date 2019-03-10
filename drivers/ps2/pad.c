// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Game Controller driver
 *
 * Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Juergen Urban
 * Copyright (C)      2018 Fredrik Noring
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/addrspace.h>
#include <asm/uaccess.h>
#include <linux/sched/signal.h>
#ifdef CONFIG_PROC_FS_FIXME
#include <linux/proc_fs.h>
#endif

#include <linux/ps2/pad.h>

#include "pad.h"
#include "padcall.h"

#define PS2PAD_NOPORTCLOSE

#define PORT(n)		(((n) & 0x10) >> 4)
#define SLOT(n)		(((n) & 0x0f) >> 0)
#define NPORTS		PS2PAD_NPORTS
#define NSLOTS		PS2PAD_NSLOTS
#define MAXNPADS	PS2PAD_MAXNPADS
#define DMABUFSIZE	(16 * 16)
#define INTERVAL_TIME  	HZ/10	/* 100ms */

#define PS2PAD_MAJOR                244     /* PS2 PAD */

struct ps2pad_dev {
	struct ps2pad_libctx *pad;
};

struct ps2pad_ctl_dev {
	int stat_is_valid;
	struct ps2pad_stat stat[MAXNPADS];
};

static void ps2pad_start_timer(void);
static inline void ps2pad_stop_timer(void);
static inline void ps2pad_update_status(void);
static int lock(void);
static void unlock(void);

#ifdef CONFIG_PROC_FS_FIXME
static int ps2pad_read_proc(char *, char **, off_t, int, int *, void *);
#endif /* CONFIG_PROC_FS_FIXME */
static ssize_t ps2pad_read(struct file *, char *, size_t, loff_t *);
static unsigned int ps2pad_poll(struct file *file, poll_table * wait);
static long ps2pad_ioctl(struct file *, u_int, u_long);
static int ps2pad_open(struct inode *, struct file *);
static int ps2pad_release(struct inode *, struct file *);

static ssize_t ps2pad_ctl_read(struct file *, char *, size_t, loff_t *);
static unsigned int ps2pad_ctl_poll(struct file *file, poll_table * wait);
static long ps2pad_ctl_ioctl(struct file *, u_int, u_long);
static int ps2pad_ctl_release(struct inode *, struct file *);

static int ps2pad_major = PS2PAD_MAJOR;
static spinlock_t spinlock;

module_param(ps2pad_major, int, 0);
MODULE_PARM_DESC(ps2pad_major,
		"Major device node number for PS2 pad driver.");

//#define PS2PAD_DEBUG
#ifdef PS2PAD_DEBUG
int ps2pad_debug = 0;
#define DPRINT(fmt, args...) \
	if (ps2pad_debug) printk(KERN_CRIT "ps2pad: " fmt, ## args)
module_param(ps2pad_debug, int, 0);
MODULE_PARM_DESC(ps2pad_debug,
		"Set debug output level of verbosity (0 = off, other value means on).");
#else
#define DPRINT(fmt, args...) do {} while (0)
#endif

#define ARRAYSIZEOF(a)	(sizeof(a)/sizeof(*(a)))

struct ps2pad_libctx ps2pad_pads[MAXNPADS];
int ps2pad_npads = 0;
EXPORT_SYMBOL(ps2pad_pads);
EXPORT_SYMBOL(ps2pad_npads);

static DECLARE_WAIT_QUEUE_HEAD(lockq);
static int locked = 0;
static DECLARE_WAIT_QUEUE_HEAD(watchq);
static struct ps2pad_timer {
	struct timer_list timer;
	unsigned long data;
} ps2pad_timer;
static struct ps2pad_stat cur_stat[MAXNPADS];
static struct ps2pad_stat new_stat[MAXNPADS];
static int open_devices = 0;
static int run_timer = 0;

static struct file_operations ps2pad_fops = {
	owner:		THIS_MODULE,
	read:		ps2pad_read,
	poll:		ps2pad_poll,
	unlocked_ioctl:		ps2pad_ioctl,
	open:		ps2pad_open,
	release:	ps2pad_release,
};

static struct file_operations ps2pad_ctlops = {
	owner:		THIS_MODULE,
	read:		ps2pad_ctl_read,
	poll:		ps2pad_ctl_poll,
	unlocked_ioctl:		ps2pad_ctl_ioctl,
	release:	ps2pad_ctl_release,
};

char *pad_type_names[16] = {
	"type 0",
	"type 1",
	"NEJICON",	/* PS2PAD_TYPE_NEJICON	*/
	"type 3",
	"DIGITAL",	/* PS2PAD_TYPE_DIGITAL	*/
	"ANALOG",	/* PS2PAD_TYPE_ANALOG	*/
	"type 6",
	"DUALSHOCK",	/* PS2PAD_TYPE_DUALSHOCK*/
	"type 8",
	"type 9",
	"type A",
	"type B",
	"type C",
	"type D",
	"type E",
	"type F",
};

static unsigned char stat_conv_table[] = {
	[PadStateDiscon]	= PS2PAD_STAT_NOTCON,
	[PadStateFindPad]	= PS2PAD_STAT_BUSY,
	[PadStateFindCTP1]	= PS2PAD_STAT_READY,
	[PadStateExecCmd]	= PS2PAD_STAT_BUSY,
	[PadStateStable]	= PS2PAD_STAT_READY,
	[PadStateError]		= PS2PAD_STAT_ERROR,
};

static unsigned char rstat_conv_table[] = {
	[PadReqStateComplete]		= PS2PAD_RSTAT_COMPLETE,
	[PadReqStateFailed]		= PS2PAD_RSTAT_FAILED,
	[PadReqStateBusy]		= PS2PAD_RSTAT_BUSY,
};

int
ps2pad_stat_conv(int stat)
{
	if (stat < 0 || ARRAYSIZEOF(stat_conv_table) <= stat) {
		return PS2PAD_STAT_ERROR;
	} else {
		return stat_conv_table[stat];
	}
}

static inline int
ps2pad_comp_stat(struct ps2pad_stat *a, struct ps2pad_stat *b)
{
	return memcmp(a, b, sizeof(struct ps2pad_stat) * ps2pad_npads);
}

static inline void
ps2pad_copy_stat(struct ps2pad_stat *a, struct ps2pad_stat *b)
{
	memcpy(a, b, sizeof(struct ps2pad_stat) * ps2pad_npads);
}

static void
ps2pad_read_stat(struct ps2pad_stat *stat)
{
	int i, res;
	u_char data[PS2PAD_DATASIZE];

	for (i = 0; i < ps2pad_npads; i++) {
		/* port and slot */
		stat[i].portslot = ((ps2pad_pads[i].port << 4) |
				    ps2pad_pads[i].slot);

		/* request status */
		res = ps2padlib_GetReqState(ps2pad_pads[i].port,
					ps2pad_pads[i].slot);
		if (res < 0 || ARRAYSIZEOF(rstat_conv_table) <= res) {
			stat[i].rstat = PS2PAD_RSTAT_FAILED;
		} else {
			stat[i].rstat = rstat_conv_table[res];
		}

		/* connection status */
		res = ps2padlib_GetState(ps2pad_pads[i].port, ps2pad_pads[i].slot);
		stat[i].type = 0;
		if (res < 0 || ARRAYSIZEOF(stat_conv_table) <= res) {
			stat[i].stat = PS2PAD_STAT_ERROR;
		} else {
			stat[i].stat = stat_conv_table[res];
			if (stat[i].stat == PS2PAD_STAT_READY) {
				res = ps2padlib_Read(ps2pad_pads[i].port,
						 ps2pad_pads[i].slot,
						 data);
				if (res != 0 && data[0] == 0) {
					/* pad data is valid */
					stat[i].type = data[1];
				} else {
					stat[i].stat = PS2PAD_STAT_ERROR;
				}
			}
		}
	}
}

static void
ps2pad_do_timer(struct timer_list *t)
{

	ps2pad_read_stat(new_stat);
	if (ps2pad_comp_stat(new_stat, cur_stat)) {
		ps2pad_copy_stat(cur_stat, new_stat);
#ifdef PS2PAD_DEBUG
			DPRINT("timer: new status: ");
			if (ps2pad_debug) {
				int i;
				u_char *p = (u_char*)new_stat;
				for (i = 0; i < sizeof(*new_stat) * 2; i++)
					printk("%02X", *p++);
				printk("\n");
			}
#endif
		wake_up_interruptible(&watchq);
	}

	ps2pad_timer.timer.expires = jiffies + INTERVAL_TIME;
	add_timer(&ps2pad_timer.timer);
}

static void
ps2pad_start_timer()
{
	unsigned long flags;

	DPRINT("start timer\n");
	ps2pad_read_stat(cur_stat);
	spin_lock_irqsave(&spinlock, flags);
	run_timer = 1;
	ps2pad_do_timer(&ps2pad_timer.timer);
	spin_unlock_irqrestore(&spinlock, flags);
}

static inline void
ps2pad_stop_timer()
{
	unsigned long flags;

	DPRINT("stop timer\n");
	spin_lock_irqsave(&spinlock, flags);
	run_timer = 0;
	del_timer(&ps2pad_timer.timer);
	spin_unlock_irqrestore(&spinlock, flags);
}

static inline void
ps2pad_update_status()
{
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);
	if (run_timer) {
		del_timer(&ps2pad_timer.timer);
		ps2pad_do_timer(&ps2pad_timer.timer);
	}
	spin_unlock_irqrestore(&spinlock, flags);
}

static int
lock()
{
#if 1
	locked = 1;
	return 0;	/* FIXME */
#else
	for ( ; ; ) {
		unsigned long flags;

		spin_lock_irqsave(&spinlock, flags);
		if (!locked) {
			locked = 1;
			spin_unlock_irqrestore(&spinlock, flags);
			return 0;
		}
		interruptible_sleep_on(&lockq);
		spin_unlock_irqrestore(&spinlock, flags);
		if(signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}
#endif
}

static void
unlock()
{
#if 1
	locked = 0;
#else
	unsigned long flags;

	spin_lock_irqsave(&spinlock, flags);
	locked = 0;
	wake_up_interruptible(&lockq);
	spin_unlock_irqrestore(&spinlock, flags);
#endif
}

static ssize_t
ps2pad_read(struct file *filp, char *buf, size_t size, loff_t *off)
{
	int res;
	struct ps2pad_dev *dev = filp->private_data;
	u_char data[PS2PAD_DATASIZE];

	/* ps2padlib_Read() does not involve any RPC to IOP.
	  if (lock() < 0) return -ERESTARTSYS;
	 */
	res = ps2padlib_Read(dev->pad->port, dev->pad->slot, data);
	/*
	  unlock();
	 */
	if (res == 0 || data[0] != 0) {
		/* pad data is invalid */
		return -EIO;	/* XXX */
	}

	/*
	 * XXX, ignore offset
	 */
	res = (data[1] & 0x0f) * 2 + 2;
	if (res < size) {
		size = res;
	}
	//memcpy_tofs(buf, data, size);
	copy_to_user(buf, data, size);
	return size;
}

static int
ps2pad_wait_req_stat(struct ps2pad_dev *dev)
{
#if 1
	return ps2padlib_GetReqState(dev->pad->port, dev->pad->slot);
#else
	int res;

	for ( ; ; ) {
		unsigned long flags;

		spin_lock_irqsave(&spinlock, flags);
		res = ps2padlib_GetReqState(dev->pad->port, dev->pad->slot);
		DPRINT("port%d slot%d: req stat %d\n",
		       dev->pad->port, dev->pad->slot, res);
		if (res != PadReqStateBusy) {
			spin_unlock_irqrestore(&spinlock, flags);
			return res;
		}
		interruptible_sleep_on(&watchq);
		spin_unlock_irqrestore(&spinlock, flags);
		if(signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}
#endif
}

static int
ps2pad_check_req_stat(struct ps2pad_dev *dev)
{
	int res;

	if ((res = ps2pad_wait_req_stat(dev)) < 0)
		return res;
	return (res == PadReqStateComplete) ? 0 : -EIO;
}

static long ps2pad_ioctl(struct file *filp, u_int cmd, u_long arg)
{
	int i, res;
	struct ps2pad_dev *dev = filp->private_data;
	int port = dev->pad->port;
	int slot = dev->pad->slot;

	switch (cmd) {
	case PS2PAD_IOCPRESSMODEINFO:
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_InfoPressMode(port, slot);
		unlock();
		return put_user(res, (int *)arg);
		break;
	case PS2PAD_IOCENTERPRESSMODE:
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_EnterPressMode(port, slot);
		unlock();
		ps2pad_update_status();
		if (res != 1)
			return -EIO;
		return (filp->f_flags & O_NONBLOCK) ? 0 : ps2pad_check_req_stat(dev);
		break;
	case PS2PAD_IOCEXITPRESSMODE:
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_ExitPressMode(port, slot);
		unlock();
		ps2pad_update_status();
		if (res != 1)
			return -EIO;
		return (filp->f_flags & O_NONBLOCK) ? 0 : ps2pad_check_req_stat(dev);
		break;
	case PS2PAD_IOCGETREQSTAT:
		if (filp->f_flags & O_NONBLOCK) {
			res = ps2padlib_GetReqState(port, slot);
		} else {
			if ((res = ps2pad_wait_req_stat(dev)) < 0)
				return res;
		}
		if (res < 0 || ARRAYSIZEOF(rstat_conv_table) <= res) {
			return -EIO;
		} else {
			return put_user(rstat_conv_table[res], (int *)arg);
		}
		break;
	case PS2PAD_IOCGETSTAT:
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_GetState(port, slot);
		unlock();
		if (res < 0 || ARRAYSIZEOF(stat_conv_table) <= res) {
			return -EIO;
		} else {
			return put_user(stat_conv_table[res], (int *)arg);
		}
		break;
	case PS2PAD_IOCACTINFO: {
		struct ps2pad_actinfo info;
		if (copy_from_user(&info, (char *)arg, sizeof(info)))
		    return (-EFAULT);
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_InfoAct(port, slot, info.actno, info.term);
		unlock();
		if (res < 0) return -EIO;
		info.result = res;
		res = copy_to_user((char *)arg, &info, sizeof(info));
		return (res ? -EFAULT : 0);
		}
		break;
	case PS2PAD_IOCCOMBINFO: {
		struct ps2pad_combinfo info;
		if (copy_from_user(&info, (char *)arg, sizeof(info)))
		    return (-EFAULT);
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_InfoComb(port, slot, info.listno, info.offs);
		unlock();
		if (res < 0) return -EIO;
		info.result = res;
		res = copy_to_user((char *)arg, &info, sizeof(info));
		return (res ? -EFAULT : 0);
		}
		break;
	case PS2PAD_IOCMODEINFO: {
		struct ps2pad_modeinfo info;
		if (copy_from_user(&info, (char *)arg, sizeof(info)))
		    return (-EFAULT);
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_InfoMode(port, slot, info.term, info.offs);
		unlock();
		if (res < 0) return -EIO;
		info.result = res;
		res = copy_to_user((char *)arg, &info, sizeof(info));
		return (res ? -EFAULT : 0);
		}
		break;
	case PS2PAD_IOCSETMODE: {
		struct ps2pad_mode mode;
		if (copy_from_user(&mode, (char *)arg, sizeof(mode)))
		    return (-EFAULT);
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_SetMainMode(port, slot, mode.offs, mode.lock);
		unlock();
		ps2pad_update_status();
		if (res != 1) {
			DPRINT("%d %d: ps2padlib_SetMainMode() failed\n",
			       dev->pad->port, dev->pad->slot);
			return -EIO;
		}
		if (filp->f_flags & O_NONBLOCK) {
			DPRINT("port%d slot%d: PS2PAD_IOCSETMODE: non-block\n",
			       dev->pad->port, dev->pad->slot);
			return 0;
		} else {
			return ps2pad_check_req_stat(dev);
		}
		}
		break;
	case PS2PAD_IOCSETACTALIGN: {
		struct ps2pad_act act;
		if (copy_from_user(&act, (char *)arg, sizeof(act)))
		    return (-EFAULT);
		if (6 < act.len) {
			return EINVAL;
		}
		for (i = act.len; i < 6; i++) {
			act.data[i] = 0xff;
		}
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_SetActAlign(port, slot, act.data);
		unlock();
		ps2pad_update_status();
		if (res != 1) return -EIO;
		return (filp->f_flags & O_NONBLOCK) ? 0 : ps2pad_check_req_stat(dev);
		}
		break;
	case PS2PAD_IOCSETACT: {
		struct ps2pad_act act;
		if (copy_from_user(&act, (char *)arg, sizeof(act)))
		    return (-EFAULT);
		if (6 < act.len) {
			return EINVAL;
		}
		if (lock() < 0) return -ERESTARTSYS;
		res = ps2padlib_SetActDirect(port, slot, act.data);
		unlock();
		if (res != 1) return -EIO;
		return 0;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
ps2pad_open(struct inode *inode, struct file *filp)
{
	dev_t devno = inode->i_rdev;

	/* diagnosis */
	if (MAJOR(devno) != ps2pad_major) {
		printk(KERN_ERR "ps2pad: incorrect major no\n");
		return -ENODEV;
	}

	DPRINT("open, devno=%04x\n", devno);

	if (MINOR(devno)== 255) {
		/*
		 * control device
		 */
		struct ps2pad_ctl_dev *dev;

		dev = kmalloc(sizeof(struct ps2pad_ctl_dev), GFP_KERNEL);
		if (dev == NULL) {
			return -ENOMEM;
		}
		filp->private_data = dev;

		filp->f_op = &ps2pad_ctlops;

		dev->stat_is_valid = 0;
	} else {
		/*
		 * control device
		 */
		struct ps2pad_dev *dev;
		int i;
		int port, slot;

		port = PORT(devno);
		slot = SLOT(devno);

		for (i = 0; i < ps2pad_npads; i++) {
			if (ps2pad_pads[i].port == port &&
			    ps2pad_pads[i].slot == slot) {
				break;
			}
		}

		if (ps2pad_npads <= i) {
			/* pad device not found */
			DPRINT("pad(%d,%d) not found\n", port, slot);
			return -ENODEV;
		}

		dev = kmalloc(sizeof(struct ps2pad_dev), GFP_KERNEL);
		if (dev == NULL) {
			return -ENOMEM;
		}
		filp->private_data = dev;

		dev->pad = &ps2pad_pads[i];
	}

	if (open_devices++ == 0)
		ps2pad_start_timer();

	return 0;
}

static ssize_t
ps2pad_ctl_read(struct file *filp, char *buf, size_t size, loff_t *off)
{
	struct ps2pad_ctl_dev *dev = filp->private_data;
	unsigned long flags;

	if (sizeof(struct ps2pad_stat) * ps2pad_npads < size)
		size = sizeof(struct ps2pad_stat) * ps2pad_npads;
	spin_lock_irqsave(&spinlock, flags);
	for ( ; ; ) {
		if ((filp->f_flags & O_NONBLOCK) ||
		    !dev->stat_is_valid ||
		    ps2pad_comp_stat(dev->stat, cur_stat)) {
			ps2pad_copy_stat(dev->stat, cur_stat);
			dev->stat_is_valid = 1;
#ifdef PS2PAD_DEBUG
			DPRINT("new status: ");
			if (ps2pad_debug) {
				int i;
				u_char *p = (u_char*)dev->stat;
				for (i = 0; i < size; i++)
					printk("%02X", *p++);
				printk("\n");
			}
#endif
			copy_to_user(buf, dev->stat, size);
			spin_unlock_irqrestore(&spinlock, flags);
			return size;
		}
		// FIXME: interruptible_sleep_on(&watchq);
		spin_unlock_irqrestore(&spinlock, flags);
		if(signal_pending(current)) {
			return -ERESTARTSYS;
		}
	}
}

static long ps2pad_ctl_ioctl(struct file *filp, u_int cmd, u_long arg)
{
	/*
	int res;
	struct ps2pad_ctl_dev *dev = filp->private_data;
	*/

	switch (cmd) {
	case PS2PAD_IOCGETNPADS:
		return put_user(ps2pad_npads, (int *)arg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned int
ps2pad_poll(struct file *file, poll_table * wait)
{
	return POLLIN | POLLRDNORM;
}

static unsigned int
ps2pad_ctl_poll(struct file *filp, poll_table * wait)
{
	unsigned int mask = 0;
	struct ps2pad_ctl_dev *dev = filp->private_data;
	unsigned long flags;

	poll_wait(filp, &watchq, wait);
	spin_lock_irqsave(&spinlock, flags);
	if (!dev->stat_is_valid || ps2pad_comp_stat(dev->stat, cur_stat))
		mask |= POLLIN | POLLRDNORM;
	spin_unlock_irqrestore(&spinlock, flags);

	return mask;
}

static int
ps2pad_release(struct inode *inode, struct file *filp)
{
	struct ps2pad_dev *dev = filp->private_data;

	DPRINT("close, dev=%lx\n", (unsigned long)dev);

       	kfree(dev);

	if (--open_devices == 0)
		ps2pad_stop_timer();

	return 0;
}

static int
ps2pad_ctl_release(struct inode *inode, struct file *filp)
{
	struct ps2pad_ctl_dev *dev = filp->private_data;

	DPRINT("ctl close, dev=%lx\n", (u_long)dev);

	kfree(dev);

	if (--open_devices == 0)
		ps2pad_stop_timer();

	return 0;
}

#ifdef CONFIG_PROC_FS_FIXME
static char* PadStateStr[] =
{"DISCONNECT", "", "FINDCTP1", "", "", "EXECCMD", "STABLE", "ERROR" };

static int
ps2pad_read_proc(char *page, char **start, off_t offset, int len, int *eof, void *data)
{
	int res;
	int n, i, j;
	char *p = page;
	u_char buf[PS2PAD_DATASIZE];
	char *tmp;
	unsigned long flags;

	p += sprintf(p, "port slot status     type      button\n");
	*eof = 1;

	spin_lock_irqsave(&spinlock, flags);
	for (i = 0; i < ps2pad_npads; i++) {
		res = ps2padlib_GetState(ps2pad_pads[i].port, ps2pad_pads[i].slot);
		tmp = (res >= 0 && res <= 7) ? PadStateStr[res] : "";
		p += sprintf(p, "%4d %4d %-10s",
			     ps2pad_pads[i].port, ps2pad_pads[i].slot, tmp);

		res = ps2padlib_Read(ps2pad_pads[i].port, ps2pad_pads[i].slot,
				 buf);
		if (res != 0 && buf[0] == 0) {
			/* pad data is valid */
			p += sprintf(p, " %-9s",
				     pad_type_names[(buf[1] & 0xf0) >> 4]);
			p += sprintf(p, " %02X%02X ", buf[2], buf[3]);
			n = (buf[1] & 0x0f) * 2 + 2;
			for (j = 4; j < n; j++) {
				p += sprintf(p, "%02X", buf[j]);
			}
		}
		p += sprintf(p, "\n");
	}
	spin_unlock_irqrestore(&spinlock, flags);

	return (p - page);
}
#endif

static unsigned char *dmabuf;
static int init_flags;
#define INIT_LIB	(1<< 0)
#define INIT_BUF	(1<< 1)
#define INIT_DEV	(1<< 2)
#define INIT_PROC	(1<< 3)

int __init ps2pad_init(void)
{
	int res, i;
	int port, slot;

	printk("PlayStation 2 game pad: initialize...\n");

	spin_lock_init(&spinlock);

	/*
	 * initialize library
	 */
	printk("PlayStation 2 game pad: ps2padlib_Init\n");
	if (ps2padlib_Init(0) != 1) {
		printk(KERN_ERR "ps2pad: failed to initialize\n");
		return -EIO;
	}
	init_flags |= INIT_LIB;

	/*
	 * allocate memory space
	 */
	dmabuf = kmalloc(DMABUFSIZE * MAXNPADS, GFP_KERNEL);
	if (dmabuf == NULL) {
		printk(KERN_ERR "ps2pad: can't allocate memory\n");
		return -ENOMEM;
	}
	init_flags |= INIT_BUF;

	for (i = 0; i < MAXNPADS; i++) {
		/* 
		 * We must access asynchronous DMA buffer via 
		 * non-cached segment(KSEG1).
		 */
		ps2pad_pads[i].dmabuf = (void *)KSEG1ADDR(&dmabuf[DMABUFSIZE * i]);
	}

	/*
	 * scan all pads and start DMA
	 */
	// FIXME: if (lock() < 0) return -ERESTARTSYS;

	for (port = 0; port < NPORTS; port++) {
	    for (slot = 0; slot < NSLOTS; slot++) {
		    printk("PlayStation 2 game pad: NPORTS %d %d\n", port, slot);
		res = ps2padlib_PortOpen(port, slot,
				     (void *)ps2pad_pads[ps2pad_npads].dmabuf);
		if (res == 1) {
			if (MAXNPADS <= ps2pad_npads) {
				printk(KERN_WARNING "ps2pad: too many pads\n");
				break;
			}
			DPRINT("port%d  slot%d\n", port, slot);
			ps2pad_pads[ps2pad_npads].port = port;
			ps2pad_pads[ps2pad_npads].slot = slot;
			ps2pad_npads++;
		}
	    }
	}
	printk("PlayStation 2 game pad: NPORTS done\n");

	// FIXME: unlock();

#if 0
	/*
	 * initialize timer
	 */
	timer_setup(&ps2pad_timer.timer, ps2pad_do_timer, 0);
	ps2pad_timer.data = 0;
#endif

	/*
	 * register device entry
	 */
	if ((res = register_chrdev(ps2pad_major, "ps2pad", &ps2pad_fops)) < 0) {
		printk(KERN_ERR "ps2pad: can't get major %d\n", ps2pad_major);
		return res;
	}
	if (ps2pad_major == 0)
		ps2pad_major = res;
	init_flags |= INIT_DEV;

#ifdef CONFIG_PROC_FS_FIXME
	create_proc_read_entry("ps2pad", 0, 0, ps2pad_read_proc, NULL);
	init_flags |= INIT_PROC;
#endif

	printk("PlayStation 2 game pad: initialize done\n");

	return( 0 );
}

void
ps2pad_cleanup(void)
{
#ifndef PS2PAD_NOPORTCLOSE
	int res, i;
#endif

	DPRINT("unload\n");

#ifndef PS2PAD_NOPORTCLOSE
	if (init_flags & INIT_LIB) {
		for (i = 0; i < ps2pad_npads; i++) {
			res = ps2padlib_PortClose(ps2pad_pads[i].port,
					      ps2pad_pads[i].slot);
			if (res != 1) {
				printk(KERN_WARNING "ps2pad: failed to close\n");
			}
		}
	}
#endif

	if (init_flags & INIT_DEV) {
		unregister_chrdev(ps2pad_major, "ps2pad");
	} else {
		printk(KERN_WARNING "ps2pad: unregister_chrdev() error\n");
	}
	init_flags &= ~INIT_DEV;

#ifdef CONFIG_PROC_FS_FIXME
	if (init_flags & INIT_PROC)
		remove_proc_entry("ps2pad", NULL);
	init_flags &= ~INIT_PROC;
#endif

	if ((init_flags & INIT_LIB) && ps2padlib_End() != 1) {
		printk(KERN_WARNING "ps2pad: failed to finalize\n");
	}
	init_flags &= ~INIT_LIB;

	if (init_flags & INIT_BUF)
		kfree(dmabuf);
	init_flags &= ~INIT_BUF;
}

module_init(ps2pad_init);
module_exit(ps2pad_cleanup);

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 game controller driver");
MODULE_LICENSE("GPL");
