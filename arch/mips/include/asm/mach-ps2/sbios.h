/*
 *  PlayStation 2 Graphic console
 *
 *  Copyright (C) 2000-2002 Sony Computer Entertainment Inc.
 *  Copyright (C) 2010-2013 Juergen Urban
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Change history:
 *
 * Version 2.57         Mar 13, 2003
 *	added:
 *		SB_SETRGBYC
 * Version 2.56         Nov 26, 2002
 *	added:
 *		SBR_REMOCON2_INIT
 *		SBR_REMOCON2_END
 *		SBR_REMOCON2_PORTOPEN
 *		SBR_REMOCON2_PORTCLOSE
 *		SBR_REMOCON2_FEATURE
 *		SBR_REMOCON2_IGNORE
 *		SB_REMOCON2_READ
 * Version 2.55		Jan 09, 2002
 *	added SBR_SOUND_REMOTE command definitions
 * Version 2.54		Jan 08, 2002
 *	deleted:
 *		SBR_SOUND_VOICE_TRANS
 *		SBR_SOUND_VOICE_TRANSSTAT
 * Version 2.53		Dec 19, 2001
 *	added:
 *		SBR_SOUND_REMOTE
 * Version 2.52		Dec 04, 2001
 *	added:
 *		SBR_CDVD_OPENCONFIG
 *		SBR_CDVD_CLOSECONFIG
 *		SBR_CDVD_READCONFIG
 *		SBR_CDVD_WRITECONFIG
 * Version 2.51		Nov 19, 2001
 *	added:
 *		SBR_REMOCON_INIT
 *		SBR_REMOCON_END
 *		SBR_REMOCON_PORTOPEN
 *		SBR_REMOCON_PORTCLOSE
 *		SB_REMOCON_READ
 * Version 6.66
 *              SBIOS provided by kernelloader
 */

#ifndef _PS2_SBIOS_H_
#define _PS2_SBIOS_H_

#include <linux/types.h>

#ifdef CONFIG_64BIT
/** Kernel uses 64-Bit pointers, but SBIOS expects 32-Bit pointers. */
typedef uint32_t ps2_addr_t;
typedef uint32_t ps2_const_addr_t;
#else
typedef void *ps2_addr_t;
typedef const void *ps2_const_addr_t;
#endif

typedef struct t_SifCmdHeader {
	u32 size;
	void *dest;
	int cid;
	u32 unknown;
} SifCmdHeader_t;

int sbios(int sbcall, void *arg);

#define SBIOS_VERSION	0x0257

#define SB_GETVER		0
#define SB_HALT			1
struct sb_halt_arg {
    int mode;
#define SB_HALT_MODE_PWROFF	0
#define SB_HALT_MODE_HALT	1
#define SB_HALT_MODE_RESTART	2
};
#define SB_SETDVE		2
struct sb_setdve_arg {
    int mode;
};
#define SB_PUTCHAR		3
struct sb_putchar_arg {
    char c;
};
#define SB_GETCHAR		4
#define SB_SETGSCRT		5
struct sb_setgscrt_arg {
    int inter;
    int omode;
    int ffmode;
    ps2_addr_t dx1, dy1, dx2, dy2;
};
#define SB_SETRGBYC		6
struct sb_setrgbyc_arg {
    int rgbyc;
};

/** Debug output in TGE (not supported by RTE). */
#define SB_SET_PRINTS_CALLBACK 15

/*
 *  SIF DMA services
 */

#define SB_SIFINIT		16
#define SB_SIFEXIT		17
#define SB_SIFSETDMA		18
struct sb_sifsetdma_arg {
    ps2_addr_t sdd;
    int len;
};
#define SB_SIFDMASTAT		19
struct sb_sifdmastat_arg {
    int id;
};
#define SB_SIFSETDCHAIN		20
/* 21-23: reserved */

/*
 *  SIF CMD services
 */

#define SB_SIFINITCMD		32
#define SB_SIFEXITCMD		33
#define SB_SIFSENDCMD		34
struct sb_sifsendcmd_arg {
    u_int fid;
    ps2_addr_t pp;
    int ps;
    ps2_addr_t src;
    ps2_addr_t dest;
    int size;
};
#define SB_SIFCMDINTRHDLR	35
#define SB_SIFADDCMDHANDLER	36
struct sb_sifaddcmdhandler_arg {
    u_int fid;
    ps2_addr_t func;
    ps2_addr_t data;
};
#define SB_SIFREMOVECMDHANDLER	37
struct sb_sifremovecmdhandler_arg {
    u_int fid;
};
#define SB_SIFSETCMDBUFFER	38
struct sb_sifsetcmdbuffer_arg {
    ps2_addr_t db;
    int size;
};
/* 39-42: reserved */

/*
 *  SIF RPC services
 */

#define SB_SIFINITRPC		48
#define SB_SIFEXITRPC		49
#define SB_SIFGETOTHERDATA	50
struct sb_sifgetotherdata_arg {
    ps2_addr_t rd;
    ps2_addr_t src;
    ps2_addr_t dest;
    int size;
    u_int mode;
    ps2_addr_t func;
    ps2_addr_t para;
};
#define SB_SIFBINDRPC		51
struct sb_sifbindrpc_arg {
    ps2_addr_t bd;
    u_int command;
    u_int mode;
    ps2_addr_t func;
    ps2_addr_t para;
};
#define SB_SIFCALLRPC		52
struct sb_sifcallrpc_arg {
    ps2_addr_t bd;
    u_int fno;
    u_int mode;
    ps2_addr_t send;
    int ssize;
    ps2_addr_t receive;
    int rsize;
    ps2_addr_t func;
    ps2_addr_t para;
};
#define SB_SIFCHECKSTATRPC	53
struct sb_sifcheckstatrpc_arg {
    ps2_addr_t cd;
};
#define SB_SIFSETRPCQUEUE	54
struct sb_sifsetrpcqueue_arg {
    ps2_addr_t pSrqd;
    ps2_addr_t callback;
    ps2_addr_t arg;
};
#define SB_SIFREGISTERRPC	55
struct sb_sifregisterrpc_arg {
    ps2_addr_t pr;
    u_int command;
    ps2_addr_t func;
    ps2_addr_t buff;
    ps2_addr_t cfunc;
    ps2_addr_t cbuff;
    ps2_addr_t pq;
};
#define SB_SIFREMOVERPC		56
struct sb_sifremoverpc_arg {
    ps2_addr_t pr;
    ps2_addr_t pq;
};
#define SB_SIFREMOVERPCQUEUE	57
struct sb_sifremoverpcqueue_arg {
    ps2_addr_t pSrqd;
};
#define SB_SIFGETNEXTREQUEST	58
struct sb_sifgetnextrequest_arg {
    ps2_addr_t qd;
};
#define SB_SIFEXECREQUEST	59
struct sb_sifexecrequest_arg {
    ps2_addr_t rdp;
};

/*
 *  device services
 */

/* RPC common argument */

struct sbr_common_arg {
    int result;
    ps2_addr_t arg;
	/* void (*func)(void *, int); */
    ps2_addr_t func;
    ps2_addr_t para;
};

/* IOP heap */

#define SBR_IOPH_INIT		64
#define SBR_IOPH_ALLOC		65
struct sbr_ioph_alloc_arg {
    int size;
};
#define SBR_IOPH_FREE		66
struct sbr_ioph_free_arg {
    uint32_t addr;
};

/* pad device */

#define SBR_PAD_INIT		80
struct sbr_pad_init_arg {
    int mode;
};
#define SBR_PAD_END		81
#define SBR_PAD_PORTOPEN	82
struct sbr_pad_portopen_arg {
    int port;
    int slot;
    ps2_addr_t addr;
};
#define SBR_PAD_PORTCLOSE	83
struct sbr_pad_portclose_arg {
    int port;
    int slot;
};
#define SBR_PAD_SETMAINMODE	84
struct sbr_pad_setmainmode_arg {
    int port;
    int slot;
    int offs;
    int lock;
};
#define SBR_PAD_SETACTDIRECT	85
struct sbr_pad_setactdirect_arg {
    int port;
    int slot;
	/* const unsigned char *data */
    ps2_const_addr_t data;
};
#define SBR_PAD_SETACTALIGN	86
struct sbr_pad_setactalign_arg {
    int port;
    int slot;
	/* const unsigned char *data */
    ps2_const_addr_t data;
};
#define SBR_PAD_INFOPRESSMODE	87
struct sbr_pad_pressmode_arg {
    int port;
    int slot;
};
#define SBR_PAD_ENTERPRESSMODE	88
#define SBR_PAD_EXITPRESSMODE	89


#define SB_PAD_READ		90
struct sb_pad_read_arg {
    int port;
    int slot;
    /* unsigned char *rdata; */
	ps2_addr_t rdata;
};
#define SB_PAD_GETSTATE		91
struct sb_pad_getstate_arg {
    int port;
    int slot;
};
#define SB_PAD_GETREQSTATE	92
struct sb_pad_getreqstate_arg {
    int port;
    int slot;
};
#define SB_PAD_INFOACT		93
struct sb_pad_infoact_arg {
    int port;
    int slot;
    int actno;
    int term;
};
#define SB_PAD_INFOCOMB		94
struct sb_pad_infocomb_arg {
    int port;
    int slot;
    int listno;
    int offs;
};
#define SB_PAD_INFOMODE		95
struct sb_pad_infomode_arg {
    int port;
    int slot;
    int term;
    int offs;
};

/* sound */

#define SBR_SOUND_INIT		112
struct sbr_sound_init_arg {
#define SB_SOUND_INIT_COLD	0
#define SB_SOUND_INIT_HOT	1
    int flag;
};
#define SBR_SOUND_END		113
#define SB_SOUND_GREG		114
#define SB_SOUND_SREG		115
struct sb_sound_reg_arg {
    u_int idx;
#define SB_SOUND_REG_MADR(core)		(0 + (core))
#define SB_SOUND_REG_BCR(core)		(2 + (core))
#define SB_SOUND_REG_BTCR(core)		(4 + (core))
#define SB_SOUND_REG_CHCR(core)		(6 + (core))

#define SB_SOUND_REG_MMIX(core)		(8 + (core))
#define SB_SOUND_REG_DMAMOD(core)	(10 + (core))
#define SB_SOUND_REG_MVOLL(core)	(12 + (core))
#define SB_SOUND_REG_MVOLR(core)	(14 + (core))
#define SB_SOUND_REG_EVOLL(core)	(16 + (core))
#define SB_SOUND_REG_EVOLR(core)	(18 + (core))
#define SB_SOUND_REG_AVOLL(core)	(20 + (core))
#define SB_SOUND_REG_AVOLR(core)	(22 + (core))
#define SB_SOUND_REG_BVOLL(core)	(24 + (core))
#define SB_SOUND_REG_BVOLR(core)	(26 + (core))
    u_int data;
};
#define SBR_SOUND_GCOREATTR	116
#define SBR_SOUND_SCOREATTR	117
struct sbr_sound_coreattr_arg {
    u_int idx;
#define SB_SOUND_CA_EFFECT_ENABLE	(1<<1)
#define SB_SOUND_CA_IRQ_ENABLE		(2<<1)
#define SB_SOUND_CA_MUTE_ENABLE		(3<<1)
#define SB_SOUND_CA_NOISE_CLK		(4<<1)
#define SB_SOUND_CA_SPDIF_MODE		(5<<1)
    u_int data;
};
#define SBR_SOUND_TRANS		118
struct sbr_sound_trans_arg {
    int channel;
    u_int mode;
#define SB_SOUND_TRANS_MODE_WRITE	0
#define SB_SOUND_TRANS_MODE_READ	1
#define SB_SOUND_TRANS_MODE_STOP	2
#define SB_SOUND_TRANS_MODE_DMA		(0<<3)
#define SB_SOUND_TRANS_MODE_PIO		(1<<3)
#define SB_SOUND_TRANS_MODE_ONCE	(0<<4)
#define SB_SOUND_TRANS_MODE_LOOP	(1<<4)
    u_int addr;
    u_int size;
    u_int start_addr;
};
#define SBR_SOUND_TRANSSTAT	119
struct sbr_sound_trans_stat_arg {
    int channel;
#define SB_SOUND_TRANSSTAT_WAIT		1
#define SB_SOUND_TRANSSTAT_CHECK	0
    int flag;
};
#define SBR_SOUND_TRANSCALLBACK	120
struct sbr_sound_trans_callback_arg {
    int channel;
    /* int (*func)(void*, int); */
	ps2_addr_t func;
    ps2_addr_t data;
    /* int (*oldfunc)(void*, int); */
	ps2_addr_t oldfunc;
    ps2_addr_t olddata;
};
#define SBR_SOUND_REMOTE	123
/*
 * XXX, 
 * struct sbr_sound_remote_arg in asm/mips/ps2/sbcall.h and
 * struct ps2sd_command must have common members and alignments.
 * double check if you change this structure.
 */
struct sbr_sound_remote_arg {
    int command;
#define PS2SDCTL_COMMAND_QUIT      0x1020
#define PS2SDCTL_COMMAND_QUIT2     0x010000d0
#define PS2SDCTL_COMMAND_OPEN1     0x1030
#define PS2SDCTL_COMMAND_OPEN2     0x1040
#define PS2SDCTL_COMMAND_OPEN3     0x1050
#define PS2SDCTL_COMMAND_OPEN4     0x01009050
#define PS2SDCTL_COMMAND_WRITE     0x1060
#define PS2SDCTL_COMMAND_WRITE2    0x01000070
#define PS2SDCTL_COMMAND_WRITE3    0x01000040
#define PS2SDCTL_COMMAND_READ      0x1070
    int args[126];
};

/* memory card */

#define SBR_MC_INIT		144
#define SBR_MC_OPEN		145
struct sbr_mc_open_arg {
    int port;
    int slot;
    /* const char *name; */
	ps2_const_addr_t name;
    int mode;
};
#define SBR_MC_MKDIR		146
struct sbr_mc_mkdir_arg {
    int port;
    int slot;
    /* const char *name; */
	ps2_const_addr_t name;
};
#define SBR_MC_CLOSE		147
struct sbr_mc_close_arg {
    int fd;
};
#define SBR_MC_SEEK		148
struct sbr_mc_seek_arg {
    int fd;
    int offset;
    int mode;
};
#define SBR_MC_READ		149
struct sbr_mc_read_arg {
    int fd;
    ps2_addr_t buff;
    int size;
};
#define SBR_MC_WRITE		150
struct sbr_mc_write_arg {
    int fd;
    ps2_const_addr_t buff;
    int size;
};
#define SBR_MC_GETINFO		151
struct sbr_mc_getinfo_arg {
    int port;
    int slot;
    /* int *type; */
	ps2_addr_t type;
    /* int *free; */
	ps2_addr_t free;
    /* int *format; */
	ps2_addr_t format;
};
#define SBR_MC_GETDIR		152
struct sbr_mc_getdir_arg {
    int port;
    int slot;
    /* const char *name; */
	ps2_const_addr_t name;
    unsigned int mode;
    int maxent;
    ps2_addr_t table;
};
#define SBR_MC_FORMAT		153
struct sbr_mc_format_arg {
    int port;
    int slot;
};
#define SBR_MC_DELETE		154
struct sbr_mc_delete_arg {
    int port;
    int slot;
    /* const char *name; */
	ps2_const_addr_t name;
};
#define SBR_MC_FLUSH		155
struct sbr_mc_flush_arg {
    int fd;
};
#define SBR_MC_SETFILEINFO	156
struct sbr_mc_setfileinfo_arg {
    int port;
    int slot;
    /* const char *name; */
	ps2_const_addr_t name;
    /* const char *info; */
	ps2_const_addr_t info;
    unsigned int valid;
};
#define SBR_MC_RENAME		157
struct sbr_mc_rename_arg {
    int port;
    int slot;
    /* const char *org; */
	ps2_const_addr_t orgname;
    /* const char *new; */
	ps2_const_addr_t newname;
};
#define SBR_MC_UNFORMAT		158
struct sbr_mc_unformat_arg {
    int port;
    int slot;
};
#define SBR_MC_GETENTSPACE	159
struct sbr_mc_getentspace_arg {
    int port;
    int slot;
    /* const char *path; */
	ps2_const_addr_t path;
};
#define SBR_MC_CALL		160

/*
 * CD/DVD
 */
#define SBR_CDVD_INIT		176
#define SBR_CDVD_RESET		177
#define SBR_CDVD_READY		178
struct sbr_cdvd_ready_arg {
    int mode;
};

#define SBR_CDVD_READ		179
struct sbr_cdvd_read_arg {
    u_int lbn;
    u_int sectors;
    ps2_addr_t buf;
    /* struct sceCdRMode *rmode; */
	ps2_addr_t rmode;
};

#define SBR_CDVD_STOP		180
#define SBR_CDVD_GETTOC		181
struct sbr_cdvd_gettoc_arg {
    /* u_char *buf; */
	ps2_addr_t buf;
    int len;
    int media;
};

#define SBR_CDVD_READRTC	182
#define SBR_CDVD_WRITERTC	183
struct sbr_cdvd_rtc_arg {
	u_char stat;		/* status */
	u_char second;		/* second */
	u_char minute;		/* minute */
	u_char hour;		/* hour   */

	u_char pad;		/* pad    */
	u_char day;		/* day    */
	u_char month;		/* 1900 or 2000 and  month  */
	u_char year;		/* year   */
};
#define SBR_CDVD_MMODE		184
struct sbr_cdvd_mmode_arg {
    int media;
};

#define SBR_CDVD_GETERROR	185
#define SBR_CDVD_GETTYPE	186
#define SBR_CDVD_TRAYREQ	187
struct sbr_cdvd_trayreq_arg {
    int req;
    int traycount;
};

#define SB_CDVD_POWERHOOK	188
struct sb_cdvd_powerhook_arg {
	/* void (*func)(void *); */
	ps2_addr_t func;
	ps2_addr_t arg;
};

#define SBR_CDVD_DASTREAM	189
struct sbr_cdvd_dastream_arg {
    u_int command;
    u_int lbn;
    int size;
    ps2_addr_t buf;
    /* struct sceCdRMode *rmode; */
	ps2_addr_t rmode;
};

#define SBR_CDVD_READSUBQ	190
struct sbr_cdvd_readsubq_arg {
    int stat;
    u_char data[10];
    u_char reserved[2];
};

#define SBR_CDVD_OPENCONFIG	191
#define SBR_CDVD_CLOSECONFIG	192
#define SBR_CDVD_READCONFIG	193
#define SBR_CDVD_WRITECONFIG	194
struct sbr_cdvd_config_arg {
    int dev;
#define SB_CDVD_CFG_OSD		0x01    /* OSD    */
    int mode;
#define SB_CDVD_CFG_READ	0x00    /* READ   */
#define SB_CDVD_CFG_WRITE	0x01    /* WRITE  */
    int blk;
#define SB_CDVD_CFG_BLKSIZE	15
    /* u_char *data; */
	ps2_addr_t data;
    int stat;
#define SB_CDVD_CFG_STAT_CMDERR	(1<<7)
#define SB_CDVD_CFG_STAT_BUSY	(1<<0)
};

#define SBR_CDVD_RCBYCTL        195
struct sbr_cdvd_rcbyctl_arg {
    int param;
    int stat;
};

/* Uses sbr_cdvd_read_arg */
#define SBR_CDVD_READ_DVD	196

/*
 * Remote Controller
 */
#define SBR_REMOCON_INIT	208
struct sbr_remocon_init_arg {
    int mode;
#define SBR_REMOCON_INIT_MODE	0
};

#define SBR_REMOCON_END		209
#define SBR_REMOCON_PORTOPEN	210
#define SBR_REMOCON_PORTCLOSE	211
struct sbr_remocon_portopen_arg {
    int port;
    int slot;
};

#define SB_REMOCON_READ		212
struct sb_remocon_read_arg {
    int port;
    int slot;
    int len;
#define SB_REMOCON_MAXDATASIZE	64
    /* unsigned char *buf; */
	ps2_addr_t buf;
};

#define SBR_REMOCON2_INIT	213
#define SBR_REMOCON2_END	214
#define SBR_REMOCON2_PORTOPEN	215
#define SBR_REMOCON2_PORTCLOSE	216
#define SB_REMOCON2_READ	217

#define SBR_REMOCON2_IRFEATURE	218
struct sbr_remocon2_feature_arg {
    unsigned char feature;
};


#endif
