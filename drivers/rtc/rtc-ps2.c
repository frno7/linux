/*
 * PlayStation 2 real-time clock (RTC) driver
 *
 * Copyright (C)      2011 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include "asm/mach-ps2/bootinfo.h"
#include "asm/mach-ps2/cdvd.h"

static int read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long t = 0;
	int err;

	err = cdvd_read_rtc(&t);
	if (err != 0)
		return err;

	rtc_time_to_tm(t, tm);

	return rtc_valid_tm(tm);
}

static int set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long t = 0;
	int err;

	/*
	 * timer_interrupt in arch/mips/kernel/time.c calls this function
	 * during interrupts. FIXME: Why?
	 */
	if (in_interrupt())
		return -EAGAIN; /* We cannot touch RTC during interrupts. */

	err = rtc_tm_to_time(tm, &t);
	if (err != 0)
		return err;

	return cdvd_write_rtc(t);
}

static const struct rtc_class_ops ps2_rtc_ops = {
	.read_time = read_time,
	.set_time = set_time,
};

static int __init ps2_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc = rtc_device_register("rtc-ps2",
		&dev->dev, &ps2_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(dev, rtc);

	return 0;
}

static int __exit rtc_remove(struct platform_device *dev)
{
	rtc_device_unregister(platform_get_drvdata(dev));

	return 0;
}

static struct platform_driver ps2_rtc_driver = {
	.driver = {
		.name = "rtc-ps2",
	},
	.remove = __exit_p(rtc_remove),
};

static int __init rtc_init(void)
{
	return platform_driver_probe(&ps2_rtc_driver, ps2_rtc_probe);
}

static void __exit rtc_exit(void)
{
	platform_driver_unregister(&ps2_rtc_driver);
}

module_init(rtc_init);
module_exit(rtc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ps2 RTC driver");
MODULE_ALIAS("platform:rtc-ps2");
