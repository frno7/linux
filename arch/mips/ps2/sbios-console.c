/*
 *  PlayStation 2 SBIOS console
 *
 *  Copyright (C) 2010-2013 Juergen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/sbios.h>

void prom_putchar(char c)
{
	struct sb_putchar_arg putc_arg;

	putc_arg.c = c;
	sbios(SB_PUTCHAR, &putc_arg);
}

int ps2_printf(const char *fmt, ...)
{
	char buffer[80];
	va_list args;
	int r;
	int i;

	va_start(args, fmt);
	r = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	r = strnlen(buffer, sizeof(buffer));
	for (i = 0; i < r; i++) {
		prom_putchar(buffer[i]);
	}
	if (r >= (sizeof(buffer) - 1)) {
		/* Add carriage return if buffer was too small. */
		prom_putchar('.');
		prom_putchar('.');
		prom_putchar('.');
		prom_putchar('\n');
	}
	return r;
}
