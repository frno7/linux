/*
 *  PlayStation 2 IOP memory management utility
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

#ifndef __PS2_IOPMEM_H
#define __PS2_IOPMEM_H

#include <linux/kernel.h>
#include <linux/semaphore.h>

#define PS2IOPMEM_MAXMEMS	16

struct ps2iopmem_entry {
	dma_addr_t addr;
	int size;
};

struct ps2iopmem_list {
	struct semaphore	lock;
	struct ps2iopmem_entry	mems[PS2IOPMEM_MAXMEMS];
};

void ps2iopmem_init(struct ps2iopmem_list *);
void ps2iopmem_end(struct ps2iopmem_list *);
int ps2iopmem_alloc(struct ps2iopmem_list *iml, int size);
void ps2iopmem_free(struct ps2iopmem_list *iml, int hdl);
unsigned long ps2iopmem_getaddr(struct ps2iopmem_list *, int hdl, int size);

#endif /* __PS2_IOPMEM_H */
