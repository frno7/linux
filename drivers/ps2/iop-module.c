// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) IRX module operations
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>

#include <asm/mach-ps2/sif.h>
#include <asm/mach-ps2/iop-memory.h>

#include "iop-module.h"
#include "sif.h"

#define LF_PATH_MAX 252
#define LF_ARG_MAX 252

enum iop_module_rpc_ops {
	rpo_mod_load = 0,
	rpo_elf_load = 1,
	rpo_set_addr = 2,
	rpo_get_addr = 3,
	rpo_mg_mod_load = 4,
	rpo_mg_elf_load = 5,
	rpo_mod_buf_load = 6,
	rpo_mod_stop = 7,
	rpo_mod_unload = 8,
	rpo_search_mod_by_name = 9,
	rpo_search_mod_by_address = 10
};

enum iop_value_type {
	iop_value_u8  = 0,
	iop_value_u16 = 1,
	iop_value_u32 = 2
};

static struct device *iopmodules_device;		// FIXME: Naming
static struct t_SifRpcClientData cd_loadfile_rpc;	// FIXME: Naming

int iop_module_load_rom_arg(const char *filepath, const char *arg_)
{
	const char * const arg = arg_ ? arg_ : "";
	const size_t arg_size = strlen(arg) + 1;
	const size_t filepath_size = strlen(filepath) + 1;
	struct {
		u32 addr;
		u32 arg_size;
		char filepath[LF_PATH_MAX];
		char arg[LF_ARG_MAX];
	} load = {
		.arg_size = arg_size
	};
	struct {
		s32 status;
		u32 modres;
	} result;
	int err;

	if (arg_size >= sizeof(load.arg)) {
		err = -EOVERFLOW;
		goto failure;
	}
	memcpy(load.arg, arg, arg_size);

	if (filepath_size >= sizeof(load.filepath)) {
		err = -ENAMETOOLONG;
		goto failure;
	}
	memcpy(load.filepath, filepath, filepath_size);

	err = sif_rpc(&cd_loadfile_rpc, rpo_mod_load,
		&load, sizeof(load), &result, sizeof(result));
	if (err < 0)
		goto failure;

	if (result.status < 0) {
#if 1
		err = -EIO;
#else
		err = result.status == -SIF_RPC_E_GETP  ||	/* FIXME */
		      result.status == -E_SIF_PKT_ALLOC ||
		      result.status == -E_IOP_NO_MEMORY     ? -ENOMEM :
		      result.status == -SIF_RPC_E_SENDP ||
		      result.status == -E_LF_FILE_IO_ERROR  ? -EIO :
		      result.status == -E_LF_FILE_NOT_FOUND ? -ENOENT :
		      result.status == -E_LF_NOT_IRX        ? -ENOEXEC : -EIO;
#endif
		goto failure;
	}

	return result.status;

failure:
	return err;
}
EXPORT_SYMBOL(iop_module_load_rom_arg);

int iop_module_load_rom(const char *filepath)
{
	return iop_module_load_rom_arg(filepath, NULL);
}
EXPORT_SYMBOL(iop_module_load_rom);

static int iop_rpc_read(u32 * const data,
	const iop_addr_t addr, const enum iop_value_type type)
{
	const struct {
		u32 addr;
		u32 type;
	} arg = {
		.addr = addr,
		.type = type
	};
	int err;

	err = sif_rpc(&cd_loadfile_rpc, rpo_get_addr,
		&arg, sizeof(arg), data, sizeof(*data));

	return err;
}

static int iop_rpc_write(const u32 data,
	const iop_addr_t addr, const enum iop_value_type type)
{
	const struct {
		u32 addr;
		u32 type;
		u32 data;
	} arg = {
		.addr = addr,
		.type = type,
		.data = data
	};
	s32 status;
	int err;

	err = sif_rpc(&cd_loadfile_rpc, rpo_get_addr,
		&arg, sizeof(arg), &status, sizeof(status));

	return err < 0 ? err : status;
}

int iop_readb(u8 * const data, const iop_addr_t addr)
{
	u32 raw;
	const int err = iop_rpc_read(&raw, addr, iop_value_u8);

	if (err >= 0)
		*data = raw & 0xff;

	return err;
}
EXPORT_SYMBOL(iop_readb);

int iop_readw(u16 * const data, const iop_addr_t addr)
{
	u32 raw;
	const int err = iop_rpc_read(&raw, addr, iop_value_u16);

	if (err >= 0)
		*data = raw & 0xffff;

	return err;
}
EXPORT_SYMBOL(iop_readw);

int iop_readl(u32 * const data, const iop_addr_t addr)
{
	u32 raw;
	const int err = iop_rpc_read(&raw, addr, iop_value_u32);

	if (err >= 0)
		*data = raw;

	return err;
}
EXPORT_SYMBOL(iop_readl);

int iop_writeb(const u8 data, const iop_addr_t addr)
{
	return iop_rpc_write(data, addr, iop_value_u8);
}
EXPORT_SYMBOL(iop_writeb);

int iop_writew(const u16 data, const iop_addr_t addr)
{
	return iop_rpc_write(data, addr, iop_value_u16);
}
EXPORT_SYMBOL(iop_writew);

int iop_writel(const u32 data, const iop_addr_t addr)
{
	return iop_rpc_write(data, addr, iop_value_u32);
}
EXPORT_SYMBOL(iop_writel);

int iop_module_load_buffer(const void *buf, size_t nbyte, const char *arg_)
{
	const char * const arg = arg_ ? arg_ : "";
	const size_t arg_size = strlen(arg) + 1;
	struct {
		u32 addr;
		u32 arg_size;
		char filepath[LF_PATH_MAX];
		char arg[LF_ARG_MAX];
	} load = {
		.addr = iop_alloc(nbyte),
		.arg_size = arg_size
	};
	struct {
		s32 status;
		u32 modres;
	} result;
	int err;

	if (!load.addr)
		return -ENOMEM;

	err = iop_write_memory(load.addr, buf, nbyte);
	if (err < 0)
		goto failure;

	if (arg_size >= sizeof(load.arg)) {
		err = -EOVERFLOW;
		goto failure;
	}
	memcpy(load.arg, arg, arg_size);

	err = sif_rpc(&cd_loadfile_rpc, rpo_mod_buf_load,
		&load, sizeof(load), &result, sizeof(result));
	if (err < 0)
		goto failure;

	if (result.status < 0) {
#if 1
		err = -EIO;
#else
		err = result.status == -SIF_RPC_E_GETP  ||	/* FIXME */
		      result.status == -E_SIF_PKT_ALLOC ||
		      result.status == -E_IOP_NO_MEMORY     ? -ENOMEM :
		      result.status == -SIF_RPC_E_SENDP ||
		      result.status == -E_LF_FILE_IO_ERROR  ? -EIO :
		      result.status == -E_LF_FILE_NOT_FOUND ? -ENOENT :
		      result.status == -E_LF_NOT_IRX        ? -ENOEXEC : -EIO;
#endif
		goto failure;
	}

	iop_free(load.addr);
	return result.status;

failure:
	iop_free(load.addr);
	return err;
}
EXPORT_SYMBOL(iop_module_load_buffer);

int iop_module_load_firmware_arg(const char *filepath, const char *arg)
{
	const struct firmware *fw;
	int err;
	int id;

	err = request_firmware(&fw, filepath, iopmodules_device);
	if (err < 0)
		return err;

	id = iop_module_load_buffer(fw->data, fw->size, arg);

	release_firmware(fw);

	return id;
}
EXPORT_SYMBOL(iop_module_load_firmware_arg);

int iop_module_load_firmware(const char *filepath)
{
	return iop_module_load_firmware_arg(filepath, NULL);
}
EXPORT_SYMBOL(iop_module_load_firmware);

static int __init iop_module_init(void)
{
	int err;
	int id;

	iopmodules_device = root_device_register("iop-module"); /* FIXME */
	if (!iopmodules_device) {
		printk(KERN_ERR "iop-module: Failed to register iopmodules root device.\n");
		return -ENOMEM;
	}

	err = sif_rpc_bind(&cd_loadfile_rpc, SIF_SID_LOAD_MODULE);
	if (err < 0) {
		printk(KERN_ERR "iop-module: bind err = %d\n", err);
		return err;
	}

	id = iop_module_load_firmware("ps2/poweroff.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/poweroff.irx failed with err = %d\n", id);
		return id;
	}

	id = iop_module_load_firmware("ps2/ps2dev9.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/ps2dev9.irx failed with err = %d\n", id);
		return id;
	}

	id = iop_module_load_firmware("ps2/intrelay-direct.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/intrelay-direct.irx failed with err = %d\n", id);
		return id;
	}

	id = iop_module_load_rom("rom0:ADDDRV");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading rom0:ADDDRV failed with err = %d\n", id);
		return id;
	}

#if 0
	id = iop_module_load_firmware("ps2/intrelay-direct-rpc.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/intrelay-direct-rpc.irx failed with err = %d\n", id);
		return id;
	}
	printk("iop-module: id %d\n", id);

	id = iop_module_load_firmware("ps2/intrelay-dev9.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/intrelay-dev9.irx failed with err = %d\n", id);
		return id;
	}
	printk("iop-module: id %d\n", id);

	id = iop_module_load_firmware("ps2/iomanX.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/iomanX.irx failed with err = %d\n", id);
		return id;
	}
	printk("iop-module: id %d\n", id);

	id = iop_module_load_firmware("ps2/fileXio.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/fileXio.irx failed with err = %d\n", id);
		return id;
	}
	printk("iop-module: id %d\n", id);
#endif

#if 0
	id = iop_module_load_firmware("ps2/pata_ps2.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/pata_ps2.irx failed with err = %d\n", id);
		return id;
	}
#endif

#if 0
	id = iop_module_load_firmware("ps2/foo.irx");
	if (id < 0) {
		printk(KERN_ERR "iop-module: Loading ps2/foo.irx failed with err = %d\n", id);
		return id;
	}
	printk("iop-module: id %d\n", id);
#endif

	return 0;
}

static void __exit iop_module_exit(void)
{
	/* FIXME */
}

module_init(iop_module_init);
module_exit(iop_module_exit);

MODULE_LICENSE("GPL");
