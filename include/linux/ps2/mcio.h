/*
 *  PlayStation 2 Memory Card driver
 *
 *        Copyright (C) 2000-2002  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id$
 */
#ifndef _PS2MCIO_H_
#define _PS2MCIO_H_

#define PS2MC_NAME_MAX	31
#define PS2MC_PATH_MAX	1023
#define PS2MC_BASEDIR	""

#define PS2MC_INVALIDPORTSLOT	-1
#define PS2MC_TYPE_EMPTY	0
#define PS2MC_TYPE_PS1		1
#define PS2MC_TYPE_PS2		2
#define PS2MC_TYPE_POCKETSTATION 3

#define PS2MC_SETDIR_MODE	(1<<0)
#define PS2MC_SETDIR_MTIME	(1<<1)
#define PS2MC_SETDIR_CTIME	(1<<2)

#define PS2MC_PORT(ps)	(((ps) >> 4) & 0xf)
#define PS2MC_SLOT(ps)	(((ps) >> 0) & 0xf)
#define PS2MC_PORTSLOT(port, slot) ((((port) & 0xf) << 4) | ((slot) & 0xf))

#define PS2MC_INIT_LISTENER(a)	INIT_LIST_HEAD(&(a)->link)

struct ps2mc_cardinfo {
	int type;
	int blocksize;
	unsigned long totalblocks;
	unsigned long freeblocks;
	int formatted;
	int generation;
	int busy;
};

struct ps2mc_arg {
	char *path;
	int pathlen;
	int mode;
	int pos;
	unsigned char *data;
	int count;
	int reserved[8];
};

struct ps2mc_dirent {
	char	name[PS2MC_NAME_MAX];
	u_short	namelen;
	umode_t	mode;
	off_t	size;
	time_t	ctime;
	time_t	mtime;
};

#ifdef __KERNEL__
struct ps2mc_listener {
	struct list_head link;
	void *ctx;
	void (*func)(void *, int, int, int);
};

void ps2mc_add_listener(struct ps2mc_listener *);
void ps2mc_del_listener(struct ps2mc_listener *);
int ps2mc_getinfo(int, struct ps2mc_cardinfo *);
int ps2mc_readdir(int, const char *, int, struct ps2mc_dirent *, int);
int ps2mc_getdir(int, const char*, struct ps2mc_dirent *);
int ps2mc_setdir(int, const char *, int, struct ps2mc_dirent *);

int ps2mc_getdtablesize(void);
int ps2mc_open(int, const char *, int);
int ps2mc_close(int);
off_t ps2mc_lseek(int, off_t, int);
ssize_t ps2mc_write(int, const void *, size_t);
ssize_t ps2mc_read(int, const void *, size_t);
int ps2mc_mkdir(int, const char *);
int ps2mc_rename(int, const char *, char *);
int ps2mc_delete(int, const char *);
int ps2mc_checkdev(dev_t dev);
char *ps2mc_terminate_name(char[PS2MC_NAME_MAX+1], const char *, int);
void ps2mc_add_disks(void);
void ps2mc_del_disks(void);

#endif /* __KERNEL__ */

#define	PS2MC_IOCTL	'm'
#define	PS2MC_IOCGETINFO		_IOR(PS2MC_IOCTL, 0, struct ps2mc_cardinfo)
#define	PS2MC_IOCFORMAT			_IO(PS2MC_IOCTL, 1)
#define	PS2MC_IOCUNFORMAT		_IO(PS2MC_IOCTL, 2)
#define	PS2MC_IOCSOFTFORMAT		_IO(PS2MC_IOCTL, 3)
#define	PS2MC_IOCWRITE			_IOWR(PS2MC_IOCTL, 4, struct ps2mc_arg)
#define	PS2MC_IOCREAD			_IOWR(PS2MC_IOCTL, 5, struct ps2mc_arg)
#define PS2MC_IOCNOTIFY			_IO(PS2MC_IOCTL, 6)
#define PS2MC_CALL			_IO(PS2MC_IOCTL, 7)

#endif /* _PS2MCIO_H_ */
