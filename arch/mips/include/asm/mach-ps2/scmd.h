// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 system command driver
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_PS2_SCMD_H
#define __ASM_PS2_SCMD_H

#include <linux/types.h>

#define SCMD_COMMAND	0x1f402016
#define SCMD_STATUS	0x1f402017
#define SCMD_SEND	0x1f402017
#define SCMD_RECV	0x1f402018

#define SCMD_STATUS_EMPTY	0x40	/* Data is unavailable */
#define SCMD_STATUS_BUSY	0x80	/* Command is processing */

/* General system command function. */
int scmd(u8 cmd,
	const void *send, size_t send_size,
	void *recv, size_t recv_size);

/* Send power off system command. */
int scmd_power_off(void);

struct scmd_machine_name {
	char name[17];	/* NUL terminated string, for example "SCPH-50004" */
};

/*
 * Reads the machine name. Returns the empty string on failure, most notably
 * for models SCPH-10000 and SCPH-15000. Late SCPH-10000 and all SCPH-15000
 * have the name in rom0:OSDSYS instead.
 */
struct scmd_machine_name scmd_read_machine_name(void);

#endif /* __ASM_PS2_SCMD_H */
