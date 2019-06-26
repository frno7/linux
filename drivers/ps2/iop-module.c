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
 * IOP module link requests are only permitted if the major versions match
 * and the module version is at least of the same minor as the requested
 * version.
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
 *
 * As a simplification the IOP module linker assumes that modules with
 * exported libraries are resident. The primary effect is that it refuses to
 * link modules exporting libraries with the same names more than once, even
 * if the they may have been unlinked in the IOP. This is because the IOP
 * module linker maintains its own list of exported libraries, rather than
 * asking the IOP about them, which remains to be implemented.
 *
 * The IOP module linker also assumes that if a module exports a library it
 * has the same name as the module. The simplifies resolving dependencies. In
 * general, and with most ROM modules, the module name is not the same as the
 * exported library names. A single module may export multiple libraries, but
 * that is currently not not fully supported with IOP modules for Linux, due
 * to dependency resolving limitations.
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
static LIST_HEAD(linked_libraries);

#define IOPMOD_MAX_PATH		252
#define IOPMOD_MAX_ARG		252
#define IOPMOD_MAX_LIBRARY_NAME	8

#define IOPMOD_NO_ID		0xffffffff
#define IOPMOD_IMPORT_MAGIC	0x41e00000
#define IOPMOD_EXPORT_MAGIC	0x41c00000

#define SHT_IOPMOD		(SHT_LOPROC + 0x80)

/**
 * struct irx_iopmod - special .iopmod section with module name, version, etc.
 * @id_addr: address of a special identification structure, or %IOPMOD_NO_ID
 * @entry_addr: module entry address to begin executing code
 * @unknown: FIXME
 * @text_size: size in bytes of text section
 * @data_size: size in bytes of data section
 * @bss_size: size in bytes of BSS section
 * @version: module version in BCD
 * @name: NUL-terminated name of module
 */
struct irx_iopmod {
	u32 id_addr;
	u32 entry_addr;
	u32 unknown;
	u32 text_size;
	u32 data_size;
	u32 bss_size;
	u16 version;
	char name[0];
};

/**
 * struct irx_import_link - link entry for an imported library
 * @jr: unconditional MIPS I jump to return address instruction
 * @jr.target: jump target, to be resolved by the IOP linker
 * @jr.op: operation code for the jump register instruction
 * @li: 16-bit load immediate pseudo-instruction
 * @li.imm: index of the imported library link entry
 * @li.rt: `$0` register
 * @li.rs: `$0` register
 * @li.op: operation code for the load immediate pseudo-instruction
 */
struct irx_import_link {
	struct {
		u32 target : 26;
		u32 op : 6;
	} jr;
	struct {
		u32 imm : 16;
		u32 rt : 5;
		u32 rs : 5;
		u32 op : 6;
	} li;
};

/**
 * struct irx_import_library - link entry table for an imported library
 * @magic: %IOPMOD_IMPORT_MAGIC marks the beginning of the link entry table
 * @zero: always zero
 * @version: 16-bit version in BCD, with 8-bit minor and 8-bit major
 * @name: library name, not NUL terminated unless shorter than
 * 	%IOPMOD_MAX_LIBRARY_NAME characters
 * @link: array of imported link entries, with the terminating entry zero
 *
 * The &struct irx_import_library resides in the .text section of the module.
 */
struct irx_import_library {
	u32 magic;
	u32 zero;
	u32 version;
	char name[IOPMOD_MAX_LIBRARY_NAME];
	struct irx_import_link link[0];
};

/**
 * struct irx_export_link - link entry for an exported library
 * @addr: address to link
 */
struct irx_export_link {
	u32 addr;
};

/**
 * struct irx_export_library - link entry table for an exported library
 * @magic: %IOPMOD_EXPORT_MAGIC marks the beginning of the link entry table
 * @zero: always zero
 * @version: 16-bit version in BCD, with 8-bit minor and 8-bit major
 * @name: library name, not NUL terminated unless shorter than
 * 	%IOPMOD_MAX_LIBRARY_NAME characters
 * @link: array of exported link entries, with the terminating entry zero
 *
 * The &struct irx_export_library resides in the .text section of the module.
 */
struct irx_export_library {
	u32 magic;
	u32 zero;
	u32 version;
	char name[IOPMOD_MAX_LIBRARY_NAME];
	struct irx_export_link link[0];
};

/**
 * struct library_entry - list of linked libraries
 * @list: linked list of library entries
 * @name: library name, not NUL terminated unless shorter than
 * 	%IOPMOD_MAX_LIBRARY_NAME characters
 * @version: 16-bit version in BCD, with 8-bit minor and 8-bit major
 *
 * The IOP module linker maintains its own list of linked libraries, rather
 * than asking the IOP about them, which remains to be implemented.
 */
struct library_entry {
	struct list_head list;

	char name[IOPMOD_MAX_LIBRARY_NAME];
	int version;
};

/**
 * elf_ent_for_offset - pointer given an ELF offset
 * @offset: ELF offset
 * @ehdr: ELF header of module
 *
 * Return: pointer for a given ELF offset
 */
static const void *elf_ent_for_offset(Elf32_Off offset,
	const struct elf32_hdr *ehdr)
{
	return &((const u8 *)ehdr)[offset];
}

/**
 * elf_first_section - first ELF section
 * @ehdr: ELF header of module
 *
 * Return: pointer to the first ELF section, or %NULL if it does not exist
 */
static const struct elf32_shdr *elf_first_section(const struct elf32_hdr *ehdr)
{
	return ehdr->e_shnum ? elf_ent_for_offset(ehdr->e_shoff, ehdr) : NULL;
}

/**
 * elf_next_section - next ELF section
 * @shdr: header of current section
 * @ehdr: ELF header of module
 *
 * Return: section following the current section, or %NULL
 */
static const struct elf32_shdr *elf_next_section(
	const struct elf32_shdr *shdr, const struct elf32_hdr *ehdr)
{
	const struct elf32_shdr *next = &shdr[1];
	const struct elf32_shdr *past = &elf_first_section(ehdr)[ehdr->e_shnum];

	return next == past ? NULL: next;
}

/**
 * elf_for_each_section - iterate over all ELF sections
 * @shdr: &struct elf32_shdr loop cursor
 * @ehdr: ELF header of module to iterate
 */
#define elf_for_each_section(shdr, ehdr)				\
	for ((shdr) = elf_first_section((ehdr));			\
	     (shdr);							\
	     (shdr) = elf_next_section((shdr), (ehdr)))

/**
 * elf_strings - base of ELF module string table
 * @ehdr: ELF header of module
 *
 * Return: pointer to base of ELF module table
 */
static const char *elf_strings(const struct elf32_hdr *ehdr)
{
	const struct elf32_shdr *shdr;

	if (ehdr->e_shstrndx == SHN_UNDEF)
		return NULL;

	shdr = &elf_first_section(ehdr)[ehdr->e_shstrndx];

	return elf_ent_for_offset(shdr->sh_offset, ehdr);
}

/**
 * elf_section_name - name of section
 * @shdr: header of section to provide name for
 * @ehdr: ELF header of module for section
 *
 * Return: name of given section
 */
static const char *elf_section_name(const struct elf32_shdr *shdr,
	const struct elf32_hdr *ehdr)
{
	return &elf_strings(ehdr)[shdr->sh_name];
}

/**
 * elf_first_section_with_type - first section with given type
 * @type: type of section to search for
 * @ehdr: ELF header of module to search
 *
 * Return: pointer to the first occurrence of the section, or %NULL if it does
 * 	not exist
 */
static const struct elf32_shdr *elf_first_section_with_type(
	Elf32_Word type, const struct elf32_hdr *ehdr)
{
	const struct elf32_shdr *shdr;

	elf_for_each_section (shdr, ehdr)
		if (shdr->sh_type == type)
			return shdr;

	return NULL;
}

/**
 * elf_first_section_with_name - first section with given name
 * @name: name of section to search for
 * @ehdr: ELF header of module to search
 *
 * Return: pointer to the first occurrence of the section, or %NULL if it does
 * 	not exist
 */
static const struct elf32_shdr *elf_first_section_with_name(
	const char *name, const struct elf32_hdr *ehdr)
{
	const struct elf32_shdr *shdr;

	elf_for_each_section (shdr, ehdr)
		if (strcmp(elf_section_name(shdr, ehdr), name) == 0)
			return shdr;

	return NULL;
}

/**
 * elf_identify - does the buffer contain an ELF object?
 * @buffer: pointer to data to identify
 * @size: size in bytes of buffer
 *
 * Return: %true if the buffer looks like an ELF object, otherwise %false
 */
static bool elf_identify(const void *buffer, size_t size)
{
	const struct elf32_hdr *ehdr = buffer;

	if (size < sizeof(*ehdr))
		return false;

	return ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
	       ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
	       ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
	       ehdr->e_ident[EI_MAG3] == ELFMAG3 &&
	       ehdr->e_ident[EI_VERSION] == EV_CURRENT;
}

/**
 * library_entry - find occurrence of 4-byte magic integer
 * @library: import or export library starting pointer
 * @magic: 4-byte magic integer to search for
 * @ehdr: ELF header of module to search
 *
 * The %IOPMOD_IMPORT_MAGIC and %IOPMOD_EXPORT_MAGIC 4-byte integers are used
 * to mark libraries that are imported and exported by the module.
 *
 * Return: library entry following the current library entry, or %NULL
 */
static const void *library_entry(const void *library, u32 magic,
	const struct elf32_hdr *ehdr)
{
	const struct elf32_shdr *shdr =
		elf_first_section_with_name(".text", ehdr);
	const u32 *text = elf_ent_for_offset(shdr->sh_offset, ehdr);
	const size_t length = shdr->sh_size / sizeof(*text);
	const size_t index = library ? ((u32 *)library - text) + 1 : 0;
	size_t i;

	for (i = index; i < length; i++)
		if (text[i] == magic)
			return &text[i];

	return NULL;
}

/**
 * irx_first_export_library - first exported library entry
 * @ehdr: ELF header of module
 *
 * Return: first exported library entry, or %NULL
 */
static const struct irx_export_library *irx_first_export_library(
	const struct elf32_hdr *ehdr)
{
	return library_entry(NULL, IOPMOD_EXPORT_MAGIC, ehdr);
}

/**
 * irx_next_export_library - next exported library entry
 * @library: current library entry
 * @ehdr: ELF header of module
 *
 * Return: exported library entry following the current library entry, or %NULL
 */
static const struct irx_export_library *irx_next_export_library(
	const struct irx_export_library *library, const struct elf32_hdr *ehdr)
{
	return library_entry(library, IOPMOD_EXPORT_MAGIC, ehdr);
}

/**
 * irx_for_each_export_library - iterate over exported libraries
 * @library: &struct irx_export_library loop cursor
 * @ehdr: ELF header of module to iterate
 */
#define irx_for_each_export_library(library, ehdr)			\
	for ((library) = irx_first_export_library(ehdr);		\
	     (library);							\
	     (library) = irx_next_export_library((library), (ehdr)))

/**
 * irx_iopmod - give .iopmod section pointer, if it exists
 * @ehdr: ELF header of module
 *
 * The .iopmod section is specific to IOP (IRX) modules.
 *
 * Return: .iopmod section pointer, or %NULL
 */
static const struct irx_iopmod *irx_iopmod(const struct elf32_hdr *ehdr)
{
	const struct elf32_shdr *shdr =
		elf_first_section_with_type(SHT_IOPMOD, ehdr);

	return shdr ? elf_ent_for_offset(shdr->sh_offset, ehdr) : NULL;
}

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
 * version_compatible - is the version compatible with the requested version?
 * @version: version to check
 * @version_request: requested version
 *
 * Return: %true if the major versions match and the version to check is at
 * 	least of the same minor as the requested version, otherwise %false
 */
static bool version_compatible(int version, int requested_version)
{
	return major_version(version) == major_version(requested_version) &&
	       minor_version(version) >= minor_version(requested_version);
}

/**
 * irx_version_compatible - is the module compatible with the requested version?
 * @ehdr: ELF header of module to check
 * @requested_version: request version
 *
 * Return: %true if the major versions match and the module version is at
 * 	least of the same minor as the requested version, otherwise %false
 */
static bool irx_version_compatible(const struct elf32_hdr *ehdr,
	int requested_version)
{
	return version_compatible(irx_iopmod(ehdr)->version, requested_version);
}

/**
 * irx_identify - does the buffer contain an IRX object?
 * @buffer: pointer to data to identify
 * @size: size in bytes of buffer
 *
 * Return: %true if the buffer looks like an IRX object, otherwise %false
 */
static bool irx_identify(const void *buffer, size_t size)
{
	return elf_identify(buffer, size) && irx_iopmod(buffer) != NULL;
}

/**
 * library_provided_by_firmware - is the library provided by linked firmware?
 * @name: name of the library to search for
 *
 * Return: %true if some previously linked firmware provides the library,
 * 	otherwise %false
 */
static bool library_provided_by_firmware(const char *name)
{
	const struct library_entry *library;

	list_for_each_entry (library, &linked_libraries, list)
		if (strncmp(name, library->name, sizeof(library->name)) == 0)
			return true;

	return false;
}

/**
 * register_libraries - register libraries provided by the given module
 * @ehdr: IOP module
 *
 * FIXME: Libraries are maintained in a linked list by the kernel as a
 * simplification. This list is not updated if the modules providing the
 * libraries are unlinked. In principle one could query the IOP about its
 * modules, but that has not been implemented yet. For now it is assumed
 * the modules do not unlink themselves.
 *
 * Return: 0 on success, otherwise a negative error number
 */
static int register_libraries(const struct elf32_hdr *ehdr)
{
	const struct irx_export_library *library;

	irx_for_each_export_library (library, ehdr) {
		struct library_entry *entry =
			kmalloc(sizeof(*entry), GFP_KERNEL);

		if (!entry)
			return -ENOMEM;

		*entry = (struct library_entry) {
			.version = library->version,
		};
		memcpy(entry->name, library->name, IOPMOD_MAX_LIBRARY_NAME);

		list_add(&entry->list, &linked_libraries);
	}

	return 0;
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

	err = register_libraries(buf);
	if (err < 0)
		goto err_libraries;

	iop_free(link.addr);
	return 0;

err_libraries:
	/* FIXME: Unlink module here */

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

	if (library_provided_by_firmware(name)) {
		pr_debug("iop-module: %s module is already provided\n", name);
		return 0;
	}

	pr_debug("iop-module: %s module linking as firmware\n", name);

	if (snprintf(filepath, sizeof(filepath),
			"ps2/%s.irx", name) == sizeof(filepath) - 1) {
		err = -ENAMETOOLONG;
		goto err_name;
	}

	err = request_firmware(&fw, filepath, iop_module_device);
	if (err < 0)
		goto err_request;

	if (!irx_identify(fw->data, fw->size)) {
		pr_err("iop-module: %s module is not an IRX object\n",
			filepath);
		err = -ENOEXEC;
		goto err_identify;
	}

	ehdr = (const struct elf32_hdr *)fw->data;

	if (!irx_version_compatible(ehdr, version)) {
		pr_err("iop-module: %s module version %u.%u is incompatible with requested version %u.%u\n",
			filepath,
			major_version(irx_iopmod(ehdr)->version),
			minor_version(irx_iopmod(ehdr)->version),
			major_version(version),
			minor_version(version));
		err = -ENOEXEC;
		goto err_incompatible;
	}

	err = iop_module_link_buffer(fw->data, fw->size, arg);

err_incompatible:
err_identify:
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
