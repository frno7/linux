// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef R5900CHECK_FILE_H
#define R5900CHECK_FILE_H

#include <sys/types.h>

#include "types.h"

/**
 * struct file - file container
 * @path: path of file
 * @size: size in bytes of file
 * @data: contents of file, always NUL terminated
 */
struct file {
	char * path;
	size_t size;
	void * data;
};

struct file file_read(const char *path);

struct file file_read_fd(int fd, const char *path);

void file_free(struct file f);

bool file_valid(struct file f);

int xopen(const char *path, int oflag, ...);

int xclose(int fd);

ssize_t xread(int fd, void *buf, size_t nbyte);

#endif /* R5900CHECK_FILE_H */
