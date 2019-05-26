// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 read-only memory (ROM)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_ROM_H
#define __ASM_MACH_PS2_ROM_H

#include <linux/types.h>

#define ROM0_BASE	0x1fc00000	/* ROM0 base address (boot) */
#define ROM1_BASE	0x1e000000	/* ROM1 base address (DVD) */

#define ROM0_SIZE	0x400000	/* ROM0 maximum size */
#define ROM1_SIZE	0x100000	/* ROM1 maximum size */

struct rom_dir_entry;

/**
 * struct rom_dir - ROM directory
 * @size: size in bytes of all files combined
 * @data: pointer to data of all files combined
 * @extinfo: extended information of all files combined
 * @extinfo.size: size in bytes of all extended information combined
 * @extinfo.data: pointer to data of all extended information combined
 * @entries: pointer to array of ROM directory entries, with the terminating
 * 	file as the last entry
 *
 * A directory is considered to be empty if @size is zero, in which case all
 * members are zero.
 */
struct rom_dir {
	size_t size;
	const void *data;

	struct {
		size_t size;
		const void *data;
	} extinfo;

	const struct rom_dir_entry *entries;
};

/**
 * struct rom_file - ROM file
 * @name: name of file, or the empty string for the terminating file
 * @size: size in bytes of file
 * @data: pointer to data of file
 * @extinfo: extended ROM file information
 * @extinfo.size: size in bytes of extended file information
 * @extinfo.data: pointer to data of extended file information
 * @next: pointer to next file, unless this is the terminating file
 *
 * A file is considered to be a terminating file if @name is the empty string.
 * A terminating file is the last file in a &struct rom_dir directory.
 */
struct rom_file {
	const char *name;
	size_t size;
	const void *data;

	struct {
		size_t size;
		const void *data;
	} extinfo;

	const struct rom_dir_entry *next;
};

/**
 * struct rom_extinfo - extended ROM file information
 * @version: version number
 * @date: date ROM was created
 * @date.year: year ROM was created
 * @date.month: month ROM was created
 * @date.day: day ROM was created
 * @comment: comment or the empty string
 */
struct rom_extinfo {
	int version;
	struct {
		int year;
		int month;
		int day;
	} date;
	const char *comment;
};

extern struct rom_dir rom0_dir;		/* ROM0 directory (boot) */
extern struct rom_dir rom1_dir;		/* ROM1 directory (DVD) */

/**
 * rom_for_each_file - iterate over files in given ROM directory
 * @file: &struct rom_file to use as a ROM file loop cursor
 * @dir: &struct rom_dir with ROM directory to iterate over
 *
 * The statement following the macro is executed for ROM files in the
 * directory.
 */
#define rom_for_each_file(file, dir)					\
	for ((file) = rom_first_file(dir);				\
	     !rom_terminating_file(file);				\
	     (file) = rom_next_file(file))

/**
 * rom_find_files - find ROM files with the given name, if they exist
 * @file: &struct rom_file to use as a ROM file match cursor
 * @dir: &struct rom_dir with ROM directory to search in
 * @filename: file name to look for
 *
 * The statement following the macro is executed for ROM files having the
 * given name.
 */
#define rom_find_files(file, dir, filename)				\
	rom_for_each_file((file), (dir))				\
		if (strcmp((file).name, filename) != 0) continue; else

/**
 * rom_read_file - read ROM file data
 * @dir: directory to read the file from
 * @name: file name to read
 * @buffer: pointer to buffer to store data that is read
 * @size: size in bytes to read
 * @offset: offset in bytes to start reading
 *
 * Context: any
 * Return: on successful completion, a nonnegative integer indicating the
 * 	number of bytes actually read; otherwise, a negative error number
 */
ssize_t rom_read_file(const struct rom_dir dir,
	const char *name, void *buffer, size_t size, loff_t offset);

/**
 * rom_read_extinfo - read EXTINFO for a ROM file
 * @name: name of ROM file, used for error reporting
 * @buffer: pointer to EXTINFO data
 * @size: size of EXTINFO data
 *
 * Return: EXTINFO for ROM file, where undefined members are zero or the empty
 * 	string in the case of the comment
 */
struct rom_extinfo rom_read_extinfo(const char *name,
	const void *buffer, size_t size);

/**
 * struct rom_ver - ROM version
 * @number: ROM version number
 * @region: ROM region with ``'J'`` for Japan, ``'E'`` for Europe,
 * 	``'C'`` for China, and ``'A'`` or ``'H'`` for the USA
 * @type: ROM type with ``'C'`` for retail (CEX), ``'D'`` for debug (DEX),
 * 	and `'T'`` for TOOL, or ``'-'`` for undefined
 * @date: date ROM was created
 * @date.year: year ROM was created
 * @date.month: month ROM was created
 * @date.day: day ROM was created
 *
 * The @type name can be resolved with rom_type_name().
 *
 * Note that the Namco System 246 arcade systems are TOOL types as well.
 *
 * A ROM version is considered to be invalid if @number is zero, in which
 * case all members are zero except @region and @type that are ``'-'``.
 */
struct rom_ver {
	int number;
	char region;
	char type;
	struct {
		int year;
		int month;
		int day;
	} date;
};

/**
 * rom_version - read the ROMVER file in ROM0
 *
 * Context: any
 * Return: ROM version; or, if reading failed, all members zeroed except
 *      @region and @type that are set to ``'-'``
 */
struct rom_ver rom_version(void);

/**
 * rom_type_name - name for the ROM type character in the ROMVER file in ROM0
 * @type: &rom_ver.type type character.
 *
 * Context: any
 * Return: ROM type name, ``"-"`` if undefined or ``"?"`` if unrecognised
 */
const char *rom_type_name(char type);

/* The ROM functions below are mainly for internal use. */

/**
 * rom_empty_dir - is the ROM directory empty?
 * @dir: ROM directory to check
 *
 * Context: any
 * Return: %true if the ROM directory is empty, else %false
 */
bool rom_empty_dir(const struct rom_dir dir);

/**
 * rom_terminating_file - is this a terminating ROM file?
 * @file: ROM file to check
 *
 * Context: any
 * Return: %true if the ROM file is a terminating entry, else %false
 */
bool rom_terminating_file(const struct rom_file file);

/**
 * rom_next_file - advance to next ROM file, unless terminating file is given
 * @file: ROM file to advance from
 *
 * Context: any
 * Return: next ROM file, or a terminating file
 */
struct rom_file rom_next_file(const struct rom_file file);

/**
 * rom_first_file - first ROM file of a ROM directory
 * @dir: ROM directory to retrieve the first file for
 *
 * Context: any
 * Return: first ROM file, or a terminating file if the directory is empty
 */
struct rom_file rom_first_file(const struct rom_dir dir);

#endif /* __ASM_MACH_PS2_ROM_H */
