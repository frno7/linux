// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 privileged Graphics Synthesizer (GS) registers
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/build_bug.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/io.h>

#include <asm/mach-ps2/gs-registers.h>

/* Shadow write-only privileged Graphics Synthesizer registers. */
static struct {
	spinlock_t lock;	/* Must be taken to access shadow registers. */

	struct {
		u64 value;
		bool valid;	/* True if value is written at least once. */
	} pmode,
	  smode1,
	  smode2,
	  srfsh,
	  synch1,
	  synch2,
	  syncv,
	  dispfb1,
	  display1,
	  dispfb2,
	  display2,
	  extbuf,
	  extdata,
	  extwrite,
	  bgcolor,
	  imr,
	  busdir;
} gs_registers = {
	.lock = __SPIN_LOCK_UNLOCKED(gs_registers.lock),
};

/* Read-write registers are not shadowed and are always valid. */
#define GS_DEFINE_VALID_RW_REG(reg, addr)				\
	bool gs_valid_##reg(void)					\
	{								\
		return true;						\
	}								\
	EXPORT_SYMBOL_GPL(gs_valid_##reg)

/* Read-write registers are not shadowed and trivially read. */
#define GS_DEFINE_RQ_RW_REG(reg, addr)					\
	u64 gs_readq_##reg(void)					\
	{								\
		return inq(addr);					\
	}								\
	EXPORT_SYMBOL_GPL(gs_readq_##reg)

/* Read-write registers are not shadowed and trivially write. */
#define GS_DEFINE_WQ_RW_REG(reg, addr)					\
	void gs_writeq_##reg(u64 value)					\
	{								\
		outq(value, addr);					\
	}								\
	EXPORT_SYMBOL_GPL(gs_writeq_##reg)

/* Write-only registers are shadowed and valid only if previously written. */
#define GS_DEFINE_VALID_WO_REG(reg, addr)				\
	bool gs_valid_##reg(void)					\
	{								\
		unsigned long flags;					\
		bool valid;						\
		spin_lock_irqsave(&gs_registers.lock, flags);		\
		valid = gs_registers.reg.valid;				\
		spin_unlock_irqrestore(&gs_registers.lock, flags);	\
		return valid;						\
	}								\
	EXPORT_SYMBOL_GPL(gs_valid_##reg)

/* Write-only registers are shadowed and reading requires a previous write. */
#define GS_DEFINE_RQ_WO_REG(reg, addr)					\
	u64 gs_readq_##reg(void)					\
	{								\
		unsigned long flags;					\
		u64 value;						\
		spin_lock_irqsave(&gs_registers.lock, flags);		\
		WARN_ON_ONCE(!gs_registers.reg.valid);			\
		value = gs_registers.reg.value;				\
		spin_unlock_irqrestore(&gs_registers.lock, flags);	\
		return value;						\
	}								\
	EXPORT_SYMBOL_GPL(gs_readq_##reg)

/* Write-only registers are shadowed and reading requires a previous write. */
#define GS_DEFINE_WQ_WO_REG(reg, addr)					\
	void gs_writeq_##reg(u64 value)					\
	{								\
		unsigned long flags;					\
		spin_lock_irqsave(&gs_registers.lock, flags);		\
		gs_registers.reg.value = value;				\
		gs_registers.reg.valid = true;				\
		outq(value, addr);					\
		spin_unlock_irqrestore(&gs_registers.lock, flags);	\
	}								\
	EXPORT_SYMBOL_GPL(gs_writeq_##reg)

/* Read registers as structures, which simplifies notation. */
#define GS_DEFINE_RS_REG(reg, str)					\
	struct gs_##str gs_read_##reg(void)				\
	{								\
		const u64 v = gs_readq_##reg();				\
		struct gs_##str value;					\
		BUILD_BUG_ON(sizeof(v) != sizeof(value));		\
		memcpy(&value, &v, sizeof(v));				\
		return value;						\
	}								\
	EXPORT_SYMBOL_GPL(gs_read_##reg)

/* Write registers as structures, which simplifies notation. */
#define GS_DEFINE_WS_REG(reg, str)					\
	void gs_write_##reg(struct gs_##str value)			\
	{								\
		u64 v;							\
		BUILD_BUG_ON(sizeof(v) != sizeof(value));		\
		memcpy(&v, &value, sizeof(v));				\
		gs_writeq_##reg(v);					\
	}								\
	EXPORT_SYMBOL_GPL(gs_write_##reg)

/* Only CSR and SIGLBLID are read-write (RW) with hardware. */
#define GS_DEFINE_RW_REG(reg, str, addr)				\
	GS_DEFINE_VALID_RW_REG(reg, addr);				\
	GS_DEFINE_RQ_RW_REG(reg, addr);					\
	GS_DEFINE_WQ_RW_REG(reg, addr);					\
	GS_DEFINE_RS_REG(reg, str);					\
	GS_DEFINE_WS_REG(reg, str)

/* The rest are write-only (WO) with reading emulated by shadow registers. */
#define GS_DEFINE_WO_REG(reg, str, addr)				\
	GS_DEFINE_VALID_WO_REG(reg, addr);				\
	GS_DEFINE_RQ_WO_REG(reg, addr);					\
	GS_DEFINE_WQ_WO_REG(reg, addr);					\
	GS_DEFINE_RS_REG(reg, str);					\
	GS_DEFINE_WS_REG(reg, str)

GS_DEFINE_WO_REG(pmode,    pmode,    GS_PMODE);
GS_DEFINE_WO_REG(smode1,   smode1,   GS_SMODE1);
GS_DEFINE_WO_REG(smode2,   smode2,   GS_SMODE2);
GS_DEFINE_WO_REG(srfsh,    srfsh,    GS_SRFSH);
GS_DEFINE_WO_REG(synch1,   synch1,   GS_SYNCH1);
GS_DEFINE_WO_REG(synch2,   synch2,   GS_SYNCH2);
GS_DEFINE_WO_REG(syncv,    syncv,    GS_SYNCV);
GS_DEFINE_WO_REG(dispfb1,  dispfb,   GS_DISPFB1);
GS_DEFINE_WO_REG(display1, display,  GS_DISPLAY1);
GS_DEFINE_WO_REG(dispfb2,  dispfb,   GS_DISPFB2);
GS_DEFINE_WO_REG(display2, display,  GS_DISPLAY2);
GS_DEFINE_WO_REG(extbuf,   extbuf,   GS_EXTBUF);
GS_DEFINE_WO_REG(extdata,  extdata,  GS_EXTDATA);
GS_DEFINE_WO_REG(extwrite, extwrite, GS_EXTWRITE);
GS_DEFINE_WO_REG(bgcolor,  bgcolor,  GS_BGCOLOR);
GS_DEFINE_RW_REG(csr,      csr,      GS_CSR);		/* Read-write */
GS_DEFINE_WO_REG(imr,      imr,      GS_IMR);
GS_DEFINE_WO_REG(busdir,   busdir,   GS_BUSDIR);
GS_DEFINE_RW_REG(siglblid, siglblid, GS_SIGLBLID);	/* Read-write */

u64 gs_xorq_imr(u64 value)
{
	unsigned long flags;

	spin_lock_irqsave(&gs_registers.lock, flags);

	WARN_ON_ONCE(!gs_registers.imr.valid);
	value = value ^ gs_registers.imr.value;
	gs_registers.imr.value = value;
	outq(value, GS_IMR);

	spin_unlock_irqrestore(&gs_registers.lock, flags);

	return value;
}
EXPORT_SYMBOL_GPL(gs_xorq_imr);

void gs_write_csr_flush(void)
{
	GS_WRITE_CSR( .flush = 1 );

	udelay(2500);		/* 2.5 ms hardware delay */
}
EXPORT_SYMBOL_GPL(gs_write_csr_flush);

void gs_write_csr_reset(void)
{
	GS_WRITE_CSR( .reset = 1 );

	udelay(2500);		/* 2.5 ms hardware delay */
}
EXPORT_SYMBOL_GPL(gs_write_csr_reset);

MODULE_DESCRIPTION("PlayStation 2 privileged Graphics Synthesizer registers");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
