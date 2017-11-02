// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer (GS)
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 *
 * The Graphics Synthesizer draws primitives such as triangles and sprites
 * to its 4 MiB local frame buffer. It can handle shading, texture mapping,
 * z-buffering, alpha blending, edge antialiasing, fogging, scissoring, etc.
 *
 * PAL, NTSC and VESA video modes are supported. Interlace and noninterlaced
 * can be switched. The resolution is variable from 256 x 224 to 1920 x 1080.
 *
 * FIXME: Expand all bitfields with proper sysfs names
 */

#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/freezer.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/kd.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ps2/dev.h>
#include <linux/ps2/gs.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/vt_buffer.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/page.h>
#include <asm/uaccess.h>

#include <asm/mach-ps2/dma.h>
#include <asm/mach-ps2/eedev.h>
#include <asm/mach-ps2/ps2.h>

#include <uapi/asm/gs.h>
#include <uapi/asm/gif.h>

#define DEVICE_NAME "gs"

/* Write only Graphics Synthesizer registers. */
static struct {
	spinlock_t lock;

	u64 pmode;	/* PCRTC modes */
	u64 smode1;	/* Sync */
	u64 smode2;	/* Sync */
	u64 srfsh;	/* DRAM refresh */
	u64 synch1;	/* Sync */
	u64 synch2;	/* Sync */
	u64 syncv;	/* Sync */
	u64 dispfb1;	/* Display buffer rectangular area 1 */
	u64 display1;	/* Rectangular area 1 display position */
	u64 dispfb2;	/* Display buffer rectangular area 2 */
	u64 display2;	/* Rectangular area 2 display position */
	u64 extbuf;	/* Rectangular area write buffer */
	u64 extdata;	/* Rectangular area write data */
	u64 extwrite;	/* Rectangular area write start */
	u64 bgcolor;	/* Background color */
	u64 imr;	/* Interrupt mask FIXME: irq.c */
	u64 busdir;	/* Host interface switching */
	u64 syscnt;
} gs_registers;	/* FIXME: Device private data */

#define D2_CHCR		0x1000a000	/* Channel 2 control */
#define D2_MADR		0x1000a010	/* Channel 2 memory address */
#define D2_QWC		0x1000a020	/* Channel 2 quad word count */

static void *gif_buffer;	/* FIXME: Device private data */
static void *dmac_buffer;	/* FIXME: Device private data */

static struct kobject *registers_kobj;	/* FIXME: Private device pointer */

#define DEFINE_READQ_RW_REG(reg, addr) \
	u64 gs_readq_##reg(void) \
	{ \
		return inq(addr); \
	} \
	EXPORT_SYMBOL(gs_readq_##reg)

#define DEFINE_WRITEQ_RW_REG(reg, addr) \
	void gs_writeq_##reg(u64 value) \
	{ \
		outq(value, addr); \
	} \
	EXPORT_SYMBOL(gs_writeq_##reg)

#define DEFINE_READQ_WO_REG(reg, addr) \
	u64 gs_readq_##reg(void) \
	{ \
		unsigned long flags; \
		u64 value; \
		spin_lock_irqsave(&gs_registers.lock, flags); \
		value = gs_registers.reg; \
		spin_unlock_irqrestore(&gs_registers.lock, flags); \
		return value; \
	} \
	EXPORT_SYMBOL(gs_readq_##reg)

#define DEFINE_WRITEQ_WO_REG(reg, addr) \
	void gs_writeq_##reg(u64 value) \
	{ \
		unsigned long flags; \
		spin_lock_irqsave(&gs_registers.lock, flags); \
		gs_registers.reg = value; \
		outq(value, addr); \
		spin_unlock_irqrestore(&gs_registers.lock, flags); \
	} \
	EXPORT_SYMBOL(gs_writeq_##reg)

#define DEFINE_READ_REG(reg, str) \
	struct gs_##str gs_read_##reg(void) \
	{ \
		const u64 v = gs_readq_##reg(); \
		struct gs_##str value; \
		memcpy(&value, &v, sizeof(v)); \
		return value; \
	} \
	EXPORT_SYMBOL(gs_read_##reg)

#define DEFINE_WRITE_REG(reg, str) \
	void gs_write_##reg(struct gs_##str value) \
	{ \
		u64 v; \
		memcpy(&v, &value, sizeof(v)); \
		gs_writeq_##reg(v); \
	} \
	EXPORT_SYMBOL(gs_write_##reg)

#define DEFINE_READ_WRITE_REG(reg, str) \
	DEFINE_READ_REG(reg, str); \
	DEFINE_WRITE_REG(reg, str)

#define DEFINE_SYSFS_REG(reg) \
	static ssize_t show_##reg(struct device *device, \
		struct device_attribute *attr, char *buf) \
	{ \
		return scnprintf(buf, PAGE_SIZE, "0x%llx\n", gs_readq_##reg()); \
	} \
	static ssize_t store_##reg(struct device *device, \
	       struct device_attribute *attr, const char *buf, size_t size) \
	{ \
	       gs_writeq_##reg(simple_strtoull(buf, NULL, 0)); \
	       return size; \
	} \
	static DEVICE_ATTR(reg, S_IWUSR | S_IRUGO, show_##reg, store_##reg)

#define DEFINE_RW_REG(reg, addr) \
	DEFINE_READQ_RW_REG(reg, addr); \
	DEFINE_WRITEQ_RW_REG(reg, addr); \
	DEFINE_SYSFS_REG(reg)

#define DEFINE_WO_REG(reg, addr) \
	DEFINE_READQ_WO_REG(reg, addr); \
	DEFINE_WRITEQ_WO_REG(reg, addr); \
	DEFINE_SYSFS_REG(reg)

DEFINE_WO_REG(pmode,	0x12000000);	/* PCRTC mode setting */
DEFINE_WO_REG(smode1,	0x12000010);
DEFINE_WO_REG(smode2,	0x12000020);	/* Video synchronization mode */
DEFINE_WO_REG(srfsh,	0x12000030);
DEFINE_WO_REG(synch1,	0x12000040);
DEFINE_WO_REG(synch2,	0x12000050);
DEFINE_WO_REG(syncv,	0x12000060);
DEFINE_WO_REG(dispfb1,	0x12000070);	/* Rectangle read output circuit 1 */
DEFINE_WO_REG(display1,	0x12000080);	/* Rectangle read output circuit 1 */
DEFINE_WO_REG(dispfb2,	0x12000090);	/* Rectangle read output circuit 2 */
DEFINE_WO_REG(display2,	0x120000a0);	/* Rectangle read output circuit 2 */
DEFINE_WO_REG(extbuf,	0x120000b0);	/* Feedback write buffer */
DEFINE_WO_REG(extdata,	0x120000c0);	/* Feedback write setting */
DEFINE_WO_REG(extwrite,	0x120000d0);	/* Feedback write function control */
DEFINE_WO_REG(bgcolor,	0x120000e0);	/* Background color setting */
DEFINE_RW_REG(csr,	0x12001000);	/* System status */
DEFINE_WO_REG(imr,	0x12001010);	/* Interrupt mask control */
DEFINE_WO_REG(busdir,	0x12001040);	/* Host interface bus switching */
DEFINE_RW_REG(siglblid,	0x12001080);	/* Signal ID value read */
DEFINE_WO_REG(syscnt,	0x120010f0);

DEFINE_READ_WRITE_REG(pmode, pmode);
DEFINE_READ_WRITE_REG(smode1, smode1);
DEFINE_READ_WRITE_REG(smode2, smode2);
DEFINE_READ_WRITE_REG(srfsh, srfsh);
DEFINE_READ_WRITE_REG(synch1, synch1);
DEFINE_READ_WRITE_REG(synch2, synch2);
DEFINE_READ_WRITE_REG(syncv, syncv);
DEFINE_READ_WRITE_REG(display1, display);
DEFINE_READ_WRITE_REG(display2, display);
DEFINE_READ_WRITE_REG(dispfb1, dispfb12);
DEFINE_READ_WRITE_REG(dispfb2, dispfb12);
DEFINE_READ_WRITE_REG(csr, csr);
DEFINE_READ_WRITE_REG(busdir, busdir);

static ssize_t show_revision(struct device *device,
	struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n", gs_read_csr().rev);
}
static DEVICE_ATTR(revision, S_IRUGO, show_revision, NULL);

static ssize_t show_id(struct device *device,
	struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n", gs_read_csr().id);
}
static DEVICE_ATTR(id, S_IRUGO, show_id, NULL);

static struct attribute *gs_registers_attributes[] = {
	&dev_attr_revision.attr,
	&dev_attr_id.attr,
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
	&dev_attr_csr.attr,
	&dev_attr_imr.attr,
	&dev_attr_busdir.attr,
	&dev_attr_siglblid.attr,
	&dev_attr_syscnt.attr,
	NULL
};

static struct attribute_group gs_registers_attribute_group = {
	.attrs = gs_registers_attributes
};

static int create_sysfs(struct device *dev)
{
	int err;

	registers_kobj = kobject_create_and_add("registers", &dev->kobj);
	if (!registers_kobj)
		return -ENOMEM;

	err = sysfs_create_group(registers_kobj, &gs_registers_attribute_group);
	if (err)
		goto out_err;

	return 0;

out_err:
	kobject_del(registers_kobj);
	registers_kobj = NULL;

	return err;
}

static void remove_sysfs(struct device *dev)
{
	kobject_del(registers_kobj);
}

#define GIF_CTRL	0x10003000	/* GIF control (w) */
#define GIF_MODE	0x10003010	/* GIF mode (w) */
#define GIF_STAT	0x10003020	/* GIF status (r) */

#define GIF_TAG0	0x10003040	/* GIF tag 31:0 (r) */
#define GIF_TAG1	0x10003050	/* GIF tag 63:32 (r) */
#define GIF_TAG2	0x10003060	/* GIF tag 95:54 (r) */
#define GIF_TAG3	0x10003070	/* GIF tag 127:96 (r) */
#define GIF_CNT		0x10003080	/* GIF transfer status counter (r) */
#define GIF_P3CNT	0x10003090	/* PATH3 transfer status counter (r) */
#define GIF_P3TAG	0x100030a0	/* PATH3 GIF tag value (r) */

void gif_writel_ctrl(u32 value)
{
	outl(value, GIF_CTRL);
}
EXPORT_SYMBOL(gif_writel_ctrl);

void gif_write_ctrl(struct gif_ctrl value)
{
	u32 v;
	memcpy(&v, &value, sizeof(v));
	gif_writel_ctrl(v);
}
EXPORT_SYMBOL(gif_write_ctrl);

void gif_writel_mode(u32 value)
{
	outl(value, GIF_MODE);
}
EXPORT_SYMBOL(gif_writel_mode);

u32 gif_readl_stat(void)
{
	return inl(GIF_STAT);
}
EXPORT_SYMBOL(gif_readl_stat);

void gif_reset(void)
{
	gif_write_ctrl((struct gif_ctrl) { .rst = 1 });

	udelay(100);		/* 100 us */
}
EXPORT_SYMBOL(gif_reset);

void gif_stop(void)
{
	gif_write_ctrl((struct gif_ctrl) { .pse = 1 });
}
EXPORT_SYMBOL(gif_stop);

void gif_resume(void)
{
	gif_write_ctrl((struct gif_ctrl) { .pse = 0 });
}
EXPORT_SYMBOL(gif_resume);

bool gif_ready(void)
{
	size_t countout = 1000000;

	while ((inl(D2_CHCR) & 0x100) != 0 && countout > 0)
		countout--;

	return countout > 0;
}
EXPORT_SYMBOL(gif_ready);

/*
 * FIXME:
 *
 * Reading from the GIF requires the following steps:
 *
 * 1. Set transmission parameters.
 * 2. Access the FINISH register (any data can be written to it; a FINISH
 *    event occurs when data is input to the GS).
 * 3. Wait for the FINISH field of the CSR register becomes 1.
 * 4. Clear the FINISH field of the CSR register.
 * 5. Set the BUSDIR register to 1, which reverses transmission direction.
 * 6. Read data from the GIF.
 * 7. Set the BUSDIR register to 0, to restore normal transmission direction.
 *
 * The Host FIFO requires that the total data size is a multiple of 128 bytes
 * for DMA transmissions and 16 bytes for IO transmissions.
 */

void gif_write(union gif_data *base_package, size_t package_count)
{
	if (package_count > 0) {
		const size_t size = package_count * sizeof(*base_package);
		const dma_addr_t madr =
			dma_map_single(NULL, base_package, size, DMA_TO_DEVICE);

		/* Wait for previous transmissions to finish. */
		while ((inl(D2_CHCR) & 0x100) != 0)
			;

		outl(madr, D2_MADR);
		outl(package_count, D2_QWC);
		outl(CHCR_SENDN, D2_CHCR);

		dma_unmap_single(NULL, madr, size, DMA_TO_DEVICE); /* FIXME: At end? */
	}
}
EXPORT_SYMBOL(gif_write);

void gs_flush(void)
{
	gs_write_csr((struct gs_csr) { .flush = 1 });

	udelay(2500);		/* 2.5 ms */
}
EXPORT_SYMBOL(gs_flush);

void gs_reset(void)
{
	gs_write_csr((struct gs_csr) { .reset = 1 });

	udelay(2500);		/* 2.5 ms */
}
EXPORT_SYMBOL(gs_reset);

/* Returns number blocks to represent the given frame buffer resolution. */
u32 gs_psmct16_block_count(const u32 fbw, const u32 fbh)
{
	const u32 block_cols = fbw * GS_PSMCT16_PAGE_COLS;
	const u32 block_rows = (fbh + GS_PSMCT16_BLOCK_HEIGHT - 1) /
		GS_PSMCT16_BLOCK_HEIGHT;

	return block_cols * block_rows;
}
EXPORT_SYMBOL(gs_psmct16_block_count);

/* Returns number blocks to represent the given frame buffer resolution. */
u32 gs_psmct32_block_count(const u32 fbw, const u32 fbh)
{
	const u32 block_cols = fbw * GS_PSMCT32_PAGE_COLS;
	const u32 block_rows = (fbh + GS_PSMCT32_BLOCK_HEIGHT - 1) /
		GS_PSMCT32_BLOCK_HEIGHT;

	return block_cols * block_rows;
}
EXPORT_SYMBOL(gs_psmct32_block_count);

u32 gs_psmct16_blocks_free(const u32 fbw, const u32 fbh)
{
	const u32 block_count = gs_psmct16_block_count(fbw, fbh);

	return block_count <= GS_BLOCK_COUNT ?
		GS_BLOCK_COUNT - block_count : 0;
}
EXPORT_SYMBOL(gs_psmct16_blocks_free);

u32 gs_psmct32_blocks_free(const u32 fbw, const u32 fbh)
{
	const u32 block_count = gs_psmct32_block_count(fbw, fbh);

	return block_count <= GS_BLOCK_COUNT ?
		GS_BLOCK_COUNT - block_count : 0;
}
EXPORT_SYMBOL(gs_psmct32_blocks_free);

/* Returns block address given a block index starting at the top left corner. */
u32 gs_psmct16_block_address(const u32 fbw, const u32 block_index)
{
	static const u32 block[GS_PSMCT16_PAGE_ROWS][GS_PSMCT16_PAGE_COLS] = {
		{  0,  2,  8, 10 },
		{  1,  3,  9, 11 },
		{  4,  6, 12, 14 },
		{  5,  7, 13, 15 },
		{ 16, 18, 24, 26 },
		{ 17, 19, 25, 27 },
		{ 20, 22, 28, 30 },
		{ 21, 23, 29, 31 }
	};

	const u32 fw = GS_PSMCT16_PAGE_COLS * fbw;
	const u32 fc = block_index % fw;
	const u32 fr = block_index / fw;
	const u32 bc = fc % GS_PSMCT16_PAGE_COLS;
	const u32 br = fr % GS_PSMCT16_PAGE_ROWS;
	const u32 pc = fc / GS_PSMCT16_PAGE_COLS;
	const u32 pr = fr / GS_PSMCT16_PAGE_ROWS;

	return GS_BLOCKS_PER_PAGE * (fbw * pr + pc) + block[br][bc];
}
EXPORT_SYMBOL(gs_psmct16_block_address);

/* Returns block address given a block index starting at the top left corner. */
u32 gs_psmct32_block_address(const u32 fbw, const u32 block_index)
{
	static const u32 block[GS_PSMCT32_PAGE_ROWS][GS_PSMCT32_PAGE_COLS] = {
		{  0,  1,  4,  5, 16, 17, 20, 21 },
		{  2,  3,  6,  7, 18, 19, 22, 23 },
		{  8,  9, 12, 13, 24, 25, 28, 29 },
		{ 10, 11, 14, 15, 26, 27, 30, 31 }
	};

	const u32 fw = GS_PSMCT32_PAGE_COLS * fbw;
	const u32 fc = block_index % fw;
	const u32 fr = block_index / fw;
	const u32 bc = fc % GS_PSMCT32_PAGE_COLS;
	const u32 br = fr % GS_PSMCT32_PAGE_ROWS;
	const u32 pc = fc / GS_PSMCT32_PAGE_COLS;
	const u32 pr = fr / GS_PSMCT32_PAGE_ROWS;

	return GS_BLOCKS_PER_PAGE * (fbw * pr + pc) + block[br][bc];
}
EXPORT_SYMBOL(gs_psmct32_block_address);

#define GS_CSR         0x12001000	/* FIXME */
#define GS_IMR         0x12001010	/* FIXME */

static inline void gs_unmask(struct irq_data *data)
{
	unsigned long flags;

	spin_lock_irqsave(&gs_registers.lock, flags);

	gs_registers.imr &= ~BIT(8 + data->irq - IRQ_GS);
	outq(gs_registers.imr, GS_IMR);

	spin_unlock_irqrestore(&gs_registers.lock, flags);
}

static inline void gs_mask(struct irq_data *data)
{
	unsigned long flags;

	spin_lock_irqsave(&gs_registers.lock, flags);

	gs_registers.imr |= BIT(8 + data->irq - IRQ_GS);
	outq(gs_registers.imr, GS_IMR);

	spin_unlock_irqrestore(&gs_registers.lock, flags);
}

static void gs_ack(struct irq_data *data)
{
	outl(BIT(data->irq - IRQ_GS), GS_CSR);
}

#define GS_IRQ_TYPE(irq_, name_) { \
	.irq = irq_, \
	.irq_chip = { \
		.name = name_, \
		.irq_unmask = gs_unmask, \
		.irq_mask = gs_mask, \
		.irq_ack = gs_ack \
	} \
}

static struct {
	unsigned int irq;
	struct irq_chip irq_chip;
} gs_irqs[] = {
	GS_IRQ_TYPE(IRQ_GS_SIGNAL,  "GS SIGNAL"),
	GS_IRQ_TYPE(IRQ_GS_FINISH,  "GS FINISH"),
	GS_IRQ_TYPE(IRQ_GS_HSYNC,   "GS HSYNC"),
	GS_IRQ_TYPE(IRQ_GS_VSYNC,   "GS VSYNC"),
	GS_IRQ_TYPE(IRQ_GS_EDW,     "GS EDW"),
	GS_IRQ_TYPE(IRQ_GS_EXHSYNC, "GS EXHSYNC"),
	GS_IRQ_TYPE(IRQ_GS_EXVSYNC, "GS EXVSYNC"),
};

static irqreturn_t gs_cascade(int irq, void *data)
{
	unsigned int pending = inl(GS_CSR) & 0x1f;

	if (!pending)
		return IRQ_NONE;

	while (pending) {
		const unsigned int irq_gs = __fls(pending);

		if (generic_handle_irq(irq_gs + IRQ_GS) < 0)
			spurious_interrupt();
		pending &= ~BIT(irq_gs);
	}

	return IRQ_HANDLED;
}

static struct irqaction cascade_gs_irqaction = {
	.name = "GS cascade",
	.handler = gs_cascade,
};

static int gs_probe(struct platform_device *pdev)
{
	int err;
	int i;

#if 0
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (regs == NULL) {
		dev_err(dev, "platform_get_resource 0 failed\n");
		ret = -ENOENT;
		goto err;
	}
#endif

	gif_buffer = (void *)__get_free_page(GFP_DMA);
	if (gif_buffer == NULL)
		return -ENOMEM;

	dmac_buffer = (void *)__get_free_page(GFP_DMA);
	if (dmac_buffer == NULL) {
		free_page((unsigned long)gif_buffer);
		return -ENOMEM;
	}

	spin_lock_init(&gs_registers.lock);

	for (i = 0; i < ARRAY_SIZE(gs_irqs); i++)
		irq_set_chip_and_handler(gs_irqs[i].irq,
			&gs_irqs[i].irq_chip, handle_level_irq);

	gs_writeq_imr(0x7f00);	/* Disable GS inerrupts */
	gs_writeq_csr(0x00ff);	/* Clear GS events */

	/* Enable cascaded GS IRQ. */
	err = setup_irq(IRQ_INTC_GS, &cascade_gs_irqaction);
	if (err)
		printk(KERN_ERR "irq: Failed to setup GS IRQ (err = %d).\n", err);

	gif_reset();
	gs_flush();
	// gs_reset();

#if 0
	setcrtc(1, 1);
#endif

	create_sysfs(&pdev->dev);	// FIXME: Check errors

	return 0;

#if 0
err:
	return ret;
#endif
}

static int gs_remove(struct platform_device *pdev)
{
	remove_sysfs(&pdev->dev);

	free_page((unsigned long)dmac_buffer);
	free_page((unsigned long)gif_buffer);

	return 0;
}

static struct platform_driver gs_driver = {
	.probe		= gs_probe,
	.remove		= gs_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

static int __init gs_init(void)
{
	return platform_driver_register(&gs_driver);
}

static void __exit gs_exit(void)
{
	platform_driver_unregister(&gs_driver);
}

module_init(gs_init);
module_exit(gs_exit);

MODULE_LICENSE("GPL");
