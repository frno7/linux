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

#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/sifdefs.h>

static __inline__ int ps2sdcall_init(int flag, int *resiop)
{
	struct sbr_sound_init_arg arg;

	arg.flag = flag;
	do {
		if (sbios_rpc(SBR_SOUND_INIT, &arg, resiop) < 0)
			return (-EIO);
	} while (*resiop == -1);

	return (0);
}

static __inline__ int ps2sdcall_end(int *resiop)
{
	return sbios_rpc(SBR_SOUND_END, NULL, resiop);
}

static __inline__ int ps2sdcall_get_reg(int reg, u_int *data)
{
	struct sb_sound_reg_arg arg;
	int res;

	arg.idx = reg;

	res = sbios(SB_SOUND_GREG, &arg);
	*data = arg.data;

	return (res);
}

static __inline__ int ps2sdcall_set_reg(int reg, u_int data)
{
	struct sb_sound_reg_arg arg;

	arg.idx = reg;
	arg.data = data;

	return sbios(SB_SOUND_SREG, &arg);
}

static __inline__ int ps2sdcall_get_coreattr(int idx, u_int *data)
{
	int res, resiop;
	struct sbr_sound_coreattr_arg arg;

	arg.idx = idx;
	res = sbios_rpc(SBR_SOUND_GCOREATTR, &arg, &resiop);
	*data = arg.data;

	return (res);
}

static __inline__ int ps2sdcall_set_coreattr(int idx, u_int data)
{
	int res, resiop;
	struct sbr_sound_coreattr_arg arg;

	arg.idx = idx;
	arg.data = data;
	res = sbios_rpc(SBR_SOUND_SCOREATTR, &arg, &resiop);

	return (res);
}

static __inline__ int ps2sdcall_trans(int channel, u_int mode, u_int addr,
				    u_int size, u_int start_addr, int *resiop)
{
	struct sbr_sound_trans_arg arg;

	arg.channel = channel;
	arg.mode = mode;
	arg.addr = addr;
	arg.size = size;
	arg.start_addr= start_addr;

	return sbios_rpc(SBR_SOUND_TRANS, &arg, resiop);
}

static __inline__ int ps2sdcall_trans_stat(int channel, int flag, int *resiop)
{
	struct sbr_sound_trans_stat_arg arg;

	arg.channel = channel;
	arg.flag = flag;

	return sbios_rpc(SBR_SOUND_TRANSSTAT, &arg, resiop);
}

static __inline__ int 
ps2sdcall_trans_callback(int channel, 
			 int (*func)(void*, int), void *data,
			 int (**oldfunc)(void*, int), void **olddata,
			 int *resiop)
{
	struct sbr_sound_trans_callback_arg arg;
	int res;

	arg.channel = channel;
	arg.func = func;
	arg.data = data;

	res = sbios_rpc(SBR_SOUND_TRANSCALLBACK, &arg, resiop);

	if (oldfunc)
		*oldfunc = arg.oldfunc;
	if (olddata)
		*olddata = arg.olddata;

	return (res);
}

static __inline__ int ps2sdcall_remote(int *command, int *resiop)
{
	/*
	 * XXX, 
	 * struct sbr_sound_remote_arg in asm/mips/ps2/sbcall.h and
	 * struct ps2sd_command must have common members and alignments.
	 * double check if you change this structure.
	 */
	return sbios_rpc(SBR_SOUND_REMOTE, command, resiop);
}
