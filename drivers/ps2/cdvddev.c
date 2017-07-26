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

#include <asm/uaccess.h>

#include "cdvd.h"

static int ps2cdvd_open(struct cdrom_device_info *, int);
static void ps2cdvd_release(struct cdrom_device_info *);
static int ps2cdvd_media_changed(struct cdrom_device_info *, int);
static int ps2cdvd_tray_move(struct cdrom_device_info *, int);
static int ps2cdvd_drive_status(struct cdrom_device_info *, int);
static int ps2cdvd_lock_door(struct cdrom_device_info *, int);
static int ps2cdvd_select_speed(struct cdrom_device_info *, int);
static int ps2cdvd_audio_ioctl(struct cdrom_device_info *, unsigned int, void*);
static int ps2cdvd_dev_ioctl(struct cdrom_device_info *, unsigned int, unsigned long);

static inline int verify_area(int type, const void *addr, unsigned long size)
{
	return access_ok(type, addr, size) ? 0 : -EFAULT;
}


static int
ps2cdvd_open(struct cdrom_device_info * cdi, int purpose)
{

	DPRINT(DBG_INFO, "open\n");

	return 0;
}

static void
ps2cdvd_release(struct cdrom_device_info * cdi)
{
	DPRINT(DBG_INFO, "release\n");
	ps2cdvd_lock("cdvd_release");
	if (ps2cdvd.stream_start) {
	  ps2cdvdcall_dastream(PS2CDVD_STREAM_STOP, 0, 0, NULL,
			       &ps2cdvd.cdda_mode);
	  ps2cdvd.stream_start = 0;
	}
	ps2cdvd_unlock();
}

static int
ps2cdvd_media_changed(struct cdrom_device_info * cdi, int disc_nr)
{
	int res;
	ps2cdvd_lock("cdvd_release");
	res = ps2cdvd.disc_changed ? 1 : 0;
	ps2cdvd.disc_changed = 0;
	ps2cdvd_unlock();
	return res;
}

static int 
ps2cdvd_tray_move(struct cdrom_device_info * cdi, int position)
{
	int res;

	if ((res = ps2cdvd_lock_interruptible("tray_move")) != 0)
		return (res);
	ps2cdvd.traycount++;
	if (position) {
		/* open */
		DPRINT(DBG_INFO, "tray open request\n");
		res = ps2cdvdcall_trayreq(SCECdTrayOpen, NULL);
	} else {
		/* close */
		DPRINT(DBG_INFO, "tray close request\n");
		res = ps2cdvdcall_trayreq(SCECdTrayClose, NULL);
	}
	ps2cdvd_unlock();

	return (res);
}

static int
ps2cdvd_drive_status(struct cdrom_device_info *cdi, int arg)
{
	int res;

	/* spinup and re-check disc if the state is in IDLE */
	if (ps2cdvd_lock_ready() != 0)
		return CDS_NO_INFO;

	switch (ps2cdvd.state) {
	case STAT_WAIT_DISC:
	case STAT_INVALID_DISC:
		DPRINT(DBG_INFO, "drive_status=NO_DISC\n");
		res = CDS_NO_DISC;
		break;

	case STAT_READY:
	case STAT_READ:
	case STAT_READ_ERROR_CHECK:
		DPRINT(DBG_INFO, "drive_status=DISC_OK\n");
		res = CDS_DISC_OK;
		break;

	case STAT_ERROR:
	default:
		DPRINT(DBG_INFO, "drive_status=NO_INFO\n");
		res = CDS_NO_INFO;
		break;
	}
	ps2cdvd_unlock();

	return res;
}

static int
ps2cdvd_lock_door(struct cdrom_device_info *cdi, int lock)
{
	int res;

	if ((res = ps2cdvd_lock_interruptible("tray_move")) != 0)
		return (res);
	if (lock) {
		ps2cdvd.disc_locked = 1;
		DPRINT(DBG_DLOCK, "disc is locked\n");
		if (ps2cdvd.label_valid) {
		  ps2cdvd.disc_lock_key =
		  	ps2cdvd_checksum((u_long*)ps2cdvd.labelbuf,
					 2048/sizeof(u_long));
		  ps2cdvd.disc_lock_key_valid = 1;
		  DPRINT(DBG_DLOCK, "disc lock key=%lX\n",
			 ps2cdvd.disc_lock_key);
		} else {
		  ps2cdvd.disc_lock_key_valid = 0;
		  DPRINT(DBG_DLOCK, "disc lock key=****\n");
		}
	} else {
		/* unlock */
		DPRINT(DBG_DLOCK, "disc is unlocked\n");
		ps2cdvd.disc_locked = 0;
	}
	ps2cdvd_unlock();

	return 0;
}

static int
ps2cdvd_select_speed(struct cdrom_device_info *cdi, int speed)
{
	int ps2_speed;

	if (speed == 0) {
	  DPRINT(DBG_INFO, "select speed, Normal\n");
	  ps2_speed = SCECdSpinNom;
	} else if (speed == 1) {
	  DPRINT(DBG_INFO, "select speed, x1\n");
	  ps2_speed = SCECdSpinX1;
	} else if (2 <= speed && speed < 4) {
	  DPRINT(DBG_INFO, "select speed, x2\n");
	  ps2_speed = SCECdSpinX2;
	} else if (4 <= speed && speed < 8) {
	  DPRINT(DBG_INFO, "select speed, x4\n");
	  ps2_speed = SCECdSpinX4;
	} else if (8 <= speed && speed <= 12) {
	  DPRINT(DBG_INFO, "select speed, x12\n");
	  ps2_speed = SCECdSpinX12;
	} else if (12 < speed && speed) {
	  DPRINT(DBG_INFO, "select speed, Max\n");
	  ps2_speed = SCECdSpinMx;
	} else {
	  ps2_speed = SCECdSpinNom;
	}

	ps2cdvd.data_mode.spindlctrl = ps2_speed;
	ps2cdvd.cdda_mode.spindlctrl = ps2_speed;

	return 0;
}

static int
ps2cdvd_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd, void *arg)
{
   int res;

   switch (cmd) {
   case CDROMSTART:     /* Spin up the drive */
   case CDROMSTOP:      /* Spin down the drive */
   case CDROMPAUSE:     /* Pause the drive */
     return 0;	/* just ignore it */
     break;

   case CDROMRESUME:    /* Start the drive after being paused */
   case CDROMPLAYMSF:   /* Play starting at the given MSF address. */
     return -EINVAL;
     break;

   case CDROMREADTOCHDR:        /* Read the table of contents header */
      {
         struct cdrom_tochdr *hdr;
	 struct ps2cdvd_tocentry *toc = (struct ps2cdvd_tocentry *)ps2cdvd.tocbuf;
         
	 if ((res = ps2cdvd_lock_ready()) != 0)
	   return (res);

	 if (!ps2cdvd.toc_valid) {
	   DPRINT(DBG_VERBOSE, "TOC is not valid\n");
	   ps2cdvd_unlock();
	   return -EIO;
	 }
         
         hdr = (struct cdrom_tochdr *) arg;
         hdr->cdth_trk0 = decode_bcd(toc[0].abs_msf[0]);
         hdr->cdth_trk1 = decode_bcd(toc[1].abs_msf[0]);
	 ps2cdvd_unlock();
      }
      return 0;

   case CDROMREADTOCENTRY:      /* Read a given table of contents entry */
      {
	 struct ps2cdvd_tocentry *toc = (struct ps2cdvd_tocentry *)ps2cdvd.tocbuf;
         struct cdrom_tocentry *entry;
         int idx;
         
	 if ((res = ps2cdvd_lock_ready()) != 0)
	   return (res);

	 if (!ps2cdvd.toc_valid) {
	   DPRINT(DBG_VERBOSE, "TOC is not valid\n");
	   ps2cdvd_unlock();
	   return -EIO;
	 }
         
         entry = (struct cdrom_tocentry *) arg;
         
	 if (entry->cdte_track == CDROM_LEADOUT)
	   entry->cdte_track = 102;
	 for (idx = 0; idx <= 102; idx++) {
	   if (decode_bcd(toc[idx].indexno) == entry->cdte_track) break;
	 }
	 if (102 < idx) {
	   DPRINT(DBG_DIAG, "Can't find track %d(0x%02x)\n",
		  entry->cdte_track, entry->cdte_track);
	   ps2cdvd_unlock();
	   return -EINVAL;
	 }

         entry->cdte_adr = toc[idx].addr;
         entry->cdte_ctrl = toc[idx].ctrl;
         
         /* Logical buffer address or MSF format requested? */
         if (entry->cdte_format == CDROM_LBA) {
            entry->cdte_addr.lba = msftolba(toc[idx].abs_msf[0],
					    toc[idx].abs_msf[1],
					    toc[idx].abs_msf[2]);
         } else
	 if (entry->cdte_format == CDROM_MSF) {
	   entry->cdte_addr.msf.minute = decode_bcd(toc[idx].abs_msf[0]);
	   entry->cdte_addr.msf.second = decode_bcd(toc[idx].abs_msf[1]);
	   entry->cdte_addr.msf.frame = decode_bcd(toc[idx].abs_msf[2]);
         } else {
	   ps2cdvd_unlock();
	   return -EINVAL;
	 }
	 ps2cdvd_unlock();
      }
      return 0;
      break;

   case CDROMPLAYTRKIND:     /* Play a track.  This currently ignores index. */
   case CDROMVOLCTRL:   /* Volume control.  What volume does this change, anyway? */
   case CDROMSUBCHNL:   /* Get subchannel info */
   default:
      return -EINVAL;
   }
}

static int
ps2cdvd_dev_ioctl(struct cdrom_device_info *cdi,
		    unsigned int  cmd,
		    unsigned long arg)
{
#ifdef PS2CDVD_CDDA
	int res;
#endif

	switch (cmd) {
#ifdef PS2CDVD_CDDA
	case CDROMREADAUDIO:      /* Read 2352 byte audio tracks and 2340 byte
				     raw data tracks. */
	  {
	    struct cdrom_read_audio param;

	    if ((res = ps2cdvd_lock_ready()) != 0)
	      return (res);

	    if (!ps2cdvd.toc_valid) {
	      DPRINT(DBG_DIAG, "TOC is not valid\n");
	      ps2cdvd_unlock();
	      return -EIO;
	    }
	    ps2cdvd_unlock();

	    if(copy_from_user(&param, (char *)arg, sizeof(param)))
	      return -EFAULT;

	    if (param.nframes == 0)
	      return 0;

	    res = verify_area(VERIFY_WRITE, param.buf,
			      CD_FRAMESIZE_RAW * param.nframes);
	    if(res < 0) return res;

	    switch (param.addr_format) {
	    case CDROM_LBA:
	      break;
	    case CDROM_MSF:
	      if (60 <= param.addr.msf.second ||
		  75 <= param.addr.msf.frame)
		return -EINVAL;
	      param.addr.lba = msftolba(param.addr.msf.minute * 4500,
					param.addr.msf.second * 75,
					param.addr.msf.frame);
	      break;
	    default:
	      return -EINVAL;
	    }


            if (ps2cdvd.leadout_start <= param.addr.lba ||
		ps2cdvd.leadout_start < param.addr.lba + param.nframes) {
	      DPRINT(DBG_DIAG,
		     "out of range: leadout_start=%ld lba=%d nframes=%d\n",
		     ps2cdvd.leadout_start, param.addr.lba, param.nframes);
	      return -EINVAL;
	    }

	    while (0 < param.nframes) {
	      if ((res = ps2cdvd_lock_ready()) != 0)
		return (res);
	      ps2cdvd.sectoidle = ps2cdvd_spindown;
	      if (ps2cdvd.databuf_addr <= param.addr.lba &&
		  param.addr.lba < ps2cdvd.databuf_addr+ps2cdvd.databuf_nsects) {
		/* we have at least one block in the cache */
		int off = param.addr.lba - ps2cdvd.databuf_addr;
		int n = MIN(ps2cdvd.databuf_nsects - off, param.nframes);
		if (copy_to_user(param.buf, 
				 ps2cdvd.databuf + AUDIO_SECT_SIZE * off,
				 AUDIO_SECT_SIZE * n)) {
		  ps2cdvd_unlock();
		  return -EFAULT;
		}
		param.addr.lba += n;
		param.nframes -= n;
		param.buf += AUDIO_SECT_SIZE * n;
	      } else {
		/* fill cache */
		int n;

		/*
		 * early firmware, before rev. 1.7.0, can't start
		 * reading at a sector which doesn't have an address
		 * in it's SUBQ channel. So, you must retry here.
		 */
		for (n = 0; n < ps2cdvd_databuf_size; n++) {
		  ps2cdvd.databuf_nsects = 0;
		  res = ps2cdvdcall_read(param.addr.lba - n,
					 ps2cdvd_databuf_size,
					 ps2cdvd.databuf, &ps2cdvd.cdda_mode);
		  if (res < 0) {
		    DPRINT(DBG_DIAG, "ps2cdvdcall_read() failed\n");
		    res = -EIO;
		    break;
		  }
		  if ((res = ps2cdvdcall_geterror()) == SCECdErNO) {
		    ps2cdvd.databuf_addr = param.addr.lba - n;
		    ps2cdvd.databuf_nsects = ps2cdvd_databuf_size;
		    res = 0;
		    break;
		  }
		  if (param.addr.lba - n == 0) {
		    res = -EIO;
		    break;
		  }
		  if (signal_pending(current)) {
		    res = -ERESTART;
		    break;
		  }
		  if (res != SCECdErEOM) {
		    DPRINT(DBG_DIAG, "error=%d, retry on sector %d-%d\n",
			   res, param.addr.lba, n + 1);
		  }
		  res = -EIO;
		}
		if (res != 0) {
		  ps2cdvd_unlock();
		  return (res);
		}
	      }
	      ps2cdvd_unlock();
	    }
	    return 0;
	  }
	  break;
	case PS2CDVDIO_DASTREAM: 
	  {
	    struct ps2cdvd_dastream_command param;
	    int size, sectsize;

	    if ((res = ps2cdvd_lock_ready()) != 0)
	      return (res);

	    if (!ps2cdvd.toc_valid) {
	      DPRINT(DBG_DIAG, "TOC is not valid\n");
	      ps2cdvd_unlock();
	      return -EIO;
	    }
	    ps2cdvd_unlock();

	    if(copy_from_user(&param, (char *)arg, sizeof(param)))
	      return -EFAULT;

	    switch(param.rmode.datapattern){
	    case SCECdSecS2352:	sectsize = 2352; break;
	    case SCECdSecS2368:	sectsize = 2368; break;
	    case SCECdSecS2448: sectsize = 2448; break;
	    default:		sectsize = 2352; break;
	    }
	    size = MIN(ps2cdvd_databuf_size,param.sectors) * sectsize;

	    /*
	     * get the lock and call SBIOS
	     */
	    ps2cdvd_lock("cdvd_dastream");
	    ps2cdvd.databuf_nsects = 0;
	    res = ps2cdvdcall_dastream(param.command, param.lbn, size,
				       ps2cdvd.databuf, &param.rmode);
	    if (param.command == PS2CDVD_STREAM_START ||
		param.command == PS2CDVD_STREAM_SEEK)
	      ps2cdvd.stream_start = 1;
	    if (param.command == PS2CDVD_STREAM_STOP)
	      ps2cdvd.stream_start = 0;
	    ps2cdvd_unlock();
	    if(res < 0) return -EIO;

	    param.result = res;
	    if (0 < res) {
	      if (param.command == PS2CDVD_STREAM_READ) {
		res = verify_area(VERIFY_WRITE, param.buf, param.result);
		if(res < 0) return res;
	      }
	      copy_to_user(param.buf, ps2cdvd.databuf, param.result);
	    }

	    return (copy_to_user((char*)arg, &param, sizeof(param))?-EFAULT:0);
	  }

	  break;
	case PS2CDVDIO_READSUBQ: 
	  {
	    struct ps2cdvd_subchannel param;

	    if(copy_from_user(&param, (char*)arg, sizeof(param)))
	      return -EFAULT;
	    if ((res = ps2cdvd_lock_interruptible("cdvd_readsubq")) != 0)
		return (res);
	    res = ps2cdvdcall_readsubq(param.data, &param.stat);
	    ps2cdvd_unlock();
	    if (res < 0)
	      return -EIO;

	    return (copy_to_user((char*)arg, &param, sizeof(param))?-EFAULT:0);
	  }
	  break;
#endif /* PS2CDVD_CDDA */

	case PS2CDVDIO_READMODE1:
	  {
	    ps2cdvd_read cdvdread;
	    int res;

	    if(copy_from_user(&cdvdread, (char*)arg, sizeof(cdvdread)))
	      return -EFAULT;

	    if (ps2cdvd_databuf_size < cdvdread.sectors)
	      return -EINVAL;

    	    if ((res = ps2cdvd_lock_ready()) != 0) 
    	      return (res); 
	    ps2cdvd.databuf_nsects = 0;
	    res = ps2cdvdcall_read(cdvdread.lbn, cdvdread.sectors,
				   ps2cdvd.databuf, &ps2cdvd.data_mode);
	    if (res < 0) {
	      DPRINT(DBG_DIAG, "ps2cdvdcall_read() failed\n");
	      res = -EIO;
	      goto readmode1_out;
	    }
	    if (ps2cdvdcall_geterror() != SCECdErNO) {
	      res = -EIO;
	      goto readmode1_out;
	    }
	    if (copy_to_user((char *)cdvdread.buf, ps2cdvd.databuf,
			     cdvdread.sectors * DATA_SECT_SIZE))
	      res = -EFAULT;
	    
	  readmode1_out:
	    ps2cdvd_unlock();
	    return res;
	  }
	  break;
	case PS2CDVDIO_CHANGETRYCNT:
	  {
	    if ((res = ps2cdvd_lock_interruptible("change_retrycount")) != 0)
	      return (res);
	    ps2cdvd.data_mode.trycount = arg;
	    ps2cdvd.cdda_mode.trycount = arg;
	    ps2cdvd_unlock();
	    return 0;
	  }
	  break;
	}

	return ps2cdvd_common_ioctl(cmd, arg);
}

int
ps2cdvd_common_ioctl(unsigned int  cmd, unsigned long arg)
{
	int res;

	switch (cmd) {
	case CDROM_SELECT_SPEED:
	    if ((res = ps2cdvd_lock_interruptible("select_speed")) != 0)
		return (res);
	    res = ps2cdvd_select_speed(NULL, arg);
	    ps2cdvd_unlock();
	    return (res);

	case PS2CDVDIO_GETDISCTYPE:
	  {
	    int type;

	    if ((res = ps2cdvd_lock_interruptible("getdisctype")) != 0)
		return (res);
	    res = ps2cdvdcall_gettype(&type);
	    ps2cdvd_unlock();
	    if (res < 0)
	      return -EIO;

	    return (copy_to_user((char*)arg, &type, sizeof(type))?-EFAULT:0);
	  }
	  break;
	case PS2CDVDIO_RCBYCTL:
	  {
	    struct ps2cdvd_rcbyctl rcbyctl;

	    if (copy_from_user(&rcbyctl, (char *)arg, sizeof(rcbyctl)))
		return -EFAULT;
	    if ((res = ps2cdvd_lock_interruptible("rcbyctl")) != 0)
		return (res);

	    if (ps2cdvdcall_rcbyctl(rcbyctl.param, &rcbyctl.stat) != 1) {
		printk(KERN_ERR "ps2cdvdstat: failed rcbyctl\n");
		ps2cdvd_unlock();
		return -EIO;
	    }
	    ps2cdvd_unlock();
	    if (copy_to_user((char*)arg, &rcbyctl, sizeof(rcbyctl)))
		return -EFAULT;
	    return 0;
	  }
	  break;
	}

	return -EINVAL;
}

struct cdrom_device_ops ps2cdvd_dops = {
	open:			ps2cdvd_open,
	release:		ps2cdvd_release,
	drive_status:		ps2cdvd_drive_status,
	media_changed:		ps2cdvd_media_changed,
	tray_move:		ps2cdvd_tray_move,
	lock_door:		ps2cdvd_lock_door,
	select_speed:		ps2cdvd_select_speed,
	reset:			ps2cdvd_reset,
	audio_ioctl:		ps2cdvd_audio_ioctl,
#if 0 // TBD: Check
	dev_ioctl:		ps2cdvd_dev_ioctl,
#endif
	capability:		CDC_OPEN_TRAY | CDC_LOCK | CDC_SELECT_SPEED |
				CDC_MEDIA_CHANGED | CDC_RESET |
				CDC_PLAY_AUDIO | CDC_DRIVE_STATUS,
	n_minors:		1,
};

struct cdrom_device_info ps2cdvd_info = {
	ops:			&ps2cdvd_dops,
	speed:			2,
	capacity:		1,
	name:			"ps2cdvd",
};
