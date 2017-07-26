/*
 *  PlayStation 2 Memory Card driver
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

#ifndef PS2MCPRIV_H
#define PS2MCPRIV_H

#include <asm/mach-ps2/siflock.h>

#define PS2MC_DIRCACHESIZE	7
#define PS2MC_CHECK_INTERVAL	(HZ/2)
#define PS2MC_RWBUFSIZE		1024

#define PS2MC_NPORTS	2
#define PS2MC_NSLOTS	1

#define ARRAYSIZEOF(a)	(sizeof(a)/sizeof(*(a)))
#define MIN(a, b)	((a) < (b) ? (a) : (b))

extern int ps2mc_debug;
extern ps2sif_lock_t *ps2mc_lock;
extern struct file_operations ps2mc_fops;
extern atomic_t ps2mc_cardgens[PS2MC_NPORTS][PS2MC_NSLOTS];
extern int ps2mc_basedir_len;
extern atomic_t ps2mc_opened[PS2MC_NPORTS][PS2MC_NSLOTS];
extern struct semaphore ps2mc_filesem;
extern char *ps2mc_rwbuf;
extern int (*ps2mc_blkrw_hook)(int, int, void*, int);
extern struct semaphore ps2mc_waitsem;
extern void ps2mc_process_request(void);
void ps2mc_signal_change(int port, int slot);
int ps2mc_devinit(void);
int ps2mc_devexit(void);
void ps2mc_dircache_invalidate(int);
void ps2mc_dircache_invalidate_next_pos(int);
char* ps2mc_terminate_name(char *, const char *, int);
int ps2mc_getdir_sub(int, const char*, int, int, struct ps2mc_dirent *);
int ps2mc_getinfo_sub(int, int *, int *, int *, int *);
int ps2mc_check_path(const char *);
int ps2mc_format(int portslot);
int ps2mc_unformat(int portslot);
void ps2mc_set_state(int portslot, int state);
int ps2mc_delete_all(int portslot, const char *path);

#endif /* PS2MCPRIV_H */
