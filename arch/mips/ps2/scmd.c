// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 system command driver
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/build_bug.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

#include <asm/mach-ps2/scmd.h>

static bool completed(bool (*condition)(void))
{
	const unsigned long timeout = jiffies + 5*HZ;

	do {
		if (condition())
			return true;

		msleep(1);
	} while (time_is_after_jiffies(timeout));

	return false;
}

static u8 scmd_status(void)
{
	return inb(SCMD_STATUS);
}

static void scmd_write(const u8 *data, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		outb(data[i], SCMD_SEND);
}

static bool scmd_ready(void)
{
	return (scmd_status() & SCMD_STATUS_BUSY) == 0;
}

static bool scmd_wait(void)
{
	return completed(scmd_ready);
}

static bool scmd_data(void)
{
	return (scmd_status() & SCMD_STATUS_EMPTY) == 0;
}

static bool scmd_flush(void)
{
	bool flushed;

	for (flushed = false; scmd_data(); flushed = true)
		inb(SCMD_RECV);

	return flushed;
}

static size_t scmd_read(u8 *data, size_t n)
{
	size_t r;

	for (r = 0; r < n && scmd_data(); r++)
		data[r] = inb(SCMD_RECV);

	return r;
}

int scmd(u8 cmd,
	const void *send, size_t send_size,
	void *recv, size_t recv_size)
{
	static DEFINE_MUTEX(scmd_lock);
	int err = 0;
	size_t r;

	mutex_lock(&scmd_lock);

	if (!scmd_ready()) {
		pr_warn("scmd: Unexpectedly busy preceding command %d\n", cmd);

		if (!scmd_wait()) {
			err = -EBUSY;
			goto out_err;
		}
	}
	if (scmd_flush())
		pr_warn("scmd: Unexpected data preceding command %d\n", cmd);

	scmd_write(send, send_size);
	outb(cmd, SCMD_COMMAND);

	if (!scmd_wait()) {
		err = -EIO;
		goto out_err;
	}
	r = scmd_read(recv, recv_size);
	if (r == recv_size && scmd_flush())
		pr_warn("scmd: Unexpected data following command %d\n", cmd);
	if (r != recv_size)
		err = -EIO;

out_err:
	mutex_unlock(&scmd_lock);
	return err;
}
EXPORT_SYMBOL_GPL(scmd);

static int scmd_send_byte(u8 cmd, u8 send_byte, void *recv, size_t recv_size)
{
	return scmd(cmd, &send_byte, sizeof(send_byte), recv, recv_size);
}

int scmd_power_off(void)
{
	u8 status;
	int err;

	err = scmd(0xf, NULL, 0, &status, sizeof(status));
	if (err < 0) {
		pr_debug("%s: Write failed with %d\n", __func__, err);
		return err;
	}

	if (status != 0) {
		pr_debug("%s: Invalid result with status 0x%x\n",
			__func__, status);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(scmd_power_off);

struct scmd_machine_name scmd_read_machine_name(void)
{
	struct scmd_machine_name machine = { .name = "" };
	struct __attribute__ ((packed)) {
		u8 status;
		char name[8];
	} buffer0, buffer8;
	int err;

	BUILD_BUG_ON(sizeof(buffer0) != 9 ||
		     sizeof(buffer8) != 9);

	/* The machine name comes in two halves that need to be combined. */

	err = scmd_send_byte(0x17, 0, &buffer0, sizeof(buffer0));
	if (err < 0) {
		pr_debug("%s: Read failed with %d at 0\n", __func__, err);
		goto out_err;
	}

	err = scmd_send_byte(0x17, 8, &buffer8, sizeof(buffer8));
	if (err < 0) {
		pr_debug("%s: Read failed with %d at 8\n", __func__, err);
		goto out_err;
	}

	if (buffer0.status != 0 ||
	    buffer8.status != 0) {
		pr_debug("%s: Invalid results with statuses 0x%x and 0x%x\n",
			__func__, buffer0.status, buffer8.status);
		goto out_err;
	}

	BUILD_BUG_ON(sizeof(machine.name) < 17);
	memcpy(&machine.name[0], buffer0.name, 8);
	memcpy(&machine.name[8], buffer8.name, 8);
	machine.name[16] = '\0';

out_err:
	return machine;
}
EXPORT_SYMBOL_GPL(scmd_read_machine_name);

MODULE_DESCRIPTION("PlayStation 2 system command driver");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
