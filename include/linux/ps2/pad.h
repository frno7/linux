#ifndef __PS2_PAD_H
#define __PS2_PAD_H
/* Copyright 2010 Mega Man */
/* TBD: Unfinished state. Rework code. */

#include <linux/ioctl.h>

#define	PS2PAD_IOCTL	'p'

#define PS2PAD_TYPE_NEJICON	0x2
#define PS2PAD_TYPE_DIGITAL	0x4
#define PS2PAD_TYPE_ANALOG	0x5
#define PS2PAD_TYPE_DUALSHOCK	0x7

#define PS2PAD_STAT_NOTCON     	0x00
#define PS2PAD_STAT_READY      	0x01
#define PS2PAD_STAT_BUSY       	0x02
#define PS2PAD_STAT_ERROR      	0x03

#define PS2PAD_RSTAT_COMPLETE	0x00
#define PS2PAD_RSTAT_FAILED	0x01
#define PS2PAD_RSTAT_BUSY	0x02

#define PS2PAD_BUTTON_LEFT	0x8000
#define PS2PAD_BUTTON_DOWN	0x4000
#define PS2PAD_BUTTON_RIGHT	0x2000
#define PS2PAD_BUTTON_UP	0x1000
#define PS2PAD_BUTTON_START	0x0800
#define PS2PAD_BUTTON_R3	0x0400
#define PS2PAD_BUTTON_L3	0x0200
#define PS2PAD_BUTTON_SELECT	0x0100
#define PS2PAD_BUTTON_SQUARE	0x0080
#define PS2PAD_BUTTON_CROSS	0x0040
#define PS2PAD_BUTTON_CIRCLE	0x0020
#define PS2PAD_BUTTON_TRIANGLE	0x0010
#define PS2PAD_BUTTON_R1	0x0008
#define PS2PAD_BUTTON_L1	0x0004
#define PS2PAD_BUTTON_R2	0x0002
#define PS2PAD_BUTTON_L2	0x0001

#define PS2PAD_BUTTON_A		0x0020
#define PS2PAD_BUTTON_B		0x0010
#define PS2PAD_BUTTON_R		0x0008

#define PS2PAD_DATASIZE		32

#define PS2PAD_SUCCEEDED	1
#define PS2PAD_FAILED		0

#define PS2PAD_PORT(n)		(((n) >> 4) & 0xf)
#define PS2PAD_SLOT(n)		(((n) >> 0) & 0xf)
#define PS2PAD_TYPE(n)		(((n) >> 4) & 0xf)

struct ps2pad_stat {
	unsigned char portslot;	/* (port# << 4) | slot#	*/
	unsigned char stat;
	unsigned char rstat;
	unsigned char type;
};

struct ps2pad_actinfo {
	int actno;
	int term;
	int result;
};

struct ps2pad_combinfo {
	int listno;
	int offs;
	int result;
};

struct ps2pad_modeinfo {
	int term;
	int offs;
	int result;
};
#define PS2PAD_MODECURID	1
#define PS2PAD_MODECUREXID	2
#define PS2PAD_MODECUROFFS	3
#define PS2PAD_MODETABLE	4

struct ps2pad_mode {
	int offs;
	int lock;
};

struct ps2pad_act {
	int len;
	unsigned char data[32];
};

#define PS2PAD_ACTFUNC		1
#define PS2PAD_ACTSUB		2
#define PS2PAD_ACTSIZE		3
#define PS2PAD_ACTCURR		4

#define	PS2PAD_IOCPRESSMODEINFO		_IOR(PS2PAD_IOCTL, 0, int)
#define	PS2PAD_IOCENTERPRESSMODE	_IO(PS2PAD_IOCTL, 1)
#define	PS2PAD_IOCEXITPRESSMODE		_IO(PS2PAD_IOCTL, 2)
#define	PS2PAD_IOCGETREQSTAT		_IOR(PS2PAD_IOCTL, 3, int)
#define	PS2PAD_IOCGETSTAT		_IOR(PS2PAD_IOCTL, 4, int)
#define	PS2PAD_IOCACTINFO		_IOWR(PS2PAD_IOCTL, 5, struct ps2pad_actinfo)
#define	PS2PAD_IOCCOMBINFO		_IOWR(PS2PAD_IOCTL, 6, struct ps2pad_combinfo)
#define	PS2PAD_IOCMODEINFO		_IOWR(PS2PAD_IOCTL, 7, struct ps2pad_modeinfo)
#define	PS2PAD_IOCSETMODE		_IOW(PS2PAD_IOCTL, 8, struct ps2pad_mode)
#define	PS2PAD_IOCSETACTALIGN		_IOW(PS2PAD_IOCTL, 9, struct ps2pad_act)
#define	PS2PAD_IOCSETACT		_IOW(PS2PAD_IOCTL, 10, struct ps2pad_act)

#define	PS2PAD_IOCGETNPADS		_IOR(PS2PAD_IOCTL, 11, int)

#endif /* __PS2_PAD_H */
