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

/* error strings */
char *ps2cdvd_errors[] = {
	[SCECdErNO]	= "no error",
	[SCECdErEOM]	= "end of media",
	[SCECdErTRMOPN]	= "terminated by user",
	[SCECdErREAD]	= "read error",
	[SCECdErPRM]	= "parameter error",
	[SCECdErILI]	= "invalid lendth",
	[SCECdErIPI]	= "invalid address",
	[SCECdErCUD]	= "inappropriate disc",
	[SCECdErNORDY]	= "not ready",
	[SCECdErNODISC]	= "no disc",
	[SCECdErOPENS]	= "open tray",
	[SCECdErCMD]	= "command not supported",
	[SCECdErABRT]	= "aborted",
};

/* disc type names */
char *ps2cdvd_disctypes[] = {
	[SCECdIllgalMedia]	= "illegal media",
	[SCECdDVDV]		= "DVD video",
	[SCECdCDDA]		= "CD DA",
	[SCECdPS2DVD]		= "PS2 DVD",
	[SCECdPS2CDDA]		= "PS2 CD DA",
	[SCECdPS2CD]		= "PS2 CD",
	[SCECdPSCDDA]		= "PS CD DA",
	[SCECdPSCD]		= "PS CD",
	[SCECdUNKNOWN]		= "unknown",
	[SCECdDETCTDVDD]	= "DVD-dual detecting",
	[SCECdDETCTDVDS]	= "DVD-single detecting",
	[SCECdDETCTCD]		= "CD detecting",
	[SCECdDETCT]		= "detecting",
	[SCECdNODISC]		= "no disc",
};

/* event names */
char *ps2cdvd_events[] = {
	[EV_NO_EVENT]		= "NONE",
	[EV_START]		= "START",
	[EV_TIMEOUT]		= "TIMEOUT",
	[EV_EXIT]		= "EXIT",
	[EV_RESET]		= "RESET",
};

/* state names */
char *ps2cdvd_states[] = {
	[STAT_INIT]			= "INIT",
	[STAT_WAIT_DISC]		= "WAIT_DISC",
	[STAT_INVALID_DISC]		= "INVALID_DISC",
	[STAT_CHECK_DISC]		= "CHECK_DISC",
	[STAT_READY]			= "READY",
	[STAT_ERROR]			= "ERROR",
	[STAT_IDLE]			= "IDLE",

	/* obsolete */
	[STAT_INIT_TRAYSTAT]		= "INIT_TRAYSTAT",
	[STAT_CHECK_DISCTYPE]		= "CHECK_DISCTYPE",
	[STAT_INIT_CHECK_READY]		= "INIT_CHECK_READY",
	[STAT_SET_MMODE]		= "SET_MMODE",
	[STAT_TOC_READ]			= "TOC_READ",
	[STAT_LABEL_READ]		= "LABEL_READ",
	[STAT_LABEL_READ_ERROR_CHECK]	= "LABEL_READ_ERROR_CHECK",
	[STAT_CHECK_TRAY]		= "CHECK_TRAY",
	[STAT_READ]			= "READ",
	[STAT_READ_EOM_RETRY]		= "READ_EOM_RETRY",
	[STAT_READ_ERROR_CHECK]		= "READ_ERROR_CHECK",
	[STAT_SPINDOWN]			= "SPINDOWN",
};

unsigned long
ps2cdvd_checksum(unsigned long *data, int len)
{
	unsigned long sum = 0;

	while (len--) {
		sum += *data++;
		sum = ((sum << 24) | sum >> 8);
		sum ^= (sum % (len + 1));
	}

	return sum;
}

void
ps2cdvd_print_isofsstr(char *str, int len)
{
	int i;
	int space = 0;
	for (i = 0;i < len; i++) {
	  if (*str == ' ') {
	    if (!space) {
	      space = 1;
	      printk("%c", *str++);
	    }
	  } else {
	    space = 0;
	    printk("%c", *str++);
	  }
	}
}

char*
ps2cdvd_geterrorstr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_errors) &&
	    ps2cdvd_errors[no]) {
		return ps2cdvd_errors[no];
	} else {
		sprintf(buf, "unknown error(0x%02x)", no);
		return buf;
	}
}

char*
ps2cdvd_getdisctypestr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_disctypes) &&
	    ps2cdvd_disctypes[no]) {
		return ps2cdvd_disctypes[no];
	} else {
		sprintf(buf, "unknown type(0x%02x)", no);
		return buf;
	}
}

char*
ps2cdvd_geteventstr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_events) &&
	    ps2cdvd_events[no]) {
		return ps2cdvd_events[no];
	} else {
		sprintf(buf, "unknown event(0x%02x)", no);
		return buf;
	}
}

char*
ps2cdvd_getstatestr(int no)
{
	static char buf[32];
	if (0 <= no && no < ARRAYSIZEOF(ps2cdvd_states) &&
	    ps2cdvd_states[no]) {
		return ps2cdvd_states[no];
	} else {
		sprintf(buf, "unknown state(0x%02x)", no);
		return buf;
	}
}

void
ps2cdvd_hexdump(char *header, unsigned char *data, int len)
{
	int i;
	char *hex = "0123456789abcdef";
	char line[70];

	for (i = 0; i < len; i++) {
		int o = i % 16;
		if (o == 0) {
			memset(line, ' ', sizeof(line));
			line[sizeof(line) - 1] = '\0';
		}
		line[o * 3 + 0] = hex[(data[i] & 0xf0) >> 4];
		line[o * 3 + 1] = hex[(data[i] & 0x0f) >> 0];
		if (0x20 <= data[i] && data[i] < 0x7f) {
			line[o + 50] = data[i];
		} else {
			line[o + 50] = '.';
		}
		if (o == 15) {
			printk("%s%s\n", header, line);
		}
	}
}

void
ps2cdvd_tocdump(char *header, struct ps2cdvd_tocentry *tocents)
{
  int i, startno, endno;

  startno = decode_bcd(tocents[0].abs_msf[0]);
  endno = decode_bcd(tocents[1].abs_msf[0]);
  printk("%strack: %d-%d  lead out: %02d:%02d:%02d\n",
	 header, startno, endno, 
	 decode_bcd(tocents[2].abs_msf[0]),
	 decode_bcd(tocents[2].abs_msf[1]),
	 decode_bcd(tocents[2].abs_msf[2]));
  tocents += 2;
  printk("%saddr/ctrl track index min/sec/frame\n", header);
  for (i = startno; i <= endno; i++) {
    printk("%s   %x/%x    %02d    %02d    %02d:%02d:%02d\n",
	   header,
	   tocents[i].addr,
	   tocents[i].ctrl,
	   decode_bcd(tocents[i].trackno),
	   decode_bcd(tocents[i].indexno),
	   decode_bcd(tocents[i].abs_msf[0]),
	   decode_bcd(tocents[i].abs_msf[1]),
	   decode_bcd(tocents[i].abs_msf[2]));
  }
}
