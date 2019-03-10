// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP)
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>

#include <asm/mach-ps2/iop-memory.h>

#define DEVICE_NAME "iop"

/* FIXME: What does these fields mean? */
struct iop_module_info {
	iop_addr_t next;	/* IOP address to the next module, or zero */
	iop_addr_t name;	/* IOP address to the module's name */
	__u16 version;
	__u16 newflags;
	__u16 id;
	__u16 flags;
	iop_addr_t entry;
	iop_addr_t gp;
	iop_addr_t text_start;
	__u32 text_size;
	__u32 data_size;
	__u32 bss_size;
	__u32 unused1;
	__u32 unused2;
};

static struct kobject *modules_kobj;	/* FIXME: Private device pointer */

static void *iop_to_virt(iop_addr_t iop_addr)
{
	return phys_to_virt(iop_bus_to_phys(iop_addr));
}

#define IOP_MODULE_BASE 0x800

/* FIXME: What does the empty module list look like? */
/* FIXME: Use a memory barrier */
#define iop_for_each_module(pos) \
	for ((pos) = iop_to_virt(IOP_MODULE_BASE); \
	     (pos) != NULL; \
	     (pos) = (pos)->next ? iop_to_virt((pos)->next) : NULL)

static const char *iop_module_name(const struct iop_module_info *module)
{
	return iop_to_virt(module->name);
}

const struct iop_module_info *iop_module_from_kobj(const struct kobject *kobj)
{
	const int id = simple_strtoull(kobj->name, NULL, 0);
	const struct iop_module_info *module;

	iop_for_each_module(module)
		if (module->id == id)
			return module;

	return NULL;
}

static ssize_t iop_module_name_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"%s\n", iop_module_name(module)) : 0;
}

static ssize_t iop_module_id_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE, "%u\n", module->id) : 0;
}

static ssize_t iop_module_version_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"%u.%u\n", module->version >> 8, module->version & 0xff) : 0;
}

static ssize_t iop_module_flags_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"0x%08x\n", module->flags) : 0;
}

static ssize_t iop_module_newflags_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"0x%08x\n", module->newflags) : 0;
}

static ssize_t iop_module_entry_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"0x%08x\n", module->entry) : 0;
}

static ssize_t iop_module_gp_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE, "0x%08x\n", module->gp) : 0;
}

static ssize_t iop_module_text_start_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"0x%08x\n", module->text_start) : 0;
}

static ssize_t iop_module_text_size_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"%u\n", module->text_size) : 0;
}

static ssize_t iop_module_data_size_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"%u\n", module->data_size) : 0;
}

static ssize_t iop_module_bss_size_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	const struct iop_module_info *module = iop_module_from_kobj(kobj);

	return module ? scnprintf(buf, PAGE_SIZE,
		"%u\n", module->bss_size) : 0;
}

#define DEFINE_IOP_MODULE_FIELD_ATTR(field) \
	static struct kobj_attribute iop_module_attribute_##field = \
		__ATTR(field, S_IRUGO, iop_module_##field##_show, NULL)

DEFINE_IOP_MODULE_FIELD_ATTR(name);
DEFINE_IOP_MODULE_FIELD_ATTR(id);
DEFINE_IOP_MODULE_FIELD_ATTR(version);
DEFINE_IOP_MODULE_FIELD_ATTR(flags);
DEFINE_IOP_MODULE_FIELD_ATTR(newflags);
DEFINE_IOP_MODULE_FIELD_ATTR(entry);
DEFINE_IOP_MODULE_FIELD_ATTR(gp);
DEFINE_IOP_MODULE_FIELD_ATTR(text_start);
DEFINE_IOP_MODULE_FIELD_ATTR(text_size);
DEFINE_IOP_MODULE_FIELD_ATTR(data_size);
DEFINE_IOP_MODULE_FIELD_ATTR(bss_size);

static struct attribute *iop_modules_attributes[] = {
	&iop_module_attribute_name.attr,
	&iop_module_attribute_id.attr,
	&iop_module_attribute_version.attr,
	&iop_module_attribute_flags.attr,
	&iop_module_attribute_newflags.attr,
	&iop_module_attribute_entry.attr,
	&iop_module_attribute_gp.attr,
	&iop_module_attribute_text_start.attr,
	&iop_module_attribute_text_size.attr,
	&iop_module_attribute_data_size.attr,
	&iop_module_attribute_bss_size.attr,
	NULL
};

static struct attribute_group iop_modules_attribute_group = {
	.attrs = iop_modules_attributes
};

static int iop_modules_create_sysfs(struct device *dev)
{
	const struct iop_module_info *module;
	int err = 0;

	modules_kobj = kobject_create_and_add("iop", firmware_kobj);
	if (!modules_kobj)
		return -ENOMEM;

	iop_for_each_module(module) {
		struct kobject *kobj;
		char id_string[20];

		scnprintf(id_string, sizeof(id_string), "%d", module->id);
		kobj = kobject_create_and_add(id_string, modules_kobj);
		if (!kobj) {
			err = -ENOMEM;
			goto out_err;
		}

		err = sysfs_create_group(kobj, &iop_modules_attribute_group);
		if (err)
			goto out_err;
	}

	return 0;

out_err:
	kobject_del(modules_kobj);
	modules_kobj = NULL;

	return err;
}

static void iop_modules_remove_sysfs(struct device *dev)
{
	kobject_del(modules_kobj);
}

static int iop_probe(struct platform_device *pdev)
{
	int err;

	// FIXME: platform_get_resource(pdev, IORESOURCE_MEM, 0);

	err = iop_modules_create_sysfs(&pdev->dev);

	return err;
}

static int iop_remove(struct platform_device *pdev)
{
	iop_modules_remove_sysfs(&pdev->dev);

	return 0;
}

static struct platform_driver iop_driver = {
	.probe		= iop_probe,
	.remove		= iop_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

static int __init iop_init(void)
{
	return platform_driver_register(&iop_driver);
}

static void __exit iop_exit(void)
{
	platform_driver_unregister(&iop_driver);
}

module_init(iop_init);
module_exit(iop_exit);

MODULE_LICENSE("GPL");
