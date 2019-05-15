// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 power off
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/pm.h>

#include <asm/processor.h>
#include <asm/reboot.h>

#include <asm/mach-ps2/scmd.h>

static void __noreturn power_off(void)
{
	scmd_power_off();

	cpu_relax_forever();
}

static int __init ps2_init_reboot(void)
{
	pm_power_off = power_off;

	return 0;
}
subsys_initcall(ps2_init_reboot);
