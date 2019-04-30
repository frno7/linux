// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer (GS) sysfs driver
 *
 * Copyright (C) 2019 Fredrik Noring
 *
 * Reading arbitrary numerical and symbolical privileged GS register
 * bit fields is supported. For example, the PMODE register can be
 * inspected with:
 *
 *	# cat /sys/devices/platform/gs/registers/pmode
 *	en1 1
 *	en2 0
 *	crtmd 1
 *	mmod circuit1
 *	amod circuit1
 *	slbg circuit2
 *	alp 0
 *
 * The implementation uses a fair amount of macro expansions. This greatly
 * simplifies register definitions, which in the case of PMODE is:
 *
 *	SYSFS_RW_REG(pmode, pmode,
 *		SYSFS_DECNUM_FIELD(en1),
 *		SYSFS_DECNUM_FIELD(en2),
 *		SYSFS_DECNUM_FIELD(crtmd),
 *		SYSFS_SYMBOL_FIELD(mmod, circuit1, alp),
 *		SYSFS_SYMBOL_FIELD(amod, circuit1, circuit2),
 *		SYSFS_SYMBOL_FIELD(slbg, circuit2, bgcolor),
 *		SYSFS_DECNUM_FIELD(alp));
 *
 * It relies on the following enum and struct definitions:
 *
 *	enum gs_pmode_mmod { gs_mmod_circuit1, gs_mmod_alp };
 *	enum gs_pmode_amod { gs_amod_circuit1, gs_amod_circuit2 };
 *	enum gs_pmode_slbg { gs_slbg_circuit2, gs_slbg_bgcolor };
 *	struct gs_pmode {
 *		u64 en1 : 1;		// Enable read circuit 1
 *		u64 en2 : 1;		// Enable read circuit 2
 *		u64 crtmd : 3;		// CRT output switching (always 001)
 *		u64 mmod : 1;		// Alpha blending value
 *		u64 amod : 1;		// OUT1 alpha output
 *		u64 slbg : 1;		// Alpha blending method
 *		u64 alp : 8;		// Fixed alpha (0xff = 1.0)
 *		u64 zero : 1;		// Must be zero
 *		u64 : 47;
 *	};
 */

#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <asm/page.h>
#include <asm/uaccess.h>

#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gs.h>

static struct kobject *registers_kobj;

#define SYSFS_STATEMENT1(prefix_, macro_)				\
	prefix_##macro_;

#define SYSFS_STATEMENT2(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT1(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT3(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT2(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT4(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT3(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT5(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT4(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT6(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT5(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT7(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT6(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT8(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT7(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT9(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT8(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT10(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT9(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT11(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT10(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT12(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT11(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT13(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT12(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT14(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT13(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT15(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT14(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT16(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT15(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT17(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT16(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT18(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT17(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT19(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT18(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT20(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT19(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT21(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT20(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT22(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT21(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT23(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT22(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT24(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT23(prefix_, __VA_ARGS__)

#define SYSFS_STATEMENT25(prefix_, macro_, ...)				\
	prefix_##macro_;						\
	SYSFS_STATEMENT24(prefix_, __VA_ARGS__)

#define SHOW_SYSFS_DECNUM_FIELD(field_, ...)				\
	do {								\
		if (valid)						\
			n += scnprintf(&buf[n], PAGE_SIZE - n,		\
				#field_ " %u\n", value.field_);		\
		else							\
			n += scnprintf(&buf[n], PAGE_SIZE - n,		\
				#field_ "\n");				\
	} while (false)

#define SHOW_SYSFS_HEXNUM_FIELD(field_, ...)				\
	do {								\
		if (valid)						\
			n += scnprintf(&buf[n], PAGE_SIZE - n,		\
				#field_ " 0x%x\n", value.field_);	\
		else							\
			n += scnprintf(&buf[n], PAGE_SIZE - n,		\
				#field_ "\n");				\
	} while (false)

#define SHOW_SYSFS_SYMBOL_STRING_ENTRY(field_, prefix_, value_)		\
	value.field_ == prefix_##_##value_ ? " " #value_ :

#define SHOW_SYSFS_SYMBOL_STRING1(field_, prefix_, value_)		\
	SHOW_SYSFS_SYMBOL_STRING_ENTRY(field_, prefix_, value_) "-"

#define SHOW_SYSFS_SYMBOL_STRING2(field_, prefix_, value_, ...)		\
	SHOW_SYSFS_SYMBOL_STRING_ENTRY(field_, prefix_, value_)		\
	SHOW_SYSFS_SYMBOL_STRING1(field_, prefix_, __VA_ARGS__)

#define SHOW_SYSFS_SYMBOL_STRING3(field_, prefix_, value_, ...)		\
	SHOW_SYSFS_SYMBOL_STRING_ENTRY(field_, prefix_, value_)		\
	SHOW_SYSFS_SYMBOL_STRING2(field_, prefix_, __VA_ARGS__)

#define SHOW_SYSFS_SYMBOL_STRING4(field_, prefix_, value_, ...)		\
	SHOW_SYSFS_SYMBOL_STRING_ENTRY(field_, prefix_, value_)		\
	SHOW_SYSFS_SYMBOL_STRING3(field_, prefix_, __VA_ARGS__)

#define SHOW_SYSFS_SYMBOL_STRING5(field_, prefix_, value_, ...)		\
	SHOW_SYSFS_SYMBOL_STRING_ENTRY(field_, prefix_, value_)		\
	SHOW_SYSFS_SYMBOL_STRING4(field_, prefix_, __VA_ARGS__)

#define SHOW_SYSFS_SYMBOL_STRING(field_, prefix_, ...)			\
	!valid ? "" :							\
	CONCATENATE(SHOW_SYSFS_SYMBOL_STRING,				\
		COUNT_ARGS(__VA_ARGS__))(field_, prefix_, __VA_ARGS__)

#define SHOW_SYSFS_SYMBOL_FIELD(field_, ...)			\
	n += scnprintf(&buf[n], PAGE_SIZE - n, #field_ "%s\n",		\
		SHOW_SYSFS_SYMBOL_STRING(field_, gs_##field_, __VA_ARGS__))

#define SHOW_SYSFS_REGISTER(reg, str, ...)				\
	static ssize_t show_##reg(struct device *device,		\
		struct device_attribute *attr, char *buf)		\
	{								\
		const bool valid = gs_valid_##reg();			\
		const struct gs_##str value = valid ?			\
			gs_read_##reg() : (struct gs_##str) { };	\
		size_t n = 0;						\
		if (valid)						\
			n += scnprintf(&buf[n], PAGE_SIZE - n,		\
				"# 0x%llx\n", gs_readq_##reg());	\
		__VA_ARGS__;						\
		return n;						\
	}

#define SYSFS_SHOW(reg, str, ...)					\
	SHOW_SYSFS_REGISTER(reg, str,					\
		CONCATENATE(SYSFS_STATEMENT,				\
			COUNT_ARGS(__VA_ARGS__))(SHOW_, __VA_ARGS__))

#define SYSFS_RO_REG(reg, str, ...)					\
	SYSFS_SHOW(reg, str, __VA_ARGS__)				\
	static DEVICE_ATTR(reg, S_IRUGO, show_##reg, NULL)

#define SYSFS_RW_REG(reg, str, ...)					\
	SYSFS_SHOW(reg, str, __VA_ARGS__)				\
	static DEVICE_ATTR(reg, S_IRUGO, show_##reg, NULL)

SYSFS_RW_REG(pmode, pmode,
	SYSFS_DECNUM_FIELD(en1),
	SYSFS_DECNUM_FIELD(en2),
	SYSFS_DECNUM_FIELD(crtmd),
	SYSFS_SYMBOL_FIELD(mmod, circuit1, alp),
	SYSFS_SYMBOL_FIELD(amod, circuit1, circuit2),
	SYSFS_SYMBOL_FIELD(slbg, circuit2, bgcolor),
	SYSFS_DECNUM_FIELD(alp));

SYSFS_RW_REG(smode1, smode1,
	SYSFS_DECNUM_FIELD(rc),
	SYSFS_DECNUM_FIELD(lc),
	SYSFS_DECNUM_FIELD(t1248),
	SYSFS_DECNUM_FIELD(slck),
	SYSFS_SYMBOL_FIELD(cmod, vesa, ntsc, pal),
	SYSFS_DECNUM_FIELD(ex),
	SYSFS_DECNUM_FIELD(prst),
	SYSFS_DECNUM_FIELD(sint),
	SYSFS_DECNUM_FIELD(xpck),
	SYSFS_DECNUM_FIELD(pck2),
	SYSFS_DECNUM_FIELD(spml),
	SYSFS_SYMBOL_FIELD(gcont, rgbyc, ycrcb),
	SYSFS_DECNUM_FIELD(phs),
	SYSFS_DECNUM_FIELD(pvs),
	SYSFS_DECNUM_FIELD(pehs),
	SYSFS_DECNUM_FIELD(pevs),
	SYSFS_DECNUM_FIELD(clksel),
	SYSFS_DECNUM_FIELD(nvck),
	SYSFS_DECNUM_FIELD(slck2),
	SYSFS_DECNUM_FIELD(vcksel),
	SYSFS_DECNUM_FIELD(vhp));

SYSFS_RW_REG(smode2, smode2,
	SYSFS_SYMBOL_FIELD(intm, progressive, interlace),
	SYSFS_SYMBOL_FIELD(ffmd, field, frame),
	SYSFS_SYMBOL_FIELD(dpms, on, standby, suspend, off));

SYSFS_RW_REG(srfsh, srfsh,
	SYSFS_DECNUM_FIELD(rfsh));

SYSFS_RW_REG(synch1, synch1,
	SYSFS_DECNUM_FIELD(hfp),
	SYSFS_DECNUM_FIELD(hbp),
	SYSFS_DECNUM_FIELD(hseq),
	SYSFS_DECNUM_FIELD(hsvs),
	SYSFS_DECNUM_FIELD(hs));

SYSFS_RW_REG(synch2, synch2,
	SYSFS_DECNUM_FIELD(hf),
	SYSFS_DECNUM_FIELD(hb));

SYSFS_RW_REG(syncv, syncv,
	SYSFS_DECNUM_FIELD(vfp),
	SYSFS_DECNUM_FIELD(vfpe),
	SYSFS_DECNUM_FIELD(vbp),
	SYSFS_DECNUM_FIELD(vbpe),
	SYSFS_DECNUM_FIELD(vdp),
	SYSFS_DECNUM_FIELD(vs));

SYSFS_RW_REG(dispfb1, dispfb,
	SYSFS_DECNUM_FIELD(fbp),
	SYSFS_DECNUM_FIELD(fbw),
	SYSFS_SYMBOL_FIELD(psm, ct32, ct24, ct16, ct16s, gpu24),
	SYSFS_DECNUM_FIELD(dbx),
	SYSFS_DECNUM_FIELD(dby));

SYSFS_RW_REG(display1, display,
	SYSFS_DECNUM_FIELD(dx),
	SYSFS_DECNUM_FIELD(dy),
	SYSFS_DECNUM_FIELD(magh),
	SYSFS_DECNUM_FIELD(magv),
	SYSFS_DECNUM_FIELD(dw),
	SYSFS_DECNUM_FIELD(dh));

SYSFS_RW_REG(dispfb2, dispfb,
	SYSFS_DECNUM_FIELD(fbp),
	SYSFS_DECNUM_FIELD(fbw),
	SYSFS_SYMBOL_FIELD(psm, ct32, ct24, ct16, ct16s),
	SYSFS_DECNUM_FIELD(dbx),
	SYSFS_DECNUM_FIELD(dby));

SYSFS_RW_REG(display2, display,
	SYSFS_DECNUM_FIELD(dx),
	SYSFS_DECNUM_FIELD(dy),
	SYSFS_DECNUM_FIELD(magh),
	SYSFS_DECNUM_FIELD(magv),
	SYSFS_DECNUM_FIELD(dw),
	SYSFS_DECNUM_FIELD(dh));

SYSFS_RW_REG(extbuf, extbuf,
	SYSFS_DECNUM_FIELD(exbp),
	SYSFS_DECNUM_FIELD(exbw),
	SYSFS_SYMBOL_FIELD(fbin, out1, out2),
	SYSFS_SYMBOL_FIELD(wffmd, field, frame),
	SYSFS_SYMBOL_FIELD(emoda, alpha, y, yhalf, zero),
	SYSFS_SYMBOL_FIELD(emodc, rgb, y, ycbcr, alpha),
	SYSFS_DECNUM_FIELD(wdx),
	SYSFS_DECNUM_FIELD(wdy));

SYSFS_RW_REG(extdata, extdata,
	SYSFS_DECNUM_FIELD(sx),
	SYSFS_DECNUM_FIELD(sy),
	SYSFS_DECNUM_FIELD(smph),
	SYSFS_DECNUM_FIELD(smpv),
	SYSFS_DECNUM_FIELD(ww),
	SYSFS_DECNUM_FIELD(wh));

SYSFS_RW_REG(extwrite, extwrite,
	SYSFS_SYMBOL_FIELD(write, complete_current, start_next));

SYSFS_RW_REG(bgcolor, bgcolor,
	SYSFS_DECNUM_FIELD(r),
	SYSFS_DECNUM_FIELD(g),
	SYSFS_DECNUM_FIELD(b));

SYSFS_RO_REG(csr, csr,
	SYSFS_DECNUM_FIELD(signal),
	SYSFS_DECNUM_FIELD(finish),
	SYSFS_DECNUM_FIELD(hsint),
	SYSFS_DECNUM_FIELD(vsint),
	SYSFS_DECNUM_FIELD(edwint),
	SYSFS_DECNUM_FIELD(flush),
	SYSFS_DECNUM_FIELD(reset),
	SYSFS_DECNUM_FIELD(nfield),
	SYSFS_SYMBOL_FIELD(field, even, odd),
	SYSFS_SYMBOL_FIELD(fifo, neither, empty, almost_full),
	SYSFS_HEXNUM_FIELD(rev),
	SYSFS_HEXNUM_FIELD(id));

SYSFS_RO_REG(imr, imr,
	SYSFS_DECNUM_FIELD(sigmsk),
	SYSFS_DECNUM_FIELD(finishmsk),
	SYSFS_DECNUM_FIELD(hsmsk),
	SYSFS_DECNUM_FIELD(vsmsk),
	SYSFS_DECNUM_FIELD(edwmsk),
	SYSFS_DECNUM_FIELD(ones));

SYSFS_RO_REG(busdir, busdir,
	SYSFS_SYMBOL_FIELD(dir, host_to_local, local_to_host));

SYSFS_RW_REG(siglblid, siglblid,
	SYSFS_DECNUM_FIELD(sigid),
	SYSFS_DECNUM_FIELD(lblid));

static struct attribute *gs_registers_attributes[] = {
	&dev_attr_pmode.attr,
	&dev_attr_smode1.attr,
	&dev_attr_smode2.attr,
	&dev_attr_srfsh.attr,
	&dev_attr_synch1.attr,
	&dev_attr_synch2.attr,
	&dev_attr_syncv.attr,
	&dev_attr_dispfb1.attr,
	&dev_attr_display1.attr,
	&dev_attr_dispfb2.attr,
	&dev_attr_display2.attr,
	&dev_attr_extbuf.attr,
	&dev_attr_extdata.attr,
	&dev_attr_extwrite.attr,
	&dev_attr_bgcolor.attr,
	&dev_attr_csr.attr,		/* FIXME: Too lowlevel for sysfs? */
	&dev_attr_imr.attr,		/* FIXME: Too lowlevel for sysfs? */
	&dev_attr_busdir.attr,		/* FIXME: Too lowlevel for sysfs? */
	&dev_attr_siglblid.attr,	/* FIXME: Too lowlevel for sysfs? */
	NULL
};

static struct attribute_group gs_registers_attribute_group = {
	.attrs = gs_registers_attributes
};

static int __init gs_sysfs_init(void)
{
	struct device *gs_dev = gs_device();	/* FIXME: Is this method appropriate? */
	int err;

	if (!gs_dev) {
		pr_err("gs-sysfs: Failed to retrieve gs device\n");
		err = -ENXIO;
		goto gs_dev_err;
	}

	registers_kobj = kobject_create_and_add("registers", &gs_dev->kobj);
	if (!registers_kobj) {
		pr_err("gs-sysfs: Failed to create and add register kernel object\n");
		err = -ENOMEM;
		goto kobj_err;
	}

	err = sysfs_create_group(registers_kobj, &gs_registers_attribute_group);
	if (err) {
		pr_err("gs-sysfs: Failed to create register sysfs group\n");
		goto group_err;
	}

	return 0;

group_err:
	kobject_del(registers_kobj);

kobj_err:
gs_dev_err:
	return err;
}

static void __exit gs_sysfs_exit(void)
{
	kobject_del(registers_kobj);
}

module_init(gs_sysfs_init);
module_exit(gs_sysfs_exit);

MODULE_DESCRIPTION("PlayStation 2 Graphics Synthesizer sysfs driver");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
