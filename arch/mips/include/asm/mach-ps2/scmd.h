// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 system commands
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_PS2_SCMD_H
#define __ASM_PS2_SCMD_H

#include <linux/time64.h>
#include <linux/types.h>

#define SCMD_COMMAND	0x1f402016
#define SCMD_STATUS	0x1f402017
#define SCMD_SEND	0x1f402017
#define SCMD_RECV	0x1f402018

#define SCMD_STATUS_EMPTY	0x40	/* Data is unavailable */
#define SCMD_STATUS_BUSY	0x80	/* Command is processing */

/**
 * enum scmd_cmd - system commands
 * @scmd_cmd_read_rtc: read the real-time clock (RTC)
 * @scmd_cmd_write_rtc: set the real-time clock (RTC)
 * @scmd_cmd_power_off: power off the system
 * @scmd_cmd_read_machine_name: read machine name
 */
enum scmd_cmd {
	scmd_cmd_read_rtc = 8,
	scmd_cmd_write_rtc = 9,
	scmd_cmd_power_off = 15,
	scmd_cmd_read_machine_name = 23,
};

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
	void *recv, size_t recv_size);

/**
 * scmd_power_off - system command to power off the system
 *
 * On success, the processor will have to wait for the shut down to take effect.
 *
 * Context: sleep
 * Return: 0 on success, else a negative error number
 */
int scmd_power_off(void);

/**
 * struct scmd_machine_name - machine name, or the empty string
 * @name: NUL terminated string, for example ``"SCPH-50004"``
 */
struct scmd_machine_name {
	char name[16];
};

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
struct scmd_machine_name scmd_read_machine_name(void);

/**
 * scmd_read_rtc - system command to read the real-time clock (RTC)
 * @t: pointer to store the time on a successful reading
 *
 * Context: sleep
 * Return: 0 on success, else a negative error number
 */
int scmd_read_rtc(time64_t *t);

/**
 * scmd_write_rtc - system command to set the real-time clock (RTC)
 * @t: the time to set
 *
 * Context: sleep
 * Return: 0 on success, else a negative error number
 */
int scmd_write_rtc(time64_t t);

#endif /* __ASM_PS2_SCMD_H */
