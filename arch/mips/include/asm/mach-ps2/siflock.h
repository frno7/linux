/*
 *  PlayStation 2 SIF locks
 *
 *  Copyright (C) 2001      Sony Computer Entertainment Inc.
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

#ifndef __ASM_PS2_SIFLOCK_H
#define __ASM_PS2_SIFLOCK_H

#include <linux/sched.h>

#define PS2SIF_LOCK_QUEUING	(1<<0)

#define PS2LOCK_CDVD	0
#define PS2LOCK_SOUND	1
#define PS2LOCK_PAD	2
#define PS2LOCK_MC	3
#define PS2LOCK_RTC	4
#define PS2LOCK_POWER	5
#define PS2LOCK_REMOCON	6
#define PS2LOCK_SYSCONF	7

#define PS2LOCK_FLAG_DEBUG	(1<<0)

typedef struct ps2siflock_queue {
	struct ps2siflock_queue *prev;
	struct ps2siflock_queue *next;
	int (*routine)(void*);
	void *arg;
	char *name;
} ps2sif_lock_queue_t;

struct ps2siflock;
typedef struct ps2siflock ps2sif_lock_t;

ps2sif_lock_t *ps2sif_getlock(int);
void ps2sif_lockinit(ps2sif_lock_t *l);
void ps2sif_lockqueueinit(ps2sif_lock_queue_t *q);
int __ps2sif_lock(ps2sif_lock_t *l, char*, long state);
void ps2sif_unlock(ps2sif_lock_t *l);
void ps2sif_unlock_interruptible(ps2sif_lock_t *l);
int ps2sif_lowlevel_lock(ps2sif_lock_t *, ps2sif_lock_queue_t *, int);
void ps2sif_lowlevel_unlock(ps2sif_lock_t *l, ps2sif_lock_queue_t *);
int ps2sif_iswaiting(ps2sif_lock_t *l);
int ps2sif_havelock(ps2sif_lock_t *l);
unsigned long ps2sif_setlockflags(ps2sif_lock_t *l, unsigned long);
unsigned long ps2sif_getlockflags(ps2sif_lock_t *l);

#define ps2sif_lock(l, n)		\
	((void)__ps2sif_lock(l, n, TASK_UNINTERRUPTIBLE))
#define ps2sif_lock_interruptible(l, n)	\
	__ps2sif_lock(l, n, TASK_INTERRUPTIBLE)
#define __ps2sif_str2(x) #x
#define __ps2sif_str(x) __ps2sif_str2(x)
#define ps2sif_assertlock(l, msg) \
	do { if (!ps2sif_havelock(l)) \
		panic(__BASE_FILE__ "(" __ps2sif_str(__LINE__) \
			"): no lock: " msg);\
	} while (0)

#endif /* __ASM_PS2_SIFLOCK_H */
