// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) IRX module operations
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#ifndef PS2_IOP_MODULE_H
#define PS2_IOP_MODULE_H

#include "iop-memory.h"

int iop_module_load_firmware(const char *filepath);
int iop_module_load_firmware_arg(const char *filepath, const char *arg);

int iop_module_load_buffer(const void *buf, size_t nbyte, const char *arg);

int iop_readb(u8 * const data, const iop_addr_t addr);
int iop_readw(u16 * const data, const iop_addr_t addr);
int iop_readl(u32 * const data, const iop_addr_t addr);

int iop_writeb(const u8 data, const iop_addr_t addr);
int iop_writew(const u16 data, const iop_addr_t addr);
int iop_writel(const u32 data, const iop_addr_t addr);

#endif /* PS2_IOP_MODULE_H */
