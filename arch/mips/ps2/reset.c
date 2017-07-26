/*
 *  Copyright (C) 2015-2015 Rick Gaiser
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
#include <linux/pm.h>

#include <asm/reboot.h>

#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>


static void ps2_halt(int mode)
{
    struct sb_halt_arg arg;
    arg.mode = mode;
    sbios(SB_HALT, &arg);
}

static void ps2_machine_restart(char *command)
{
	ps2_halt(SB_HALT_MODE_RESTART);
}

static void ps2_machine_halt(void)
{
	ps2_halt(SB_HALT_MODE_HALT);
}

static void ps2_pm_power_off(void)
{
	ps2_halt(SB_HALT_MODE_PWROFF);
}

void __init ps2_reset_init(void)
{
	_machine_restart = ps2_machine_restart;
	_machine_halt = ps2_machine_halt;
	pm_power_off = ps2_pm_power_off;
}
