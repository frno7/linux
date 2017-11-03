/*
 * PlayStation 2 real-time clock (RTC) registration
 *
 * Copyright (C) 2011-2013 Jürgen Urban
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>

int __init ps2rtc_init(void)
{
	/* Register PS2 RTC driver. */
	struct platform_device *pdev =
		platform_device_register_simple("rtc-ps2", -1, NULL, 0);

	return IS_ERR(pdev) ? PTR_ERR(pdev) : 0;
}
