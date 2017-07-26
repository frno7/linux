/*
 *  PlayStation 2 Sound driver
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
#include <linux/interrupt.h>
#include <linux/soundcard.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>

#include <asm/io.h>
#include <asm/addrspace.h>
#include <asm/uaccess.h>

#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sifutil.h>
#include <asm/mach-ps2/siflock.h>
#include <asm/mach-ps2/sound.h>
#include <asm/mach-ps2/bootinfo.h>

#include "../../sound/oss/sound_config.h"
#include "iopmem.h"

#define PS2SD_DMA_ADDR_CHECK
#define PS2SD_USE_THREAD

#include "sd.h"
#include "sdmacro.h"
#include "sdcall.h"

/*
 * macro defines
 */
#define PS2SD_INIT_REGDEV	(1 <<  0)
#define PS2SD_INIT_UNIT		(1 <<  1)
#define PS2SD_INIT_IOP		(1 <<  3)
#define PS2SD_INIT_DMACALLBACK  (1 <<  4)
#define PS2SD_INIT_IOPZERO    	(1 <<  5)
#define PS2SD_INIT_REGMIXERDEV	(1 <<  6)
#define PS2SD_INIT_TIMER	(1 <<  7)
#define PS2SD_INIT_BUFFERALLOC	(1 <<  8)
#define PS2SD_INIT_THREAD	(1 <<  9)

#define DEVICE_NAME	"PS2 Sound"

#define BUFUNIT	1024		/* don't change this */
#define INTRSIZE 512		/* don't change this */
#define CNVBUFSIZE (BUFUNIT*2)
#define INTBUFSIZE BUFUNIT	/* don't change this */
#define SPU2SPEED 48000		/* 48KHz */
#define SPU2FMT AFMT_S16_LE
#define SUPPORTEDFMT	(AFMT_S16_LE)

#define UNIT0_FLAGS	0
#ifdef CONFIG_PS2_SD_ALTPCM
#define UNIT1_FLAGS	0
#else
#define UNIT1_FLAGS	(PS2SD_UNIT_NOPCM)
#endif
#define UNIT2_FLAGS	(PS2SD_UNIT_EXCLUSIVE)

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define PS2SD_MINIMUM_BUFSIZE		(BUFUNIT * 2)	/* 2KB */
#define PS2SD_DEFAULT_DMA_BUFSIZE	16	/*  16KB	*/
#define PS2SD_DEFAULT_IOP_BUFSIZE	8	/*   8KB	*/
#define PS2SD_MAX_DMA_BUFSIZE		128	/* 128KB	*/
#define PS2SD_MAX_IOP_BUFSIZE		64	/*  64KB	*/

#define PS2SD_SPU2PCMBUFSIZE	1024

#define PS2SD_FADEINOUT

#define CHECKPOINT(s)	devc->debug_check_point = (s)

#define UNIT(dev)	((MINOR(dev) & 0x00f0) >> 4)

#define SWAP(a, b)	\
    do { \
	__typeof__(a) tmp; \
	tmp = (a); \
	(a) = (b); \
	(b) = tmp; \
    } while (0)

#define CHECKBUFRANGE
#ifdef CHECKBUFRANGE
#define CHECK_BUF_RANGE(startaddr, bufsize, tailaddr, size, count) \
    do { \
	if ((startaddr + bufsize) < (tailaddr + size)) { \
		printk("Out of range: end of buf = 0x%p (start 0x%p + bufsize 0x%x), \
but writing at 0x%p (tail 0x%p + size 0x%x), bufcount = %d\n", \
		       startaddr + bufsize, startaddr ,bufsize, tailaddr + size, \
		       tailaddr, size, count); \
	} \
    } while (0)
#else
#define CHECK_BUF_RANGE(startaddr, bufsize, tailaddr, size) do {} while (0)
#endif

/*
 * data types
 */
enum dmastat {
	DMASTAT_STOP,
	DMASTAT_START,
	DMASTAT_RUNNING,
	DMASTAT_STOPREQ,
	DMASTAT_STOPPING,
	DMASTAT_CANCEL,
	DMASTAT_CLEAR,
	DMASTAT_ERROR,
	DMASTAT_RESET,
};

char *dmastatnames[] = {
	[DMASTAT_STOP]		= "STOP",
	[DMASTAT_START]		= "START",
	[DMASTAT_RUNNING]	= "RUNNING",
	[DMASTAT_STOPREQ]	= "STOPREQ",
	[DMASTAT_STOPPING]	= "STOPPING",
	[DMASTAT_CANCEL]	= "CANCEL",
	[DMASTAT_CLEAR]		= "CLEAR",
	[DMASTAT_ERROR]		= "ERROR",
	[DMASTAT_RESET]		= "RESET",
};

/*
 * function prototypes
 */
static void ps2sd_cleanup(void);

static loff_t ps2sd_llseek(struct file *, loff_t, int);
static ssize_t ps2sd_read(struct file *, char *, size_t, loff_t *);
static ssize_t ps2sd_write(struct file *, const char *, size_t, loff_t *);
static unsigned int ps2sd_poll(struct file *, struct poll_table_struct *);
static int ps2sd_mmap(struct file *, struct vm_area_struct *);
static int ps2sd_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
static int ps2sd_open(struct inode *, struct file *);
static int ps2sd_release(struct inode *, struct file *);
static void ps2sd_setup(void);
static int ps2sd_command_end(struct ps2sd_unit_context *devc);
static void ps2sd_mute(struct ps2sd_unit_context *);
static void ps2sd_unmute(struct ps2sd_unit_context *);

#ifdef PS2SD_USE_THREAD
static int ps2sd_thread(void *);
static int ps2sd_intr(void *, int);
#else
#define ps2sd_intr ps2sd_dmaintr
#endif
static int ps2sd_dmaintr(void* argx, int dmach);

static int adjust_bufsize(int size);
static int alloc_buffer(struct ps2sd_unit_context *devc);
static int free_buffer(struct ps2sd_unit_context *devc);
static int reset_buffer(struct ps2sd_unit_context *devc);
static int start(struct ps2sd_unit_context *devc);
static int stop(struct ps2sd_unit_context *devc);
static int wait_dma_stop(struct ps2sd_unit_context *devc);
static int reset_error(struct ps2sd_unit_context *devc);
static int stop_sequence0(void*);
static void stop_sequence(void*);
static int set_format(struct ps2sd_unit_context *, int, int, int, int);

/*
 * variables
 */
unsigned long ps2sd_debug = 0;
struct ps2sd_module_context ps2sd_mc;
struct ps2iopmem_list ps2sd_unit2_iopmemlist;
struct ps2sd_unit_context ps2sd_units[3];
int ps2sd_nunits = ARRAYSIZEOF(ps2sd_units);
struct ps2sd_mixer_context ps2sd_mixers[1];
int ps2sd_nmixers = ARRAYSIZEOF(ps2sd_mixers);
struct ps2sd_mixer_channel mixer_dummy_channel;
int ps2sd_dmabufsize = PS2SD_DEFAULT_DMA_BUFSIZE * 1024;
int ps2sd_iopbufsize = PS2SD_DEFAULT_IOP_BUFSIZE * 1024;
int ps2sd_max_dmabufsize = PS2SD_MAX_DMA_BUFSIZE * 1024;
int ps2sd_max_iopbufsize = PS2SD_MAX_IOP_BUFSIZE * 1024;
int ps2sd_normal_debug;
#ifdef CONFIG_T10000_DEBUG_HOOK
int ps2sd_debug_hook = 0;
#endif

module_param(ps2sd_debug, ulong, 0);
module_param(ps2sd_dmabufsize, int, 0);
module_param(ps2sd_iopbufsize, int, 0);
module_param(ps2sd_max_dmabufsize, int, 0);
module_param(ps2sd_max_iopbufsize, int, 0);
#ifdef CONFIG_T10000_DEBUG_HOOK
module_param(ps2sd_debug_hook, bool, 0);
#endif

static struct file_operations ps2sd_dsp_fops = {
	owner:		THIS_MODULE,
	llseek:		ps2sd_llseek,
	read:		ps2sd_read,
	write:		ps2sd_write,
	poll:		ps2sd_poll,
	ioctl:		ps2sd_ioctl,
	mmap:		ps2sd_mmap,
	open:		ps2sd_open,
	release:	ps2sd_release,
};

/*
 * function bodies
 */
/*
 * retrieve a value of counter register,
 * which is counting CPU clock.
 *
 * PS2 CPU clock is 294.912MHz.
 * 294.912MHz = 294912000
 *  = 2048 * 3 * 48000
 *  = 2^15 * 9 * 1000
 */
static inline __u32
getcounter(void)
{
        __u32 r;
        asm volatile (
                "mfc0   %0,$9;"
                : "=r" (r) /* output */
                :
           );
	return (r);
}

static inline __u32
convertusec(int32_t c)
{
	/* usec = counter * 1000000 / 294912000 */
	return (c / 128 * 1000 / 2304);
}

static inline int
getudelay(struct ps2sd_unit_context *devc)
{
	return (convertusec(getcounter() - devc->prevdmaintr));
}

static inline int
dest_to_src_bytes(struct ps2sd_unit_context *devc, int dest_bytes)
{
	if (devc->noconversion)
	  return (dest_bytes);
	else
	  return (dest_bytes / 4 * devc->samplesize *
		  devc->cnvsrcrate / devc->cnvdstrate);
}

static inline int
src_to_dest_bytes(struct ps2sd_unit_context *devc, int src_bytes)
{
	if (devc->noconversion)
	  return (src_bytes);
	else
	  return (src_bytes / devc->samplesize * 4 *
		  devc->cnvdstrate / devc->cnvsrcrate);
}

static int
setdmastate(struct ps2sd_unit_context *devc, int curstat, int newstat, char *cause)
{
	int res;
	unsigned long flags;

	spin_lock_irqsave(&devc->spinlock, flags);
	if (devc->dmastat == curstat) {
		DPRINT(DBG_DMASTAT,
		       "core%d %s->%s, '%s'\n", devc->core,
		       dmastatnames[curstat], dmastatnames[newstat], cause);
		devc->dmastat = newstat;
		res = 0;
	} else {
		DPRINT(DBG_DMASTAT,
		       "core%d %s->%s denied, current stat=%s, '%s'\n",
		       devc->core,
		       dmastatnames[curstat], dmastatnames[newstat],
		       dmastatnames[devc->dmastat], cause);
		res = -1;
	}
	wake_up(&devc->dmastat_wq);
	spin_unlock_irqrestore(&devc->spinlock, flags);

	return res;
}

static void
ps2sd_timer(unsigned long data)
{
	struct ps2sd_unit_context *devc = (struct ps2sd_unit_context *)data;

	if (devc->intr_count == 0) {
		DPRINT(DBG_INFO, "DMA%d seems to be hunging up\n",
		       devc->dmach);
		if (setdmastate(devc, DMASTAT_RUNNING, DMASTAT_STOPPING,
				"DMA seems to be hunging up") == 0 ||
		    setdmastate(devc, DMASTAT_STOPREQ, DMASTAT_STOPPING,
				"DMA seems to be hunging up") == 0){
			/* mute */
			ps2sd_mute(devc);
			devc->lockq.routine = stop_sequence0;
			devc->lockq.arg = devc;
			devc->lockq.name = "stop by timer";
			ps2sif_lowlevel_lock(ps2sd_mc.lock, &devc->lockq,
					     PS2SIF_LOCK_QUEUING);
		}
	} else {
		devc->intr_count = 0;
		devc->timer.expires = jiffies + devc->timeout;
		add_timer(&devc->timer);
	}
}

static inline void
ps2sd_iopdma(ps2sif_dmadata_t *sdd, int len)
{
	unsigned int dmaid;

	dmaid = ps2sif_setdma_wait(sdd, len);
	ps2sif_dmastat_wait(dmaid);
}

static inline int
ps2sd_iopdma_interruptible(ps2sif_dmadata_t *sdd, int len)
{
	unsigned int dmaid;

	if ((dmaid = ps2sif_setdma_wait_interruptible(sdd, len)) == 0 ||
	    0 <= ps2sif_dmastat_wait_interruptible(dmaid))
		return (-ERESTART);

	return (0);
}

/*
 * audio_driver interface functions
 */
static loff_t
ps2sd_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

static ssize_t
ps2sd_read(struct file *filp, char *buffer, size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int
flush_intbuf(struct ps2sd_unit_context *devc, int nonblock)
{
	int res, i, buftail, bufcount;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);

	TRACE("flush_intbuf\n");
	spin_lock_irqsave(&devc->spinlock, flags);
	add_wait_queue(&devc->write_wq, &wait);
	res = -EINVAL; /* failsafe */
	for ( ; ; ) {
		/* is dma buffer space available? */
		buftail = devc->dmabuftail;
		bufcount = devc->dmabufcount;

		if (bufcount < devc->dmabufsize) {
			res = 0;
			break;
		}

		/* no space */
		if (nonblock) {
			res = -EBUSY;
			break;
		}
		if ((res = start(devc)) < 0)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irq(&devc->spinlock);
		schedule();
		spin_lock_irq(&devc->spinlock);
		if (signal_pending(current)) {
			DPRINT(DBG_INFO, "flush_intbuf(): interrupted\n");
			res = -ERESTARTSYS;
			break;
		}
		TRACE2("flush_intbuf loop\n");
		continue;
	}
	remove_wait_queue(&devc->write_wq, &wait);
	spin_unlock_irqrestore(&devc->spinlock, flags);
	if (res != 0)
		return (res);

	/* now, we have at least BUFUNIT space in dma buffer */
	/* convert into 512 bytes interleaved format */
	if (devc->flags & PS2SD_UNIT_INT512) {
		memcpy(&devc->dmabuf[buftail], devc->intbuf, BUFUNIT);
	} else {
		unsigned short *src, *dst;
		src = (unsigned short*)devc->intbuf;
		dst = (unsigned short*)&devc->dmabuf[buftail];
		for (i = 0; i < BUFUNIT/sizeof(u_short)/2; i++) {
			dst[0] = *src++;
			dst[BUFUNIT/sizeof(u_short)/2] = *src++;
			dst++;
		}
	}
	ps2sif_writebackdcache(&devc->dmabuf[buftail], BUFUNIT);

	buftail += BUFUNIT;
	buftail %= devc->dmabufsize;
	bufcount += BUFUNIT;
	devc->intbufcount = 0;
	spin_lock_irqsave(&devc->spinlock, flags);
	devc->dmabuftail = buftail;
	devc->dmabufcount += BUFUNIT;
	spin_unlock_irqrestore(&devc->spinlock, flags);

	return (0);
}

static void
flush_cnvbuf(struct ps2sd_unit_context *devc)
{
	int n;

	preempt_disable();
	if (devc->noconversion) {
		TRACE("flush_cnvbuf no conversion\n");
		n = MIN(INTBUFSIZE - devc->intbufcount, 
			devc->cnvbufcount);
		if (CNVBUFSIZE - devc->cnvbufhead < n)
			n = CNVBUFSIZE - devc->cnvbufhead;
		memcpy(&devc->intbuf[devc->intbufcount],
		       &devc->cnvbuf[devc->cnvbufhead], n);
		devc->intbufcount += n;
		devc->cnvbufcount -= n;
		devc->cnvbufhead += n;
		devc->cnvbufhead %= CNVBUFSIZE;
	} else {
		struct ps2sd_sample s0, s1, *d;
		int scount = 0, dcount = 0;

#define CUR_ADDR &devc->cnvbuf[devc->cnvbufhead]
#define NEXT_ADDR &devc->cnvbuf[(devc->cnvbufhead + devc->samplesize) % CNVBUFSIZE]
		(*devc->fetch)(&s0, CUR_ADDR);
		(*devc->fetch)(&s1, NEXT_ADDR);
		d = (void*)&devc->intbuf[devc->intbufcount];
		while (devc->intbufcount < INTBUFSIZE &&
		       devc->samplesize * 2 <= devc->cnvbufcount) {
			d->l = (s0.l * devc->cnvd + s1.l * (devc->cnvdstrate - devc->cnvd))/devc->cnvdstrate;
			d->r = (s0.r * devc->cnvd + s1.r * (devc->cnvdstrate - devc->cnvd))/devc->cnvdstrate;
			d++;
			devc->intbufcount += sizeof(struct ps2sd_sample);
			dcount += sizeof(struct ps2sd_sample);
			if ((devc->cnvd -= devc->cnvsrcrate) < 0) {
				s0 = s1;
				devc->cnvbufhead += devc->samplesize;
				devc->cnvbufhead %= CNVBUFSIZE;
				devc->cnvbufcount -= devc->samplesize;
				(*devc->fetch)(&s1, NEXT_ADDR);
				devc->cnvd += devc->cnvdstrate;
				scount += devc->samplesize;
			}
		}
		DPRINT(DBG_VERBOSE, "dma%d: flush_cnvbuf convert %d -> %d bytes cnvbufcount=%d intbufcount=%d\n",
		       devc->dmach, scount, dcount, devc->cnvbufcount, devc->intbufcount);
	}
	preempt_enable();
}

static int
post_buffer(struct ps2sd_unit_context *devc)
{
	int res;
	unsigned long flags;


	/* just falesafe, this sould not occur */
	if (devc->samplesize == 0) {
		printk(KERN_CRIT "ps2sd: internal error, sampelsize = 0");
		devc->samplesize = 1;
	}

	/* make sure that conversion buffer is empty */
	while (devc->samplesize * 2 <= devc->cnvbufcount) {
		flush_cnvbuf(devc);
		if (devc->intbufcount == INTBUFSIZE) {
			if ((res = flush_intbuf(devc, 0 /* block */)) < 0)
				return res;
		}
	}

	/* flush interleave buffer */
	if (devc->intbufcount != 0) {
		/* pad interleave buffer if necessary */
		if (devc->intbufcount < INTBUFSIZE) {
			DPRINT(DBG_INFO, "pad interleave buf %d bytes\n", 
			       INTBUFSIZE - devc->intbufcount);
			memset(&devc->intbuf[devc->intbufcount], 0,
			       INTBUFSIZE - devc->intbufcount);
		}
		/* flush the buffer */
		if ((res = flush_intbuf(devc, 0 /* block */)) < 0)
			return res;
	}

	spin_lock_irqsave(&devc->spinlock, flags);
	if (devc->dmastat == DMASTAT_STOP &&
	    devc->dmabufcount != 0) {
		spin_unlock_irqrestore(&devc->spinlock, flags);
		memset(devc->intbuf, 0, INTBUFSIZE);
		DPRINT(DBG_INFO, "pad dma buf %d bytes\n", 
		       devc->iopbufsize/2 - devc->dmabufcount);
		while (devc->dmabufcount < devc->iopbufsize/2) {
			res = flush_intbuf(devc, 0 /* block */);
			if (res < 0)
				return res;
		}
		start(devc);
	} else {
		spin_unlock_irqrestore(&devc->spinlock, flags);
	}

	return 0;
}

static int
sync_buffer(struct ps2sd_unit_context *devc)
{
	int res;

	if ((res = post_buffer(devc)) < 0)
		return res;
	return wait_dma_stop(devc);
}

static ssize_t
ps2sd_write(struct file *filp, const char *buffer, size_t count, loff_t *ppos)
{
	int ret, res;
	struct ps2sd_unit_context *devc;

	devc = PS2SD_DEVC(filp);

	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	DPRINT(DBG_WRITE, "write %dbytes head=%p cnt=0x%x\n",
	       count, &devc->dmabuf[devc->dmabuftail], devc->dmabufcount);

	if (devc->cnvbuf == NULL)
		return -EINVAL; /* no PCM buffers allocated */

	ret = 0;
	while (0 < count) {
		int n;

		/* check errors and reset it */
		if ((res = reset_error(devc)) < 0)
			return ret ? ret : res;

		/*
		 * copy into format conversion buffer
		 * user space -> cnvbuf
		 */
		preempt_disable();
		if (0 < (n = CNVBUFSIZE - devc->cnvbufcount)) {
			if (devc->cnvbufhead <= devc->cnvbuftail)
				n = CNVBUFSIZE - devc->cnvbuftail;
			if (count < n) {
				n = count;
			}

			CHECK_BUF_RANGE(&devc->cnvbuf[0], CNVBUFSIZE, &devc->cnvbuf[devc->cnvbuftail], n,
					devc->cnvbufcount);
			if (copy_from_user(&devc->cnvbuf[devc->cnvbuftail],
					   buffer, n)) {
				preempt_enable();
				return ret ? ret : -EFAULT;
			}
			DPRINT(DBG_VERBOSE, "dma%d: copy_from_user %d bytes\n", devc->dmach, n);
			devc->cnvbuftail += n;
			devc->cnvbuftail %= CNVBUFSIZE;
			devc->cnvbufcount += n;
			count -= n;
			buffer += n;
			ret += n;
		}
		preempt_enable();

		/* 
		 * format conversion
		 * cnvbuf -> intbuf
		 */
		flush_cnvbuf(devc);

		/*
		 * flush interleave buffer if it gets full
		 * intbuf -> dmabuf
		 */
		if (devc->intbufcount == INTBUFSIZE) {
			res = flush_intbuf(devc, filp->f_flags & O_NONBLOCK);
			if (res < 0) {
				return ret ? ret : res;
			}
		}

		/* kick DMA */
		if (devc->iopbufsize <= devc->dmabufcount)
			if ((res = start(devc)) < 0)
				return ret ? ret : res;
	}

	return ret;
}

static unsigned int 
ps2sd_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct ps2sd_unit_context *devc;
	unsigned long flags;
	int space;
	unsigned int mask = 0;

	devc = PS2SD_DEVC(filp);
	poll_wait(filp, &devc->write_wq, wait);

	spin_lock_irqsave(&devc->spinlock, flags);
	space = devc->dmabufsize - devc->dmabufcount;
	spin_unlock_irqrestore(&devc->spinlock, flags);

	if (devc->iopbufsize/2 <= space)
	  mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static int
ps2sd_mmap(struct file *filp, struct vm_area_struct *vma)
{
       	return -EINVAL;
}

static int
ps2sd_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int i, val, res, dmabytes;
	unsigned long flags;
	struct ps2sd_unit_context *devc;
        audio_buf_info abinfo;
        count_info cinfo;

	devc = PS2SD_DEVC(filp);

	switch (cmd) {
	case OSS_GETVERSION:
		DPRINT(DBG_IOCTL, "ioctl(OSS_GETVERSION)\n");
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_SYNC)\n");
		return sync_buffer(devc);
		break;

	case SNDCTL_DSP_SETDUPLEX:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_SETDUPLEX)\n");
		return -EINVAL;

	case SNDCTL_DSP_GETCAPS:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETCAPS)\n");
		return put_user(DSP_CAP_BATCH, (int *)arg);

	case SNDCTL_DSP_RESET:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_RESET)\n");
		/* stop DMA */
		res = stop(devc);
		reset_error(devc);
		reset_buffer(devc); /* clear write buffer */
		return res;

	case SNDCTL_DSP_SPEED:
		if (get_user(val, (int *)arg))
			return (-EFAULT);
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_SPEED): %d\n", val);
		if (0 <= val) {
			if ((res = set_format(devc, devc->format,
					      val, devc->stereo, 0)) < 0)
				return res;
		}
		return put_user(devc->speed, (int *)arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, (int *)arg))
			return (-EFAULT);
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_STEREO): %s\n",
		       val ? "stereo" : "monaural");
		return set_format(devc, devc->format, devc->speed, val, 0);

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *)arg))
			return (-EFAULT);
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_CHANNELS): %d\n", val);
		if (val == 1) /* momaural */
			return set_format(devc, devc->format, devc->speed,0,0);
		if (val == 2) /* stereo */
			return set_format(devc, devc->format, devc->speed,1,0);
		return -EINVAL;

	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETFMTS)\n");
		/*
		 * SPU2 supports only one format,
		 * little endian signed 16 bit natively.
		 */
		return put_user(SUPPORTEDFMT, (int *)arg);
		break;

	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		if (get_user(val, (int *)arg))
			return (-EFAULT);
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_SETFMT): %x\n", val);
		if (val != AFMT_QUERY) {
			if ((res = set_format(devc, val, devc->speed,
					      devc->stereo, 0)) < 0)
				return res;
		}
		return put_user(devc->format, (int *)arg);

	case SNDCTL_DSP_POST:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_POST)\n");
		return post_buffer(devc);

	case SNDCTL_DSP_GETTRIGGER:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETTRIGGER)\n");
		/* trigger function is not supported */
		return -EINVAL;

	case SNDCTL_DSP_SETTRIGGER:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_SETTRIGGER)\n");
		/* trigger function is not supported */
		return -EINVAL;

	case SNDCTL_DSP_GETOSPACE:
		/*DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETOSPACE)\n");
		 */
		spin_lock_irqsave(&devc->spinlock, flags);
		abinfo.fragsize = dest_to_src_bytes(devc, devc->iopbufsize/2);
		abinfo.bytes = dest_to_src_bytes(devc, 
						 devc->dmabufsize -
						 devc->dmabufcount);
		abinfo.fragstotal = devc->dmabufsize / (devc->iopbufsize/2);
		abinfo.fragments = (devc->dmabufsize - devc->dmabufcount) /
					(devc->iopbufsize/2);
		spin_unlock_irqrestore(&devc->spinlock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETISPACE)\n");
		/* SPU2 has no input device */
		return -EINVAL;

	case SNDCTL_DSP_NONBLOCK:
		/* This command seems to be undocumented!? */
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_NONBLOCK)\n");
                filp->f_flags |= O_NONBLOCK;
		return (0);

	case SNDCTL_DSP_GETODELAY:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETODELAY)\n");
		/* How many bytes are there in the buffer? */
		if (!(filp->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&devc->spinlock, flags);
		if (devc->dmastat == DMASTAT_RUNNING) {
		    dmabytes = devc->iopbufsize/2;
		    if (devc->prevdmaintrvalid) {
			dmabytes -= (192*getudelay(devc)/1000);
		    } else {
			dmabytes -= (192*(getudelay(devc) - 1000)/1000);
		    }
		} else {
		    dmabytes = 0;
		}
		val = dest_to_src_bytes(devc,
					devc->dmabufcount +
					devc->intbufcount +
					dmabytes);
		val += devc->cnvbufcount;
		DPRINT(DBG_INTR, "%s%s val=%d dma=%d int=%d iop=%d cnv=%d\n",
		       devc->dmastat == DMASTAT_RUNNING ? "run" : "stop",
		       devc->prevdmaintrvalid ? " " : "?",
		       val,
		       devc->dmabufcount,
		       devc->intbufcount,
		       dmabytes,
		       devc->cnvbufcount);
		spin_unlock_irqrestore(&devc->spinlock, flags);
		return put_user(val, (int *)arg);

	case SNDCTL_DSP_GETIPTR:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETIPTR)\n");
		/* SPU2 has no input device */
		return -EINVAL;

	case SNDCTL_DSP_GETOPTR:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETOPTR)\n");
		spin_lock_irqsave(&devc->spinlock, flags);
                cinfo.bytes = dest_to_src_bytes(devc,devc->total_output_bytes);
                cinfo.blocks = devc->total_output_bytes / (devc->iopbufsize/2);
                cinfo.ptr = 0;
		spin_unlock_irqrestore(&devc->spinlock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETBLKSIZE:
		DPRINT(DBG_IOCTL, "ioctl(SNDCTL_DSP_GETBLKSIZE)\n");
		return put_user(dest_to_src_bytes(devc, devc->iopbufsize/2),
				(int *)arg);

	case SNDCTL_DSP_SETFRAGMENT:
                if (get_user(val, (int *)arg))
			return (-EFAULT);
		DPRINT(DBG_IOCTL,
		       "ioctl(SNDCTL_DSP_SETFRAGMENT) size=2^%d, max=%d\n",
		       (val & 0xffff), ((val >> 16) & 0xffff));
		devc->requested_fragsize = 2 << ((val & 0xffff) - 1);
		devc->requested_maxfrags = (val >> 16) & 0xffff;
		set_format(devc, devc->format, devc->speed, devc->stereo, 1);
		return (0);

	/*
	 * I couldn't found these commands in OSS programers...
	 */
	case SNDCTL_DSP_SUBDIVIDE:
	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_READ_RATE:
	case SOUND_PCM_READ_CHANNELS:
	case SOUND_PCM_READ_BITS:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;

	case PS2SDCTL_SET_INTMODE:
	    if (get_user(val, (int *)arg))
		return (-EFAULT);
	    switch (val) {
	    case PS2SD_INTMODE_NORMAL:
		DPRINT(DBG_IOCTL, "ioctl(PS2SDCTL_SET_INTMODE): normal\n");
		devc->flags &= ~PS2SD_UNIT_INT512;
		break;
	    case PS2SD_INTMODE_512:
		DPRINT(DBG_IOCTL, "ioctl(PS2SDCTL_SET_INTMODE): 512\n");
		devc->flags |= PS2SD_UNIT_INT512;
		break;
	    default:
		return (-EINVAL);
	    }
	    return (0);

	case PS2SDCTL_SET_SPDIFMODE:
	    if (get_user(val, (int *)arg))
		return (-EFAULT);
	    if (ps2sdcall_set_coreattr(SB_SOUND_CA_SPDIF_MODE, val) < 0)
	      return (-EIO);
	    return (0);

	case PS2SDCTL_IOP_ALLOC:
	    {
		ps2sd_voice_data data;

		if (devc->iopmemlist == NULL)
		    return (-EINVAL);
		if (copy_from_user(&data, (char *)arg, sizeof(data)))
		    return (-EFAULT);

		DPRINT(DBG_COMMAND,
		       "ioctl(PS2SDCTL_IOP_ALLOC): %d bytes...\n",
		       data.len);
		data.addr = ps2iopmem_alloc(devc->iopmemlist, data.len);
		DPRINT(DBG_COMMAND,
		       "ioctl(PS2SDCTL_IOP_ALLOC): 0x%x\n",
		       data.addr);
		if (data.addr == 0)
		    return (-ENOMEM);
		return copy_to_user((void *)arg, &data, sizeof(data)) ? -EFAULT : 0;
	    }

	case PS2SDCTL_IOP_FREE:
	    if (devc->iopmemlist == NULL)
		return (-EINVAL);
	    DPRINT(DBG_COMMAND, "ioctl(PS2SDCTL_IOP_FREE): 0x%lx...\n", arg);
	    ps2iopmem_free(devc->iopmemlist, arg);
	    return (0);

#if 0 /* This function isn't implemented yet */
	case PS2SDCTL_IOP_GET:
	    break;
#endif
	case PS2SDCTL_IOP_PUT:
	    {
		ps2sd_voice_data data;
		int count;
	    
		if (!(devc->flags & PS2SD_UNIT_COMMANDMODE) ||
		    devc->iopmemlist == NULL)
		    return (-EINVAL);
		if (copy_from_user(&data, (char *)arg, sizeof(data)) ||
		    !access_ok(VERIFY_READ, data.data, data.len))
		    return (-EFAULT);

		data.addr = ps2iopmem_getaddr(devc->iopmemlist,
					      data.addr, data.len);

		DPRINT(DBG_COMMAND,
		       "ioctl(PS2SDCTL_IOP_PUT): %d bytes to 0x%08x\n",
		       data.len, data.addr);
		if (data.addr == 0)
			return (-EFAULT);

		res = ps2sif_lock_interruptible(ps2sd_mc.lock, "iop put");
		if (res < 0)
		    return (res);
		count = 0;
		data.len = ALIGN(data.len, 64);
		while (count < data.len) {
		    int n;
		    ps2sif_dmadata_t dmacmd;

		    /* copy from user space to kernel space */
		    n = MIN(data.len - count, devc->dmabufsize);
		    if (copy_from_user(devc->dmabuf, &data.data[count], n)) {
			ps2sif_unlock(ps2sd_mc.lock);
			return (count ? count : -EFAULT);
		    }
		    ps2sif_writebackdcache(devc->dmabuf, n);

		    /* copy from kernel space to IOP */
		    dmacmd.data = (u_int)devc->dmabuf;
		    dmacmd.addr = (u_int)(data.addr + count);
		    dmacmd.size = n;
		    dmacmd.mode = 0;
		    if ((res = ps2sd_iopdma_interruptible(&dmacmd, 1)) != 0)
			return (count ? count : res);
		    count += n;
		}
		ps2sif_unlock(ps2sd_mc.lock);
		return (count);
	    }

	case PS2SDCTL_COMMAND_INIT:
	    DPRINT(DBG_COMMAND, "ioctl(PS2SDCTL_COMMAND_INIT): ");
	    if (!(devc->flags & PS2SD_UNIT_EXCLUSIVE) ||
		(devc->flags & PS2SD_UNIT_COMMANDMODE)) {
		DPRINTK(DBG_COMMAND, "error\n");
		return (-EINVAL);
	    }
	    DPRINTK(DBG_COMMAND, "ok\n");
	    /* enter command mode */
	    for (i = 0; i < ps2sd_nunits; i++)
		ps2sd_units[i].flags |= PS2SD_UNIT_COMMANDMODE;
	    return (0);

	case PS2SDCTL_COMMAND_KERNEL22:
	case PS2SDCTL_COMMAND:
	    {
#define CHECKIOPADDR(a, h, s)do {     \
    if (((a) = ps2iopmem_getaddr(devc->iopmemlist, (h), (s))) == 0) \
return (-EFAULT);    \
} while (0)
#define CHECKIOPADDR_ALLOWNULL(a, h, s)do {    \
    if ((h) != 0 &&    \
((a) = ps2iopmem_getaddr(devc->iopmemlist, (h), (s))) == 0) \
return (-EFAULT);    \
} while (0)
		ps2sd_command cmd;

		if (!(devc->flags & PS2SD_UNIT_COMMANDMODE))
		    return (-EINVAL);
		if(copy_from_user(&cmd, (char *)arg, sizeof(cmd)))
		    return (-EFAULT);

		switch (cmd.command) {
		case PS2SDCTL_COMMAND_WRITE:
		case PS2SDCTL_COMMAND_WRITE2:		    
		case PS2SDCTL_COMMAND_WRITE3:
		case PS2SDCTL_COMMAND_READ:
		case PS2SDCTL_COMMAND_OPEN1:
		case PS2SDCTL_COMMAND_OPEN2:
		case PS2SDCTL_COMMAND_OPEN3:
		case PS2SDCTL_COMMAND_OPEN4:
		    if (devc->iopmemlist == NULL)
			return (-EINVAL);
		}

		switch (cmd.command) {
		case PS2SDCTL_COMMAND_OPEN1:
		    CHECKIOPADDR(cmd.args[0], cmd.args[0], 0);
		    CHECKIOPADDR(cmd.args[1], cmd.args[1], 0);
		    break;
		case PS2SDCTL_COMMAND_WRITE:
		case PS2SDCTL_COMMAND_WRITE3:
		case PS2SDCTL_COMMAND_OPEN2:
		    CHECKIOPADDR(cmd.args[0], cmd.args[0], 0);
		    break;
		case PS2SDCTL_COMMAND_READ:
		case PS2SDCTL_COMMAND_OPEN3:
		    CHECKIOPADDR(cmd.args[1], cmd.args[1], 0);
		    break;
		case PS2SDCTL_COMMAND_WRITE2:
		case PS2SDCTL_COMMAND_OPEN4:
		    CHECKIOPADDR_ALLOWNULL(cmd.args[1], cmd.args[1], 0);
		    CHECKIOPADDR_ALLOWNULL(cmd.args[2], cmd.args[2], 0);
		    break;
		}
		DPRINT(DBG_COMMAND, "command: %x %x %x %x %x\n", cmd.command,
		       cmd.args[0], cmd.args[1], cmd.args[2], cmd.args[3]);
		res = ps2sif_lock_interruptible(ps2sd_mc.lock, "command");
		if (res < 0)
		    return (res);
		res = ps2sdcall_remote(&cmd.command, &cmd.result);
		ps2sif_unlock(ps2sd_mc.lock);
		DPRINT(DBG_COMMAND, "command: %x res=%d, 0x%x\n", cmd.command,
		       res, cmd.result);
	        if (res < 0)
		    return (-EIO);
                return copy_to_user((void *)arg, &cmd, sizeof(cmd)) ? -EFAULT : 0;
	    }

	case PS2SDCTL_COMMAND_END:
	    DPRINT(DBG_COMMAND, "ioctl(PS2SDCTL_COMMAND_END)\n");
	    return ps2sd_command_end(devc);

	case PS2SDCTL_CHANGE_THPRI:
	{
		struct task_struct *tsk;
		int priority = (int)arg;
		struct sched_param param = { .sched_priority = priority };

		DPRINT(DBG_COMMAND, "ioctl(PS2SDCTL_CHANGE_THPRI)\n");
		tsk = ps2sd_mc.sd_task;
		if (tsk == NULL) {
			printk("ps2sd: sound thread doesn't exist.\n");
			return -1;
		}
		if (priority == 0) {
			sched_setscheduler(tsk, SCHED_NORMAL, &param);
		} else if (priority > 0) {
			sched_setscheduler(tsk, SCHED_FIFO, &param);
		} else {
			return -EINVAL;
		}
		return (0);
	}
	}

	return ps2sdmixer_do_ioctl(&ps2sd_mixers[0], cmd, arg);
}

static int
ps2sd_open(struct inode *inode, struct file *filp)
{
	int i, minor = MINOR(inode->i_rdev);
	struct ps2sd_unit_context *devc;

	/*
	 * we have no input device
	 */
	if (filp->f_mode & FMODE_READ)
		return -ENODEV;

	devc = ps2sd_lookup_by_dsp(minor);
	if (devc == NULL) return -ENODEV;

	DPRINT(DBG_INFO, "open: core%d, dmastat=%s\n",
	       devc->core, dmastatnames[devc->dmastat]);

	spin_lock_irq(&ps2sd_mc.spinlock);
	if (devc->flags & PS2SD_UNIT_EXCLUSIVE) {
		for (i = 0; i < ps2sd_nunits; i++) {
			if (ps2sd_units[i].flags & PS2SD_UNIT_OPENED) {
				spin_unlock_irq(&ps2sd_mc.spinlock);
				return -EBUSY;
			}
		}
	} else {
		if (devc->flags & PS2SD_UNIT_OPENED) {
			spin_unlock_irq(&ps2sd_mc.spinlock);
			return -EBUSY;
		}
		for (i = 0; i < ps2sd_nunits; i++) {
			if ((ps2sd_units[i].flags & PS2SD_UNIT_EXCLUSIVE) &&
			    (ps2sd_units[i].flags & PS2SD_UNIT_OPENED)) {
				spin_unlock_irq(&ps2sd_mc.spinlock);
				return -EBUSY;
			}
		}
	}
	devc->flags = (devc->init_flags | PS2SD_UNIT_OPENED);
	spin_unlock_irq(&ps2sd_mc.spinlock);

	devc->total_output_bytes = 0;
	filp->private_data = devc;

	/*
	 * set defaut format and fragment size
	 */
	devc->requested_fragsize = 0; /* no request */
	devc->dmabufsize = ps2sd_dmabufsize;
	devc->iopbufsize = ps2sd_iopbufsize;
	set_format(devc, AFMT_MU_LAW, 8000, 0 /* monaural */, 1);
	reset_buffer(devc);

	/* initialize iopmem list (if any) */
	if (devc->iopmemlist != NULL)
		ps2iopmem_init(devc->iopmemlist);

	return 0;
}

static int
ps2sd_release(struct inode *inode, struct file *filp)
{
	struct ps2sd_unit_context *devc;

	devc = PS2SD_DEVC(filp);
	DPRINT(DBG_INFO, "close: core%d, dmastat=%s\n",
	       devc->core, dmastatnames[devc->dmastat]);
	sync_buffer(devc);

	if (devc->flags & PS2SD_UNIT_COMMANDMODE)
		ps2sd_command_end(devc);

	/* free iopmem (if any) */
	if (devc->iopmemlist != NULL)
		ps2iopmem_end(devc->iopmemlist);

	spin_lock_irq(&ps2sd_mc.spinlock);
	devc->flags &= ~PS2SD_UNIT_OPENED;
	spin_unlock_irq(&ps2sd_mc.spinlock);

	return 0;
}

struct ps2sd_unit_context *
ps2sd_lookup_by_dsp(int dsp)
{
	int i;
	for (i = 0; i < ps2sd_nunits; i++) {
		if (ps2sd_units[i].dsp == dsp)
			return &ps2sd_units[i];
	}
	return NULL;
}

struct ps2sd_unit_context *
ps2sd_lookup_by_dmach(int dmach)
{
	int i;
	for (i = 0; i < ps2sd_nunits; i++) {
		if (ps2sd_units[i].dmach == dmach &&
		    (ps2sd_units[i].flags & PS2SD_UNIT_OPENED))
			return &ps2sd_units[i];
	}
	for (i = 0; i < ps2sd_nunits; i++) {
		if (ps2sd_units[i].dmach == dmach)
			return &ps2sd_units[i];
	}
	return NULL;
}

struct ps2sd_mixer_context *
ps2sd_lookup_mixer(int mixer)
{
	int i;
	for (i = 0; i < ps2sd_nmixers; i++) {
		if (ps2sd_mixers[i].mixer == mixer)
			return &ps2sd_mixers[i];
	}
	return NULL;
}

#ifdef PS2SD_USE_THREAD
static int
ps2sd_thread(void *arg)
{
	int status, i;

	/* notify we are running */
	complete(&ps2sd_mc.ack_comp);

	while (1) {
		interruptible_sleep_on(&ps2sd_mc.intr_wait);

		if (kthread_should_stop())
			break;

		DPRINT(DBG_INTR, "DMA interrupt service thread\n");
	  
		spin_lock_irq(&ps2sd_mc.spinlock);
		status = ps2sd_mc.intr_status;
		ps2sd_mc.intr_status = 0;
		spin_unlock_irq(&ps2sd_mc.spinlock);

		for (i = 0; i < 32; i++)
			if (status & (1 << i)) {
				struct ps2sd_unit_context *devc;
				unsigned long flags;

				devc = ps2sd_lookup_by_dmach(i);

				if (devc != NULL) {
					spin_lock_irqsave(&devc->spinlock, flags);
					ps2sd_dmaintr(NULL, i);
					spin_unlock_irqrestore(&devc->spinlock, flags);
				} else {
					DPRINT(DBG_DIAG, "ignore DMA interrupt %d\n", dmach);
				}
			}
		DPRINT(DBG_INTR, "DMA interrupt service thread...sleep\n");
	}

	DPRINT(DBG_INFO, "the thread is exiting...\n");

	return (0);
}

static int
ps2sd_intr(void* argx, int dmach)
{

	spin_lock(&ps2sd_mc.spinlock);
	ps2sd_mc.intr_status |= (1 << dmach);
	spin_unlock(&ps2sd_mc.spinlock);
	DPRINT(DBG_INTR, "DMA interrupt %d\n", dmach);
#ifdef PS2SD_USE_THREAD
	wake_up_interruptible(&ps2sd_mc.intr_wait);
#endif

	return (0);
}
#endif /* PS2SD_USE_THREAD */

static int
ps2sd_dmaintr(void* argx, int dmach)
{
	int i;
	struct ps2sd_unit_context *devc;
	ps2sif_dmadata_t dmacmd;
#ifdef PS2SD_DMA_ADDR_CHECK
	unsigned int dmamode, maddr;
#endif

	if ((devc = ps2sd_lookup_by_dmach(dmach)) == NULL) {
		DPRINT(DBG_DIAG, "ignore DMA interrupt %d\n", dmach);
		return (0);
	}
	devc->total_output_bytes += devc->fg->size;
	devc->intr_count++;

#ifdef PS2SD_DEBUG
	if (ps2sd_debug & DBG_INTR) {
	    int delay;
	    delay = getudelay(devc);
	    devc->prevdmaintr = getcounter();
	    DPRINT(DBG_INTR, "DMA interrupt %d (+%d usec)\n",
		   dmach, delay);
	} else
#endif
	devc->prevdmaintr = getcounter();
	devc->prevdmaintrvalid = 1;
	if (devc->dmastat == DMASTAT_STOP) {
		if (devc->flags & PS2SD_UNIT_COMMANDMODE)
			DPRINT(DBG_DIAG, "ignore DMA interrupt %d\n", dmach);
			return (0);
		/*
		 * stray interrupt
		 * try to stop the DMA again
		 */
		printk(KERN_CRIT "ps2sd: core%d stray interrupt\n",
		       devc->core);
		setdmastate(devc, DMASTAT_STOP, DMASTAT_STOPPING,
			    "stray interrupt");
		goto stopdma;
	}

	if (devc->dmastat == DMASTAT_STOPREQ) {
		/*
		 * setdmastate() must always succeed because we are in
		 * interrupt and no one interrupt us.
		 */
#if 0
		setdmastate(devc, DMASTAT_STOPREQ, DMASTAT_STOPPING,
			    "stop by request");
		goto stopdma;
#else
		/*
		 * force it to be underflow instead of stopping DMA
		 * immediately so that it will clear page0 on IOP for
		 * next session.
		 */
		setdmastate(devc, DMASTAT_STOPREQ, DMASTAT_RUNNING,
			    "stop by request, dmabufcount -> 0");
		devc->dmabufcount = 0;
#endif
	}

	if (devc->dmastat != DMASTAT_RUNNING) {
		/*
		 * stop sequence might be in progress...
		 */
		return (0);
	}

	if (devc->bg->dmaid != 0) {
		if (0 <= ps2sif_dmastat(devc->bg->dmaid)) {
			/* previous DMA operation does not finish */
			DPRINT(DBG_DIAG, "DMA %d does not finish\n",
			       devc->bg->dmaid);
			/*
			 * setdmastate() must always succeed because we are in
			 * interrupt and no one interrupt us.
			 */
			setdmastate(devc, DMASTAT_RUNNING, DMASTAT_STOPPING,
				    "previous DMA session is not completed");
			goto stopdma;
		} else {
			/* previous DMA operation completed */
			unsigned long flags;

			spin_lock_irqsave(&devc->spinlock, flags);
			devc->dmabufhead += devc->bg->size;
			devc->dmabufhead %= devc->dmabufsize;
			DPRINT(DBG_INTR | DBG_VERBOSE, "dmabufcount = %d-%d\n",
			       devc->dmabufcount, devc->bg->size);
			devc->dmabufcount -= devc->bg->size;
			wake_up(&devc->write_wq);
			spin_unlock_irqrestore(&devc->spinlock, flags);
		}
	}

#ifdef PS2SD_DMA_ADDR_CHECK
	/*
	 * check DMA address register on IOP
	 * if it has the same value as the address expected,
	 * that's ok.
	 */
	if (ps2sdcall_get_reg(SB_SOUND_REG_DMAMOD(devc->core), &dmamode) < 0 ||
	    ps2sdcall_get_reg(SB_SOUND_REG_MADR(devc->core), &maddr) < 0) {
		/* SBIOS failed */
		SWAP(devc->fg, devc->bg);
	} else {
		if (dmamode) {
			DPRINT(DBG_INTR | DBG_VERBOSE,
			       "dma%d reg=%x devc->bg->iopaddr=%lx\n",
			       devc->dmach, maddr, devc->bg->iopaddr);
			if (devc->bg->iopaddr <= maddr &&
			    maddr < devc->bg->iopaddr + devc->iopbufsize / 2) {
				/* ok */
				SWAP(devc->fg, devc->bg);
			} else {
		 		DPRINT(DBG_INTR, "phase error\n");
				devc->phaseerr++;
			}
		}
	}
#else
	SWAP(devc->fg, devc->bg);
#endif

	if ((devc->dmabufunderflow == 0 && devc->dmabufcount < devc->bg->size)
	    || devc->dmabufunderflow != 0) {
		unsigned char *head = 0;
		int n;

		if (devc->dmabufunderflow == 0 &&
		    devc->dmabufcount < 0)
			devc->dmabufunderflow = 1;

		switch (devc->dmabufunderflow) {
		case 0:
			/*
			 * buffer underflow
			 */
			head = &devc->dmabuf[devc->dmabufhead + 
					     devc->dmabufcount];
			n = devc->bg->size - devc->dmabufcount;
			DPRINT(DBG_INFO, "dma%d buffer underflow %d+%dbytes\n",
			       devc->dmach, devc->dmabufcount, n);
			memset(head, 0, n);
			ps2sif_writebackdcache(head, n);
#ifdef PS2SD_FADEINOUT
			if (BUFUNIT <= devc->dmabufcount) {
			    int i, j;
			    short *p;
			    p = (short*)&devc->dmabuf[devc->dmabufhead +
						      devc->dmabufcount -
						      BUFUNIT];
			    DPRINT(DBG_INFO, "dma%d: fade out %p~",
				   devc->dmach, p);
			    for (i = 0; i < 2; i++) {
				for (j = INTRSIZE/sizeof(*p)-1; 0 <= j; j--) {
				    int r = 0x10000 * j/(INTRSIZE/sizeof(*p));
				    *p = (((*p * r) >> 16) & 0xffff);
				    p++;
				}
			    }
			    DPRINTK(DBG_INFO, "%p\n", p);
			    ps2sif_writebackdcache(p - BUFUNIT/sizeof(*p),
						   BUFUNIT);
			}
#endif
			devc->dmabufunderflow = 1;
			devc->dmabufcount = devc->bg->size;
			break;
		case 1:
		case 2:
			/*
			 * buffer underflow have been detected but we have
			 * the last fragment to output on IOP.
			 */
			head = &devc->dmabuf[devc->dmabufhead];
			n = devc->bg->size;
			devc->dmabufunderflow++;
			memset(head, 0, n);
			ps2sif_writebackdcache(head, n);
			devc->dmabufcount = n;
			break;
		case 3:
			setdmastate(devc, DMASTAT_RUNNING, DMASTAT_STOPPING,
				    "underflow 3");
			goto stopdma;
		default: /* XXX, This should not occur. */
			setdmastate(devc, DMASTAT_RUNNING, DMASTAT_STOPPING,
				    "underflow sequence error");
			goto stopdma;
		}
	}

	dmacmd.data = (u_int)&devc->dmabuf[devc->dmabufhead];
	dmacmd.addr = (u_int)devc->bg->iopaddr;
	dmacmd.size = devc->bg->size;
	dmacmd.mode = 0;

#ifdef PS2SD_DEBUG
	if (devc->dmabufunderflow)
		DPRINT(DBG_INFO, "ps2sif_setdma(%x->%x  %d bytes)\n",
		       dmacmd.data, dmacmd.addr, dmacmd.size);
#endif
	DPRINT(DBG_INTR | DBG_VERBOSE, "ps2sif_setdma(%x->%x  %d bytes) ",
	       dmacmd.data, dmacmd.addr, dmacmd.size);

	for (i = 0; i < 20; i++) {
		if ((devc->bg->dmaid = ps2sif_setdma(&dmacmd, 1)) != 0)
			break;
		udelay(50);
	}
#ifdef PS2SD_DEBUG
	if (i)
		DPRINT(DBG_DIAG, "dmaintr: setdma() retry %d times\n", i);
#endif
	if (devc->bg->dmaid == 0) {
		printk("dmaintr: ps2sif_setdma() failed\n");
		setdmastate(devc, DMASTAT_RUNNING, DMASTAT_STOPPING,
			    "setdma failed. (DMA queue might be full)");
		goto stopdma;
	}
	DPRINTK(DBG_INTR | DBG_VERBOSE, "= %d\n", devc->bg->dmaid);
	
	return 0;

 stopdma:
	/* mute */
	ps2sd_mute(devc);

	/*
	 * get the lock and invoke stop sequence
	 */
	del_timer(&devc->timer);
	devc->lockq.routine = stop_sequence0;
	devc->lockq.arg = devc;
	devc->lockq.name = "stop sequence";
	if (ps2sif_lowlevel_lock(ps2sd_mc.lock, &devc->lockq,
				 PS2SIF_LOCK_QUEUING) == 0) {
		DPRINT(DBG_DMASTAT, "core%d lock succeeded\n", devc->core);
	} else {
		DPRINT(DBG_DMASTAT, "core%d lock deferred\n", devc->core);
	}

	return 0;
}

static int
ps2sd_attach_unit(struct ps2sd_unit_context *devc, int core, int dmach,
		  struct ps2sd_mixer_context *mixer, int init_flags)
{
	int res, resiop;
	ps2sif_dmadata_t dmacmd;

	devc->init = 0;

	devc->flags = devc->init_flags = init_flags;
	devc->core = core;
	devc->dmach = dmach;
	devc->dmabuf = NULL;
	devc->cnvbuf = NULL;
	devc->intbuf = NULL;
	devc->iopbuf = 0;
	init_waitqueue_head(&devc->write_wq);
	init_waitqueue_head(&devc->dmastat_wq);
	spin_lock_init(&devc->spinlock);
	devc->dmastat = DMASTAT_STOP;
	ps2sif_lockqueueinit(&devc->lockq);
#ifdef PS2SD_DEBUG_DMA
	if (devc->dmabufsize < PS2SD_DEBUG_DMA_BUFSIZE)
		devc->dmabufsize = PS2SD_DEBUG_DMA_BUFSIZE;
	if (devc->iopbufsize < PS2SD_DEBUG_IOP_BUFSIZE)
		devc->iopbufsize = PS2SD_DEBUG_IOP_BUFSIZE;
#endif

	/* initialize timer */
	init_timer(&devc->timer);
	devc->timer.function = ps2sd_timer;
	devc->timer.data = (long)devc;
	devc->init |= PS2SD_INIT_TIMER;

	/* set format, sampling rate and stereo  */
	set_format(devc, SPU2FMT, SPU2SPEED, 1, 1);

	/* never fail to get the lock */
	ps2sif_lock(ps2sd_mc.lock, "attach unit");

	if(init_flags & PS2SD_UNIT_NOPCM)
		goto allocated;

	/*
	 * allocate DMA buffer
	 */
	if(init_flags & PS2SD_UNIT_EXCLUSIVE) {
		/*
		 * XXX, You can use another unit's buffer.
		 */
		devc->iopbuf = ps2sd_units[0].iopbuf;
		devc->dmabuf = ps2sd_units[0].dmabuf;
		devc->cnvbuf = ps2sd_units[0].cnvbuf;
		devc->intbuf = ps2sd_units[0].intbuf;
		goto allocated;
	}

	if ((res = alloc_buffer(devc)) < 0)
		goto unlock_and_return;

	/*
	 * clear IOP buffer
	 */
	memset(devc->dmabuf, 0, ps2sd_max_iopbufsize);
	dmacmd.data = (u_int)devc->dmabuf;
	dmacmd.addr = (u_int)devc->iopbuf;
	dmacmd.size = ps2sd_max_iopbufsize;
	dmacmd.mode = 0;

	DPRINT(DBG_INFO, "clear IOP buffer\n");
	DPRINT(DBG_INFO, "sd_iopdma(%x->%x  %d bytes)\n", 
	       dmacmd.data, dmacmd.addr, dmacmd.size);
	ps2sif_writebackdcache(devc->dmabuf, dmacmd.size);
	ps2sd_iopdma(&dmacmd, 1);
	DPRINTK(DBG_INFO, "done\n");

	/*
	 * clear ZERO buffer
	 */
	dmacmd.data = (u_int)devc->dmabuf;
	dmacmd.addr = (u_int)ps2sd_mc.iopzero;
	dmacmd.size = PS2SD_SPU2PCMBUFSIZE;
	dmacmd.mode = 0;

	DPRINT(DBG_INFO, "clear IOP ZERO buffer\n");
	DPRINT(DBG_INFO, "sd_iopdma(%x->%x  %d bytes)\n", 
	       dmacmd.data, dmacmd.addr, dmacmd.size);
	ps2sif_writebackdcache(devc->dmabuf, dmacmd.size);
	ps2sd_iopdma(&dmacmd, 1);
	DPRINTK(DBG_INFO, "done\n");

	while(1) {
	  volatile int i;
	  res = ps2sdcall_trans_stat(devc->core, 0, &resiop);
	  if (res < 0) {
	    DPRINT(DBG_INFO, "core%d: ps2sdcall_trans_stat res=%d\n",
		   devc->core, res);
	    break;
	  }
	  if (resiop == 0 ) break;
	  for (i = 0; i < 0x200000; i++) {
	    /* XXX, busy wait */
	  }
	}

 allocated:
	/*
	 * clear PCM buffer
	 */
	res = ps2sdcall_trans(devc->core,
			      SD_TRANS_MODE_WRITE|
			      SD_BLOCK_MEM_DRY|
			      SD_BLOCK_ONESHOT,
			      ps2sd_units[0].iopbuf,
			      MIN(ps2sd_max_iopbufsize, 4096), 0, &resiop);
	DPRINT(DBG_DIAG,
	       "core%d: clear PCM buffer %lx %dbytes res=%d resiop=%d\n", 
	       devc->core, ps2sd_units[0].iopbuf,
	       MIN(ps2sd_max_iopbufsize, 4096), res, resiop);
	if (res < 0) {
		res = -EIO;
		goto unlock_and_return;
	}

	/*
	 * install DMA callback routine
	 */
	res = ps2sdcall_trans_callback(dmach, ps2sd_intr, NULL,
				       NULL, NULL, &resiop);
	DPRINT(DBG_INFO, "core%d: ps2sdcall_trans_callback res=%d resiop=%x\n",
	       core, res, resiop);
	if (res < 0) {
		printk(KERN_CRIT "ps2sd: SetTransCallback failed\n");
		res = -EIO;
		goto unlock_and_return;
	}
	devc->init |= PS2SD_INIT_DMACALLBACK;

	/*
	 * register device
	 */
	devc->dsp = register_sound_dsp(&ps2sd_dsp_fops, -1);
	if (devc->dsp < 0) {
		printk(KERN_ERR "ps2sd: Can't install sound device\n");
	}
	DPRINT(DBG_INFO, "core%d: register_sound_dsp() = %d\n",
	       core, devc->dsp);
	devc->init |= PS2SD_INIT_REGDEV;
	
	/*
	 * initialize mixer stuff
	 */
	ps2sd_mute(devc);
	devc->mixer_main.scale = 0x3fff;
	devc->mixer_main.volr = 0;
	devc->mixer_main.voll = 0;
	devc->mixer_main.regr = SB_SOUND_REG_MVOLR(core);
	devc->mixer_main.regl = SB_SOUND_REG_MVOLL(core);
	devc->mixer_main.mixer = mixer;

	devc->mixer_pcm.scale = 0x7fff;
	devc->mixer_pcm.volr = 0;
	devc->mixer_pcm.voll = 0;
	devc->mixer_pcm.regr = SB_SOUND_REG_BVOLR(core);
	devc->mixer_pcm.regl = SB_SOUND_REG_BVOLL(core);
	devc->mixer_pcm.mixer = mixer;

	devc->mixer_extrn.scale = 0x7fff;
	devc->mixer_extrn.volr = 0;
	devc->mixer_extrn.voll = 0;
	devc->mixer_extrn.regr = SB_SOUND_REG_AVOLR(core);
	devc->mixer_extrn.regl = SB_SOUND_REG_AVOLL(core);
	devc->mixer_extrn.mixer = mixer;

	res = 0;

 unlock_and_return:
	ps2sif_unlock(ps2sd_mc.lock);

	return res;
}

static void
ps2sd_detach_unit(struct ps2sd_unit_context *devc)
{
	int res, resiop;

	/*
	 * unregister device entry
	 */
	if (devc->init & PS2SD_INIT_REGDEV)
		unregister_sound_dsp(devc->dsp);
	devc->init &= ~PS2SD_INIT_REGDEV;

	/*
	 * delete timer
	 */
	if (devc->init & PS2SD_INIT_TIMER)
		del_timer(&devc->timer);
	devc->init &= ~PS2SD_INIT_TIMER;

	/*
	 * uninstall DMA callback routine
	 */
	if (devc->init & PS2SD_INIT_DMACALLBACK) {
		/* never fail to get the lock */
		ps2sif_lock(ps2sd_mc.lock, "detach unit");
		res = ps2sdcall_trans_callback(devc->dmach, NULL, NULL,
					       NULL, NULL, &resiop);
		ps2sif_unlock(ps2sd_mc.lock);
		if (res < 0)
			printk(KERN_CRIT "ps2sd: SetTransCallback failed\n");
	}
	devc->init &= ~PS2SD_INIT_DMACALLBACK;

	/*
	 * free buffers
	 */
	free_buffer(devc);

	devc->init = 0;
}

/*
 * adjust buffer size
 */
static int
adjust_bufsize(int size)
{
	if (size < PS2SD_MINIMUM_BUFSIZE)
		return PS2SD_MINIMUM_BUFSIZE;
	return ALIGN(size, BUFUNIT);
}

static int
alloc_buffer(struct ps2sd_unit_context *devc)
{
	int res;

	if ((res = free_buffer(devc)) < 0) return res;

	devc->init |= PS2SD_INIT_BUFFERALLOC;
	res = ps2sif_lock_interruptible(ps2sd_mc.lock, "alloc buffer");
	if (res < 0)
		return res;

	/*
	 * allocate buffer on IOP
	 */
	devc->iopbuf = (long)ps2sif_allociopheap(ps2sd_max_iopbufsize);
	if(devc->iopbuf == 0) {
		printk(KERN_ERR "ps2sd: can't alloc iop heap\n");
		return -EIO;
	}
	DPRINT(DBG_INFO, "core%d: allocate %d bytes on IOP 0x%lx\n",
	       devc->core, ps2sd_max_iopbufsize, devc->iopbuf);

	ps2sif_unlock(ps2sd_mc.lock);

	/*
	 * allocate buffer on main memory
	 */
	devc->dmabuf = kmalloc(ps2sd_max_dmabufsize, GFP_KERNEL);
	if (devc->dmabuf == NULL) {
		printk(KERN_ERR "ps2sd: can't alloc DMA buffer\n");
		return -ENOMEM;
	}
	DPRINT(DBG_INFO, "core%d: allocate %d bytes, 0x%p for DMA\n",
	       devc->core, ps2sd_max_dmabufsize, devc->dmabuf);

	devc->cnvbuf = kmalloc(CNVBUFSIZE, GFP_KERNEL);
	if (devc->cnvbuf == NULL) {
		printk(KERN_ERR "ps2sd: can't alloc converting buffer\n");
		return -ENOMEM;
	}
	DPRINT(DBG_INFO, "core%d: allocate %d bytes, 0x%p for conversion\n",
	       devc->core,  CNVBUFSIZE, devc->cnvbuf);

	devc->intbuf = kmalloc(INTBUFSIZE, GFP_KERNEL);
	if (devc->intbuf == NULL) {
		printk(KERN_ERR "ps2sd: can't alloc converting buffer\n");
		return -ENOMEM;
	}
	DPRINT(DBG_INFO, "core%d: allocate %d bytes, 0x%p for conversion\n",
	       devc->core,  INTBUFSIZE, devc->intbuf);

	return reset_buffer(devc);
}

static void fetch_s8(struct ps2sd_sample *s, unsigned char *addr)
{
	char *p = (char *)addr;
	s->r = (int)*p++ * 256;
	s->l = (int)*p++ * 256;
}

static void fetch_s8_m(struct ps2sd_sample *s, unsigned char *addr)
{
	char *p = (char *)addr;
	s->r = (int)*p * 256;
	s->l = (int)*p * 256;
}

static void fetch_u8(struct ps2sd_sample *s, unsigned char *addr)
{
	char *p = (u_char *)addr;
	s->r = ((u_int)*p++ * 256) - 0x8000;
	s->l = ((u_int)*p++ * 256) - 0x8000;
}

static void fetch_u8_m(struct ps2sd_sample *s, unsigned char *addr)
{
	char *p = (u_char *)addr;
	s->r = ((u_int)*p * 256) - 0x8000;
	s->l = ((u_int)*p * 256) - 0x8000;
}

static void fetch_s16le(struct ps2sd_sample *s, unsigned char *addr)
{
	*s = *(struct ps2sd_sample *)addr;
}

static void fetch_s16le_m(struct ps2sd_sample *s, unsigned char *addr)
{
	short *p = (short*)addr;
	s->r = s->l = *p;
}

static void fetch_s16be(struct ps2sd_sample *s, unsigned char *addr)
{
	struct ps2sd_sample *p = (struct ps2sd_sample *)addr;
	s->r = __swab16p(&p->r);
	s->l = __swab16p(&p->l);
}

static void fetch_s16be_m(struct ps2sd_sample *s, unsigned char *addr)
{
	short *p = (short*)addr;
	s->r = s->l = __swab16p(p);
}

static void fetch_u16le(struct ps2sd_sample *s, unsigned char *addr)
{
	struct ps2sd_sample *p = (struct ps2sd_sample *)addr;
	s->r = p->r - 0x8000;
	s->l = p->l - 0x8000;
}

static void fetch_u16le_m(struct ps2sd_sample *s, unsigned char *addr)
{
	short *p = (short*)addr;
	s->r = s->l = *p - 0x8000;
}

static void fetch_u16be(struct ps2sd_sample *s, unsigned char *addr)
{
	struct ps2sd_sample *p = (struct ps2sd_sample *)addr;
	s->r = __swab16p(&p->r) - 0x8000;
	s->l = __swab16p(&p->l) - 0x8000;
}

static void fetch_u16be_m(struct ps2sd_sample *s, unsigned char *addr)
{
	short *p = (short*)addr;
	s->r = s->l = __swab16p(p) - 0x8000;
}

static void fetch_mulaw(struct ps2sd_sample *s, unsigned char *addr)
{
	extern short ps2sd_mulaw2liner16[];
	unsigned char *p = (unsigned char *)addr;
	s->r = ps2sd_mulaw2liner16[(int)*p++];
	s->l = ps2sd_mulaw2liner16[(int)*p++ * 256];
}

static void fetch_mulaw_m(struct ps2sd_sample *s, unsigned char *addr)
{
	extern short ps2sd_mulaw2liner16[];
	unsigned char *p = (unsigned char *)addr;
	s->r = ps2sd_mulaw2liner16[(int)*p];
	s->l = ps2sd_mulaw2liner16[(int)*p];
}

static int
set_format(struct ps2sd_unit_context *devc, int format, int speed, int stereo, int force)
{
	int res;
	char *formatname = "???";

	/* Adjust speed value */
	speed = ALIGN(speed, 25);
	if (speed < 4000) speed = 4000;
	if (SPU2SPEED < speed) speed = SPU2SPEED;

	/* Just return, if specified value are the same as current settings. */
	if (!force &&
	    devc->format == format &&
	    devc->speed == speed &&
	    devc->stereo == stereo)
		return 0;

	/* Ensure to stop any DMA operation and clear the buffer. */
	if (devc->dmastat != DMASTAT_STOP)
		if ((res = stop(devc)) < 0)
			return res;
	reset_error(devc);
	reset_buffer(devc);

	devc->format = format;
	devc->speed = speed;
	devc->stereo = stereo;

	/*
	 * translation paramaters
	 */
	if (format == SPU2FMT && speed == SPU2SPEED && stereo) {
		devc->samplesize = 4;
		devc->noconversion = 1;
		formatname = "SPU2 native";
	} else {
		devc->noconversion = 0;
		devc->cnvsrcrate = speed/25 * 2;
		devc->cnvdstrate = SPU2SPEED/25 * 2;
		devc->cnvd = devc->cnvdstrate / 2;

		switch (format) {
		case AFMT_S8:
			devc->samplesize = stereo ? 2 : 1;
			devc->fetch = stereo ? fetch_s8 : fetch_s8_m;
			formatname = "8bit signed";
			break;
		case AFMT_U8:
			devc->samplesize = stereo ? 2 : 1;
			devc->fetch = stereo ? fetch_u8 : fetch_u8_m;
			formatname = "8bit unsigned";
			break;
		case AFMT_S16_LE:
			devc->samplesize = stereo ? 4 : 2;
			devc->fetch = stereo ? fetch_s16le : fetch_s16le_m;
			formatname = "16bit signed little endian";
			break;
		case AFMT_S16_BE:
			devc->samplesize = stereo ? 4 : 2;
			devc->fetch = stereo ? fetch_s16be : fetch_s16be_m;
			formatname = "16bit signed big endian";
			break;
		case AFMT_U16_LE:
			devc->samplesize = stereo ? 4 : 2;
			devc->fetch = stereo ? fetch_u16le : fetch_u16le_m;
			formatname = "16bit unsigned little endian";
			break;
		case AFMT_U16_BE:
			devc->samplesize = stereo ? 4 : 2;
			devc->fetch = stereo ? fetch_u16be : fetch_u16be_m;
			formatname = "16bit unsigned big endian";
			break;
		case AFMT_MU_LAW:
			devc->samplesize = stereo ? 2 : 1;
			devc->fetch = stereo ? fetch_mulaw : fetch_mulaw_m;
			formatname = "logarithmic mu-Law";
			break;
		case AFMT_A_LAW:
		case AFMT_IMA_ADPCM:
		case AFMT_MPEG:
			devc->samplesize = 1; /* XXX */
			return -EINVAL;
		}
	}

	/*
	 * buffer size
	 */
	if (devc->requested_fragsize == 0) {
		devc->dmabufsize = ps2sd_dmabufsize;
		devc->iopbufsize = ps2sd_iopbufsize;
	} else {
		int fragsize, maxfrags;
		unsigned long flags;

		fragsize = src_to_dest_bytes(devc, devc->requested_fragsize);
		fragsize = ALIGN(fragsize, BUFUNIT);
		if (fragsize < BUFUNIT)
			fragsize = BUFUNIT;
		if (ps2sd_max_iopbufsize/2 < fragsize)
			fragsize = ps2sd_max_iopbufsize/2;
		maxfrags = devc->requested_maxfrags;
		if (maxfrags < 2)
			maxfrags = 2;
		if (ps2sd_max_dmabufsize/fragsize < maxfrags)
			maxfrags = ps2sd_max_dmabufsize/fragsize;
		DPRINT(DBG_INFO, "fragment  req=%dx%d  set=%dx%d\n",
		       devc->requested_fragsize, devc->requested_maxfrags,
		       dest_to_src_bytes(devc, fragsize), maxfrags);
		spin_lock_irqsave(&devc->spinlock, flags);
		devc->iopbufsize = fragsize * 2;
		devc->dmabufsize = fragsize * maxfrags;
		reset_buffer(devc);
		spin_unlock_irqrestore(&devc->spinlock, flags);
	}

	DPRINT(DBG_INFO,
	       "set format=%s speed=%d %s samplesize=%d bufsize=%d(%d)\n",
	       formatname, devc->speed, devc->stereo ? "stereo" : "monaural",
	       devc->samplesize, devc->iopbufsize, devc->dmabufsize);

	return 0;
}

static int
reset_buffer(struct ps2sd_unit_context *devc)
{
	int i;

	/*
	 * initialize IOP buffer context
	 */
	for (i = 0; i < 2; i++) {
		devc->iopbufs[i].size = devc->iopbufsize/2;
		devc->iopbufs[i].iopaddr =
			devc->iopbuf + devc->iopbufs[i].size * i;
		devc->iopbufs[i].dmaid = 0;
	}
	devc->fg = &devc->iopbufs[0];
	devc->bg = &devc->iopbufs[1];

	/*
	 * initialize buffer context
	 */
	devc->dmabufcount = 0;
	devc->dmabufhead = 0;
	devc->dmabuftail = 0;
	devc->dmabufunderflow = 0;
	devc->cnvbufcount = 0;
	devc->cnvbufhead = 0;
	devc->cnvbuftail = 0;
	devc->intbufcount = 0;

	return 0;
}

static int
free_buffer(struct ps2sd_unit_context *devc)
{

	if (!(devc->init & PS2SD_INIT_BUFFERALLOC))
		return (0);

	/* never fail to get the lock */
	ps2sif_lock(ps2sd_mc.lock, "free buffer");

	if (devc->iopbuf != 0) {
		ps2sif_freeiopheap(devc->iopbuf);
		DPRINT(DBG_INFO, "core%d: free %d bytes on IOP 0x%lx\n",
		       devc->core,  devc->iopbufsize, devc->iopbuf);
		devc->iopbuf = -1;
	}
	ps2sif_unlock(ps2sd_mc.lock);
	if (devc->dmabuf != NULL) {
		kfree(devc->dmabuf);
		DPRINT(DBG_INFO, "core%d: free %d bytes, 0x%p for DMA\n",
		       devc->core,  devc->dmabufsize, devc->dmabuf);
		devc->dmabuf = NULL;
		devc->dmabufcount = 0;
	}
	if (devc->cnvbuf != NULL) {
		kfree(devc->cnvbuf);
		DPRINT(DBG_INFO, "core%d: free %d bytes, 0x%p for conversion\n",
		       devc->core,  CNVBUFSIZE, devc->cnvbuf);
		devc->cnvbuf = NULL;
	}
	if (devc->intbuf != NULL) {
		kfree(devc->intbuf);
		DPRINT(DBG_INFO, "core%d: free %d bytes, 0x%p for conversion\n",
		       devc->core,  INTBUFSIZE, devc->intbuf);
		devc->intbuf = NULL;
	}
	devc->init &= ~PS2SD_INIT_BUFFERALLOC;

	return (0);
}

static void
ps2sd_setup(void)
{
	DPRINT(DBG_VERBOSE, "initialize\n");

	/* XXX, nothing to do... */
}

static int
ps2sd_command_end(struct ps2sd_unit_context *devc)
{
	int i, res;
	ps2sd_command cmd;

	DPRINT(DBG_COMMAND, "ps2sd_command_end: ");
	if (!(devc->flags & PS2SD_UNIT_COMMANDMODE)) {
		DPRINTK(DBG_COMMAND, "not in command mode\n");
		return (-EINVAL);
	}
	DPRINTK(DBG_COMMAND, "exit command mode\n");
	DPRINT(DBG_COMMAND, "call PS2SDCTL_COMMAND_QUIT\n");
	res = ps2sif_lock_interruptible(ps2sd_mc.lock, "command_end");
	if (res < 0)
		return (-EBUSY);
	cmd.command = PS2SDCTL_COMMAND_QUIT;
	res = ps2sdcall_remote(&cmd.command, &cmd.result);
	DPRINT(DBG_COMMAND, "command: %x res=%d, 0x%x\n", cmd.command,
	       res, cmd.result);
	cmd.command = PS2SDCTL_COMMAND_QUIT2;
	res = ps2sdcall_remote(&cmd.command, &cmd.result);
	DPRINT(DBG_COMMAND, "command: %x res=%d, 0x%x\n", cmd.command,
	       res, cmd.result);
	ps2sif_unlock(ps2sd_mc.lock);
	ps2sd_setup(); /* reset all settings */
	/* leave command mode */
	for (i = 0; i < ps2sd_nunits; i++)
	    ps2sd_units[i].flags &= ~PS2SD_UNIT_COMMANDMODE;
	return (0);
}

static void
ps2sd_mute(struct ps2sd_unit_context *devc)
{
	DPRINT(DBG_VERBOSE, "core%d: MMIX output off\n", devc->core);
	ps2sdcall_set_reg(SB_SOUND_REG_MMIX(devc->core),
			  ~(SD_MMIX_MINEL | SD_MMIX_MINER |
			    SD_MMIX_MINL | SD_MMIX_MINR));
}

static void
ps2sd_unmute(struct ps2sd_unit_context *devc)
{
	DPRINT(DBG_VERBOSE, "core%d: MMIX output on\n", devc->core);
	ps2sdcall_set_reg(SB_SOUND_REG_MMIX(devc->core),
			  ~( SD_MMIX_MINEL | SD_MMIX_MINER ));
}

static int
start(struct ps2sd_unit_context *devc)
{
	int res, resiop, dmamode;
	unsigned long flags;

	if (devc->dmastat != DMASTAT_STOP /* just avoid verbose messages */
	    || setdmastate(devc, DMASTAT_STOP, DMASTAT_START, "start") < 0)
		return (0);

	CHECKPOINT("start getting lock");
	res = ps2sif_lock_interruptible(ps2sd_mc.lock,
				devc->dmach?"start dma1" : "start dma0");
	if (res < 0) {
		setdmastate(devc, DMASTAT_START, DMASTAT_STOP,
			    "can't get lock");
		return (res);
	}

	if (devc->dmabufcount < devc->iopbufsize/2) {
		setdmastate(devc, DMASTAT_START, DMASTAT_STOP,
			    "no enought data");
		CHECKPOINT("start unlock 1");
		ps2sif_unlock(ps2sd_mc.lock);
		return (0);
	}

	DPRINT(DBG_INFO, "start DMA%d -------------- \n", devc->dmach);

	setdmastate(devc, DMASTAT_START, DMASTAT_RUNNING, "start");

	/*
	 * transfer first data fragment
	 */
	spin_lock_irqsave(&devc->spinlock, flags);
	CHECKPOINT("start fill IOP");
#ifdef PS2SD_FADEINOUT
	{
	    int i, j;
	    short *p = (short*)&devc->dmabuf[devc->dmabufhead];
	    DPRINT(DBG_INFO, "dma%d: fade in\n", devc->dmach);
	    for (i = 0; i < 2; i++) {
		for (j = 0; j < INTRSIZE / sizeof(*p); j++) {
		    int r = 0x10000 * j / (INTRSIZE / sizeof(*p));
		    *p = (((*p * r) >> 16) & 0xffff);
		    p++;
		}
	    }
	    ps2sif_writebackdcache(&devc->dmabuf[devc->dmabufhead], BUFUNIT);
	}
#endif
	devc->bg->dmaid = 0;
	ps2sd_dmaintr(NULL, devc->dmach);
	devc->bg->dmaid = 0;
	devc->dmabufhead += devc->bg->size;
	devc->dmabufhead %= devc->dmabufsize;
	devc->dmabufcount -= devc->bg->size;
	ps2sd_dmaintr(NULL, devc->dmach);
	devc->prevdmaintrvalid = 0;
	spin_unlock_irqrestore(&devc->spinlock, flags);

	/*
	 * start auto DMA IOP -> SPU2
	 */
	devc->phaseerr = 0;
	CHECKPOINT("start DMA");
	dmamode = 0;

	res = ps2sdcall_trans(devc->dmach | dmamode,
			    SD_TRANS_MODE_WRITE|SD_BLOCK_MEM_DRY|SD_BLOCK_LOOP,
			    devc->iopbuf, devc->iopbufsize, 0, &resiop);
	if (res < 0 || resiop < 0) {
		printk(KERN_ERR "ps2sd: can't start DMA%d res=%d resiop=%x\n",
		       devc->dmach, res, resiop);
		DPRINT(DBG_INFO, "ps2sd: can't start DMA%d res=%d resiop=%x\n",
		       devc->dmach, res, resiop);
		setdmastate(devc, DMASTAT_RUNNING, DMASTAT_ERROR,
			    "can't start DMA");
		CHECKPOINT("start unlock 3");
		ps2sif_unlock(ps2sd_mc.lock);
		return -EIO;
	}

	/* start DMA watch timer */
	/*
	 * (devc->iopbufsize/2/4) is samples/sec
	 * (devc->iopbufsize/2/4) / SPU2SPEED is expected interval in second
	 */
	CHECKPOINT("start start timer");
	del_timer(&devc->timer); /* there might be previous timer, delete it */
	devc->timeout = (devc->iopbufsize/2/4) * HZ / SPU2SPEED;
	devc->timeout *= 5; /* 400% margin */
	if (devc->timeout < HZ/20)
		devc->timeout = HZ/20;
	devc->timer.expires = jiffies + devc->timeout;
	add_timer(&devc->timer);

	/*
	 * be sure to set this register after starting auto DMA, 
	 * otherwise you will hear some noise.
	 */
	/* un-mute */
	CHECKPOINT("start set_reg");
	ps2sd_unmute(devc);

	CHECKPOINT("start unlock 4");
	ps2sif_unlock(ps2sd_mc.lock);
	CHECKPOINT("start end");

	return (0);
}

static int
stop(struct ps2sd_unit_context *devc)
{
	setdmastate(devc, DMASTAT_RUNNING, DMASTAT_STOPREQ, "stop request");

	return wait_dma_stop(devc);
}

static int
wait_dma_stop(struct ps2sd_unit_context *devc)
{
	int res, rest;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);

	spin_lock_irqsave(&devc->spinlock, flags);
	add_wait_queue(&devc->dmastat_wq, &wait);
	CHECKPOINT("stop 0");
	res = 0; /* succeed */
	while (devc->dmastat != DMASTAT_STOP) {
		CHECKPOINT("stop sleep");
		set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&devc->spinlock);
		rest = schedule_timeout(HZ);
		spin_lock_irq(&devc->spinlock);
		if (rest == 0) {
			/*
			 * timeout, failed to stop DMA.
			 * DMA might be hunging up...
			 */
			printk(KERN_CRIT "ps2sd: stop DMA%d, timeout\n",
			       devc->dmach);
			setdmastate(devc, devc->dmastat, DMASTAT_ERROR,
				    "stop DMA, timeout");
			CHECKPOINT("stop timeout");
			res = -EBUSY;
			break;
		}
		if (signal_pending(current)) {
			CHECKPOINT("stop interrupted");
			res = -ERESTARTSYS;
			break;
		}
	}
	remove_wait_queue(&devc->dmastat_wq, &wait);
	spin_unlock_irqrestore(&devc->spinlock, flags);

#ifdef PS2SD_DEBUG
	if (ps2sd_debug & DBG_FLUSHONSTOP)
		debuglog_flush(NULL);
#endif

	CHECKPOINT("stop end");
	return res;
}


static int
stop_sequence0(void* arg)
{
	struct ps2sd_unit_context *devc;
	static struct sbr_common_arg carg;
	static struct sbr_sound_trans_arg dmaarg;
	unsigned long flags;
	int res;

	devc = arg;

	DPRINT(DBG_INFO, "stop DMA%d --------------\n", devc->dmach);

	if (setdmastate(devc, DMASTAT_STOPPING, DMASTAT_CANCEL,
			"stop sequence 0") < 0) {
		printk(KERN_CRIT "stop_sequence0: invalid status, %s\n",
		       dmastatnames[devc->dmastat]);
		return (0);
	}

	spin_lock_irqsave(&devc->spinlock, flags);
	reset_buffer(devc);
	wake_up(&devc->write_wq);
	spin_unlock_irqrestore(&devc->spinlock, flags);

	ps2sd_mc.lock_owner = devc;

	dmaarg.channel = devc->dmach;
	dmaarg.mode = SB_SOUND_TRANS_MODE_STOP;
	carg.arg = &dmaarg;
	carg.func = (void(*)(void *, int))stop_sequence;
	carg.para = devc;
	res = sbios(SBR_SOUND_TRANS, &carg);
	if (res < 0) {
		printk("ps2sd: can't stop DMA%d\n", devc->dmach);
		ps2sif_lowlevel_unlock(ps2sd_mc.lock, &devc->lockq);
		setdmastate(devc, DMASTAT_CANCEL, DMASTAT_ERROR,
			    "stop sequence 0, can't stop DMA");
	}

	return (0);
}

static void
stop_sequence(void* arg)
{
	struct ps2sd_unit_context *devc;
#ifndef SPU2_REG_DIRECT_ACCESS
	int res;
#endif

	devc = ps2sd_mc.lock_owner;

	switch (devc->dmastat) {
	case DMASTAT_CANCEL:
		if (arg != NULL)
			DPRINT(DBG_INFO,
			       "stop DMA resiop = %d\n", *(int*)arg);
		else
			DPRINT(DBG_INFO,
			       "stop DMA timeout\n");

		res = ps2sdcall_set_reg(SB_SOUND_REG_MMIX(devc->core),
					~(SD_MMIX_MINEL | SD_MMIX_MINER |
					  SD_MMIX_MINL | SD_MMIX_MINR));
		setdmastate(devc, DMASTAT_CANCEL, DMASTAT_STOP,
			    "stop sequence");
		ps2sif_lowlevel_unlock(ps2sd_mc.lock, &devc->lockq);
		break;
	case DMASTAT_CLEAR:
		if (arg != NULL)
			DPRINT(DBG_INFO,
			       "clear RPC resiop = %d\n", *(int*)arg);
		else
			DPRINT(DBG_INFO,
			       "clear RPC timeout\n");
		setdmastate(devc, DMASTAT_CLEAR, DMASTAT_STOP,
			    "stop sequence");
		ps2sif_lowlevel_unlock(ps2sd_mc.lock, &devc->lockq);
		break;
	default:
		setdmastate(devc, DMASTAT_CLEAR, DMASTAT_STOP,
			    "stop sequence, state error");
		ps2sif_lowlevel_unlock(ps2sd_mc.lock, &devc->lockq);
		break;
	}
}

static int
reset_error(struct ps2sd_unit_context *devc)
{
	int res;

	/*
	 * get the lock
	 */
	res = ps2sif_lock_interruptible(ps2sd_mc.lock, "reset error");
	if (res < 0) {
		DPRINT(DBG_INFO,
		       "reset_error(): can't get the lock(interrupted)\n");
		return (res);
	}

	/*
	 * Did any error occurr?
	 */
	if (devc->dmastat != DMASTAT_ERROR /* just avoid verbose message */
	    || setdmastate(devc, DMASTAT_ERROR, DMASTAT_RESET,
			   "reset") < 0) {
		/* no error */
		ps2sif_unlock(ps2sd_mc.lock);
		return (0);
	}

	/* ok, we got in error recovery sequence */

	/*
	 * reset all
	 */
	/*
	 * FIX ME!
	 */
	DPRINT(DBG_INFO, "CLEAR ERROR(not implemented)\n");

	/*
	 * unlock and enter normal status
	 */
	setdmastate(devc, DMASTAT_RESET, DMASTAT_STOP, "reset");
	ps2sif_unlock(ps2sd_mc.lock);

	return (0);
}

#ifdef CONFIG_T10000_DEBUG_HOOK
static void
ps2sd_debug_proc(int c)
{
	int i;

	switch (c) {
	case 't':
		ps2sd_debug |= DBG_TRACE;
		printk("ps2sd: debug flags=%08lx\n", ps2sd_debug);
		break;
	case 'T':
		ps2sd_debug |= (DBG_TRACE | DBG_VERBOSE);
		printk("ps2sd: debug flags=%08lx\n", ps2sd_debug);
		break;
	case 'A':
		ps2sd_debug = 0x7fffffff;
		printk("ps2sd: debug flags=%08lx\n", ps2sd_debug);
		break;
	case 'c':
		ps2sd_debug = ps2sd_normal_debug;
		printk("ps2sd: debug flags=%08lx\n", ps2sd_debug);
		break;
	case 'C':
		ps2sd_debug = 0;
		printk("ps2sd: debug flags=%08lx\n", ps2sd_debug);
		break;
	case 'd':
		printk("ps2sd: lock=%d %s\n", (int)ps2sd_mc.lock->owner,
		       ps2sd_mc.lock->ownername?ps2sd_mc.lock->ownername:"");
		for (i = 0; i < 2; i++) {
		  struct ps2sd_unit_context *devc;
		  devc = ps2sd_lookup_by_dmach(i);
		  printk("ps2sd: core%d %s dma=%s underflow=%d total=%8d",
			 i,
			 (devc->flags & PS2SD_UNIT_OPENED) ? "open" : "close",
			 dmastatnames[devc->dmastat],
			 devc->dmabufunderflow,
			 devc->total_output_bytes);
		  printk(" intr=%d count=%d/%d/%d\n",
			 devc->intr_count,
			 devc->cnvbufcount,
			 devc->intbufcount,
			 devc->dmabufcount);
		  printk("ps2sd: checkpoint=%s phaseerr=%d\n",
			 devc->debug_check_point, devc->phaseerr);
		}
		break;
	case 'D':
#ifdef PS2SD_DEBUG
		debuglog_flush(NULL);
#endif
		break;
	}
}
#endif /* CONFIG_T10000_DEBUG_HOOK */

#ifdef PS2SD_DEBUG
static void
ps2sd_print_debug_flags(void)
{
	printk(KERN_CRIT "bits for ps2sd_debug:\n");
	printk(KERN_CRIT "%13s: %08x\n", "verbose",	DBG_VERBOSE);
	printk(KERN_CRIT "%13s: %08x\n", "information",	DBG_INFO);
	printk(KERN_CRIT "%13s: %08x\n", "interrupt",	DBG_INTR);
	printk(KERN_CRIT "%13s: %08x\n", "diagnostic",	DBG_DIAG);
	printk(KERN_CRIT "%13s: %08x\n", "write",	DBG_WRITE);
	printk(KERN_CRIT "%13s: %08x\n", "mixer",	DBG_MIXER);
	printk(KERN_CRIT "%13s: %08x\n", "ioctl",	DBG_IOCTL);
	printk(KERN_CRIT "%13s: %08x\n", "dma stat",	DBG_DMASTAT);
	printk(KERN_CRIT "%13s: %08x\n", "trace",	DBG_TRACE);
	printk(KERN_CRIT "%13s: %08x\n", "RPC",		DBG_RPC);
	printk(KERN_CRIT "%13s: %08x\n", "RPC server",	DBG_RPCSVC);
	printk(KERN_CRIT "%13s: %08x\n", "command mode",DBG_COMMAND);
	printk(KERN_CRIT "%13s: %08x\n", "flush on stop",DBG_FLUSHONSTOP);
	printk(KERN_CRIT "%13s: %08x\n", "through",	DBG_THROUGH);
}
#endif

static int __devinit ps2sd_probe(struct platform_device *dev)
{
	int i, res, resiop;

	printk("PlayStation 2 Sound driver");
	printk("\n");

	ps2sd_mc.init = 0;
	ps2sd_mc.iopzero = 0;
	ps2sd_normal_debug = ps2sd_debug;
	spin_lock_init(&ps2sd_mc.spinlock);

#ifdef PS2SD_DEBUG
	if (ps2sd_debug == -1) {
		ps2sd_print_debug_flags();
		res = -EINVAL;
		goto error_out;
	}
#endif

	if ((ps2sd_mc.lock = ps2sif_getlock(PS2LOCK_SOUND)) == NULL) {
		printk(KERN_ERR "ps2sd: Can't get lock\n");
		res = -EIO;
		goto error_out;
	}

	/* allocate zero buffer on IOP */
	ps2sd_mc.iopzero = ps2sif_allociopheap(PS2SD_SPU2PCMBUFSIZE);
	if(ps2sd_mc.iopzero == 0) {
		printk(KERN_ERR "ps2sd: can't alloc iop heap\n");
		res = -ENOMEM;
		goto error_out;
	}
	DPRINT(DBG_INFO, "allocate %d bytes on IOP 0x%p\n",
	       PS2SD_SPU2PCMBUFSIZE, ps2sd_mc.iopzero);

	/* adjust buffer size */
	ps2sd_max_iopbufsize = adjust_bufsize(ps2sd_max_iopbufsize);
	ps2sd_max_dmabufsize = ALIGN(ps2sd_max_dmabufsize,
				     ps2sd_max_iopbufsize);
	ps2sd_iopbufsize = adjust_bufsize(ps2sd_iopbufsize);
	if (ps2sd_max_iopbufsize < ps2sd_iopbufsize)
		ps2sd_iopbufsize = ps2sd_max_iopbufsize;
	ps2sd_dmabufsize = ALIGN(ps2sd_dmabufsize, ps2sd_iopbufsize);

	DPRINT(DBG_INFO, "iopbufsize %3dKB (max %3dKB)\n",
	       ps2sd_iopbufsize / 1024, ps2sd_max_iopbufsize / 1024);
	DPRINT(DBG_INFO, "dmabufsize %3dKB (max %3dKB)\n",
	       ps2sd_dmabufsize / 1024, ps2sd_max_dmabufsize / 1024);

	/* get lock */
	res = ps2sif_lock_interruptible(ps2sd_mc.lock, "sd init");
	if (res < 0)
		goto error_out;

	/* XXX, suppress output before init */
	ps2sdcall_set_reg(SB_SOUND_REG_MVOLR(1), 0);
	ps2sdcall_set_reg(SB_SOUND_REG_MVOLL(1), 0);

	res = ps2sdcall_init(SB_SOUND_INIT_COLD, &resiop);
	if (res != 0 || resiop < 0) {
		ps2sif_unlock(ps2sd_mc.lock);
		if (res == 0) {
			res = -EIO;
		}
		goto error_out;
	}
	ps2sd_mc.init |= PS2SD_INIT_IOP;

	/* setup SPU2 registers */
	ps2sd_setup();

	/* setup SPDIF */
	if (ps2_bootinfo->sysconf.spdif == 0) {
		DPRINT(DBG_INFO, "SPDIF output on\n");
		ps2sdcall_set_coreattr(SB_SOUND_CA_SPDIF_MODE,
				       SD_SPDIF_OUT_PCM |
				       SD_SPDIF_COPY_PROHIBIT |
				       SD_SPDIF_MEDIA_CD);
	} else {
		DPRINT(DBG_INFO, "SPDIF output off\n");
		ps2sdcall_set_coreattr(SB_SOUND_CA_SPDIF_MODE,
				       SD_SPDIF_OUT_OFF);
	}

	/* release lock */
	ps2sif_unlock(ps2sd_mc.lock);

	ps2sd_mc.init |= PS2SD_INIT_UNIT;

	res = ps2sd_attach_unit(&ps2sd_units[0], 0, 0, &ps2sd_mixers[0],
			      UNIT0_FLAGS);
	if (res < 0)
		goto error_out;

	res = ps2sd_attach_unit(&ps2sd_units[1], 1, 1, &ps2sd_mixers[0],
			      UNIT1_FLAGS);
	if (res < 0)
		goto error_out;

	res = ps2sd_attach_unit(&ps2sd_units[2], 1, 1, &ps2sd_mixers[0],
			      UNIT2_FLAGS);
	if (res < 0)
		goto error_out;
	ps2sd_units[2].iopmemlist = &ps2sd_unit2_iopmemlist;

#ifdef PS2SD_USE_THREAD
	/*
	 * start interrupt service thread
	 */
	init_waitqueue_head(&ps2sd_mc.intr_wait);
	init_completion(&ps2sd_mc.ack_comp);
	ps2sd_mc.sd_task = kthread_run(ps2sd_thread, NULL, "ps2sd");
	if (ps2sd_mc.sd_task == NULL) {
		printk(KERN_ERR "ps2sd: can't start thread\n");
		res = -1;
		goto error_out;
	}
	/* wait for the thread to start */
	wait_for_completion(&ps2sd_mc.ack_comp);
	ps2sd_mc.init |= PS2SD_INIT_THREAD;
#endif 

	/* initialize mixer device */
	mixer_dummy_channel.regr = -1;
	mixer_dummy_channel.regl = -1;
	mixer_dummy_channel.name = "dummy";

	ps2sd_mixers[0].channels[SOUND_MIXER_VOLUME] = 
		&ps2sd_units[1].mixer_main;
	ps2sd_mixers[0].channels[SOUND_MIXER_PCM] =
		&ps2sd_units[0].mixer_pcm;
	ps2sd_mixers[0].channels[SOUND_MIXER_ALTPCM] =
		&ps2sd_units[1].mixer_pcm;

	ps2sd_mixers[0].channels[SOUND_MIXER_BASS] = &mixer_dummy_channel;
	ps2sd_mixers[0].channels[SOUND_MIXER_TREBLE] = &mixer_dummy_channel;
	ps2sd_mixers[0].channels[SOUND_MIXER_SYNTH] = &mixer_dummy_channel;

	ps2sd_mixers[0].devmask = 0;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if (ps2sd_mixers[0].channels[i] != NULL)
			ps2sd_mixers[0].devmask |= 1 << i;

	ps2sd_units[0].mixer_main.name = "core0 volume";
	ps2sd_units[0].mixer_pcm.name = "pcm";
	ps2sd_units[1].mixer_main.name = "master volume";
	ps2sd_units[1].mixer_pcm.name = "alternate pcm";
	ps2sd_units[1].mixer_extrn.name = "core0->core1";

	ps2sdmixer_setvol(&mixer_dummy_channel, 50, 50);
	ps2sdmixer_setvol(&ps2sd_units[0].mixer_main, 100, 100);
	ps2sdmixer_setvol(&ps2sd_units[0].mixer_pcm, 50, 50);
	/* external input volume is disable in CORE0 
	   ps2sdmixer_setvol(&ps2sd_units[0].mixer_extrn, 100, 100);
	*/
	ps2sdmixer_setvol(&ps2sd_units[1].mixer_main, 50, 50);
	ps2sdmixer_setvol(&ps2sd_units[1].mixer_pcm, 50, 50);
	ps2sdmixer_setvol(&ps2sd_units[1].mixer_extrn, 100, 100);

	/* register mixer device */
	ps2sd_mixers[0].mixer = register_sound_mixer(&ps2sd_mixer_fops, -1);
	if (ps2sd_mixers[0].mixer < 0) {
		printk(KERN_ERR "ps2sd: Can't install mixer device\n");
	}
	DPRINT(DBG_INFO, "register_sound_mixer() = %d\n",
	       ps2sd_mixers[0].mixer);
	ps2sd_mc.init |= PS2SD_INIT_REGMIXERDEV;

#ifdef CONFIG_T10000_DEBUG_HOOK
	if (ps2sd_debug_hook) {
		extern void (*ps2_debug_hook[0x80])(int c);
		char *p = "tTAcCdD";
		DPRINT(DBG_INFO, "install debug hook '%s'\n", p);
		while (*p)
			ps2_debug_hook[(int)*p++] = ps2sd_debug_proc;
	}
#endif

	return 0;

 error_out:
	printk("ps2sd: init failed res = %d\n", res);
	ps2sd_cleanup();

	return res;
}

static int __devexit ps2sd_driver_remove(struct platform_device *pdev)
{
	ps2sd_cleanup();

	return 0;
}

static void ps2sd_cleanup(void)
{
	int i, resiop;

	if (ps2sd_mc.lock)
		ps2sif_lock(ps2sd_mc.lock, "cleanup");

	if (ps2sd_mc.iopzero != 0) {
		ps2sif_freeiopheap(ps2sd_mc.iopzero);
		DPRINT(DBG_INFO, "free %d bytes on IOP 0x%p\n",
		       PS2SD_SPU2PCMBUFSIZE, ps2sd_mc.iopzero);
		ps2sd_mc.iopzero = 0;
	}

#ifdef PS2SD_USE_THREAD
	/*
	 * stop thread
	 */
	if (ps2sd_mc.init & PS2SD_INIT_THREAD) {
		DPRINT(DBG_VERBOSE, "stop thread\n");
		kthread_stop(ps2sd_mc.sd_task);
	}
	ps2sd_mc.init &= ~PS2SD_INIT_THREAD;
#endif

	if (ps2sd_mc.init & PS2SD_INIT_UNIT)
		for (i = 0; i < ps2sd_nunits; i++)
		       	ps2sd_detach_unit(&ps2sd_units[i]);
	ps2sd_mc.init &= ~PS2SD_INIT_UNIT;

	if (ps2sd_mc.init & PS2SD_INIT_REGMIXERDEV)
		unregister_sound_mixer(ps2sd_mixers[0].mixer);
	ps2sd_mc.init &= ~PS2SD_INIT_REGMIXERDEV;

	if (ps2sd_mc.init & PS2SD_INIT_IOP)
		ps2sdcall_end(&resiop);
	ps2sd_mc.init &= ~PS2SD_INIT_IOP;

	ps2sd_mc.init = 0;

#ifdef CONFIG_T10000_DEBUG_HOOK
	if (ps2sd_debug_hook) {
		extern void (*ps2_debug_hook[0x80])(int c);
		char *p = "tTAcCdD";
		DPRINT(DBG_INFO, "clear debug hook '%s'\n", p);
		while (*p)
			ps2_debug_hook[(int)*p++] = NULL;
	}
#endif

	if (ps2sd_mc.lock)
		ps2sif_unlock(ps2sd_mc.lock);
}

static struct platform_driver ps2sd_driver = {
	.probe	= ps2sd_probe,
	.remove	= __devexit_p(ps2sd_driver_remove),
	.driver	= {
		.name	= "ps2sd",
		.owner	= THIS_MODULE,
	},
};

static int __init ps2sd_init(void)
{
	return platform_driver_register(&ps2sd_driver);
}

static void __exit ps2sd_exit(void)
{
	platform_driver_unregister(&ps2sd_driver);
	return;
}

module_init(ps2sd_init);
module_exit(ps2sd_exit);

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 sound driver");
MODULE_LICENSE("GPL");
