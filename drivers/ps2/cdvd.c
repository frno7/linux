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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/iso_fs.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/kthread.h>

#include "cdvd.h"

/*
 * macro defines
 */
#define MAJOR_NR	ps2cdvd_major
#define DEVICE_NAME	"PS2 CD/DVD-ROM"
/* #define DEVICE_INTR	do_ps2cdvd */

#include <linux/blkdev.h>

#define ENTER	1
#define LEAVE	0
#define INVALID_DISCTYPE	-1

#define BUFFER_ALIGNMENT	64

/* return values of checkdisc */
#define DISC_ERROR	-1
#define DISC_OK		0
#define DISC_NODISC	1
#define DISC_RETRY	2

#define DVD_DATA_SECT_SIZE 2064
#define DVD_DATA_OFFSET 12

/*
 * data types
 */
struct ps2cdvd_event {
	int type;
	void *arg;
};

static DEFINE_SPINLOCK(cdvd_lock);
static DEFINE_SPINLOCK(cdvd_queue_lock);

/*
 * function prototypes
 */
static void ps2cdvd_request(struct request_queue *);
static void ps2cdvd_timer(unsigned long);
void ps2cdvd_cleanup(void);
static int ps2cdvd_bdops_open(struct block_device *bdev, fmode_t mode);
static void ps2cdvd_bdops_release(struct gendisk *disk, fmode_t mode);
static int ps2cdvd_bdops_mediachanged(struct gendisk *disk);
static int ps2cdvd_bdops_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg);
static int checkdisc(void);
static int spindown(void);

/*
 * variables
 */
int ps2cdvd_check_interval = 2;
int ps2cdvd_databuf_size = 16;
int ps2cdvd_debug = DBG_DEFAULT_FLAGS;
int ps2cdvd_immediate_ioerr = 0;
int ps2cdvd_major = PS2CDVD_MAJOR;
int ps2cdvd_spindown = 0;
int ps2cdvd_wrong_disc_retry = 0;

module_param(ps2cdvd_check_interval, int, 0);
MODULE_PARM_DESC(ps2cdvd_check_interval,
	"CDVD check interval (1-30)");
module_param(ps2cdvd_databuf_size, int, 0);
MODULE_PARM_DESC(ps2cdvd_databuf_size,
	"CDVD data buffer size (1-256)");
module_param(ps2cdvd_debug, int, 0);
MODULE_PARM_DESC(ps2cdvd_debug,
	"Set debug level verbosity.");
module_param(ps2cdvd_immediate_ioerr, int, 0);
MODULE_PARM_DESC(ps2cdvd_immediate_ioerr,
	"(0-1)");
module_param(ps2cdvd_major, int, 0);
MODULE_PARM_DESC(ps2cdvd_major,
	"Major number for device (0-255)");
module_param(ps2cdvd_spindown, int, 0);
MODULE_PARM_DESC(ps2cdvd_spindown,
	"CDVD spindown value (0-3600)");
module_param(ps2cdvd_wrong_disc_retry, int, 0);
MODULE_PARM_DESC(ps2cdvd_wrong_disc_retry,
	"Enable wrong disc retry (0-1)");

struct ps2cdvd_ctx ps2cdvd = {
	disc_changed:	1,
	disc_type:	INVALID_DISCTYPE,
	databuf_nsects:	0,
	label_mode: {
		100,			/* try count			*/
		0,			/* will be set			*/
		SCECdSecS2048,		/* data size = 2048		*/
		0xff			/* padding data			*/
	},
	data_mode: {
		50,			/* try count			*/
		SCECdSpinNom,		/* try with maximum speed	*/
		SCECdSecS2048,		/* data size = 2048		*/
		0xff			/* padding data			*/
	},
	cdda_mode: {
		50,			/* try count			*/
		SCECdSpinNom,		/* try with maximum speed	*/
		SCECdSecS2352|0x80,	/* data size = 2352, CD-DA	*/
		0x0f			/* padding data			*/
	},
};

static LIST_HEAD(cdvd_deferred);
static struct gendisk *disk;

/*
 * function bodies
 */
static void
ps2cdvd_invalidate_discinfo(void)
{
	ps2cdvd.disc_type = INVALID_DISCTYPE;
	if (ps2cdvd.label_valid)
		DPRINT(DBG_DLOCK, "label gets invalid\n");
	ps2cdvd.label_valid = 0;
	if (ps2cdvd.toc_valid)
		DPRINT(DBG_VERBOSE, "toc gets invalid\n");
	ps2cdvd.toc_valid = 0;
	ps2cdvd.databuf_nsects = 0;
	ps2cdvd.disc_changed++;
}

static void
ps2cdvd_request(struct request_queue *rq)
{
	unsigned long flags;
	struct request *req;

	while ((req = blk_fetch_request(rq)) != NULL) {
		if (blk_rq_is_passthrough(req)) { /* FIXME aebf526b53aea164508730427597d45f3e06b376 */
			printk(KERN_DEBUG "ps2cdvd: Non-fs request ignored\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		if (rq_data_dir(req) != READ) {
			printk(KERN_NOTICE "ps2cdvd: Read only device -");
			printk(" write request ignored\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		spin_lock_irqsave(&cdvd_queue_lock, flags);
		list_add_tail(&req->queuelist, &cdvd_deferred); // TBD: Protect with lock?
		spin_unlock_irqrestore(&cdvd_queue_lock, flags);

		spin_lock_irqsave(&ps2cdvd.ievent_lock, flags);
		if (ps2cdvd.ievent == EV_NO_EVENT) {
		    ps2cdvd.ievent = EV_START;
		    up(&ps2cdvd.wait_sem);
		}
		spin_unlock_irqrestore(&ps2cdvd.ievent_lock, flags);
	}
}

void
ps2cdvd_lock(char *msg)
{
    DPRINT(DBG_LOCK, "lock '%s' pid=%d\n", msg, current->pid);
    ps2sif_lock(ps2cdvd.lock, msg);
    DPRINT(DBG_LOCK, "locked pid=%d\n", current->pid);
}

int
ps2cdvd_lock_interruptible(char *msg)
{
    int res;

    DPRINT(DBG_LOCK, "interruptible lock '%s' pid=%d\n", msg, current->pid);
    res = ps2sif_lock_interruptible(ps2cdvd.lock, msg);
    DPRINT(DBG_LOCK, "interruptible locked pid=%d res=%d\n",current->pid,res);

    return (res);
}

void
ps2cdvd_unlock()
{

    DPRINT(DBG_LOCK, "unlock pid=%d\n", current->pid);
    ps2sif_unlock(ps2cdvd.lock);
}

static int
ps2cdvd_command(int command)
{
    int res;

    if ((res = down_interruptible(&ps2cdvd.command_sem)) != 0)
	return (res);
    ps2cdvd.event = command;
    up(&ps2cdvd.wait_sem);

    return (0);
}

static void
ps2cdvd_sleep(long timeout)
{
    DECLARE_WAIT_QUEUE_HEAD(wq);

    sleep_on_timeout(&wq, timeout);
}

int
ps2cdvd_lock_ready(void)
{
    int res, state;
    unsigned long flags;
    DECLARE_WAITQUEUE(wait, current);

    while (1) {
	if ((res = ps2cdvd_lock_interruptible("ready")) != 0)
	    return (res);
	if ((state = ps2cdvd.state) == STAT_READY)
	    return (0);
	ps2cdvd_unlock();

	switch (state) {
	case STAT_WAIT_DISC:
	    return (-ENOMEDIUM);
	    break;

	case STAT_IDLE:
	    if ((res = ps2cdvd_command(EV_START)) != 0)
		return (res);
	    break;

	case STAT_INVALID_DISC:
	    if (!ps2cdvd_wrong_disc_retry)
		return (-ENOMEDIUM);
	    break;
	}

	spin_lock_irqsave(&ps2cdvd.state_lock, flags);
	add_wait_queue(&ps2cdvd.statq, &wait);
	while (ps2cdvd.state == state && !signal_pending(current)) {
	    set_current_state(TASK_INTERRUPTIBLE);
	    spin_unlock_irq(&ps2cdvd.state_lock);
	    schedule();
	    spin_lock_irq(&ps2cdvd.state_lock);
	}
	remove_wait_queue(&ps2cdvd.statq, &wait);
	spin_unlock_irqrestore(&ps2cdvd.state_lock, flags);
	if(signal_pending(current))
	    return (-ERESTARTSYS);
    }

    /* not reached */
}

static void
ps2cdvd_timer(unsigned long arg)
{
    unsigned long flags;

    spin_lock_irqsave(&ps2cdvd.ievent_lock, flags);
    if (ps2cdvd.ievent == EV_NO_EVENT) {
	ps2cdvd.ievent = EV_TIMEOUT;
	up(&ps2cdvd.wait_sem);
    }
    spin_unlock_irqrestore(&ps2cdvd.ievent_lock, flags);
}

static int
ps2cdvd_getevent(int timeout)
{
    int ev, res;
    unsigned long flags;
    struct timer_list timer;

    if (kthread_should_stop())
	return (EV_EXIT);

    init_timer(&timer);
    timer.function = (void(*)(u_long))ps2cdvd_timer;
    timer.expires = jiffies + (timeout);
    add_timer(&timer);
    res = down_interruptible(&ps2cdvd.wait_sem);
    del_timer(&timer);

    if (res != 0)
	return (EV_EXIT);

    spin_lock_irqsave(&ps2cdvd.ievent_lock, flags);
    ev = ps2cdvd.ievent;
    ps2cdvd.ievent = EV_NO_EVENT;
    spin_unlock_irqrestore(&ps2cdvd.ievent_lock, flags);
    if (ev == EV_NO_EVENT) {
	ev = ps2cdvd.event;
	ps2cdvd.event = EV_NO_EVENT;
	up(&ps2cdvd.command_sem);
    }

    return (ev);
}

static int
ps2cdvd_check_cache(void)
{
    struct list_head *elem, *next;
    unsigned long flags;
    int rv;

    spin_lock_irqsave(&cdvd_queue_lock, flags);
    list_for_each_safe(elem, next, &cdvd_deferred) {
        struct request *req;
	unsigned int pos;
	unsigned int sectors;
	sector_t start;
	sector_t end;
	void *src;
	void *dst;

	req = list_entry(elem, struct request, queuelist);
	start = blk_rq_pos(req);
	end = start + blk_rq_sectors(req);

	/* Check if data is in buffer. */
	if (ps2cdvd.databuf_addr > start/4)
		break;
	if (end/4 > ps2cdvd.databuf_addr + ps2cdvd.databuf_nsects)
		break;
	DPRINT(DBG_READ, "REQ %p: sec=%lld  n=%d  buf=%p\n",
	       req, start,
	       blk_rq_sectors(req), req->buffer);
	sectors = blk_rq_sectors(req) / 4;
	src = ps2cdvd.databuf;
	dst = req->buffer;
	if (ps2cdvd.disc_type == SCECdDVDV) {
		src += DVD_DATA_OFFSET + DVD_DATA_SECT_SIZE * (start/4 - ps2cdvd.databuf_addr);
		/* Copy all sectors. */
		for (pos = 0; pos < sectors; pos++) {
			/* Copy one sector from cache to buffer of request. */
			memcpy(dst, src, DATA_SECT_SIZE);
			dst += DATA_SECT_SIZE;
			src += DVD_DATA_SECT_SIZE;
		}
	} else {
		/* Copy data from cache to buffer of request. */
		src += DATA_SECT_SIZE * (start/4 - ps2cdvd.databuf_addr);
		memcpy(dst, src, sectors * DATA_SECT_SIZE);
	}
	list_del_init(&req->queuelist);
	__blk_end_request_all(req, 0);
    }

    rv = list_empty(&cdvd_deferred);
    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
    return rv;
}

static int
ps2cdvd_thread(void *arg)
{
    int res, new_state, disc_type;
    int sum, traycount, ev;
    unsigned long flags;
    long sn;
    int nsects;

    ps2cdvd.state = STAT_INIT;
    new_state = STAT_CHECK_DISC;
    ev = EV_NO_EVENT;
    ps2cdvd.sectoidle = ps2cdvd_spindown;
    ps2cdvd_invalidate_discinfo();

    /* notify we are running */
    up(&ps2cdvd.ack_sem);

#define NEW_STATE(s)	do { new_state = (s); goto set_state; } while (0)

    if (ps2cdvd_lock_interruptible("cdvd thread") != 0)
	goto out;

 set_state:
    spin_lock_irqsave(&ps2cdvd.state_lock, flags);
    if (ps2cdvd.state != new_state) {
	DPRINT(DBG_STATE, "event: %s  state: %s -> %s\n",
	       ps2cdvd_geteventstr(ev),
	       ps2cdvd_getstatestr(ps2cdvd.state),
	       ps2cdvd_getstatestr(new_state));
	ps2cdvd.state = new_state;
	wake_up(&ps2cdvd.statq);
    }
    spin_unlock_irqrestore(&ps2cdvd.state_lock, flags);

    switch (ps2cdvd.state) {
    case STAT_WAIT_DISC:
    case STAT_INVALID_DISC:
	ps2cdvd_invalidate_discinfo();
	if (!ps2cdvd.disc_locked ||
	    (ps2cdvd.state == STAT_INVALID_DISC &&
	     !ps2cdvd_wrong_disc_retry)) {
	    unsigned long flags;

	    spin_lock_irqsave(&cdvd_queue_lock, flags);
	    if (!list_empty(&cdvd_deferred)) {
    		struct list_head *elem, *next;
		DPRINT(DBG_DIAG, "abort all pending request\n");

    		list_for_each_safe(elem, next, &cdvd_deferred) {
		        struct request *req;

			req = list_entry(elem, struct request, queuelist);
			list_del_init(&req->queuelist);
			__blk_end_request_all(req, -EIO);
		}
	    }
	    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	}
	ps2cdvd_unlock();
	ev = ps2cdvd_getevent(ps2cdvd_check_interval * HZ);
	if (ps2cdvd_lock_interruptible("cdvd thread") != 0)
	    goto out;
	switch (ev) {
	case EV_START:
	    NEW_STATE(STAT_CHECK_DISC);
	    break;
	case EV_TIMEOUT:
	    if (ps2cdvdcall_gettype(&disc_type) != 0 ||
		disc_type != SCECdNODISC)
		NEW_STATE(STAT_CHECK_DISC);
	    break;
	case EV_EXIT:
	    goto unlock_out;
	}
	break;

    case STAT_CHECK_DISC:
	ps2cdvd.sectoidle = ps2cdvd_spindown;
	res = checkdisc();
	sum = ps2cdvd_checksum((u_long*)ps2cdvd.labelbuf,
			       2048/sizeof(u_long));
	if (res != DISC_OK) {
	    if (res == DISC_RETRY) {
		ps2cdvd_sleep(HZ);
		NEW_STATE(STAT_CHECK_DISC);
	    }
	    if (ps2cdvd.disc_locked)
		NEW_STATE(STAT_INVALID_DISC);
	    NEW_STATE(STAT_WAIT_DISC);
	}
	if (!ps2cdvd.disc_locked || ps2cdvd.disc_type == SCECdCDDA) {
	    NEW_STATE(STAT_READY);
	}
	if (ps2cdvd.disc_lock_key_valid) {
	    if (ps2cdvd.label_valid &&
		ps2cdvd.disc_lock_key == sum) {
		NEW_STATE(STAT_READY);
	    }
	    NEW_STATE(STAT_INVALID_DISC);
	}
	if (ps2cdvd.label_valid) {
	    ps2cdvd.disc_lock_key = sum;
	    NEW_STATE(STAT_READY);
	}
	NEW_STATE(STAT_INVALID_DISC);
	break;

    case STAT_READY: {
	unsigned long flags;
        struct request *req;
	spin_lock_irqsave(&cdvd_queue_lock, flags);
	if (list_empty(&cdvd_deferred)) {
	    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	    ps2cdvd_unlock();
	    ev = ps2cdvd_getevent(ps2cdvd_check_interval * HZ);
	    if (ps2cdvd_lock_interruptible("cdvd thread") != 0)
		goto out;
	    ps2cdvd.sectoidle -= ps2cdvd_check_interval;
	    switch (ev) {
	    case EV_START:
		break;
	    case EV_TIMEOUT:
	        spin_lock_irqsave(&cdvd_queue_lock, flags);
		if (ps2cdvd_spindown != 0 && ps2cdvd.sectoidle <= 0 && list_empty(&cdvd_deferred)
		    && ps2cdvd.stream_start == 0) {
	          spin_unlock_irqrestore(&cdvd_queue_lock, flags);
		  NEW_STATE(STAT_IDLE);
                } else {
	          spin_unlock_irqrestore(&cdvd_queue_lock, flags);
		}
		if (ps2cdvdcall_trayreq(SCECdTrayCheck, &traycount) != 0 ||
		    traycount != 0 ||
		    ps2cdvd.traycount != 0) {
		    ps2cdvd.traycount = 0;
		    DPRINT(DBG_INFO, "tray was opened\n");
		    NEW_STATE(STAT_CHECK_DISC);
		}
		break;
	    case EV_EXIT:
		goto unlock_out;
	    }
	    NEW_STATE(STAT_READY);
	} else {
	    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	}

	if (ps2cdvdcall_trayreq(SCECdTrayCheck, &traycount) != 0 ||
	    traycount != 0 ||
	    ps2cdvd.traycount != 0) {
	    DPRINT(DBG_INFO, "tray was opened\n");
	    ps2cdvd.traycount = 0;
	    NEW_STATE(STAT_CHECK_DISC);
	}

	ps2cdvd.sectoidle = ps2cdvd_spindown;
	if (ps2cdvd_check_cache())
	    NEW_STATE(STAT_READY);

	spin_lock_irqsave(&cdvd_queue_lock, flags);
	req = list_first_entry(&cdvd_deferred, struct request, queuelist);
	sn = blk_rq_pos(req)/4;
	spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	nsects = ps2cdvd_databuf_size;

    retry:
	DPRINT(DBG_READ, "read: sec=%ld  n=%d  buf=%p %s\n",
	       sn * 4, ps2cdvd_databuf_size, ps2cdvd.databuf,
	       nsects != ps2cdvd_databuf_size ? "(retry)" : "");
	ps2cdvd.databuf_nsects = 0;
	if (ps2cdvd.disc_type == SCECdDVDV) {
		if (ps2cdvdcall_read_dvd(sn, nsects, ps2cdvd.databuf,
				     &ps2cdvd.data_mode) != 0) {
		    NEW_STATE(STAT_CHECK_DISC);
		}
	} else {
		if (ps2cdvdcall_read(sn, nsects, ps2cdvd.databuf,
				     &ps2cdvd.data_mode) != 0) {
		    NEW_STATE(STAT_CHECK_DISC);
		}
	}
	res = ps2cdvdcall_geterror();
	if (res == SCECdErNO) {
	    ps2cdvd.databuf_addr = sn;
	    ps2cdvd.databuf_nsects = nsects;
	    ps2cdvd_check_cache();

	    if (ps2sif_iswaiting(ps2cdvd.lock)) {
		ps2cdvd_unlock();
		schedule();
		if (ps2cdvd_lock_interruptible("cdvd thread") != 0)
		    goto out;
	    }
	    NEW_STATE(STAT_READY);
	}
	if ((res == SCECdErEOM ||
	     res == SCECdErREAD ||
	     res == SCECdErILI ||
	     res == SCECdErIPI ||
	     res == SCECdErABRT ||
	     res == 0xfd /* XXX, should be defined in libcdvd.h */ ||
	     res == 0x38 /* XXX, should be defined in libcdvd.h */) &&
	    nsects != 1) {
	    /* you got an error and you have not retried */
	    DPRINT(DBG_DIAG, "error: %s, code=0x%02x (retry...)\n",
		   ps2cdvd_geterrorstr(res), res);
	    spin_lock_irqsave(&cdvd_queue_lock, flags);
	    req = list_first_entry(&cdvd_deferred, struct request, queuelist);
	    sn = blk_rq_pos(req)/4;
	    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	    nsects = 1;
	    goto retry;
	}
	DPRINT(DBG_DIAG, "error: %s, code=0x%02x\n",
	       ps2cdvd_geterrorstr(res), res);
	spin_lock_irqsave(&cdvd_queue_lock, flags);
	req = list_first_entry(&cdvd_deferred, struct request, queuelist);
	list_del_init(&req->queuelist);
	__blk_end_request_all(req, -EIO);
	spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	NEW_STATE(STAT_CHECK_DISC);
	break;
    }

    case STAT_IDLE: {
	unsigned long flags;

	if (ps2cdvd.disc_type != INVALID_DISCTYPE)
		spindown();
	ps2cdvd_unlock();
	ev = ps2cdvd_getevent(ps2cdvd_check_interval * HZ);
	if (ps2cdvd_lock_interruptible("cdvd thread") != 0)
	    goto out;
	switch (ev) {
	case EV_START:
	    NEW_STATE(STAT_CHECK_DISC);
	    break;
	case EV_EXIT:
	    goto unlock_out;
	}
	/*
	 * XXX, fail safe
	 * EV_START might be lost
	 */
        spin_lock_irqsave(&cdvd_queue_lock, flags);
	if (!list_empty(&cdvd_deferred)) {
	    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	    NEW_STATE(STAT_CHECK_DISC);
	} else {
	    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
	}
	if (ps2cdvdcall_trayreq(SCECdTrayCheck, &traycount) != 0 ||
	    traycount != 0 ||
	    ps2cdvd.traycount != 0) {
	    ps2cdvd.traycount = 0;
	    DPRINT(DBG_INFO, "tray was opened\n");
	    NEW_STATE(STAT_CHECK_DISC);
	}
	break;
    }

    case STAT_ERROR:
	ps2cdvd_unlock();
	ev = ps2cdvd_getevent(ps2cdvd_check_interval * HZ);
	if (ps2cdvd_lock_interruptible("cdvd thread") != 0)
	    goto out;
	if (ev == EV_EXIT)
	    goto unlock_out;
	break;
    }

    goto set_state;

 unlock_out:
    ps2cdvd_unlock();

 out: {
    unsigned long flags;

    DPRINT(DBG_INFO, "the thread is exiting...\n");

    spin_lock_irqsave(&cdvd_queue_lock, flags);
    if (!list_empty(&cdvd_deferred)) {
	struct list_head *elem, *next;
	DPRINT(DBG_DIAG, "abort all pending request\n");

	list_for_each_safe(elem, next, &cdvd_deferred) {
	        struct request *req;

		req = list_entry(elem, struct request, queuelist);
		list_del_init(&req->queuelist);
		__blk_end_request_all(req, -EIO);
	}
    }
    spin_unlock_irqrestore(&cdvd_queue_lock, flags);
    }

    return (0);
}

static int
checkdisc()
{
    int res;
    int media, traycount, media_mode, disc_type;
    int read_toc, read_label;

    ps2cdvd_invalidate_discinfo();

    /*
     *  clear tray count
     */
    if (ps2cdvdcall_trayreq(SCECdTrayCheck, &traycount) != 0) {
	DPRINT(DBG_DIAG, "trayreq() failed\n");
	res = DISC_ERROR;
	goto error_out;
    }

    /*
     *  check disc type
     */
    if (ps2cdvdcall_gettype(&disc_type) != 0) {
	/* error */
	DPRINT(DBG_DIAG, "gettype() failed\n");
	res = DISC_ERROR;
	goto error_out;
    }
    ps2cdvd.disc_type = disc_type;
    DPRINT(DBG_INFO, "ps2cdvdcall_gettype()='%s', %d\n",
	   ps2cdvd_getdisctypestr(ps2cdvd.disc_type),
	   ps2cdvd.disc_type);

    read_toc = 1;
    read_label = 1;
    media_mode = SCECdCD;
    ps2cdvd.label_mode.spindlctrl = SCECdSpinX4;
    switch (ps2cdvd.disc_type) {
    case SCECdPS2CDDA:		/* PS2 CD DA */
    case SCECdPS2CD:		/* PS2 CD */
	break;	/* go ahead */
    case SCECdPSCDDA:		/* PS CD DA */
    case SCECdPSCD:		/* PS CD */
	ps2cdvd.label_mode.spindlctrl = SCECdSpinX1;
	break;	/* go ahead */
    case SCECdCDDA:		/* CD DA */
	read_label = 0;
	break;	/* go ahead */
    case SCECdPS2DVD:		/* PS2 DVD */
    case SCECdDVDV:		/* DVD video */
	media_mode = SCECdDVD;
	read_toc = 0;
	break;	/* go ahead */
    case SCECdDETCTDVDD:	/* DVD-dual detecting */
    case SCECdDETCTDVDS:	/* DVD-single detecting */
    case SCECdDETCTCD:		/* CD detecting */
    case SCECdDETCT:		/* detecting */
	res = DISC_RETRY;
	goto error_out;
    case SCECdNODISC:		/* no disc */
	res = DISC_NODISC;
	goto error_out;
    case SCECdIllgalMedia:	/* illegal media */
    case SCECdUNKNOWN:		/* unknown */
	printk(KERN_CRIT "ps2cdvd: illegal media\n");
	res = DISC_NODISC;
	goto error_out;
    default:
	printk(KERN_CRIT "ps2cdvd: unknown disc type 0x%02x\n",
	       ps2cdvd.disc_type);
	res = DISC_NODISC;
	goto error_out;
    }

    /*
     *  get ready
     */
    DPRINT(DBG_INFO, "getting ready...\n");
    if (ps2cdvdcall_ready(0 /* block */) != SCECdComplete) {
	DPRINT(DBG_DIAG, "ready() failed\n");
	res = DISC_ERROR;
	goto error_out;
    }

    /*
     *  set media mode
     */
    DPRINT(DBG_INFO, "media mode %s\n", media_mode == SCECdCD ? "CD" : "DVD");
    if (ps2cdvdcall_mmode(media_mode) != 1) {
	DPRINT(DBG_DIAG, "mmode() failed\n");
	res = DISC_ERROR;
	goto error_out;
    }

    /*
     *  read TOC
     */
    if (read_toc) {
	struct ps2cdvd_tocentry *toc;
	int toclen = sizeof(ps2cdvd.tocbuf);
	memset(ps2cdvd.tocbuf, 0, toclen);
	if (ps2cdvdcall_gettoc(ps2cdvd.tocbuf, &toclen, &media) != 0) {
	    DPRINT(DBG_DIAG, "gettoc() failed\n");
	    res = DISC_ERROR;
	    goto error_out;
	}

	ps2cdvd.toc_valid = 1;
	DPRINT(DBG_DLOCK, "toc is valid\n");
	toc = (struct ps2cdvd_tocentry *)ps2cdvd.tocbuf;
	ps2cdvd.leadout_start = msftolba(decode_bcd(toc[2].abs_msf[0]),
					 decode_bcd(toc[2].abs_msf[1]),
					 decode_bcd(toc[2].abs_msf[2]));
#ifdef PS2CDVD_DEBUG
	if (ps2cdvd_debug & DBG_INFO) {
	    if (media == 0) {
		ps2cdvd_tocdump(DBG_LOG_LEVEL "ps2cdvd: ",
				(struct ps2cdvd_tocentry *)ps2cdvd.tocbuf);
	    } else {
		/*
		 * we have no interrest in DVD Physical format information
		   ps2cdvd_hexdump(ps2cdvd.tocbuf, toclen);
		 */
	    }
	}
#endif
    }

    /*
     *  read label
     */
    if (read_label) {
	if (ps2cdvd.disc_type == SCECdDVDV) {
		if (ps2cdvdcall_read_dvd(16, 1, ps2cdvd.labelbuf, &ps2cdvd.label_mode)!=0||
		    ps2cdvdcall_geterror() != SCECdErNO) {
		    DPRINT(DBG_DIAG, "read() failed\n");
		    res = DISC_ERROR;
		    goto error_out;
		}
	} else {
		if (ps2cdvdcall_read(16, 1, ps2cdvd.labelbuf, &ps2cdvd.label_mode)!=0||
		    ps2cdvdcall_geterror() != SCECdErNO) {
		    DPRINT(DBG_DIAG, "read() failed\n");
		    res = DISC_ERROR;
		    goto error_out;
		}
	}
	memcpy(ps2cdvd.labelbuf, ps2cdvd.labelbuf + DVD_DATA_OFFSET, DATA_SECT_SIZE);
	ps2cdvd.label_valid = 1;
	DPRINT(DBG_DLOCK, "label is valid\n");
#ifdef PS2CDVD_DEBUG
	{
	    struct iso_primary_descriptor *label;
	    label = (struct iso_primary_descriptor*)ps2cdvd.labelbuf;

	    if (ps2cdvd_debug & DBG_INFO) {
		printk(DBG_LOG_LEVEL "ps2cdvd: ");
		ps2cdvd_print_isofsstr(label->system_id,
				       sizeof(label->system_id));
		ps2cdvd_print_isofsstr(label->volume_id,
				       sizeof(label->volume_id));
		ps2cdvd_print_isofsstr(label->volume_set_id,
				       sizeof(label->volume_set_id));
		ps2cdvd_print_isofsstr(label->publisher_id,
				       sizeof(label->publisher_id));
		ps2cdvd_print_isofsstr(label->application_id,
				       sizeof(label->application_id));
		printk("\n");

		/* ps2cdvd_hexdump(DBG_LOG_LEVEL "ps2cdvd: ", ps2cdvd.labelbuf,
		   2048);
		 */
	    }
	}
#endif
    }

    /*
     *  check tray count
     */
    if (ps2cdvdcall_trayreq(SCECdTrayCheck, &traycount) != 0) {
	DPRINT(DBG_DIAG, "trayreq() failed\n");
	res = DISC_ERROR;
	goto error_out;
    }
    if (traycount != 0) {
	DPRINT(DBG_DIAG, "tray count != 0 (%d)\n", traycount);
	res = DISC_RETRY;
	goto error_out;
    }

    return (DISC_OK);

 error_out:
    ps2cdvd_invalidate_discinfo();

    return (res);
}

static int
spindown(void)
{
    struct sceCdRMode mode;

    switch (ps2cdvd.disc_type) {
    case SCECdPS2CDDA:		/* PS2 CD DA */
    case SCECdPS2CD:		/* PS2 CD */
    case SCECdPSCDDA:		/* PS CD DA */
    case SCECdPSCD:		/* PS CD */
    case SCECdPS2DVD:		/* PS2 DVD */
    case SCECdIllgalMedia:	/* illegal media */
    case SCECdUNKNOWN:		/* unknown */
	DPRINT(DBG_INFO, "spindown: data\n");
	mode = ps2cdvd.data_mode;
	mode.spindlctrl = SCECdSpinX2;
	if (ps2cdvdcall_read(16, 1, ps2cdvd.databuf, &mode) != 0)
	    DPRINT(DBG_DIAG, "spindown: data failed\n");
	ps2cdvd_invalidate_discinfo();
	break;

    case SCECdDVDV:		/* DVD video */
	DPRINT(DBG_INFO, "spindown: data\n");
	mode = ps2cdvd.data_mode;
	mode.spindlctrl = SCECdSpinX2;
	if (ps2cdvdcall_read_dvd(16, 1, ps2cdvd.databuf, &mode) != 0)
	    DPRINT(DBG_DIAG, "spindown: data failed\n");
	ps2cdvd_invalidate_discinfo();
	break;

    case SCECdCDDA:		/* CD DA */
	DPRINT(DBG_INFO, "spindown: CD-DA\n");
	mode = ps2cdvd.cdda_mode;
	mode.spindlctrl = SCECdSpinX2;
	if (ps2cdvdcall_read(16, 1, ps2cdvd.databuf, &mode))
	    DPRINT(DBG_DIAG, "spindown: CD-DA failed\n");
	ps2cdvd_invalidate_discinfo();
	break;

    case SCECdNODISC:		/* no disc */
	ps2cdvd_invalidate_discinfo();
	break;

    case INVALID_DISCTYPE:
    default:
	/* nothing to do */
	break;
    }

    return (0);
}

int
ps2cdvd_reset(struct cdrom_device_info *cdi)
{

    DPRINT(DBG_INFO, "reset\n");

    return (ps2cdvd_command(EV_RESET));
}

static struct block_device_operations ps2cdvd_bdops =
{
	owner:			THIS_MODULE,
	open:			ps2cdvd_bdops_open,
	release:		ps2cdvd_bdops_release,
	media_changed:		ps2cdvd_bdops_mediachanged,
	ioctl:			ps2cdvd_bdops_ioctl,
};

static int
ps2cdvd_bdops_open(struct block_device *bdev, fmode_t mode)
{

	switch (MINOR(bdev->bd_inode->i_rdev)) {
#if 0
	case 255:
		filp->f_op = &ps2cdvd_altdev_fops; // TBD: check and test
		break;
#endif
	default:
		return cdrom_open(&ps2cdvd_info, bdev, mode);
		break;
	}
#if 0 // TBD: Check and test
	if (filp->f_op && filp->f_op->open)
		return filp->f_op->open(inode,filp);
#endif

	return 0;
}

static void ps2cdvd_bdops_release(struct gendisk *disk, fmode_t mode)
{
	cdrom_release(&ps2cdvd_info, mode);
}

static int ps2cdvd_bdops_mediachanged(struct gendisk *disk)
{
	return cdrom_media_changed(&ps2cdvd_info);
}

static int ps2cdvd_bdops_ioctl(struct block_device *bdev, fmode_t mode, unsigned cmd, unsigned long arg)
{
	/* TBD: Call ps2cdvd_dev_ioctl() also. */
	return cdrom_ioctl(&ps2cdvd_info, bdev, mode, cmd, arg);
}

static int ps2cdvd_initialized;
#define PS2CDVD_INIT_BLKDEV	0x0001
#define PS2CDVD_INIT_CDROM	0x0002
#define PS2CDVD_INIT_IOPSIDE	0x0004
#define PS2CDVD_INIT_LABELBUF	0x0008
#define PS2CDVD_INIT_DATABUF	0x0010
#define PS2CDVD_INIT_THREAD	0x0020

int __init ps2cdvd_init(void)
{
	int res;

	/*
	 * initialize variables
	 */
	init_waitqueue_head(&ps2cdvd.statq);
	spin_lock_init(&ps2cdvd.state_lock);
	sema_init(&ps2cdvd.ack_sem, 0);
	sema_init(&ps2cdvd.wait_sem, 0);
	sema_init(&ps2cdvd.command_sem, 1);
	ps2cdvd.ievent = EV_NO_EVENT;
	ps2cdvd.event = EV_NO_EVENT;
	spin_lock_init(&ps2cdvd.ievent_lock);

	/*
	 * CD/DVD SBIOS lock
	 */
	DPRINT(DBG_VERBOSE, "init: get lock\n");
	if ((ps2cdvd.lock = ps2sif_getlock(PS2LOCK_CDVD)) == NULL) {
		printk(KERN_ERR "ps2cdvd: Can't get lock\n");
		return (-1);
	}

	/*
	 * allocate buffer
	 */
	DPRINT(DBG_VERBOSE, "init: allocate disklabel buffer\n");
	ps2cdvd.labelbuf = kmalloc(DVD_DATA_SECT_SIZE, GFP_KERNEL);
	if (ps2cdvd.labelbuf == NULL) {
		printk(KERN_ERR "ps2cdvd: Can't allocate buffer\n");
		ps2cdvd_cleanup();
		return (-1);
	}
	ps2cdvd_initialized |= PS2CDVD_INIT_LABELBUF;

	DPRINT(DBG_VERBOSE, "allocate buffer\n");
	ps2cdvd.databufx = kmalloc(ps2cdvd_databuf_size * MAX_AUDIO_SECT_SIZE +
				   BUFFER_ALIGNMENT, GFP_KERNEL);
	if (ps2cdvd.databufx == NULL) {
		printk(KERN_ERR "ps2cdvd: Can't allocate buffer\n");
		ps2cdvd_cleanup();
		return (-1);
	}
	ps2cdvd.databuf = (void *) ALIGN((unsigned long) ps2cdvd.databufx, BUFFER_ALIGNMENT);
	ps2cdvd_initialized |= PS2CDVD_INIT_DATABUF;

	/*
	 * initialize CD/DVD SBIOS
	 */
	DPRINT(DBG_VERBOSE, "init: call sbios\n");

	if (ps2cdvd_lock_interruptible("cdvd init") != 0)
		return (-1);
	res = ps2cdvdcall_init();
	if (res) {
		printk(KERN_ERR "ps2cdvd: Can't initialize CD/DVD-ROM subsystem\n");
		ps2cdvd_unlock();
		ps2cdvd_cleanup();
		return (-1);
	}
#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (0x0201 <= sbios(SB_GETVER, NULL))
		ps2cdvdcall_reset();
#else
	ps2cdvdcall_reset();
#endif
	ps2cdvd_unlock();
	ps2cdvd_initialized |= PS2CDVD_INIT_IOPSIDE;

	/*
	 * start control thread
	 */
	ps2cdvd.cdvd_task = kthread_run(ps2cdvd_thread, &ps2cdvd, "ps2cdvd");
	if (ps2cdvd.cdvd_task == NULL) {
		printk(KERN_ERR "ps2cdvd: can't start thread\n");
		ps2cdvd_cleanup();
		return (-1);
	}
	/* wait for the thread to start */
	down(&ps2cdvd.ack_sem);
	ps2cdvd_initialized |= PS2CDVD_INIT_THREAD;

	/*
	 * register block device
	 */
	DPRINT(DBG_VERBOSE, "init: register block device\n");
	if ((res = register_blkdev(MAJOR_NR, "ps2cdvd")) < 0) {
		printk(KERN_ERR "ps2cdvd: Unable to get major %d for PS2 CD/DVD-ROM\n",
		       MAJOR_NR);
		ps2cdvd_cleanup();
		return (-1);
	}

	if (MAJOR_NR == 0) MAJOR_NR = res;
	ps2cdvd_initialized |= PS2CDVD_INIT_BLKDEV;

	disk = alloc_disk(1);
	if (!disk) {
		printk(KERN_ERR "ps2cdvd: Cannot alloc disk\n");
		ps2cdvd_cleanup();
		return (-1);
	}
	disk->major = MAJOR_NR;
	disk->first_minor = 0;
	disk->minors = 256;
	strcpy(disk->disk_name, "ps2cdvd");

	/*
	 * register cdrom device
	 */
	DPRINT(DBG_VERBOSE, "init: register cdrom\n");
	ps2cdvd.cdvd_queue = blk_init_queue(ps2cdvd_request, &cdvd_lock);
       	if (register_cdrom(&ps2cdvd_info) != 0) {
		printk(KERN_ERR "ps2cdvd: Cannot init queue\n");
		ps2cdvd_cleanup();
		return (-1);
	}
	disk->fops = &ps2cdvd_bdops;
	blk_queue_logical_block_size(ps2cdvd.cdvd_queue, DATA_SECT_SIZE);
	/* Maximum one segment. */
	blk_queue_max_segments(ps2cdvd.cdvd_queue, 1);
	blk_queue_max_segment_size(ps2cdvd.cdvd_queue, ps2cdvd_databuf_size * DATA_SECT_SIZE);
	disk->queue = ps2cdvd.cdvd_queue;

	ps2cdvd_initialized |= PS2CDVD_INIT_CDROM;

	add_disk(disk);

	printk(KERN_INFO "PlayStation 2 CD/DVD-ROM driver\n");

	return (0);
}

void
ps2cdvd_cleanup()
{

	DPRINT(DBG_VERBOSE, "cleanup\n");

	if (ps2cdvd_initialized & PS2CDVD_INIT_THREAD) {
		DPRINT(DBG_VERBOSE, "stop thread\n");
		kthread_stop(ps2cdvd.cdvd_task);
	}

	ps2cdvd_lock("cdvd_cleanup");

	if (ps2cdvd_initialized & PS2CDVD_INIT_LABELBUF) {
		DPRINT(DBG_VERBOSE, "free labelbuf %p\n", ps2cdvd.labelbuf);
		kfree(ps2cdvd.labelbuf);
	}

	if ((ps2cdvd_initialized & PS2CDVD_INIT_IOPSIDE) &&
	    (ps2cdvd_initialized & PS2CDVD_INIT_DATABUF)) {
		spindown();
	}

	if (ps2cdvd_initialized & PS2CDVD_INIT_DATABUF) {
		DPRINT(DBG_VERBOSE, "free databuf %p\n", ps2cdvd.databufx);
		kfree(ps2cdvd.databufx);
	}

	if (ps2cdvd_initialized & PS2CDVD_INIT_BLKDEV) {
		DPRINT(DBG_VERBOSE, "unregister block device\n");
		unregister_blkdev(MAJOR_NR, "ps2cdvd");
	}

	if (ps2cdvd_initialized & PS2CDVD_INIT_CDROM) {
		DPRINT(DBG_VERBOSE, "unregister cdrom\n");
		unregister_cdrom(&ps2cdvd_info);
	}

	ps2cdvd_initialized = 0;
	ps2cdvd_unlock();

	if (disk) {
		del_gendisk(disk);
		disk = NULL;
	}

	if (ps2cdvd.cdvd_queue) {
		blk_cleanup_queue(ps2cdvd.cdvd_queue);
		ps2cdvd.cdvd_queue = NULL;
	}
}

module_init(ps2cdvd_init);
module_exit(ps2cdvd_cleanup);

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 CD/DVD driver");
MODULE_LICENSE("GPL");
