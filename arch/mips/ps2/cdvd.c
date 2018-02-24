/*
 * PlayStation 2 CD/DVD driver
 *
 * Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/bcd.h>
#include <linux/rtc.h>

#include <asm/mach-ps2/cdvd.h>
#include <asm/mach-ps2/sbios.h>

#define SBIOS_RPC_CDVD_INIT	176
#define SBIOS_RPC_CDVD_READRTC	182
#define SBIOS_RPC_CDVD_WRITERTC	183

#define PS2_RTC_TZONE	(9 * 60 * 60)

struct cdvd_rtc {
	uint8_t status;
	uint8_t second;
	uint8_t minute;
	uint8_t hour;

	uint8_t pad;
	uint8_t day;
	uint8_t month;
	uint8_t year;	/* year - 2000 */
};

static DEFINE_MUTEX(cdvd_mutex);

static int read_rtc(struct cdvd_rtc *rtc)
{
	int res = 0;
	int err;

	mutex_lock(&cdvd_mutex);
	err = sbios_rpc(SBIOS_RPC_CDVD_READRTC, rtc, &res);
	mutex_unlock(&cdvd_mutex);

	return err < 0 ? -1 : res;
}

static int write_rtc(struct cdvd_rtc *rtc)
{
	int res = 0;
	int err;

	mutex_lock(&cdvd_mutex);
	err = sbios_rpc(SBIOS_RPC_CDVD_WRITERTC, rtc, &res);
	mutex_unlock(&cdvd_mutex);

	return err < 0 ? -1 : res;
}

int cdvd_read_rtc(unsigned long *t)
{
	struct cdvd_rtc rtc_arg;
	int res = read_rtc(&rtc_arg);
	unsigned int sec;
	unsigned int min;
	unsigned int hour;
	unsigned int day;
	unsigned int mon;
	unsigned int year;

	if (res != 1 || rtc_arg.status != 0)
		return -EIO; /* RTC read error. */

	sec  = bcd2bin(rtc_arg.second);
	min  = bcd2bin(rtc_arg.minute);
	hour = bcd2bin(rtc_arg.hour);
	day  = bcd2bin(rtc_arg.day);
	mon  = bcd2bin(rtc_arg.month);
	year = bcd2bin(rtc_arg.year);

	/* Convert PlayStation 2 system time (JST) to UTC. */
	*t = mktime(year + 2000, mon, day, hour, min, sec) - PS2_RTC_TZONE;

	return 0;
}

int cdvd_write_rtc(unsigned long t)
{
	struct cdvd_rtc rtc_arg = { };
	struct rtc_time tm;
	int res;

	/* Convert UTC to PlayStation 2 system time (JST). */
	rtc_time_to_tm(t + PS2_RTC_TZONE, &tm);

	rtc_arg.status = 0;
	rtc_arg.second = bin2bcd(tm.tm_sec);
	rtc_arg.minute = bin2bcd(tm.tm_min);
	rtc_arg.hour   = bin2bcd(tm.tm_hour);
	rtc_arg.day    = bin2bcd(tm.tm_mday);
	rtc_arg.month  = bin2bcd(tm.tm_mon + 1);
	rtc_arg.year   = bin2bcd(tm.tm_year - 100);

	res = write_rtc(&rtc_arg);

	return res != 1 || rtc_arg.status != 0 ? -EIO : 0;
}

int __init cdvd_init(void)
{
	int res;

#ifdef CONFIG_PS2_SBIOS_VER_CHECK
	if (sbios_version() < 0x0200)	/* FIXME: Is this needed? */
		return -1;
#endif

	do {
		if (sbios_rpc(SBIOS_RPC_CDVD_INIT, NULL, &res) < 0)
			return -1;

		mdelay(5); /* Wait a little before trying again. */
	} while (res < 0);

	return res;
}
