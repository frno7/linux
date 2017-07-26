/*
 * PS2 RTC Driver
 *
 * Copyright 2011 Mega Man
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include "asm/mach-ps2/siflock.h"
#include "asm/mach-ps2/sbios.h"
#include "asm/mach-ps2/bootinfo.h"
#include "asm/mach-ps2/cdvdcall.h"

static ps2sif_lock_t *ps2rtc_lock;

#define PS2_RTC_TZONE	(9 * 60 * 60)

static inline int bcd_to_bin(int val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}

static inline int bin_to_bcd(int val)
{
	return ((val / 10) << 4) + (val % 10);
}

static unsigned long ps2_rtc_get_time(void)
{
	int rv;
	unsigned long t;
	struct sbr_cdvd_rtc_arg rtc_arg;
	struct ps2_rtc rtc;

	ps2sif_lock(ps2rtc_lock, "read rtc");
	rv = ps2cdvdcall_readrtc(&rtc_arg);
	ps2sif_unlock(ps2rtc_lock);

	if (rv != 1 || rtc_arg.stat != 0) {
		/* RTC read error */
		return 0;
	}

	rtc.sec = bcd_to_bin(rtc_arg.second);
	rtc.min = bcd_to_bin(rtc_arg.minute);
	rtc.hour = bcd_to_bin(rtc_arg.hour);
	rtc.day = bcd_to_bin(rtc_arg.day);
	rtc.mon = bcd_to_bin(rtc_arg.month);
	rtc.year = bcd_to_bin(rtc_arg.year);

	/* Convert PlayStation 2 system time (JST) to UTC */
	t = mktime(rtc.year + 2000, rtc.mon, rtc.day,
			rtc.hour, rtc.min, rtc.sec);
	t -= PS2_RTC_TZONE;

	return (t);
}

static int ps2_rtc_set_time(unsigned long t)
{
	int res;
	struct sbr_cdvd_rtc_arg rtc_arg;
	struct rtc_time tm;

	/*
	 * timer_interrupt in arch/mips/kernel/time.c calls this function
	 * in interrupt.
	 */
	if (in_interrupt()) {
		/* You can't touch RTC in interrupt */
		return -EAGAIN;
	}

	/* convert UTC to PlayStation 2 system time (JST) */
	t += PS2_RTC_TZONE;
	rtc_time_to_tm(t, &tm);

	rtc_arg.stat = 0;
	rtc_arg.second = bin_to_bcd(tm.tm_sec);
	rtc_arg.minute = bin_to_bcd(tm.tm_min);
	rtc_arg.hour = bin_to_bcd(tm.tm_hour);
	rtc_arg.day = bin_to_bcd(tm.tm_mday);
	rtc_arg.month = bin_to_bcd(tm.tm_mon + 1);
	rtc_arg.year = bin_to_bcd(tm.tm_year - 100);

	ps2sif_lock(ps2rtc_lock, "write rtc");
	res = ps2cdvdcall_writertc(&rtc_arg);
	ps2sif_unlock(ps2rtc_lock);
	if (res != 1)
		return -EIO;
	if (rtc_arg.stat != 0)
		return -EIO;

	return 0;
}

static int ps2_get_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long t;

	t = ps2_rtc_get_time();
	if (t == 0) {
		return -EIO;
	}
	rtc_time_to_tm(t, tm);
	return rtc_valid_tm(tm);
}

static int ps2_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long t;

	rtc_tm_to_time(tm, &t);
	return ps2_rtc_set_time(t);
}

static const struct rtc_class_ops ps2_rtc_ops = {
	.read_time = ps2_get_time,
	.set_time = ps2_set_time,
};

static int __init ps2_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc;

	if ((ps2rtc_lock = ps2sif_getlock(PS2LOCK_RTC)) == NULL) {
		printk(KERN_ERR "ps2rtc: Can't get lock\n");
		return -EINVAL;
	}

	ps2sif_lock(ps2rtc_lock, "rtc init");
	if (ps2cdvdcall_init()) {
		ps2sif_unlock(ps2rtc_lock);
		printk(KERN_ERR "ps2rtc: Can't initialize CD/DVD-ROM subsystem\n");
		return -ENODEV;
	}

	printk(KERN_INFO "PlayStation 2 Real Time Clock driver\n");
	ps2sif_unlock(ps2rtc_lock);

	rtc = rtc_device_register("rtc-ps2", &dev->dev, &ps2_rtc_ops,
				  THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(dev, rtc);
	return 0;
}

static int __exit ps2_rtc_remove(struct platform_device *dev)
{
	rtc_device_unregister(platform_get_drvdata(dev));
	return 0;
}

static struct platform_driver ps2_rtc_driver = {
	.driver = {
		.name = "rtc-ps2",
		.owner = THIS_MODULE,
	},
	.remove = __exit_p(ps2_rtc_remove),
};

static int __init ps2_rtc_init(void)
{
	return platform_driver_probe(&ps2_rtc_driver, ps2_rtc_probe);
}

static void __exit ps2_rtc_fini(void)
{
	platform_driver_unregister(&ps2_rtc_driver);
}

module_init(ps2_rtc_init);
module_exit(ps2_rtc_fini);

MODULE_AUTHOR("Mega Man");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ps2 RTC driver");
MODULE_ALIAS("platform:rtc-ps2");
