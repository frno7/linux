// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Fredrik Noring
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compare.h"
#include "macro.h"
#include "print.h"
#include "tool.h"

static void report(const char *prefix, const char *suffix,
	const char *fmt, va_list ap)
{
	char msg[4096];

	vsnprintf(msg, sizeof(msg), fmt, ap);

	fprintf(stderr, "%s: %s%s%s%s%s", progname,
		prefix, prefix[0] ? ": " : "",
		suffix, suffix[0] ? ": " : "",
		msg);
}

void pr_info(const char *fmt, ...)
{
	if (option_verbosity()) {
		char msg[4096];
		va_list ap;

		va_start(ap, fmt);
		vsnprintf(msg, sizeof(msg), fmt, ap);
		printf("%s", msg);
		va_end(ap);
	}
}

void pr_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report("warning", "", fmt, ap);
	va_end(ap);
}

void pr_warn_errno(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report("warning", strerror(errno), fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

void pr_errno(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report("error", strerror(errno), fmt, ap);
	va_end(ap);
}

void NORETURN pr_fatal_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report("error", "", fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

void NORETURN pr_fatal_errno(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	report("error", strerror(errno), fmt, ap);
	va_end(ap);

	exit(EXIT_FAILURE);
}

void NORETURN pr_bug(const char *file, int line,
	const char *func, const char *expr)
{
	fprintf(stderr, "%s: BUG: %s:%d:%s: %s\n",
		progname, file, line, func, expr);

	exit(EXIT_FAILURE);
}

static void pr_printables(FILE *f,
	size_t offset, size_t columns, size_t size, const u8 *b)
{
	const size_t d = size - offset;
	for (size_t i = 0; i < (d < columns ? columns - d : 0); i++)
		fprintf(f, "   ");
	fprintf(f, " ");

	for (size_t i = 0; i < min(columns, size - offset); i++)
		fprintf(f, "%c", isprint(b[offset + i]) ? b[offset + i] : '.');
}

void pr_mem(FILE *f, const void *data, size_t size)
{
	const int columns = 16;
	const u8 *b = data;

	for (size_t i = 0; i < size; i++) {
		char offset[32];

		sprintf(offset, "\n\t%06zx ", i & 0xfff);

		fprintf(f, "%s%02x",
			!i ? &offset[1] : i % columns == 0 ?  &offset[0] : " ",
			b[i]);

		if ((i + 1) % columns == 0 || i + 1 == size)
			pr_printables(f, i - (i % columns), columns, size, b);
	}
}
