// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Fredrik Noring
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "file.h"
#include "macro.h"
#include "print.h"
#include "tool.h"

char progname[] = "r5900check";

static struct {
	int verbose;
} option;

int option_verbosity(void)
{
	return option.verbose;
}

static void help(FILE *file)
{
	fprintf(file,
"usage: %s [options]... <infile>...\n"
"\n"
"options:\n"
"    -h, --help            display this help and exit\n"
"    -v, --verbose         increase verbosity\n"
"\n",
		progname);
}

static void NORETURN help_exit(void)
{
	help(stdout);
	exit(EXIT_SUCCESS);
}

static void parse_options(int argc, char **argv)
{
#define OPT(option) (strcmp(options[index].name, (option)) == 0)

	static const struct option options[] = {
		{ "help",    no_argument, NULL, 0 },
		{ "verbose", no_argument, NULL, 0 },
		{ NULL, 0, NULL, 0 }
	};

	argv[0] = progname;	/* For better getopt_long error messages. */

	for (;;) {
		int index = 0;

		switch (getopt_long(argc, argv, "hv", options, &index)) {
		case -1:
			return;

		case 0:
			if (OPT("help"))
				help_exit();
			else if (OPT("verbose"))
				option.verbose++;
			break;

		case 'h':
			help_exit();

		case 'v':
			option.verbose++;
			break;

		case '?':
			exit(EXIT_FAILURE);
		}
	}

#undef OPT
}

int main(int argc, char **argv)
{
	parse_options(argc, argv);

	for (int i = optind; i < argc; i++) {
		struct file file = file_read(argv[i]);

		if (!file_valid(file))
			pr_fatal_errno("%s\n", file.path);

		check_file(file);

		file_free(file);
	}

	return EXIT_SUCCESS;
}
