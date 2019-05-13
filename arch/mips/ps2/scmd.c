// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 system commands
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

/**
 * completed - poll for condition to happen, or timeout
 * @condition: function to poll for condition
 *
 * Return: %true if condition happened, else %false on timeout
 */
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

/**
 * scmd_status - read system command status register
 *
 * Return: system command status register value
 */
static u8 scmd_status(void)
{
	return inb(SCMD_STATUS);
}

/**
 * scmd_write - write system command data
 * @data: pointer to data to write
 * @size: number of bytes to write
 */
static void scmd_write(const u8 *data, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		outb(data[i], SCMD_SEND);
}

/**
 * scmd_ready - can the system receive a command or has finished processing?
 *
 * Return: %true if the system is ready to receive a command, or has finished
 * 	processing a previous command, otherwise %false
 */
static bool scmd_ready(void)
{
	return (scmd_status() & SCMD_STATUS_BUSY) == 0;
}

/**
 * scmd_wait - wait for the system command to become ready
 *
 * Return: %true if the system command is ready, else %false on timeout
 */
static bool scmd_wait(void)
{
	return completed(scmd_ready);
}

/**
 * scmd_data - is command data available to be read from the system?
 *
 * Return: %true if system data is readable, else %false
 */
static bool scmd_data(void)
{
	return (scmd_status() & SCMD_STATUS_EMPTY) == 0;
}

/**
 * scmd_flush - read and discard all available command data from the system
 *
 * Return: %true if something was read, else %false
 */
static bool scmd_flush(void)
{
	bool flushed;

	for (flushed = false; scmd_data(); flushed = true)
		inb(SCMD_RECV);

	return flushed;
}

/**
 * scmd_read - read command data from the system
 * @data: pointer to data to read
 * @size: maximum number of bytes to read
 *
 * Return: actual number of bytes read
 */
static size_t scmd_read(u8 *data, size_t size)
{
	size_t r;

	for (r = 0; r < size && scmd_data(); r++)
		data[r] = inb(SCMD_RECV);

	return r;
}

/**
 * scmd - general system command function
 * @cmd: system command
 * @send: pointer to command data to send
 * @send_size: size in bytes of command data to send
 * @recv: pointer to command data to receive
 * @recv_size: exact size in bytes of command data to receive
 *
 * Context: sleep
 * Return: 0 on success, else a negative error number
 */
int scmd(enum scmd_cmd cmd,
	const void *send, size_t send_size,
	void *recv, size_t recv_size)
{
	static DEFINE_MUTEX(scmd_lock);
	int err = 0;
	size_t r;

	mutex_lock(&scmd_lock);

	if (!scmd_ready()) {
		pr_warn("%s: Unexpectedly busy preceding command %d\n",
			__func__, cmd);

		if (!scmd_wait()) {
			err = -EBUSY;
			goto out_err;
		}
	}
	if (scmd_flush())
		pr_warn("%s: Unexpected data preceding command %d\n",
			__func__, cmd);

	scmd_write(send, send_size);
	outb(cmd, SCMD_COMMAND);

	if (!scmd_wait()) {
		err = -EIO;
		goto out_err;
	}
	r = scmd_read(recv, recv_size);
	if (r == recv_size && scmd_flush())
		pr_warn("%s: Unexpected data following command %d\n",
			__func__, cmd);
	if (r != recv_size)
		err = -EIO;

out_err:
	mutex_unlock(&scmd_lock);
	return err;
}
EXPORT_SYMBOL_GPL(scmd);

static int scmd_send_byte(enum scmd_cmd cmd, u8 send_byte,
	void *recv, size_t recv_size)
{
	return scmd(cmd, &send_byte, sizeof(send_byte), recv, recv_size);
}

/**
 * scmd_power_off - system command to power off the system
 *
 * On success, the processor will have to wait for the shut down to take effect.
 *
 * Context: sleep
 * Return: 0 on success, else a negative error number
 */
int scmd_power_off(void)
{
	u8 status;
	int err;

	err = scmd(scmd_cmd_power_off, NULL, 0, &status, sizeof(status));
	if (err < 0) {
		pr_err("%s: Write failed with %d\n", __func__, err);
		return err;
	}

	if (status != 0) {
		pr_err("%s: Invalid result with status 0x%x\n",
			__func__, status);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(scmd_power_off);

/**
 * scmd_read_machine_name - system command to read the machine name
 *
 * An example of machine name is SCPH-50004.
 *
 * Machines SCPH-10000 and SCPH-15000 do not implement this command. Late
 * SCPH-10000 and all SCPH-15000 have the name in rom0:OSDSYS instead.
 *
 * Context: sleep
 * Return: the machine name, or the empty string on failure
 */
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

	err = scmd_send_byte(scmd_cmd_read_machine_name, 0,
		&buffer0, sizeof(buffer0));
	if (err < 0) {
		pr_debug("%s: Read failed with %d at 0\n", __func__, err);
		goto out_err;
	}

	err = scmd_send_byte(scmd_cmd_read_machine_name, 8,
		&buffer8, sizeof(buffer8));
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

	BUILD_BUG_ON(sizeof(machine.name) < 16);
	memcpy(&machine.name[0], buffer0.name, 8);
	memcpy(&machine.name[8], buffer8.name, 8);
	machine.name[15] = '\0';

out_err:
	return machine;
}
EXPORT_SYMBOL_GPL(scmd_read_machine_name);

MODULE_DESCRIPTION("PlayStation 2 system commands");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
