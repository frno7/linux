/*
 *  PlayStation 2 SIF lock
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/mach-ps2/siflock.h>

/*
 * debug stuff
 */
#define PS2SIFLOCK_DEBUG
#ifdef PS2SIFLOCK_DEBUG
#define DBG_LOCK	(1<< 0)

#define DBG_LOG_LEVEL	KERN_CRIT

#define DPRINT(l, fmt, args...) \
	if ((l)->flags & PS2LOCK_FLAG_DEBUG) \
		printk(DBG_LOG_LEVEL "ps2siflock: " fmt, ## args)
#define DPRINTK(l, fmt, args...) \
	if ((l)->flags & PS2LOCK_FLAG_DEBUG) \
		printk(fmt, ## args)
#else
#define DPRINT(mask, fmt, args...)	do {} while (0)
#define DPRINTK(mask, fmt, args...)	do {} while (0)
#endif

EXPORT_SYMBOL(ps2sif_lockinit);
EXPORT_SYMBOL(ps2sif_lockqueueinit);
EXPORT_SYMBOL(__ps2sif_lock);
EXPORT_SYMBOL(ps2sif_unlock);
EXPORT_SYMBOL(ps2sif_unlock_interruptible);
EXPORT_SYMBOL(ps2sif_lowlevel_lock);
EXPORT_SYMBOL(ps2sif_lowlevel_unlock);
EXPORT_SYMBOL(ps2sif_iswaiting);
EXPORT_SYMBOL(ps2sif_getlock);
EXPORT_SYMBOL(ps2sif_havelock);
EXPORT_SYMBOL(ps2sif_setlockflags);
EXPORT_SYMBOL(ps2sif_getlockflags);

static ps2sif_lock_t lock_cdvd;
static ps2sif_lock_t lock_sound;
static ps2sif_lock_t lock_pad;
static ps2sif_lock_t lock_mc;
static ps2sif_lock_t lock_remocon;

struct ps2siflock {
	volatile int locked;
	volatile pid_t owner;
	volatile void *lowlevel_owner;
	volatile int waiting;
	char *ownername;
	struct ps2siflock_queue low_level_waitq;
	wait_queue_head_t waitq;
	spinlock_t spinlock;
	int flags;
};

static ps2sif_lock_t *locks[] = {
  [PS2LOCK_CDVD]	= &lock_cdvd,
  [PS2LOCK_SOUND]	= &lock_sound,
  [PS2LOCK_PAD]		= &lock_pad,
  [PS2LOCK_MC]		= &lock_mc,
  [PS2LOCK_RTC]		= &lock_cdvd,
  [PS2LOCK_POWER]	= &lock_cdvd,
  [PS2LOCK_REMOCON]	= &lock_remocon,
  [PS2LOCK_SYSCONF]	= &lock_cdvd,
};

/*
 * utility functions
 */
static inline void
qadd(ps2sif_lock_queue_t *q, ps2sif_lock_queue_t *i)
{
	if (q->prev)
		q->prev->next = i;
	i->prev = q->prev;
	q->prev = i;
	i->next = q;
}

static inline ps2sif_lock_queue_t *
qpop(ps2sif_lock_queue_t *q)
{
	ps2sif_lock_queue_t *i;
	if (q->next == q) {
		return NULL;
	}
	i = q->next;
	q->next = q->next->next;
	q->next->prev = q;
	i->next = NULL; /* failsafe */
	i->prev = NULL; /* failsafe */

	return (i);
}

/*
 * functions
 */
ps2sif_lock_t *
ps2sif_getlock(int lockid)
{
	int i;
	static int initialized = 0;

	if (!initialized) {
		initialized = 1;
		/* XXX, some items in locks will be initialized twice */
		for (i = 0; i < sizeof(locks)/sizeof(*locks); i++)
			ps2sif_lockinit(locks[i]);
	}

	if (lockid < 0 || sizeof(locks)/sizeof(*locks) <= lockid)
		return (NULL);
	else
		return (locks[lockid]);
}

void
ps2sif_lockinit(ps2sif_lock_t *l)
{
	l->locked = 0;
	l->owner = 0;
	l->lowlevel_owner = NULL;
	init_waitqueue_head(&l->waitq);
	l->waiting = 0;
	l->ownername = NULL;
	l->low_level_waitq.prev = &l->low_level_waitq;
	l->low_level_waitq.next = &l->low_level_waitq;
	spin_lock_init(&l->spinlock);
}

void
ps2sif_lockqueueinit(ps2sif_lock_queue_t *q)
{
	q->prev = NULL;
	q->next = NULL;
	q->routine = NULL;
}

int
__ps2sif_lock(ps2sif_lock_t *l, char *name, long state)
{
	int res;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);

	spin_lock_irqsave(&l->spinlock, flags);
	add_wait_queue(&l->waitq, &wait);
	res = -ERESTARTSYS;
	for ( ; ; ) {
		if (!l->locked || l->owner == current->pid) {
			if (l->locked++ == 0) {
				DPRINT(l, "  LOCK: pid=%d\n",
				       current->pid);
				l->owner = current->pid;
				l->ownername = name;
			} else {
				DPRINT(l, "  lock: pid=%d count=%d\n",
				       current->pid, l->locked);
			}
			res = 0;
			break;
		}
		l->waiting++;
		DPRINT(l, "sleep: pid=%d\n", current->pid);
		set_current_state(state);
		spin_unlock_irq(&l->spinlock);
		schedule();
		spin_lock_irq(&l->spinlock);
		DPRINT(l, "waken up: pid=%d\n", current->pid);
		l->waiting--;
		if(state == TASK_INTERRUPTIBLE && signal_pending(current)) {
			res = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&l->waitq, &wait);
	spin_unlock_irqrestore(&l->spinlock, flags);

	return (res);
}

int
ps2sif_havelock(ps2sif_lock_t *l)
{
	int res;
	unsigned long flags;

	spin_lock_irqsave(&l->spinlock, flags);
	res = (l->locked && l->owner == current->pid);
	spin_unlock_irqrestore(&l->spinlock, flags);

	return (res);
}

static inline void
clear_lock(ps2sif_lock_t *l)
{
	l->locked = 0;
	l->owner = 0;
	l->ownername = NULL;
	l->lowlevel_owner = NULL;
}

static inline void
give_lowlevel_lock(ps2sif_lock_t *l, ps2sif_lock_queue_t *q, unsigned long flags)
{
	int t;

	if (l->locked++ == 0) {
		DPRINT(l, "  LOCK: qi=%p\n", q);
	} else {
		DPRINT(l, "  lock: qi=%p  count=%d\n", q, l->locked);
	}
	l->owner = -1;
	l->lowlevel_owner = q;
	l->ownername = q->name;
	spin_unlock_irqrestore(&l->spinlock, flags);
	t = (q->routine != NULL && q->routine(q->arg) == -1);
	spin_lock_irqsave(&l->spinlock, flags);
	if (t && --l->locked <= 0)
		clear_lock(l);
}

void
ps2sif_unlock(ps2sif_lock_t *l)
{
	unsigned long flags;
	ps2sif_lock_queue_t *qi;

	spin_lock_irqsave(&l->spinlock, flags);
	if (l->owner != current->pid) {
		spin_unlock_irqrestore(&l->spinlock, flags);
		printk(KERN_CRIT "ps2cdvd: invalid unlock operation\n");
		return;
	}
	if (--l->locked <= 0) {
		clear_lock(l);
		DPRINT(l, "UNLOCK: pid=%d\n", current->pid);
		while (!l->locked && (qi = qpop(&l->low_level_waitq))) {
			give_lowlevel_lock(l, qi, flags);
		}
		DPRINT(l, "        locked=%d  waiting=%d\n",
		       l->locked, l->waiting);
		if (!l->locked && l->waiting) {
			wake_up(&l->waitq);
			DPRINT(l, "        wake_up\n");
		}
	} else {
		DPRINT(l, "unlock: pid=%d count=%d\n",
		       current->pid, l->locked);
	}
	spin_unlock_irqrestore(&l->spinlock, flags);
}

void
ps2sif_unlock_interruptible(ps2sif_lock_t *l)
{
	unsigned long flags;
	int interrupt;

	ps2sif_unlock(l);
	spin_lock_irqsave(&l->spinlock, flags);
	interrupt = (l->owner != current->pid && ps2sif_iswaiting(l));
	spin_unlock_irqrestore(&l->spinlock, flags);
	if (interrupt)
		schedule();
}

int
ps2sif_lowlevel_lock(ps2sif_lock_t *l, ps2sif_lock_queue_t *qi, int opt)
{
	int res;
	unsigned long flags;

	if (qi == NULL)
		return (-1);

	spin_lock_irqsave(&l->spinlock, flags);
	if (!l->locked || l->lowlevel_owner == qi) {
		give_lowlevel_lock(l, qi, flags);
		res = 0;
	} else {
		if (opt & PS2SIF_LOCK_QUEUING) {
			DPRINT(l, "enqueue: qi=%p\n", qi);
			qadd(&l->low_level_waitq, qi);
		}
		res = -1;
	}
	spin_unlock_irqrestore(&l->spinlock, flags);

	return res;
}

void
ps2sif_lowlevel_unlock(ps2sif_lock_t *l, ps2sif_lock_queue_t *qi)
{
	unsigned long flags;

	if (qi == NULL)
		return;

	spin_lock_irqsave(&l->spinlock, flags);
	if (l->locked && l->lowlevel_owner == qi && l->owner == -1) {
		if (--l->locked <= 0) {
			clear_lock(l);
			DPRINT(l, "UNLOCK: qi=%p\n", qi);
			while (!l->locked &&
			       (qi = qpop(&l->low_level_waitq))) {
				give_lowlevel_lock(l, qi, flags);
			}
			if (!l->locked && l->waiting) {
				DPRINT(l, "wakeup upper level\n");
				wake_up(&l->waitq);
			}
		} else {
			DPRINT(l, "unlock: qi=%p  count=%d\n",
			       qi, l->locked);
		}
	} else {
		printk(KERN_CRIT
		       "ps2sif_lock: low level locking violation\n");
	}
	spin_unlock_irqrestore(&l->spinlock, flags);
}

int
ps2sif_iswaiting(ps2sif_lock_t *l)
{
	return (l->waiting);
}

unsigned long
ps2sif_setlockflags(ps2sif_lock_t *l, unsigned long flags)
{
	unsigned long irq, res;

	spin_lock_irqsave(&l->spinlock, irq);
	res = l->flags;
	l->flags = flags;
	spin_unlock_irqrestore(&l->spinlock, irq);

	return (res);
}

unsigned long
ps2sif_getlockflags(ps2sif_lock_t *l)
{
	return (l->flags);
}

int __init ps2sif_lock_init(void)
{
	return (0);
}

void
ps2sif_lock_cleanup(void)
{
}

module_init(ps2sif_lock_init);
module_exit(ps2sif_lock_cleanup);

MODULE_AUTHOR("Mega Man");
MODULE_DESCRIPTION("SBIOS lock module");
MODULE_LICENSE("GPL");
