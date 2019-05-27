// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 read-only memory (ROM) sysfs
 *
 * Copyright (C) 2019 Fredrik Noring
 *
 * FIXME: Is /sys/rom the proper placement?
 */

///
// DOC:
//
// ROM0 and ROM1 contain simple file systems that can be inspected with this
// sysfs module. For example, listing all files of ROM0::
//
// 	# cat /sys/rom/rom0/file/*/name | column
// 	RESET       VBLANK      RMRESET     MCSERV      XSIFCMD
// 	ROMDIR      IOMAN       OSDVER      PADMAN      XCDVDMAN
// 	EXTINFO     MODLOAD     -           CDVDMAN     XCDVDFSV
// 	ROMVER      ROMDRV      IOPBOOT     CDVDFSV     XFILEIO
// 	SBIN        ADDDRV      OSDCNF      FILEIO      XSIO2MAN
// 	LOGO        STDIO       -           CLEARSPU    XMTAPMAN
// 	IOPBTCONF   SIFMAN      TBIN        UDNL        XMCMAN
// 	...
//
// Viewing for example the contents of the ROM file ROMVER using /dev/mem::
//
//	# grep -l ROMVER /sys/rom/rom0/file/*/name
//	/sys/rom/rom0/file/3/name
//	# cd /sys/rom/rom0/file/3
//	# ls
//	data     extinfo  name     size
//	# dd if=/dev/mem bs=$(cat size) iflag=skip_bytes
//		skip=$(( $(cat data) )) count=1 status=none
//	0170EC20030227
//
// For convenience, the ROMVER file is also available directly in sysfs::
//
//	# ls /sys/rom/rom0/version
//	date    number  region  type
//	# cat /sys/rom/rom0/version/*
//	2003-02-27
//	0x0170
//	Europe
//	CEX
//
// The CEX type indicates that it is a retail machine, as opposed to for
// example DEX that would be a debug machine.
//
// The extended ROM file information can also be viewed with sysfs::
//
// 	# ls /sys/rom/rom0/file/8/extinfo
// 	comment  data     date     size     version
// 	# cat /sys/rom/rom0/file/8/extinfo/*
// 	System_Memory_Manager
// 	0x1fc02df4
// 	2002-04-03
// 	40
// 	0x0101
//

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <asm/page.h>
#include <asm/uaccess.h>

#include <asm/mach-ps2/rom.h>

static int rom0_sysfs(struct kobject *romn_kobj);

/* FIXME */
static const struct {
	const char *name;
	const struct rom_dir *dir;
	int (*func)(struct kobject *romn_kobj);
} rom_dirs[] = {
	{ "rom0", &rom0_dir, rom0_sysfs },
	{ "rom1", &rom1_dir, NULL },
};

static struct rom_dir rom_dir_from_kobj(const struct kobject *kobj)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(rom_dirs); i++)
		if (strcmp(rom_dirs[i].name, kobj->parent->name) == 0)
			return *rom_dirs[i].dir;

	pr_err("%s: ROM dir for \"%s\" does not exist\n", __func__,
		kobj->parent->name);

	return (struct rom_dir) { };
}

static struct rom_file rom_file_from_kobj(const struct kobject *kobj)
{
	const size_t rom_id = simple_strtoull(kobj->name, NULL, 0);
	const struct rom_dir dir = rom_dir_from_kobj(kobj->parent);
	struct rom_file file;
	size_t id = 0;

	rom_for_each_file(file, dir)
		if (id++ == rom_id)
			return file;

	pr_err("%s: ROM id %zu for \"%s\" does not exist\n", __func__,
		rom_id, kobj->parent->name);

	return (struct rom_file) { .name = "<undefined>" };
}

static struct rom_extinfo rom_extinfo_from_kobj(const struct kobject *kobj)
{
	const struct rom_file file = rom_file_from_kobj(kobj->parent);

	return rom_read_extinfo(file.name,
		file.extinfo.data, file.extinfo.size);
}

static ssize_t rom_version_number_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%04x\n", rom_version().number);
}

static ssize_t rom_version_region_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n",
		rom_region_name(rom_version().region));
}

static ssize_t rom_version_type_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n",
		rom_type_name(rom_version().type));
}

static ssize_t rom_version_date_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct rom_ver v = rom_version();

	return scnprintf(buf, PAGE_SIZE, "%04d-%02d-%02d\n",
		v.date.year, v.date.month, v.date.day);
}

static ssize_t rom_extinfo_size_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%zu\n",
		rom_file_from_kobj(kobj->parent).extinfo.size);
}

static ssize_t rom_extinfo_data_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%lx\n",
		virt_to_phys(rom_file_from_kobj(kobj->parent).extinfo.data));
}

static ssize_t rom_extinfo_version_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct rom_extinfo ei = rom_extinfo_from_kobj(kobj);

	if (!ei.version)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "0x%04x\n", ei.version);
}

static ssize_t rom_extinfo_date_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct rom_extinfo ei = rom_extinfo_from_kobj(kobj);

	if (!ei.date.year && !ei.date.month && !ei.date.day)
		return 0;

	return scnprintf(buf, PAGE_SIZE, "%04d-%02d-%02d\n",
		ei.date.year, ei.date.month, ei.date.day);
}

static ssize_t rom_extinfo_comment_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct rom_extinfo ei = rom_extinfo_from_kobj(kobj);

	if (ei.comment[0] == '\0')
		return 0;

	return scnprintf(buf, PAGE_SIZE, "%s\n", ei.comment);
}

static ssize_t rom_file_name_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n",
		rom_file_from_kobj(kobj).name);
}

static ssize_t rom_file_size_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%zu\n",
		rom_file_from_kobj(kobj).size);
}

static ssize_t rom_file_data_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%lx\n",
		virt_to_phys(rom_file_from_kobj(kobj).data));
}

#define DEFINE_ROM_FIELD_ATTR(prefix, field)				\
	static struct kobj_attribute rom_attribute_##prefix##_##field =	\
		__ATTR(field, S_IRUGO, rom_##prefix##_##field##_show, NULL)

DEFINE_ROM_FIELD_ATTR(version, number);
DEFINE_ROM_FIELD_ATTR(version, region);
DEFINE_ROM_FIELD_ATTR(version, type);
DEFINE_ROM_FIELD_ATTR(version, date);

static struct attribute *rom_version_attributes[] = {
	&rom_attribute_version_number.attr,
	&rom_attribute_version_region.attr,
	&rom_attribute_version_type.attr,
	&rom_attribute_version_date.attr,
	NULL
};

static struct attribute_group rom_version_attribute_group = {
	.attrs = rom_version_attributes
};

DEFINE_ROM_FIELD_ATTR(extinfo, size);
DEFINE_ROM_FIELD_ATTR(extinfo, data);
DEFINE_ROM_FIELD_ATTR(extinfo, version);
DEFINE_ROM_FIELD_ATTR(extinfo, date);
DEFINE_ROM_FIELD_ATTR(extinfo, comment);

static struct attribute *rom_extinfo_attributes[] = {
	&rom_attribute_extinfo_size.attr,
	&rom_attribute_extinfo_data.attr,
	&rom_attribute_extinfo_version.attr,
	&rom_attribute_extinfo_date.attr,
	&rom_attribute_extinfo_comment.attr,
	NULL
};

static struct attribute_group rom_extinfo_attribute_group = {
	.attrs = rom_extinfo_attributes
};

DEFINE_ROM_FIELD_ATTR(file, name);
DEFINE_ROM_FIELD_ATTR(file, size);
DEFINE_ROM_FIELD_ATTR(file, data);

static struct attribute *rom_file_attributes[] = {
	&rom_attribute_file_name.attr,
	&rom_attribute_file_size.attr,
	&rom_attribute_file_data.attr,
	NULL
};

static struct attribute_group rom_file_attribute_group = {
	.attrs = rom_file_attributes
};

static int rom0_sysfs(struct kobject *rom0_kobj)
{
	struct kobject *version_kobj;

	version_kobj = kobject_create_and_add("version", rom0_kobj);
	if (!version_kobj)
		return -ENOMEM;

	return sysfs_create_group(version_kobj, &rom_version_attribute_group);
}

static int __init rom_init_file_extinfo(struct kobject *index_kobj,
	const struct rom_file file)
{
	struct kobject *extinfo_kobj;

	extinfo_kobj = kobject_create_and_add("extinfo", index_kobj);
	if (!extinfo_kobj)
		return -ENOMEM;

	return sysfs_create_group(extinfo_kobj, &rom_extinfo_attribute_group);
}

static int __init rom_init_file(struct kobject *file_kobj, size_t index,
	const struct rom_file file)
{
	struct kobject *index_kobj;
	char index_string[20];
	int err;

	scnprintf(index_string, sizeof(index_string), "%zu", index);

	index_kobj = kobject_create_and_add(index_string, file_kobj);
	if (!index_kobj)
		return -ENOMEM;

	err = rom_init_file_extinfo(index_kobj, file);
	if (err)
		return err;

	return sysfs_create_group(index_kobj, &rom_file_attribute_group);
}

static int __init rom_init_dir(struct kobject *romn_kobj,
	const struct rom_dir dir)
{
	struct kobject *file_kobj = kobject_create_and_add("file", romn_kobj);
	struct rom_file file;
	size_t i = 0;

	if (!file_kobj)
		return -ENOMEM;

	rom_for_each_file(file, dir) {
		int err = rom_init_file(file_kobj, i++, file);

		if (err)
			return err;
	}

	return 0;
}

static int __init rom_init_rom(struct kobject *rom_kobj,
	const char *name, const struct rom_dir dir,
	int (*func)(struct kobject *romn_kobj))
{
	struct kobject *romn_kobj = kobject_create_and_add(name, rom_kobj);
	int err;

	if (!romn_kobj)
		return -ENOMEM;

	err = rom_init_dir(romn_kobj, dir);
	if (!err && func)
		err = func(romn_kobj);

	return err;
}

static struct kobject *rom_kobj;

static int __init rom_sysfs_init(void)
{
	size_t i;

	rom_kobj = kobject_create_and_add("rom", NULL);
	if (!rom_kobj)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(rom_dirs); i++) {
		int err = rom_init_rom(rom_kobj, rom_dirs[i].name,
			*rom_dirs[i].dir, rom_dirs[i].func);

		if (err) {
			kobject_del(rom_kobj);
			return err;
		}
	}

	return 0;
}

static void __exit rom_sysfs_exit(void)
{
	kobject_del(rom_kobj);
}

module_init(rom_sysfs_init);
module_exit(rom_sysfs_exit);

MODULE_DESCRIPTION("PlayStation 2 read-only memory (ROM) sysfs");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
