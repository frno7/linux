// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) module linker
 *
 * Copyright (C) 2019 Fredrik Noring
 */

/**
 * DOC: PlayStation 2 input/output processor (IOP) module linker
 *
 * IOP modules are IRX objects based on the executable and linkable format
 * (ELF). All valid IOP modules have a special `.iopmod` section containing
 * the module name, version, etc.
 *
 * When the IOP is reset, a set of modules are automatically linked from
 * read-only memory (ROM). Non-ROM modules are handled as firmware by the
 * IOP module linker.
 *
 * IOP modules may import and export any number of library functions,
 * including non at all. Imported libraries must be resolved and prelinked
 * before the given module is allowed to link itself. Other modules can link
 * with its exported libraries.
 *
 * IOP modules begin to execute their entry function immediately after linking.
 * The modules can either stay resident in the IOP, and provide services, or
 * unlink themselves when exiting the entry function. Many modules provide
 * remote procedure call (RPC) services via the sub-system interface (SIF).
 */

#include <linux/bcd.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "uapi/linux/elf.h"

#include <asm/mach-ps2/iop-error.h>
#include <asm/mach-ps2/iop-heap.h>
#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/rom.h>
#include <asm/mach-ps2/sif.h>

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

/** The @iop_module_lock must be taken for all IOP module linking operations */
static DEFINE_MUTEX(iop_module_lock);

static struct device *iop_module_device;
static struct sif_rpc_client load_file_rpc_client;

#define IOPMOD_MAX_PATH		252
#define IOPMOD_MAX_ARG		252

/**
 * major_version - major version of version in BCD
 * @version: 16-bit version in BCD, with 8-bit minor and 8-bit major
 *
 * Return: major version
 */
static unsigned int major_version(unsigned int version)
{
	return bcd2bin((version >> 8) & 0xff);
}

/**
 * minor_version - minor version of version in BCD
 * @version: 16-bit version in BCD, with 8-bit minor and 8-bit major
 *
 * Return: minor version
 */
static unsigned int minor_version(unsigned int version)
{
	return bcd2bin(version & 0xff);
}

/**
 * iop_module_link_buffer - link IOP module given in buffer
 * @buf: buffer containing the IOP module to link
 * @nbyte: size in bytes of given buffer
 * @arg: arguments to the IOP module entry function, or %NULL
 *
 * Return: 0 on success, otherwise a negative error number
 */
static int iop_module_link_buffer(const void *buf, size_t nbyte,
	const char *arg)
{
	const char * const arg_ = arg ? arg : "";
	const size_t arg_size = strlen(arg_) + 1;
	struct {
		u32 addr;
		u32 arg_size;
		char filepath[IOPMOD_MAX_PATH];
		char arg[IOPMOD_MAX_ARG];
	} link = {
		.addr = iop_alloc(nbyte),
		.arg_size = arg_size
	};
	struct {
		s32 status;
		u32 modres;
	} result;
	int err;

	BUILD_BUG_ON(sizeof(link) != 512);

	if (!link.addr)
		return -ENOMEM;

	/* Copy the module to IOP memory. */
	memcpy(iop_bus_to_virt(link.addr), buf, nbyte);

	/* Make the module visible to the IOP. */
	dma_cache_wback((unsigned long)iop_bus_to_virt(link.addr), nbyte);

	if (arg_size >= sizeof(link.arg)) {
		err = -EOVERFLOW;
		goto err_out;
	}
	memcpy(link.arg, arg_, arg_size);

	err = sif_rpc(&load_file_rpc_client, rpo_mod_buf_load,
		&link, sizeof(link), &result, sizeof(result));
	if (err < 0)
		goto err_out;

	if (result.status < 0) {
		pr_err("iop-module: %s: sif_rpc failed with %d: %s\n", __func__,
			result.status, iop_error_message(result.status));
		err = errno_for_iop_error(result.status);
		goto err_out;
	}

	iop_free(link.addr);
	return 0;

err_out:
	iop_free(link.addr);
	return err;
}

static int iop_module_request_firmware(
	const char *name, int version, const char *arg);

/**
 * iop_module_request_firmware - link IOP module as firmware
 * @name: name of requested module
 * @version: requested version in BCD, where major must match with a least
 * 	the same minor
 * @arg: module arguments or %NULL
 *
 * Return: 0 on success, otherwise a negative error number
 */
static int iop_module_request_firmware(
	const char *name, int version, const char *arg)
{
	const struct firmware *fw = NULL;
	const struct elf32_hdr *ehdr;
	char filepath[32];
	int err;

	pr_debug("iop-module: %s module linking as firmware\n", name);

	if (snprintf(filepath, sizeof(filepath),
			"ps2/%s.irx", name) == sizeof(filepath) - 1) {
		err = -ENAMETOOLONG;
		goto err_name;
	}

	err = request_firmware(&fw, filepath, iop_module_device);
	if (err < 0)
		goto err_request;

	ehdr = (const struct elf32_hdr *)fw->data;

	err = iop_module_link_buffer(fw->data, fw->size, arg);

err_request:
err_name:

	if (err < 0)
		pr_err("iop-module: %s module version %u.%u request failed with %d\n",
			filepath, major_version(version),
			minor_version(version), err);

	release_firmware(fw);

	return err;
}

/**
 * iop_module_request - link requested IOP module unless it is already linked
 * @name: name of requested module
 * @version: requested version in BCD, where major must match with a least
 * 	the same minor
 * @arg: module arguments or %NULL
 *
 * Module library dependencies are resolved and prelinked as necessary. Module
 * files are handled as firmware by the IOP module linker.
 *
 * IOP module link requests are only permitted if the major versions match
 * and the version is at least of the same minor as the requested version.
 *
 * Context: mutex
 * Return: 0 on success, otherwise a negative error number
 */
int iop_module_request(const char *name, int version, const char *arg)
{
	int err;

	mutex_lock(&iop_module_lock);

	pr_debug("iop-module: %s module version %u.%u requested%s%s\n",
		name, major_version(version), minor_version(version),
		arg ? " with argument " : "", arg ? arg : "");

	err = iop_module_request_firmware(name, version, arg);

	if (err)
		pr_debug("iop-module: %s module request resulted in %d\n",
			name, err);
	else
		pr_debug("iop-module: %s module request successful\n", name);

	mutex_unlock(&iop_module_lock);

	return err;
}
EXPORT_SYMBOL_GPL(iop_module_request);

static int __init iop_module_init(void)
{
	int err;

	iop_module_device = root_device_register("iop-module");
	if (!iop_module_device) {
		pr_err("iop-module: Failed to register root device\n");
		return -ENOMEM;
	}

	err = sif_rpc_bind(&load_file_rpc_client, SIF_SID_LOAD_MODULE);
	if (err < 0) {
		pr_err("iop-module: Failed to bind load module with %d\n", err);
		goto err_bind;
	}

	return 0;

err_bind:
	root_device_unregister(iop_module_device);

	return err;
}

module_init(iop_module_init);

MODULE_DESCRIPTION("PlayStation 2 input/output processor (IOP) module linker");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
