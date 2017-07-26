/*
 *  PlayStation 2 event handling device driver (/dev/ps2event)
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
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/ps2/dev.h>

#include <asm/uaccess.h>

#include <asm/mach-ps2/irq.h>

#include "ps2dev.h"

/* TBD: Convert Linux 2.4 module to 2.6 */

struct ps2ev_data {
    struct ps2ev_data *next;
    u32 intr_flag;
    u32 intr_mask;
    unsigned int intr_count[PS2EV_N_MAX];
    unsigned int hsync_active;
    wait_queue_head_t wq;
    struct fasync_struct *fa;
    struct pid *pid;
    int sig;
};

struct ev_list {
    int event;
    int irq;
    irqreturn_t (*handler)(int, void *);
    char *device;
};

static DEFINE_SPINLOCK(ps2ev_lock);
static struct ps2ev_data *ps2ev_data = NULL;
static u32 intr_mask = 0;
static volatile unsigned int hsync_count;

static inline void ev_check(struct ps2ev_data *p, int event, u32 evbit)
{
    if (p->intr_mask & evbit) {
	p->intr_flag |= evbit;
	p->intr_count[event]++;
	if (waitqueue_active(&p->wq))
	    wake_up_interruptible(&p->wq);
	if (p->fa)
	    kill_fasync(&p->fa, SIGIO, POLL_IN);
	if (p->sig)
	    kill_pid(p->pid, p->sig, 1);
    }
}

static irqreturn_t ev_handler(int irq, void *dev_id)
{
    struct ps2ev_data *p;
    int event = ((struct ev_list *)dev_id)->event;
    u32 evbit = 1 << event;

    for (p = ps2ev_data; p != NULL; p = p->next)
	ev_check(p, event, evbit);
    return IRQ_HANDLED;
}

/* TBD: Check return values of interrupt handlers. */
static irqreturn_t ev_finish_handler(int irq, void *dev_id)
{
    struct ps2ev_data *p;
    extern int ps2gs_storeimage_finish(void);

    /* for storeimage */
    if (ps2gs_storeimage_finish())
	return IRQ_HANDLED;

    for (p = ps2ev_data; p != NULL; p = p->next)
	ev_check(p, PS2EV_N_FINISH, PS2EV_FINISH);
    return IRQ_HANDLED;
}

static irqreturn_t ev_hsync_handler(int irq, void *dev_id)
{
    struct ps2ev_data *p;

    hsync_count++;
    for (p = ps2ev_data; p != NULL; p = p->next) {
	if (p->hsync_active != hsync_count)
	    continue;
	ev_check(p, PS2EV_N_HSYNC, PS2EV_HSYNC);
    }
    return IRQ_HANDLED;
}

static irqreturn_t ev_vsync_handler(int irq, void *dev_id)
{
    struct ps2ev_data *p;

    hsync_count = 0;
    for (p = ps2ev_data; p != NULL; p = p->next)
	ev_check(p, PS2EV_N_VSYNC, PS2EV_VSYNC);
    return IRQ_HANDLED;
}

static irqreturn_t ev_vbstart_handler(int irq, void *dev_id)
{
    struct ps2ev_data *p;
    extern void ps2gs_sgssreg_vb(void);

    ps2gs_sgssreg_vb();
    for (p = ps2ev_data; p != NULL; p = p->next)
	ev_check(p, PS2EV_N_VBSTART, PS2EV_VBSTART);
    return IRQ_HANDLED;
}

static struct ev_list ev_list[] = {
    { PS2EV_N_VBSTART, IRQ_INTC_VB_ON,  ev_vbstart_handler, "V-Blank start" },
    { PS2EV_N_VBEND,   IRQ_INTC_VB_OFF, ev_handler,         "V-Blank end" },
    { PS2EV_N_VIF0,    IRQ_INTC_VIF0,   ev_handler,         "VIF0" },
    { PS2EV_N_VIF1,    IRQ_INTC_VIF1,   ev_handler,         "VIF1" },
    { PS2EV_N_VU0,     IRQ_INTC_VU0,    ev_handler,         "VU0" },
    { PS2EV_N_VU1,     IRQ_INTC_VU1,    ev_handler,         "VU1" },
    { PS2EV_N_IPU,     IRQ_INTC_IPU,    ev_handler,         "IPU" },
    { PS2EV_N_SIGNAL,  IRQ_GS_SIGNAL,   ev_handler,         "GS SIGNAL" },
    { PS2EV_N_FINISH,  IRQ_GS_FINISH,   ev_finish_handler,  "GS FINISH" },
    { PS2EV_N_HSYNC,   IRQ_GS_HSYNC,    ev_hsync_handler,   "GS HSYNC" },
    { PS2EV_N_VSYNC,   IRQ_GS_VSYNC,    ev_vsync_handler,   "GS VSYNC" },
    { PS2EV_N_EDW,     IRQ_GS_EDW,      ev_handler,         "GS EDW" },
    { -1, -1, NULL, NULL }
};

static void register_intr_handler(int free)
{
    struct ps2ev_data *p;
    struct ev_list *ep;
    u32 intr_mask_new;
    u32 ev_bit;

    if (free) {
	intr_mask_new = 0;
    } else {
	/* storeimage needs FINISH event */
	/* sgssreg_vb needs VBSTART event */
	intr_mask_new = PS2EV_FINISH | PS2EV_VBSTART;
	for (p = ps2ev_data; p != NULL; p = p->next)
	    intr_mask_new |= p->intr_mask;

	/* hsync needs vsync handler for counter reset */
	if (intr_mask_new & PS2EV_HSYNC)
	    intr_mask_new |= PS2EV_VSYNC;
    }

    for (ep = ev_list; ep->event >= 0; ep++) {
	ev_bit = 1 << ep->event;
	if (intr_mask_new & ev_bit) {
	    if (!(intr_mask & ev_bit)) {
		if (request_irq(ep->irq, ep->handler, IRQF_SHARED,
				ep->device, ep)) {
		    printk("unable to get irq %d\n", ep->irq);
		    intr_mask_new &= ~ev_bit;
		}
	    }
	} else {
	    if (intr_mask & ev_bit) {
		free_irq(ep->irq, ep);
	    }
	}
    }
    intr_mask = intr_mask_new;
}

static long ps2ev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct ps2ev_data *data;
    u32 ff, oldff;
    int val;
    int i;

    switch (cmd) {
    case PS2IOC_ENABLEEVENT:
	data = (struct ps2ev_data *)file->private_data;
	oldff = data->intr_mask;
	if ((int)arg >= 0) {
	    spin_lock_irq(&ps2ev_lock);
	    ff = data->intr_mask ^ arg;
	    data->intr_flag &= ~ff;
	    for (i = 0; i < PS2EV_N_MAX; i++)
		if (ff & (1 << i))
		    data->intr_count[i] = 0;
	    data->intr_mask = arg;
	    register_intr_handler(0);
	    spin_unlock_irq(&ps2ev_lock);
	}
	return oldff;
    case PS2IOC_GETEVENT:
	data = (struct ps2ev_data *)file->private_data;
	spin_lock_irq(&ps2ev_lock);
	oldff = data->intr_flag;
	if ((int)arg > 0)
	    data->intr_flag &= ~arg;
	spin_unlock_irq(&ps2ev_lock);
	return oldff;
    case PS2IOC_WAITEVENT:
	data = (struct ps2ev_data *)file->private_data;
	if (wait_event_interruptible(data->wq, (data->intr_flag & arg))) {
	    return -ERESTARTSYS;	/* signal arrived */
	}
	spin_lock_irq(&ps2ev_lock);
	oldff = data->intr_flag;
	data->intr_flag &= ~arg;
	spin_unlock_irq(&ps2ev_lock);
	return oldff;
    case PS2IOC_EVENTCOUNT:
	data = (struct ps2ev_data *)file->private_data;
	if ((long)arg < 0) {		/* clear all event counters */
	    spin_lock_irq(&ps2ev_lock);
	    for (i = 0; i < PS2EV_N_MAX; i++)
		data->intr_count[i] = 0;
	    spin_unlock_irq(&ps2ev_lock);
	    return 0;
	}
	if (arg >= PS2EV_N_MAX)
	    return -EINVAL;
	spin_lock_irq(&ps2ev_lock);
	val = data->intr_count[arg];
	data->intr_count[arg] = 0;
	spin_unlock_irq(&ps2ev_lock);
	return val;
    case PS2IOC_HSYNCACT:
	data = (struct ps2ev_data *)file->private_data;
	val = data->hsync_active;
	if ((long)arg >= 0)
	    data->hsync_active = arg;
	return val;
    case PS2IOC_GETHSYNC:
	return hsync_count;
    case PS2IOC_SETSIGNAL:
	data = (struct ps2ev_data *)file->private_data;
	val = data->sig;
	if ((int)arg >= 0)
	    data->sig = arg;
	return val;
    default:
	return -EINVAL;
    }

    return 0;
}

static ssize_t ps2ev_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
    struct ps2ev_data *data = file->private_data;
    u32 ff;
    int len;

    if (file->f_flags & O_NONBLOCK) {
	if (!(data->intr_flag & data->intr_mask)) {
	    return -EAGAIN;
	}
    } else {
	if (wait_event_interruptible(data->wq,
				     (data->intr_flag & data->intr_mask))) {
	    return -ERESTARTSYS;	/* signal arrived */
	}
    }

    spin_lock_irq(&ps2ev_lock);
    ff = data->intr_flag;
    data->intr_flag &= ~data->intr_mask;
    spin_unlock_irq(&ps2ev_lock);

    len = count > sizeof(ff) ? sizeof(ff) : count;
    if (copy_to_user(buf, &ff, len))
	return -EFAULT;
    return len;
}

static unsigned int ps2ev_poll(struct file *file, poll_table *wait)
{
    struct ps2ev_data *data = file->private_data;

    poll_wait(file, &data->wq, wait);
    if (data->intr_flag & data->intr_mask)
	return POLLIN | POLLRDNORM;
    return 0;
}

static int ps2ev_fasync(int fd, struct file *file, int on)
{
    int retval;
    struct ps2ev_data *data = file->private_data;

    retval = fasync_helper(fd, file, on, &data->fa);
    if (retval < 0)
	return retval;
    return 0;
}

static int ps2ev_open(struct inode *inode, struct file *file)
{
    struct ps2ev_data *data;

    if ((data = kmalloc(sizeof(struct ps2ev_data), GFP_KERNEL)) == NULL)
	return -ENOMEM;
    memset(data, 0, sizeof(struct ps2ev_data));
    init_waitqueue_head(&data->wq);
    data->pid = get_pid(task_pid(current));

    file->private_data = data;
    spin_lock_irq(&ps2ev_lock);
    data->next = ps2ev_data;
    ps2ev_data = data;
    spin_unlock_irq(&ps2ev_lock);
    return 0;
}

static int ps2ev_release(struct inode *inode, struct file *file)
{
    struct ps2ev_data *data, **p;

    data = (struct ps2ev_data *)file->private_data;
    ps2ev_fasync(-1, file, 0);

    spin_lock_irq(&ps2ev_lock);
    data->intr_mask = 0;
    register_intr_handler(0);
    p = &ps2ev_data;
    while (*p != NULL) {
	if (*p == data) {
	    *p = data->next;
	    break;
	}
	p = &(*p)->next;
    }
    spin_unlock_irq(&ps2ev_lock);
    put_pid(data->pid);
    kfree(data);
    return 0;
}

struct file_operations ps2ev_fops = {
    llseek:		no_llseek,
    read:		ps2ev_read,
    poll:		ps2ev_poll,
    unlocked_ioctl:	ps2ev_ioctl,
    open:		ps2ev_open,
    release:		ps2ev_release,	
    fasync:		ps2ev_fasync,
};

void ps2ev_init(void)
{
    register_intr_handler(0);
}

void ps2ev_cleanup(void)
{
    register_intr_handler(-1);
}
