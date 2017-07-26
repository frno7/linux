/*
 *  PlayStation 2 CD/DVD driver
 *
 *  Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
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

#include <linux/sched.h>

#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>

static __inline__ int ps2cdvdcall_init(void)
{
	int res;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios(SB_GETVER, NULL) < 0x0200)
		return -1;
#endif

	do {
		if (sbios_rpc(SBR_CDVD_INIT, NULL, &res) < 0)
			return (-1);
		schedule_timeout(1);
	} while (res < 0);

	return (res);
}

static __inline__ int ps2cdvdcall_reset(void)
{
	int res;

	if (sbios_rpc(SBR_CDVD_RESET, NULL, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_ready(int mode)
{
	struct sbr_cdvd_ready_arg arg;
	int res;

	arg.mode = mode;

	if (sbios_rpc(SBR_CDVD_READY, &arg, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_read(u_int lbn, u_int sectors, void *buf,
				       struct sceCdRMode *rmode)
{
	struct sbr_cdvd_read_arg arg;
	int res;

	arg.lbn = lbn;
	arg.sectors = sectors;
	arg.buf = buf;
	arg.rmode = rmode;

	if (sbios_rpc(SBR_CDVD_READ, &arg, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_stop(void)
{
	int res;

	if (sbios_rpc(SBR_CDVD_STOP, NULL, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_gettoc(u_char *buf, int *len, int *media)
{
	struct sbr_cdvd_gettoc_arg arg;
	int res;

	arg.buf = buf;
	arg.len = *len;
	if (sbios_rpc(SBR_CDVD_GETTOC, &arg, &res) < 0)
		return (-1);
	*len = arg.len;
	*media = arg.media;

	return (res);
}

static __inline__ int ps2cdvdcall_readrtc(struct sbr_cdvd_rtc_arg *rtc)
{
	int res;

	if (sbios_rpc(SBR_CDVD_READRTC, rtc, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_writertc(struct sbr_cdvd_rtc_arg *rtc)
{
	int res;

	if (sbios_rpc(SBR_CDVD_WRITERTC, rtc, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_mmode(int media)
{
	struct sbr_cdvd_mmode_arg arg;
	int res;

	arg.media = media;
	if (sbios_rpc(SBR_CDVD_MMODE, &arg, &res) < 0)
		return (-1);

	return (res);
}

#ifdef PS2LIBCDVD_H /* TBD: Move definition of SCECdErFAIL. */
static __inline__ int ps2cdvdcall_geterror(void)
{
	int res;

	if (sbios_rpc(SBR_CDVD_GETERROR, NULL, &res) < 0)
		return (SCECdErFAIL);

	return (res);
}
#endif

static __inline__ int ps2cdvdcall_gettype(int *type)
{
	return sbios_rpc(SBR_CDVD_GETTYPE, NULL, type);
}

static __inline__ int ps2cdvdcall_trayreq(int req, int *traycount)
{
	struct sbr_cdvd_trayreq_arg arg;
	int res;

	arg.req = req;
	if (sbios_rpc(SBR_CDVD_TRAYREQ, &arg, &res) < 0)
		return (-1);
	if (traycount)
		*traycount = arg.traycount; 

	return (res);
}

static __inline__ int ps2cdvdcall_dastream(u_int command, u_int lbn,
					   int size, void *buf,
					   struct sceCdRMode *rmode)
{
	struct sbr_cdvd_dastream_arg arg;
	int res;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios(SB_GETVER, NULL) < 0x0202)
		return (-1);
#endif

	arg.command = command;
	arg.lbn = lbn;
	arg.size = size;
	arg.buf = buf;
	arg.rmode = rmode;

	if (sbios_rpc(SBR_CDVD_DASTREAM, &arg, &res) < 0)
		return (-1);

	return (res);
}

static __inline__ int ps2cdvdcall_readsubq(u_char *subq, int *stat)
{
	struct sbr_cdvd_readsubq_arg arg;
	int res;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios(SB_GETVER, NULL) < 0x0202)
		return (-1);
#endif
        if (sbios_rpc(SBR_CDVD_READSUBQ, &arg, &res) < 0)
		return (-1);
	if (subq)
		memcpy(subq, arg.data, 10);
	if (stat)
		*stat = arg.stat;

	return (res != 0 ? 0 : -1); /* res=0 means failure */
}

static __inline__ int ps2cdvdcall_rcbyctl(int param, int *stat)
{
	struct sbr_cdvd_rcbyctl_arg arg;
	int res;

	arg.param = param;

	if (sbios_rpc(SBR_CDVD_RCBYCTL, &arg, &res) < 0)
		return (0);

	*stat = arg.stat;

	return (res);
}

static __inline__ int ps2cdvdcall_read_dvd(u_int lbn, u_int sectors, void *buf,
				       struct sceCdRMode *rmode)
{
	struct sbr_cdvd_read_arg arg;
	int res;

	arg.lbn = lbn;
	arg.sectors = sectors;
	arg.buf = buf;
	arg.rmode = rmode;

	if (sbios_rpc(SBR_CDVD_READ_DVD, &arg, &res) < 0)
		return (-1);

	return (res);
}
