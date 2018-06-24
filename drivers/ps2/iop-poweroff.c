// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) power off
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 *
 * FIXME: POFF_RPC_BUTTON
 */

#include <linux/delay.h>	/* FIXME */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/sif.h>

#define SIF_SID_POWER_OFF	0x9090900

enum iop_power_off_rpc_ops {
	rpo_power_off = 1,
	rpo_auto_power_off = 2
};

static struct t_SifRpcClientData cd_power_off;

int iop_enable_power_off_button(void)
{
	const u32 button = 1;

	return sif_rpc(&cd_power_off, rpo_auto_power_off,
		&button, sizeof(button), NULL, 0);
}
EXPORT_SYMBOL(iop_enable_power_off_button);

int iop_disable_power_off_button(void)
{
	const u32 button = 0;

	return sif_rpc(&cd_power_off, rpo_auto_power_off,
		&button, sizeof(button), NULL, 0);
}
EXPORT_SYMBOL(iop_disable_power_off_button);

int iop_power_off(void)
{
	return sif_rpc(&cd_power_off, rpo_power_off, NULL, 0, NULL, 0);
}
EXPORT_SYMBOL(iop_power_off);

static inline void iop_cdvd_write_scmd(const u8 scmd)	/* FIXME */
{
	outb(scmd, 0x1F402016);
}

static inline void iop_cdvd_write_sdin(const u8 sdin)	/* FIXME */
{
	outb(sdin, 0x1F402017);
}

static void __noreturn power_off(void)
{
	// int err;

	local_irq_disable();

	printk("power_off begin\n");

#if 0
	err = iop_power_off();

	if (err < 0)
		printk("iop-poweroff: Power off failed with %d\n", err);

	msleep(1000);
#endif

	printk("power_off try\n");

	outw(inw(0x1f80146c) & 0xFFFE, 0x1f80146c);
	outw(0, 0x1f801460);
	outw(0, 0x1f80146c);

	iop_cdvd_write_sdin(0);
	iop_cdvd_write_scmd(0xF); /* Power off */

	printk("power_off end\n");

	cpu_relax_forever();
}

static int __init iop_power_off_init(void)
{
	int err;

	printk("iop_power_off_init begin\n");

	err = sif_rpc_bind(&cd_power_off, SIF_SID_POWER_OFF);

	if (err < 0) {
		printk("iop-poweroff: ps2sif_bindrpc failed with %d\n", err);
		return err;
	}

#if 0	/* FIXME */
	printk("iop_power_off_init auto\n");

	err = iop_enable_power_off_button();
	if (err < 0) {
		printk("iop-poweroff: iop_enable_power_off_button failed width %d\n", err);
		return err;
	}
#endif

	pm_power_off = power_off;

	printk("iop_power_off_init end\n");

	return 0;
}

static void __exit iop_power_off_exit(void)
{
}

module_init(iop_power_off_init);
module_exit(iop_power_off_exit);

MODULE_LICENSE("GPL");
