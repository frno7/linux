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

#ifndef PS2CDVD_H
#define PS2CDVD_H

#include <linux/cdrom.h>

#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/siflock.h>
#include <asm/mach-ps2/cdrom.h>
#include "libcdvd.h"
#include <asm/mach-ps2/cdvdcall.h>

/*
 * macro defines
 */
#define PS2CDVD_DEBUG
#ifdef PS2CDVD_DEBUG
#define DBG_VERBOSE	(1<< 0)
#define DBG_DIAG	(1<< 1)
#define DBG_READ	(1<< 2)
#define DBG_INFO	(1<< 3)
#define DBG_STATE	(1<< 4)
#define DBG_LOCK	(1<< 5)
#define DBG_DLOCK	(1<< 6)
#define DBG_IOPRPC	(1<< 7)

#define DBG_LOG_LEVEL	KERN_CRIT
#define DBG_DEFAULT_FLAGS DBG_DIAG

#define DPRINT(mask, fmt, args...) \
	if (ps2cdvd_debug & (mask)) printk(DBG_LOG_LEVEL "ps2cdvd: " fmt, ## args)
#define DPRINTK(mask, fmt, args...) \
	if (ps2cdvd_debug & (mask)) printk(fmt, ## args)
#else
#define DBG_DEFAULT_FLAGS 0
#define DPRINT(mask, fmt, args...) do {} while(0)
#define DPRINTK(mask, fmt, args...) do {} while(0)
#endif

#define ARRAYSIZEOF(a)	(sizeof(a)/sizeof(*(a)))
#define MIN(a, b)	((a) < (b) ? (a) : (b))

#define PS2CDVD_CDDA

#define DATA_SECT_SIZE	2048
#define AUDIO_SECT_SIZE	2352
#define MAX_AUDIO_SECT_SIZE	2448

#define SEND_BUSY	0
#define SEND_NOWAIT	1
#define SEND_BLOCK	2

enum {
	EV_NO_EVENT,
	EV_START,
	EV_TIMEOUT,
	EV_EXIT,
	EV_RESET,
};

enum {
	STAT_INIT,
	STAT_WAIT_DISC,
	STAT_INVALID_DISC,
	STAT_CHECK_DISC,
	STAT_READY,
	STAT_ERROR,
	STAT_IDLE,

	/* obsolete */
	STAT_INIT_TRAYSTAT,
	STAT_CHECK_DISCTYPE,
	STAT_INIT_CHECK_READY,
	STAT_SET_MMODE,
	STAT_TOC_READ,
	STAT_LABEL_READ,
	STAT_LABEL_READ_ERROR_CHECK,
	STAT_CHECK_TRAY,
	STAT_READ,
	STAT_READ_EOM_RETRY,
	STAT_READ_ERROR_CHECK,
	STAT_SPINDOWN,
};

/*
 * types
 */
struct ps2cdvd_tocentry {
	unsigned char addr:4;
	unsigned char ctrl:4;
	unsigned char trackno;
	unsigned char indexno;
	unsigned char rel_msf[3];
	unsigned char zero;
	unsigned char abs_msf[3];
};

struct ps2cdvd_ctx {
	volatile int		state;
	spinlock_t		state_lock;
	wait_queue_head_t 	statq;
	ps2sif_lock_t		*lock;
	struct sceCdRMode	label_mode;
	struct sceCdRMode	data_mode;
	struct sceCdRMode	cdda_mode;

	volatile int		traycount;

	volatile int		disc_locked;
	volatile int		disc_lock_key_valid;
	volatile unsigned long	disc_lock_key;
	volatile int		label_valid;
	unsigned char		*labelbuf;
	volatile int		toc_valid;
	unsigned char		tocbuf[1024];

	volatile int		disc_changed;
	volatile int		disc_type;
	long			leadout_start;
	int			stream_start;
	volatile int		sectoidle;

	unsigned char		*databuf;
	unsigned char		*databufx;
	int			databuf_addr;
	int			databuf_nsects;

	struct semaphore	ack_sem;
	struct semaphore	command_sem;
	struct semaphore	wait_sem;
	volatile int		ievent, event;
	spinlock_t		ievent_lock;
	struct task_struct	*cdvd_task;
	struct request_queue	*cdvd_queue;
};

/*
 * function prototypes
 */
char* ps2cdvd_geterrorstr(int);
char* ps2cdvd_getdisctypestr(int);
void ps2cdvd_tocdump(char*, struct ps2cdvd_tocentry *);
void ps2cdvd_hexdump(char*, unsigned char *, int);
unsigned long ps2cdvd_checksum(unsigned long *data, int len);
void ps2cdvd_print_isofsstr(char *str, int len);
char* ps2cdvd_geteventstr(int no);
char* ps2cdvd_getstatestr(int no);

void ps2cdvd_lock(char *);
int ps2cdvd_lock_interruptible(char *);
void ps2cdvd_unlock(void);
int ps2cdvd_reset(struct cdrom_device_info *);
int ps2cdvd_lock_ready(void);
int ps2cdvd_common_ioctl(unsigned int, unsigned long);

/*
 * variables
 */
extern struct ps2cdvd_ctx ps2cdvd;
extern struct file_operations ps2cdvd_altdev_fops;
extern struct cdrom_device_ops ps2cdvd_dops;
extern struct cdrom_device_info ps2cdvd_info;

extern int ps2cdvd_check_interval;
extern int ps2cdvd_databuf_size;
extern int ps2cdvd_debug;
extern int ps2cdvd_immediate_ioerr;
extern int ps2cdvd_major;
extern int ps2cdvd_read_ahead;
extern int ps2cdvd_spindown;
extern int ps2cdvd_wrong_disc_retry;

/*
 * utilities
 */
static inline int decode_bcd(int bcd) {
	return ((bcd >> 4) & 0x0f) * 10 + (bcd & 0x0f);
}

static inline long msftolba(int m, int s, int f)
{
	return (m) * 4500 + (s) * 75 + (f) - 150;
}

static inline void lbatomsf(long lba, int *m, int *s, int *f)
{
	lba += 150;
	*m = (lba / 4500);
	*s = (lba % 4500) / 75;
	*f = (lba % 75);
}

#endif /* PS2CDVD_H */
