// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 I/O processor (IOP) file operations
 *
 * Copyright (C) 2018 Fredrik Noring <noring@nocrew.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include <asm/mach-ps2/iop-memory.h>
#include <asm/mach-ps2/sif.h>

enum iop_fio_rpc_ops {
	rpo_open    =  0, rpo_close  =  1, rpo_read   =  2, rpo_write  =  3,
	rpo_lseek   =  4, rpo_ioctl  =  5, rpo_remove =  6, rpo_mkdir  =  7,
	rpo_rmdir   =  8, rpo_dopen  =  9, rpo_dclose = 10, rpo_dread  = 11,
	rpo_getstat = 12, rpo_chstat = 13, rpo_format = 14, rpo_adddrv = 15,
	rpo_deldrv  = 16
};

static struct t_SifRpcClientData cd_fio;

static void *dma_buffer;	/* FIXME: Device private data */
static dma_addr_t dma_addr;

#define FIO_PATH_MAX 256	/* FIXME: Check this */

int iop_fio_open(const char *name, int oflag)
{
	const size_t name_size = strlen(name) + 1; /* Including NUL character */
	struct {
		s32 oflag;
		char name[FIO_PATH_MAX];
	} arg;
	s32 fd;
	int err;

	arg.oflag = oflag;	/* FIXME: Transcode flags */
	if (name_size > sizeof(arg.name))
		return -EINVAL;
	memcpy(arg.name, name, name_size);

	err = sif_rpc(&cd_fio, rpo_open,
		&arg, sizeof(arg.oflag) + name_size,
		&fd, sizeof(fd));

	return err < 0 ? err : fd;	/* FIXME: Transcode fd errors */
}
EXPORT_SYMBOL(iop_fio_open);

int iop_fio_close(int fd)
{
	const struct {
		s32 fd;
	} arg = {
		.fd = fd
	};
	s32 status;
	int err;

	err = sif_rpc(&cd_fio, rpo_close,
		&arg, sizeof(arg),
		&status, sizeof(status));

	return err < 0 ? err : status;
}
EXPORT_SYMBOL(iop_fio_close);

ssize_t iop_fio_read(int fd, void *buf, size_t nbyte_)
{
	struct rest {
		u32 size1;
		u32 size2;
		u32 dst1;
		u32 dst2;
		u8 buf1[16];
		u8 buf2[16];
	};

	const size_t nbyte = min_t(size_t, nbyte_, PAGE_SIZE - sizeof(struct rest));

	struct {
		s32 fd;
		u32 buf;
		u32 nbyte;
		u32 rest;
	} arg = {
		.fd = fd,
		.buf = dma_addr + sizeof(struct rest),
		.nbyte = nbyte,
		.rest = dma_addr
	};
	struct rest *rest = dma_buffer;
	u8 *ptr = (u8 *)&rest[1];
	s32 rd;
	int err;

	err = sif_rpc(&cd_fio, rpo_read,
		&arg, sizeof(arg),
		&rd, sizeof(rd));

	dma_cache_inv((unsigned long)dma_buffer, PAGE_SIZE);

	if (err < 0)
		return err;
	if (rd < 0)
		return rd;	/* FIXME: Transcode fd errors */

	memcpy(&ptr[rest->dst1 - dma_addr - sizeof(struct rest)], rest->buf1, rest->size1);
	memcpy(&ptr[rest->dst2 - dma_addr - sizeof(struct rest)], rest->buf2, rest->size2);

	memcpy(buf, ptr, rd);

	return rd;
}
EXPORT_SYMBOL(iop_fio_read);

off_t iop_fio_lseek(int fd, off_t offset, int whence)
{
	const struct {
		s32 fd;
		s32 offset;
		s32 whence;
	} arg = {
		.fd = fd,
		.offset = offset,
		.whence = whence
	};
	s32 status;
	int err;

	if (whence != SEEK_SET &&
	    whence != SEEK_CUR &&
	    whence != SEEK_END)
		return -EINVAL;

	if (arg.offset != offset)
		return -EINVAL;

	err = sif_rpc(&cd_fio, rpo_lseek,
		&arg, sizeof(arg),
		&status, sizeof(status));

	return err < 0 ? err : status;
}
EXPORT_SYMBOL(iop_fio_lseek);

#define FIO_O_RDONLY     0x0001

static const char *romver;

static const char *read_romver(void)
{
	static const char *filepath = "rom0:ROMVER";
	static char buffer[20];	/* FIXME: Test many nbyte read combinations */
	int fd, cl;
	ssize_t rd;

	fd = iop_fio_open(filepath, FIO_O_RDONLY);
	if (fd < 0) {
		printk("%s: open failed with %d\n", filepath, fd);
		return "";
	}

        rd = iop_fio_read(fd, buffer, sizeof(buffer) - 1);
	if (rd < 0)
		printk("%s: read failed with %zd\n", filepath, rd);
	else if (rd == sizeof(buffer) - 1)
		printk("%s: truncated\n", filepath);

        cl = iop_fio_close(fd);
	if (cl < 0)
		printk("%s: close failed with %d\n", filepath, cl);

	return strim(buffer);
}

const char *iop_romver(void)
{
	return romver;
}
EXPORT_SYMBOL(iop_romver);

#if 0
struct iop_model {
	char name[12];
};

struct iop_model iop_read_model(void)
{
	struct iop_model model = { };
	int fd;
	int rd;
	
	fd = iop_fio_open("rom0:OSDSYS", FIO_O_RDONLY);
	printk("iop_read_model open fd %d\n", fd);
        rd = iop_fio_lseek(fd, 0x8c808, IOP_FIO_LSEEK_SET);
	printk("iop_read_model lseek rd %d\n", rd);
        rd = iop_fio_read(fd, model.name, sizeof(model.name) - 1);
	printk("iop_read_model read rd %d\n", rd);
        iop_fio_close(fd);

	return model;
}
EXPORT_SYMBOL(iop_read_model);
#endif

static int __init iop_fio_init(void)
{
	int err = sif_rpc_bind(&cd_fio, SIF_SID_FILE_IO);

	if (err < 0) {
		printk("iop-fio: ps2sif_bindrpc failed\n");
		return err;
	}

	/* FIXME: Check errors */
	dma_buffer = (void *)__get_free_page(GFP_DMA);
	dma_addr = virt_to_phys(dma_buffer);

	romver = read_romver();
	printk("iop: version: %s\n", iop_romver());

	return 0;
}

static void __exit iop_fio_exit(void)
{
	free_page((unsigned long)dma_buffer);
}

module_init(iop_fio_init);
module_exit(iop_fio_exit);

MODULE_LICENSE("GPL");
