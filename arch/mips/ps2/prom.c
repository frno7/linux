/*
 * PlayStation 2 SBIOS and PROM handling
 *
 * Copyright (C) 2010-2013 Jürgen Urban
 * Copyright (C) 2017-2018 Fredrik Noring
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/memblock.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/sections.h>

#include <asm/mach-ps2/bootinfo.h>
#include <asm/mach-ps2/ps2.h>
#include <asm/mach-ps2/sbios.h>

#define SBIOS_BASE		0x80001000
#define SBIOS_SIGNATURE_OFFSET	4
#define PS2_BOOTINFO_MAGIC	0x50324c42	/* "P2LB" */
#define PS2_BOOTINFO_ADDR	0x01fff000

int ps2_pccard_present;
int ps2_pcic_type;
struct ps2_sysconf *ps2_sysconf;

EXPORT_SYMBOL(ps2_sysconf);
EXPORT_SYMBOL(ps2_pcic_type);
EXPORT_SYMBOL(ps2_pccard_present);

static struct ps2_bootinfo bootinfo;
struct ps2_bootinfo *ps2_bootinfo = &bootinfo;
EXPORT_SYMBOL(ps2_bootinfo);

void prom_putchar(char c)
{
}

static u32 ps2_sbios_signature(void)
{
	return *(u32*)(SBIOS_BASE + SBIOS_SIGNATURE_OFFSET);
}

static bool ps2_has_bootinfo(void)
{
	return ps2_sbios_signature() == 0x62325350;	/* "9588" */
}

static void __init prom_init_cmdline(void)
{
	if (ps2_bootinfo->opt_string != 0)
		strlcpy(arcs_cmdline, (const char *)ps2_bootinfo->opt_string,
			COMMAND_LINE_SIZE);
}

void __init prom_init(void)
{
	memset(&bootinfo, 0, sizeof(bootinfo));
	bootinfo.sbios_base = SBIOS_BASE;
	if (ps2_has_bootinfo())
		memcpy(ps2_bootinfo, phys_to_virt(PS2_BOOTINFO_ADDR),
			PS2_BOOTINFO_SIZE);

	prom_init_cmdline();

	ps2_pccard_present = ps2_bootinfo->pccard_type;
	ps2_pcic_type = ps2_bootinfo->pcic_type;
	ps2_sysconf = &ps2_bootinfo->sysconf;
}

void __init prom_free_prom_memory(void)
{
}
