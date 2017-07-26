/*
 *  Playstation 2 SBIOS/PROM handling.
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

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/sections.h>

#include <asm/mach-ps2/bootinfo.h>
#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/sbios.h>

#define SBIOS_BASE	0x80001000
#define SBIOS_SIGNATURE	4

int ps2_pccard_present;
int ps2_pcic_type;
struct ps2_sysconf *ps2_sysconf;

EXPORT_SYMBOL(ps2_sysconf);
EXPORT_SYMBOL(ps2_pcic_type);
EXPORT_SYMBOL(ps2_pccard_present);

static struct ps2_bootinfo ps2_bootinfox;
struct ps2_bootinfo *ps2_bootinfo = &ps2_bootinfox;
EXPORT_SYMBOL(ps2_bootinfo);

static void sbios_prints(const char *text)
{
	printk("SBIOS: %s", text);
}

void prom_putchar(char c)
{
}

void __init prom_init(void)
{
	struct ps2_bootinfo *bootinfo;
	int version;

	memset(&ps2_bootinfox, 0, sizeof(struct ps2_bootinfo));
	ps2_bootinfox.sbios_base = SBIOS_BASE;

	if (*(uint32_t *)(SBIOS_BASE + SBIOS_SIGNATURE) == 0x62325350) {
	    bootinfo = (struct ps2_bootinfo *)phys_to_virt(PS2_BOOTINFO_OLDADDR);
	    memcpy(ps2_bootinfo, bootinfo, PS2_BOOTINFO_OLDSIZE);
	}

	/* get command line parameters */
	if (ps2_bootinfo->opt_string != 0) {
	    strncpy(arcs_cmdline, (const char *) ((uintptr_t) ps2_bootinfo->opt_string), COMMAND_LINE_SIZE);
	    arcs_cmdline[COMMAND_LINE_SIZE - 1] = '\0';
	}

	ps2_pccard_present = ps2_bootinfo->pccard_type;
	ps2_pcic_type = ps2_bootinfo->pcic_type;
	ps2_sysconf = &ps2_bootinfo->sysconf;

	version = sbios(SB_GETVER, 0);
	printk("PlayStation 2 SIF BIOS: %04x\n", version);

	sbios(SB_SET_PRINTS_CALLBACK, sbios_prints);

	/* Remove restriction to /BWLINUX path in mc calls. */
	/* This patches the string in SBIOS. */
	if (version == 0x200) {
		/* Patch beta kit */
		*((volatile unsigned char *)0x80007c20) = 0;
	} else if (version == 0x250) {
		/* Patch 1.0 kit */
		*((volatile unsigned char *)0x800081b0) = 0;
	}
}

void __init prom_free_prom_memory(void)
{
}
