// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 sub-system interface (SIF)
 *
 * The SIF is an interface unit to the input/output processor (IOP).
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/build_bug.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/types.h>

#include <asm/io.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/iop-error.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>

#define IOP_RESET_ARGS "rom0:UDNL rom0:OSDCNF"

#define SIF0_BUFFER_SIZE	PAGE_SIZE
#define SIF1_BUFFER_SIZE	PAGE_SIZE

/**
 * struct sif_cmd_header - 16-byte SIF command header
 * @packet_size: min 1x16 for header only, max 7*16 bytes
 * @data_size: data size in bytes
 * @data_addr: data address or zero
 * @cmd: command number
 * @opt: optional argument
 */
struct sif_cmd_header
{
	u32 packet_size : 8;
	u32 data_size : 24;
	u32 data_addr;
	u32 cmd;
	u32 opt;
};

static iop_addr_t iop_buffer; /* Address of IOP SIF DMA receive address */
static void *sif0_buffer;
static void *sif1_buffer;

/**
 * sif_write_msflag - write to set main-to-sub flag register bits
 * @mask: MSFLAG register bit values to set
 */
static void sif_write_msflag(u32 mask)
{
	outl(mask, SIF_MSFLAG);
}

/**
 * sif_write_smflag - write to clear sub-to-main flag register bits
 * @mask: SMFLAG register bit values to clear
 */
static void sif_write_smflag(u32 mask)
{
	outl(mask, SIF_SMFLAG);
}

/**
 * sif_read_smflag - read the sub-to-main flag register
 *
 * Return: SMFLAG register value
 */
static u32 sif_read_smflag(void)
{
	u32 a = inl(SIF_SMFLAG);
	u32 b;

	do {
		b = a;

		a = inl(SIF_SMFLAG);
	} while (a != b);	/* Ensure SMFLAG reading is stable */

	return a;
}

static bool completed(bool (*condition)(void))
{
	const unsigned long timeout = jiffies + 5*HZ;

	do {
		if (condition())
			return true;

		msleep(1);
	} while (time_is_after_jiffies(timeout));

	return false;
}

static bool sif_smflag_cmdinit(void)
{
	return (sif_read_smflag() & SIF_STATUS_CMDINIT) != 0;
}

static bool sif_smflag_bootend(void)
{
	return (sif_read_smflag() & SIF_STATUS_BOOTEND) != 0;
}

static bool sif1_busy(void)
{
	return (inl(DMAC_SIF1_CHCR) & DMAC_CHCR_BUSY) != 0;
}

static bool sif1_ready(void)
{
	size_t countout = 50000;	/* About 5 s */

	while (sif1_busy() && countout > 0) {
		udelay(100);
		countout--;
	}

	return countout > 0;
}

/* Bytes to 32-bit word count. */
static u32 nbytes_to_wc(size_t nbytes)
{
	const u32 wc = nbytes / 4;

	BUG_ON(nbytes & 0x3);	/* Word count must align */
	BUG_ON(nbytes != (size_t)wc * 4);

	return wc;
}

/* Bytes to 128-bit quadword count. */
static u32 nbytes_to_qwc(size_t nbytes)
{
	const size_t qwc = nbytes / 16;

	BUG_ON(nbytes & 0xf);	/* Quadword count must align */
	BUG_ON(qwc > 0xffff);	/* QWC DMA field is only 16 bits */

	return qwc;
}

static int sif1_write_ert_int_0(const struct sif_cmd_header *header,
	bool ert, bool int_0, iop_addr_t dst, const void *src, size_t nbytes)
{
	const size_t header_size = header != NULL ? sizeof(*header) : 0;
	const size_t aligned_size = (header_size + nbytes + 15) & ~(size_t)15;
	const struct iop_dma_tag iop_dma_tag = {
		.ert	= ert,
		.int_0	= int_0,
		.addr	= dst,
		.wc	= nbytes_to_wc(aligned_size)
	};
	const size_t dma_nbytes = sizeof(iop_dma_tag) + aligned_size;
	u8 *dma_buffer = sif1_buffer;
	dma_addr_t madr;

	if (!aligned_size)
		return 0;
	if (dma_nbytes > SIF1_BUFFER_SIZE)
		return -EINVAL;
	if (!sif1_ready())
		return -EBUSY;

	memcpy(&dma_buffer[0], &iop_dma_tag, sizeof(iop_dma_tag));
	memcpy(&dma_buffer[sizeof(iop_dma_tag)], header, header_size);
	memcpy(&dma_buffer[sizeof(iop_dma_tag) + header_size], src, nbytes);

	madr = virt_to_phys(dma_buffer);
	dma_cache_wback((unsigned long)dma_buffer, dma_nbytes);

	outl(madr, DMAC_SIF1_MADR);
	outl(nbytes_to_qwc(dma_nbytes), DMAC_SIF1_QWC);
	outl(DMAC_CHCR_SENDN_TIE, DMAC_SIF1_CHCR);

	return 0;
}

static int sif1_write(const struct sif_cmd_header *header,
	iop_addr_t dst, const void *src, size_t nbytes)
{
	return sif1_write_ert_int_0(header, false, false, dst, src, nbytes);
}

static int sif1_write_irq(const struct sif_cmd_header *header,
	iop_addr_t dst, const void *src, size_t nbytes)
{
	return sif1_write_ert_int_0(header, true, true, dst, src, nbytes);
}

static int sif_cmd_opt_copy(u32 cmd_id, u32 opt, const void *pkt,
	size_t pktsize, iop_addr_t dst, const void *src, size_t nbytes)
{
	const struct sif_cmd_header header = {
		.packet_size = sizeof(header) + pktsize,
		.data_size   = nbytes,
		.data_addr   = dst,
		.cmd         = cmd_id,
		.opt         = opt
	};
	int err;

	if (pktsize > SIF_CMD_PACKET_DATA_MAX)
		return -EINVAL;

	err = sif1_write(NULL, dst, src, nbytes);
	if (!err)
		err = sif1_write_irq(&header, iop_buffer, pkt, pktsize);

	return err;
}

static int sif_cmd_copy(u32 cmd_id, const void *pkt, size_t pktsize,
	iop_addr_t dst, const void *src, size_t nbytes)
{
	return sif_cmd_opt_copy(cmd_id, 0, pkt, pktsize, dst, src, nbytes);
}

static int sif_cmd(u32 cmd_id, const void *pkt, size_t pktsize)
{
	return sif_cmd_copy(cmd_id, pkt, pktsize, 0, NULL, 0);
}

static int iop_reset_arg(const char *arg)
{
	const size_t arglen = strlen(arg) + 1;
	struct {
		u32 arglen;
		u32 mode;
		char arg[79 + 1];	/* Including NUL */
	} reset_pkt = {
		.arglen = arglen,
		.mode = 0
	};
	int err;

	if (arglen > sizeof(reset_pkt.arg))
		return -EINVAL;
	memcpy(reset_pkt.arg, arg, arglen);

	sif_write_smflag(SIF_STATUS_BOOTEND);

	err = sif_cmd(SIF_CMD_RESET_CMD, &reset_pkt, sizeof(reset_pkt));
	if (err)
		return err;

	sif_write_smflag(SIF_STATUS_SIFINIT | SIF_STATUS_CMDINIT);

	return completed(sif_smflag_bootend) ? 0 : -EIO;
}

static int iop_reset(void)
{
	return iop_reset_arg(IOP_RESET_ARGS);
}

static int sif_read_subaddr(dma_addr_t *subaddr)
{
	if (!completed(sif_smflag_cmdinit))
		return -EIO;

	*subaddr = inl(SIF_SUBADDR);

	return 0;
}

static void sif_write_mainaddr_bootend(dma_addr_t mainaddr)
{
	outl(0xff, SIF_UNKNF260);
	outl(mainaddr, SIF_MAINADDR);
	sif_write_msflag(SIF_STATUS_CMDINIT | SIF_STATUS_BOOTEND);
}

static void put_dma_buffers(void)
{
	free_page((unsigned long)sif1_buffer);
	free_page((unsigned long)sif0_buffer);
}

static int get_dma_buffers(void)
{
	sif0_buffer = (void *)__get_free_page(GFP_DMA);
	sif1_buffer = (void *)__get_free_page(GFP_DMA);

	if (sif0_buffer == NULL ||
	    sif1_buffer == NULL) {
		put_dma_buffers();
		return -ENOMEM;
	}

	return 0;
}

static void sif_disable_dma(void)
{
	outl(DMAC_CHCR_STOP, DMAC_SIF0_CHCR);
	outl(0, DMAC_SIF0_MADR);
	outl(0, DMAC_SIF0_QWC);
	inl(DMAC_SIF0_QWC);

	outl(DMAC_CHCR_STOP, DMAC_SIF1_CHCR);
}

/**
 * errno_for_iop_error - kernel error number corresponding to a given IOP error
 * @ioperr: IOP error number
 *
 * Return: approximative kernel error number
 */
int errno_for_iop_error(int ioperr)
{
	switch (ioperr) {
#define IOP_ERROR_ERRNO(identifier, number, errno, description)		\
	case -IOP_E##identifier: return -errno;
	IOP_ERRORS(IOP_ERROR_ERRNO)
	}

	return -1000 < ioperr && ioperr < 0 ? -EINVAL : ioperr;
}
EXPORT_SYMBOL_GPL(errno_for_iop_error);

/**
 * iop_error_message - message corresponding to a given IOP error
 * @ioperr: IOP error number
 *
 * Return: error message string
 */
const char *iop_error_message(int ioperr)
{
	switch (ioperr) {
	case 0:              return "Success";
	case 1:              return "Error";
#define IOP_ERROR_MSG(identifier, number, errno, description)		\
	case IOP_E##identifier: return description;
	IOP_ERRORS(IOP_ERROR_MSG)
	}

	return "Unknown error";
}
EXPORT_SYMBOL_GPL(iop_error_message);

static int __init sif_init(void)
{
	int err;

	BUILD_BUG_ON(sizeof(struct sif_cmd_header) != 16);

	sif_disable_dma();

	err = get_dma_buffers();
	if (err) {
		pr_err("sif: Failed to allocate DMA buffers with %d\n", err);
		goto err_dma_buffers;
	}

	/* Read provisional subaddr in preparation for the IOP reset. */
	err = sif_read_subaddr(&iop_buffer);
	if (err) {
		pr_err("sif: Failed to read provisional subaddr with %d\n",
			err);
		goto err_provisional_subaddr;
	}

	/* Write provisional mainaddr in preparation for the IOP reset. */
	sif_write_mainaddr_bootend(virt_to_phys(sif0_buffer));

	err = iop_reset();
	if (err) {
		pr_err("sif: Failed to reset the IOP with %d\n", err);
		goto err_iop_reset;
	}

	/* Write final mainaddr and indicate end of boot. */
	sif_write_mainaddr_bootend(virt_to_phys(sif0_buffer));

	/* Read final subaddr. */
	err = sif_read_subaddr(&iop_buffer);
	if (err) {
		pr_err("sif: Failed to read final subaddr with %d\n", err);
		goto err_final_subaddr;
	}

	return 0;

err_final_subaddr:
err_iop_reset:
err_provisional_subaddr:
	put_dma_buffers();

err_dma_buffers:
	return err;
}

static void __exit sif_exit(void)
{
	put_dma_buffers();
}

module_init(sif_init);
module_exit(sif_exit);

MODULE_DESCRIPTION("PlayStation 2 sub-system interface (SIF)");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
