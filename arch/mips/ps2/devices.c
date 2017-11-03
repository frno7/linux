// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 devices
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>

static struct platform_device rtc_device = {
	.name		= "rtc-ps2",
	.id		= -1,
};

static struct platform_device *ps2_platform_devices[] __initdata = {
	&rtc_device,
};

static int __init ps2_device_setup(void)
{
	return platform_add_devices(ps2_platform_devices,
		ARRAY_SIZE(ps2_platform_devices));
}
device_initcall(ps2_device_setup);
