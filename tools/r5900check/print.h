// SPDX-License-Identifier: GPL-2.0

#ifndef R5900CHECK_PRINT_H
#define R5900CHECK_PRINT_H

#include <stdarg.h>
#include <stdio.h>

#include "macro.h"

void pr_info(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

void pr_warn(const char *msg, ...)
	__attribute__((format(printf, 1, 2)));

void pr_warn_errno(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

void pr_errno(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

void NORETURN pr_fatal_error(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

void NORETURN pr_fatal_errno(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

void NORETURN pr_bug(const char *file, int line,
	const char *func, const char *expr);

void pr_mem(FILE *f, const void *data, size_t size);

#endif /* R5900CHECK_PRINT_H */
