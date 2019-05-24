// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 identification
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/printk.h>

#include <asm/prom.h>

#include <asm/mach-ps2/rom.h>
#include <asm/mach-ps2/scmd.h>

static int __init set_machine_name_by_scmd(void)
{
	const struct scmd_machine_name machine = scmd_read_machine_name();

	if (machine.name[0] == '\0') {
		pr_err("identify: %s: Reading failed\n", __func__);
		return -EIO;
	}

	mips_set_machine_name(machine.name);

	return 0;
}

static int __init set_machine_name_by_osdsys(void)
{
	char name[12] = { };
	int err = rom_read_file(rom0_dir, "OSDSYS",
		name, sizeof(name) - 1, 0x8c808);

	if (err) {
		pr_err("identify: %s: Reading failed with %d\n", __func__, err);
		return err;
	}

	mips_set_machine_name(name);

	return 0;
}

static void __init set_machine_name(void)
{
	const int rom_version_number = rom_version().number;
	int err = 0;

	/*
	 * ROM version 1.00 is always SCPH-10000. Later machines with
	 * ROM version 1.0x have the machine name in the ROM0 file OSDSYS
	 * at offset 0x8c808. These are late SCPH-10000 and all SCPH-15000.
	 * Even later machines have a system command (SCMD) to read the
	 * machine name.
	 */

	if (rom_version_number >= 0x110)
		err = set_machine_name_by_scmd();	/* ver >= 1.10 */
	else if (rom_version_number > 0x100)
		err = set_machine_name_by_osdsys();	/* 1.10 > ver > 1.00 */
	else if (rom_version_number == 0x100)
		mips_set_machine_name("SCPH-10000");	/* ver = 1.00 */
	else
		err = -ENODEV;

	if (err)
		pr_err("identify: Determining machine name for ROM %04x failed with %d\n",
			rom_version_number, err);
}

static int __init ps2_identify_init(void)
{
	set_machine_name();

	return 0;
}
subsys_initcall(ps2_identify_init);
