// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) DEV9
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>

#include <asm/mach-ps2/iop-registers.h>

#include "iop-module.h"

static struct {
	u16 rev;
} dev9;

#define DEFINE_READW_REG(type, reg, name) \
	static inline int iop_##type##_read_##name(u16 * const value) \
	{ \
		return iop_readw(value, 0xbf800000 + (reg)); \
	}

#define DEFINE_WRITEW_REG(type, reg, name) \
	static inline int iop_##type##_write_##name(const u16 value) \
	{ \
		return iop_writew(value, 0xbf800000 + (reg)); \
	}

#define DEFINE_READL_REG(type, reg, name) \
	static inline int iop_##type##_read_##name(u32 * const value) \
	{ \
		return iop_readl(value, 0xbf800000 + (reg)); \
	}

#define DEFINE_WRITEL_REG(type, reg, name) \
	static inline int iop_##type##_write_##name(const u32 value) \
	{ \
		return iop_writel(value, 0xbf800000 + (reg)); \
	}

#define DEFINE_DEV9_READ_WRITE_REG(reg, name) \
	DEFINE_READW_REG(dev9, reg, name); \
	DEFINE_WRITEW_REG(dev9, reg, name)

#define DEFINE_SSBUS_READ_WRITE_REG(reg, name) \
	DEFINE_READL_REG(ssbus, reg, name); \
	DEFINE_WRITEL_REG(ssbus, reg, name)

DEFINE_DEV9_READ_WRITE_REG(0x1460, 1460);
DEFINE_DEV9_READ_WRITE_REG(0x1462, 1462);
DEFINE_DEV9_READ_WRITE_REG(0x1464, 1464);
DEFINE_DEV9_READ_WRITE_REG(0x1466, 1466);
DEFINE_DEV9_READ_WRITE_REG(0x1468, 1468);
DEFINE_DEV9_READ_WRITE_REG(0x146a, 146a);
DEFINE_DEV9_READ_WRITE_REG(0x146c, power);
DEFINE_DEV9_READ_WRITE_REG(0x146e, rev);
DEFINE_DEV9_READ_WRITE_REG(0x1470, 1470);
DEFINE_DEV9_READ_WRITE_REG(0x1472, 1472);
DEFINE_DEV9_READ_WRITE_REG(0x1474, 1474);
DEFINE_DEV9_READ_WRITE_REG(0x1476, 1476);
DEFINE_DEV9_READ_WRITE_REG(0x1478, 1478);
DEFINE_DEV9_READ_WRITE_REG(0x147a, 147a);
DEFINE_DEV9_READ_WRITE_REG(0x147c, 147c);
DEFINE_DEV9_READ_WRITE_REG(0x147e, 147e);

DEFINE_SSBUS_READ_WRITE_REG(0x1418, 1418);
DEFINE_SSBUS_READ_WRITE_REG(0x141c, 141c);
DEFINE_SSBUS_READ_WRITE_REG(0x1420, 1420);

static bool pc_card(void)
{
	return (dev9.rev & 0xf0) == 0x20;
}

static bool exp_dev(void)
{
	return (dev9.rev & 0xf0) == 0x30;
}

static int iop_dev9_power(bool * const power)
{
	u16 raw;
	int err;

	err = iop_dev9_read_power(&raw);
	if (err < 0) {
		*power = false;
		return err;
	}

	*power = ((raw & 0x4) != 0);

	return 0;
}

static int __init exp_dev_probe(void)
{
	u16 reg_1462;
	int err;

	err = iop_dev9_read_1462(&reg_1462);
	if (err < 0)
		return err;

	return (reg_1462 & 0x1) ? -ENODEV : 0;
}

static int __init exp_dev_reset(void)
{
	u16 reg_power;
	u16 reg_1460;
	int err;

	err = exp_dev_probe();
	if (err < 0)
		return err;

	err = iop_dev9_read_power(&reg_power);	/* FIXME: iop_dev9_ -> dev9_ */
	if (err < 0)
		return err;

	err = iop_dev9_write_power((reg_power & ~0x1) | 0x4);
	if (err < 0)
		return err;

	msleep(500);	/* FIXME */

	err = iop_dev9_read_1460(&reg_1460);
	if (err < 0)
		return err;

	err = iop_dev9_write_1460(reg_1460 | 0x1);
	if (err < 0)
		return err;

	err = iop_dev9_read_power(&reg_power);
	if (err < 0)
		return err;

	err = iop_dev9_write_power(reg_power | 0x1);
	if (err < 0)
		return err;

	msleep(500);	/* FIXME */

	return 0;
}

static int __init exp_dev_init(void)
{
	bool power;
	int err;

	err = iop_ssbus_write_1420(0x51011);
	if (err < 0)
		return err;

	err = iop_ssbus_write_1418(0xe01a3043);
	if (err < 0)
		return err;

	err = iop_ssbus_write_141c(0xef1a3043);
	if (err < 0)
		return err;

	err = iop_dev9_power(&power);
	if (err < 0)
		return err;

#if 1
	if (!power)
#endif
	{
		u16 reg_1464;

		printk("dev9: Expansion device power on\n");	/* FIXME */

		err = iop_dev9_write_1466(1);
		if (err < 0)
			return err;

		err = iop_dev9_write_1464(0);
		if (err < 0)
			return err;

		err = iop_dev9_read_1464(&reg_1464);
		if (err < 0)
			return err;

		err = iop_dev9_write_1460(reg_1464);
		if (err < 0)
			return err;

		err = exp_dev_reset();
		if (err < 0)
			return err;
	}

	err = iop_dev9_write_1464(0x103 & 0x3f);	/* ??? */
	if (err < 0)
		return err;

	err = iop_dev9_write_1466(0);
	if (err < 0)
		return err;

	iop_set_dma_dpcr2(IOP_DMA_DPCR2_DEV9);

	return 0;
}

static inline u16 iop_speed_read_rev1(void) { return inw(0x14000002); }
static inline u16 iop_speed_read_rev3(void) { return inw(0x14000004); }
static inline u16 iop_speed_read_rev8(void) { return inw(0x1400000e); }

static void rev_test(void)
{
	u16 rev;
	int err;

	err = iop_readw(&rev, 0xb0000002);
	if (err < 0)
		goto out;
	printk("rev-test: rev1 %x\n", rev);

	err = iop_readw(&rev, 0xb0000004);
	if (err < 0)
		goto out;
	printk("rev-test: rev3 %x\n", rev);

	err = iop_readw(&rev, 0xb000000e);
	if (err < 0)
		goto out;
	printk("rev-test: rev8 %x\n", rev);

out:
	if (err)
		printk("rev-test: Failed with %d\n", err);
}

static int __init iop_dev9_init(void)
{
	int err;

	err = iop_dev9_read_rev(&dev9.rev);
	if (err < 0) {
		printk("iop_dev9_init: err %d\n", err);
		return err;
	}

	if (pc_card()) {
		printk("dev9: PC card interface is not implemented\n");
		return -EINVAL;
	} else if (exp_dev()) {
		printk("dev9: Expansion device interface\n");
		// FIXME: err = exp_dev_init();
	} else {
		printk("dev9: Unknown interface %x\n", dev9.rev);
		return -EINVAL;
	}

	if (err < 0) {
		printk("dev9: Initialization failed with %d\n", err);
		return err;
	}

#if 0
	err = iop_dev9_write_1464(3);	/* Activate network */
	if (err < 0)
		return err;
#endif

	printk("dev9: Interface initialized\n");

#if 0
	iop_writeb(0x0, 0xBF402017);
	iop_writeb(0xf, 0xBF402016);
#endif

	printk("dev9: speed: %x %x %x\n",
		iop_speed_read_rev1(),
		iop_speed_read_rev3(),
		iop_speed_read_rev8());
	rev_test();

	return 0;
}

static void __exit iop_dev9_exit(void)
{
	int err = 0;

	if (pc_card()) {
		/* FIXME */
	} else if (exp_dev()) {
		u16 reg_power;
		u16 reg_1464;

		printk("dev9: Expansion device power off\n");	/* FIXME */

		err = iop_dev9_write_1466(1);
		if (err < 0)
			goto out;

		err = iop_dev9_write_1464(0);
		if (err < 0)
			goto out;

		err = iop_dev9_read_1464(&reg_1464);
		if (err < 0)
			goto out;

		err = iop_dev9_write_1460(reg_1464);
		if (err < 0)
			goto out;

		err = iop_dev9_read_power(&reg_power);
		if (err < 0)
			goto out;

		err = iop_dev9_write_power(reg_power & ~0x4);
		if (err < 0)
			goto out;

		err = iop_dev9_read_power(&reg_power);
		if (err < 0)
			goto out;

		err = iop_dev9_write_power(reg_power & ~0x1);
		if (err < 0)
			goto out;
	}

out:
	if (err)
		printk("dev9: Exit failed with %d\n", err);
}

module_init(iop_dev9_init);
module_exit(iop_dev9_exit);

MODULE_LICENSE("GPL");
