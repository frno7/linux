// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 real-time clock (RTC)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include "asm/mach-ps2/scmd.h"

#define DRVNAME "rtc-ps2"

static int read_time(struct device *dev, struct rtc_time *tm)
{
	time64_t t;
	int err = scmd_read_rtc(&t);

	if (!err)
		rtc_time64_to_tm(t, tm);

	return err;
}

static int set_time(struct device *dev, struct rtc_time *tm)
{
	return scmd_set_rtc(rtc_tm_to_time64(tm));
}

static const struct rtc_class_ops ps2_rtc_ops = {
	.read_time = read_time,
	.set_time = set_time,
};

static int ps2_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	rtc = devm_rtc_device_register(&pdev->dev,
		DRVNAME, &ps2_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->uie_unsupported = 1;

	return 0;
}

static struct platform_driver ps2_rtc_driver = {
	.probe = ps2_rtc_probe,
	.driver = {
		.name = DRVNAME,
	},
};

static int __init ps2_rtc_init(void)
{
	return platform_driver_register(&ps2_rtc_driver);
}

static void __exit ps2_rtc_exit(void)
{
	platform_driver_unregister(&ps2_rtc_driver);
}

module_init(ps2_rtc_init);
module_exit(ps2_rtc_exit);

MODULE_DESCRIPTION("PlayStation 2 real-time clock (RTC)");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
