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
#define ROM0_SIZE	0x400000	/* ROM0 maximum size */

#define ROM1_BASE	0x1e000000	/* ROM1 base address (DVD) */
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

bool rom_empty_dir(const struct rom_dir dir);

bool rom_terminating_file(const struct rom_file file);

struct rom_file rom_next_file(const struct rom_file file);

struct rom_file rom_first_file(const struct rom_dir dir);

#endif /* __ASM_MACH_PS2_ROM_H */
