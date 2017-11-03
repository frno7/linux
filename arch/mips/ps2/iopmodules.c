/*
 *  Playstation 2 IOP IRX module management
 *
 *  Copyright (C) 2014 Juergen Urban
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/device.h>

#include <asm/mach-ps2/sifdefs.h>

#include "loadfile.h"

/* Module info entry. */
typedef struct _smod_mod_info {
	dma_addr_t next;	/* A pointer to the next module, this must be read with iop_read(). */
	dma_addr_t name;	/* A pointer to the name in IOP RAM, this must be read with iop_read().  */
	uint16_t version;
	uint16_t newflags;
	uint16_t id;
	uint16_t flags;
	uint32_t entry;
	uint32_t gp;
	uint32_t text_start;
	uint32_t text_size;
	uint32_t data_size;
	uint32_t bss_size;
	uint32_t unused1;
	uint32_t unused2;
} smod_mod_info_t;

struct mod_attr {
	struct kobject mod_kobj;
	smod_mod_info_t mod;
	char name[40];
};

static void update_iopmodules(struct kobject *kobj, int moduleid);

static int mod_args_len;
static char *mod_args;
static struct kobject *iopmodules_kobj;
static struct device *iopmodules_device;

static ssize_t name_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "%s\n", pmod_attr->name);
}

static ssize_t id_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "%u\n", pmod_attr->mod.id);
}

static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "%u.%02u\n", (pmod_attr->mod.version >> 8) & 0xFF, pmod_attr->mod.version & 0xFF);
}

static ssize_t flags_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "0x%08x\n", pmod_attr->mod.flags);
}

static ssize_t newflags_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "0x%08x\n", pmod_attr->mod.newflags);
}

static ssize_t entry_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "0x%08x\n", pmod_attr->mod.entry);
}

static ssize_t gp_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "0x%08x\n", pmod_attr->mod.gp);
}

static ssize_t text_start_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "0x%08x\n", pmod_attr->mod.text_start);
}

static ssize_t text_size_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "%u\n", pmod_attr->mod.text_size);
}

static ssize_t data_size_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "%u\n", pmod_attr->mod.data_size);
}

static ssize_t bss_size_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	return sprintf(buf, "%u\n", pmod_attr->mod.bss_size);
}

static struct kobj_attribute iopmodules_attr_name =
	__ATTR(name, 0644, name_show, NULL);

static struct kobj_attribute iopmodules_attr_id =
	__ATTR(id, 0644, id_show, NULL);

static struct kobj_attribute iopmodules_attr_version =
	__ATTR(version, 0644, version_show, NULL);

static struct kobj_attribute iopmodules_attr_flags =
	__ATTR(flags, 0644, flags_show, NULL);

static struct kobj_attribute iopmodules_attr_newflags =
	__ATTR(newflags, 0644, newflags_show, NULL);

static struct kobj_attribute iopmodules_attr_entry =
	__ATTR(entry, 0644, entry_show, NULL);

static struct kobj_attribute iopmodules_attr_gp =
	__ATTR(gp, 0644, gp_show, NULL);

static struct kobj_attribute iopmodules_attr_text_start =
	__ATTR(text_start, 0644, text_start_show, NULL);

static struct kobj_attribute iopmodules_attr_text_size =
	__ATTR(text_size, 0644, text_size_show, NULL);

static struct kobj_attribute iopmodules_attr_data_size =
	__ATTR(data_size, 0644, data_size_show, NULL);

static struct kobj_attribute iopmodules_attr_bss_size =
	__ATTR(bss_size, 0644, bss_size_show, NULL);


static struct attribute *iopmodules_subsys_attrs[] = {
	&iopmodules_attr_name.attr,
	&iopmodules_attr_id.attr,
	&iopmodules_attr_version.attr,
	&iopmodules_attr_flags.attr,
	&iopmodules_attr_newflags.attr,
	&iopmodules_attr_entry.attr,
	&iopmodules_attr_gp.attr,
	&iopmodules_attr_text_start.attr,
	&iopmodules_attr_text_size.attr,
	&iopmodules_attr_data_size.attr,
	&iopmodules_attr_bss_size.attr,
	NULL,	/* maybe more in the future? */
};

static struct attribute_group iopmodules_subsys_attr_group = {
	.attrs = iopmodules_subsys_attrs,
};

static ssize_t load_module(const char *path, size_t count)
{
	int rv;

	/* Load IRX module on IOP from IOP file system. */
	rv = ps2_load_module(path, count, mod_args, mod_args_len, NULL);
	if (mod_args != NULL) {
		vfree(mod_args);
		mod_args_len = 0;
	}
	if (rv >= 0) {
		/* Add loaded IOP module to sysfs. */
		update_iopmodules(iopmodules_kobj, rv);

		rv = count;
	}

	return rv;
}

ssize_t load_module_firmware(const char *path, size_t count)
{
	int rv;
	const struct firmware *fw;

	/* request the firmware, this will block until someone uploads it */
	rv = request_firmware(&fw, path, iopmodules_device);
	if (rv) {
		if (rv == -ENOENT) {
			printk(KERN_ERR "Firmware '%s' not found.\n", path);
		}
		return rv;
	}

	/* Load IRX module on IOP from buffer. */
	rv = ps2_load_module_buffer(fw->data, fw->size, mod_args, mod_args_len, NULL);
	if (mod_args != NULL) {
		vfree(mod_args);
		mod_args_len = 0;
	}
	release_firmware(fw);
	if (rv >= 0) {
		/* Add loaded IOP module to sysfs. */
		update_iopmodules(iopmodules_kobj, rv);

		rv = count;
	}

	return rv;
}
EXPORT_SYMBOL(load_module_firmware);

static ssize_t load_module_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	char *path;
	int pathContainsColon = 0;
	ssize_t rv;
	int n;

	path = vmalloc(count + 1);
	if (path == NULL) {
		return -ENOMEM;
	}

	for (n = 0; n < count; n++) {
		switch (buf[n]) {
			case '\n':
				path[n] = 0;
				break;
			case ':':
				/* It contains a colon, this must be a
				 * path to rom0: or rom1:
				 */
				pathContainsColon = 1;
			default:
				path[n] = buf[n];
				break;
		}
	}
	path[count] = 0;

	if (pathContainsColon) {
		/* Load a module from "rom0:", "rom1:" or "host:". */
		rv = load_module(path, count);
	} else {
		/* Load module from /lib/firmware via hotplug. */
		rv = load_module_firmware(path, count);
	}

	vfree(path);
	path = NULL;
	return rv;
}


static ssize_t args_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	if (mod_args != NULL) {
		int pos;

		for (pos = 0; pos < mod_args_len; pos++) {
			/* Convert 0 to '\n'. */
			if (mod_args[pos] != 0) {
				buf[pos] = mod_args[pos];
			} else {
				buf[pos] = '\n';
			}
		}
		return mod_args_len;
	} else {
		return 0;
	}
}

static ssize_t args_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rv = -ENOMEM;

	if (mod_args != NULL) {
		vfree(mod_args);
		mod_args_len = 0;
	}
	mod_args = vmalloc(count);
	if (mod_args != NULL) {
		int pos;

		mod_args_len = count;

		for (pos = 0; pos < mod_args_len; pos++) {
			/* Convert '\n' to 0. */
			if (buf[pos] != '\n') {
				mod_args[pos] = buf[pos];
			} else {
				mod_args[pos] = 0;
			}
		}
		rv = mod_args_len;
	}

	return rv;
}

static struct kobj_attribute iopmodules_attr_load_module =
	__ATTR(load_module, 0644, NULL, load_module_store);

static struct kobj_attribute iopmodules_attr_args =
	__ATTR(args, 0644, args_show, args_store);

static struct attribute *iopmodules_attrs[] = {
	&iopmodules_attr_load_module.attr,
	&iopmodules_attr_args.attr,
	NULL,	/* maybe more in the future? */
};

static struct attribute_group iopmodules_attr_group = {
	.attrs = iopmodules_attrs,
};

/** Read iop memory address. */
static unsigned int iop_read(dma_addr_t addr, void *buf, unsigned int size)
{
	void *p;

	p = phys_to_virt(ps2sif_bustophys(addr));
	memcpy(buf, p, size);

	return size;
}

/**
 * smod_get_next_mod - Return the next module referenced in the global module list.
 */
static int smod_get_next_mod(smod_mod_info_t *cur_mod, smod_mod_info_t *next_mod)
{
	dma_addr_t addr;

	/* If cur_mod is 0, return the head of the list (IOP address 0x800).  */
	if (!cur_mod) {
		addr = 0x800;
	} else {
		if (!cur_mod->next)
			return 0;
		else
			addr = cur_mod->next;
	}

	iop_read(addr, next_mod, sizeof(smod_mod_info_t));
	return next_mod->id;
}

static void iopmodules_kobj_release(struct kobject *kobj)
{
	struct mod_attr *pmod_attr;

	pmod_attr = container_of(kobj, struct mod_attr, mod_kobj);

	kfree(pmod_attr);
}

static struct kobj_type iopmodules_kobj_ktype = {
	.release	= iopmodules_kobj_release,
	.sysfs_ops	= &kobj_sysfs_ops,
};

static void update_iopmodules(struct kobject *kobj, int moduleid)
{
	smod_mod_info_t *cur;
	smod_mod_info_t module;
	int i;

	cur = NULL;
	i = 0;
	while (smod_get_next_mod(cur, &module) != 0)
	{
		if ((moduleid < 0) || (moduleid == module.id)) {
			struct mod_attr *pmod_attr;

			pmod_attr = kzalloc(sizeof(struct mod_attr), GFP_KERNEL);
			if (pmod_attr != NULL) {
				kobject_init(&pmod_attr->mod_kobj, &iopmodules_kobj_ktype);
				memcpy(&pmod_attr->mod, &module, sizeof(module));

				iop_read(module.name, pmod_attr->name, sizeof(pmod_attr->name));
				pmod_attr->name[sizeof(pmod_attr->name) - 1] = 0;

				if (kobject_add(&pmod_attr->mod_kobj, kobj, "%d", module.id) == 0) {
					int err;

					/* TBD: Cleanup allocated memory somewhere. */
					err = sysfs_create_group(&pmod_attr->mod_kobj, &iopmodules_subsys_attr_group);
					if (err != 0) {
						printk(KERN_WARNING "%s: sysfs_create_group error %d\n", __func__, err);
					}
				} else {
					kfree(pmod_attr);
				}
			}
		}
		cur = &module;
		i++;
	}
}


static int __init iopmodules_init(void)
{
	int result = 0;
	int err;

	mod_args = NULL;
	mod_args_len = 0;

	iopmodules_device = root_device_register("iopmodules");
	if (iopmodules_device == NULL) {
		printk(KERN_ERR "%s: Failed to register iopmodules root device.\n", __func__);
		return -ENOMEM;
	}

	/* Add entry at /sys/firmware/iopmodules. */
	iopmodules_kobj = kobject_create_and_add("iopmodules", firmware_kobj);
	if (!iopmodules_kobj) {
		printk(KERN_WARNING "%s: kobject_create_and_add error\n", __func__);
		iopmodules_kobj = NULL;
	}
	/* Add all loaded IOP modules to sysfs. */
	update_iopmodules(iopmodules_kobj, -1);

	/* Add entry for loading modules. */
	err = sysfs_create_group(iopmodules_kobj, &iopmodules_attr_group);
	if (err != 0) {
		printk(KERN_WARNING "%s: sysfs_create_group error %d\n", __func__, err);
	}

	return result;
}

subsys_initcall(iopmodules_init);
