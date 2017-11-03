/*
 * Copyright (C) 2015-2015 Rick Gaiser
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/init.h>
#include <linux/pm.h>

#include <asm/reboot.h>

#include <asm/mach-ps2/sif.h>
#include <asm/mach-ps2/sbios.h>

static void ps2_halt(int mode)
{
	struct sb_halt_arg arg = { .mode = mode };

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
