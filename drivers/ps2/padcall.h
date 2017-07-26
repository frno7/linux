/*
 *  PlayStation 2 Game Controller driver
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

#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>

static __inline__ int ps2padlib_Init(int mode)
{
	struct sbr_pad_init_arg arg;
	int res;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios(SB_GETVER, NULL) < 0x0200)
		return -1;
#endif

	arg.mode = mode;
	do {
		if (sbios_rpc(SBR_PAD_INIT, &arg, &res) < 0 || res < 0)
			return -1;
	} while (res == 0);
	return 1;
}

static __inline__ int ps2padlib_End(void)
{
	int res;
	if (sbios_rpc(SBR_PAD_END, NULL, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_PortOpen(int port, int slot, void *addr)
{
	struct sbr_pad_portopen_arg po_arg;
	int res;

	po_arg.port = port;
	po_arg.slot = slot;
	po_arg.addr = (void *)addr;
	if (sbios_rpc(SBR_PAD_PORTOPEN, &po_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_PortClose(int port, int slot)
{
	struct sbr_pad_portclose_arg pc_arg;
	int res;

	pc_arg.port = port;
	pc_arg.slot = slot;
	if (sbios_rpc(SBR_PAD_PORTCLOSE, &pc_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_SetMainMode(int port, int slot, int offs, int lock)
{
	struct sbr_pad_setmainmode_arg sm_arg;
	int res;

	sm_arg.port = port;
	sm_arg.slot = slot;
	sm_arg.offs = offs;
	sm_arg.lock = lock;
	if (sbios_rpc(SBR_PAD_SETMAINMODE, &sm_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_SetActDirect(int port, int slot, const unsigned char *data)
{
	struct sbr_pad_setactdirect_arg sd_arg;
	int res;

	sd_arg.port = port;
	sd_arg.slot = slot;
	sd_arg.data = data;
	if (sbios_rpc(SBR_PAD_SETACTDIRECT, &sd_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_SetActAlign(int port, int slot, const unsigned char *data)
{
	struct sbr_pad_setactalign_arg sa_arg;
	int res;

	sa_arg.port = port;
	sa_arg.slot = slot;
	sa_arg.data = data;
	if (sbios_rpc(SBR_PAD_SETACTALIGN, &sa_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_InfoPressMode(int port, int slot)
{
	struct sbr_pad_pressmode_arg pr_arg;
	int res;

	pr_arg.port = port;
	pr_arg.slot = slot;
	if (sbios_rpc(SBR_PAD_INFOPRESSMODE, &pr_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_EnterPressMode(int port, int slot)
{
	struct sbr_pad_pressmode_arg pr_arg;
	int res;

	pr_arg.port = port;
	pr_arg.slot = slot;
	if (sbios_rpc(SBR_PAD_ENTERPRESSMODE, &pr_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_ExitPressMode(int port, int slot)
{
	struct sbr_pad_pressmode_arg pr_arg;
	int res;

	pr_arg.port = port;
	pr_arg.slot = slot;
	if (sbios_rpc(SBR_PAD_EXITPRESSMODE, &pr_arg, &res) < 0)
		return -1;
	return res;
}

static __inline__ int ps2padlib_Read(int port, int slot, unsigned char *rdata)
{
	struct sb_pad_read_arg rd_arg;

	rd_arg.port = port;
	rd_arg.slot = slot;
	rd_arg.rdata = rdata;
	return sbios(SB_PAD_READ, &rd_arg);
}

static __inline__ int ps2padlib_GetState(int port, int slot)
{
	struct sb_pad_getstate_arg gs_arg;

	gs_arg.port = port;
	gs_arg.slot = slot;
	return sbios(SB_PAD_GETSTATE, &gs_arg);
}

static __inline__ int ps2padlib_GetReqState(int port, int slot)
{	
	struct sb_pad_getreqstate_arg gr_arg;

	gr_arg.port = port;
	gr_arg.slot = slot;
	return sbios(SB_PAD_GETREQSTATE, &gr_arg);
}

static __inline__ int ps2padlib_InfoAct(int port, int slot, int actno, int term)
{	
	struct sb_pad_infoact_arg ia_arg;

	ia_arg.port = port;
	ia_arg.slot = slot;
	ia_arg.actno = actno;
	ia_arg.term = term;
	return sbios(SB_PAD_INFOACT, &ia_arg);
}

static __inline__ int ps2padlib_InfoComb(int port, int slot, int listno, int offs)
{	
	struct sb_pad_infocomb_arg ic_arg;

	ic_arg.port = port;
	ic_arg.slot = slot;
	ic_arg.listno = listno;
	ic_arg.offs = offs;
	return sbios(SB_PAD_INFOCOMB, &ic_arg);
}

static __inline__ int ps2padlib_InfoMode(int port, int slot, int term, int offs)
{	
	struct sb_pad_infomode_arg im_arg;

	im_arg.port = port;
	im_arg.slot = slot;
	im_arg.term = term;
	im_arg.offs = offs;
	return sbios(SB_PAD_INFOMODE, &im_arg);
}
