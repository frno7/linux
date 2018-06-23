// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) IRX module operations
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#ifndef PS2_IOP_MODULE_H
#define PS2_IOP_MODULE_H

int iop_module_load_firmware(const char *filepath);
int iop_module_load_firmware_arg(const char *filepath, const char *arg);

int iop_module_load_buffer(const void *buf, size_t nbyte, const char *arg);

#endif /* PS2_IOP_MODULE_H */
