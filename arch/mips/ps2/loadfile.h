#ifndef _LOADFILE_H_
#define _LOADFILE_H_

/*
 *  Playstation 2 IOP loadfile
 *
 *  Copyright (C) 2014 Juergen Urban
 */

int __init ps2_loadfile_init(void);

/**
 * Load IRX module on IOP
 *
 * @param path Path to module (needs to begin with rom0: or rom1:).
 * @size path_len length of the path.
 * @param args Module arguments, can be NULL.
 * @param arg_len Length of module arguments.
 * @param mod_res Result, can be NULL.
 *
 * @returns Module id, or negative on error.
 * @retval -ENOENT Module not found.
 * @retval -ENOEXEC Module can't be executed (not an IRX module).
 */
int ps2_load_module(const char *path, int path_len, const char *args, int arg_len, int *mod_res);

/**
 * Load IRX module on IOP
 *
 * @param ptr Pointer to module in kernel memory.
 * @size size Size of the IRX module binary.
 * @param args Module arguments, can be NULL.
 * @param arg_len Length of module arguments.
 * @param mod_res Result, can be NULL.
 *
 * @returns Module id, or negative on error.
 */
int ps2_load_module_buffer(const void *ptr, int size, const char *args, int arg_len, int *mod_res);

#endif
