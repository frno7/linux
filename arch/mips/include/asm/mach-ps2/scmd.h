// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 system commands
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_SCMD_H
#define __ASM_MACH_PS2_SCMD_H

#include <linux/types.h>

#define SCMD_COMMAND	0x1f402016
#define SCMD_STATUS	0x1f402017
#define SCMD_SEND	0x1f402017
#define SCMD_RECV	0x1f402018

#define SCMD_STATUS_EMPTY	0x40	/* Data is unavailable */
#define SCMD_STATUS_BUSY	0x80	/* Command is processing */

/**
 * enum scmd_cmd - system commands
 * @scmd_cmd_power_off: power off the system
 */
enum scmd_cmd {
	scmd_cmd_power_off = 15,
};

int scmd(enum scmd_cmd cmd,
	const void *send, size_t send_size,
	void *recv, size_t recv_size);

int scmd_power_off(void);

#endif /* __ASM_MACH_PS2_SCMD_H */
