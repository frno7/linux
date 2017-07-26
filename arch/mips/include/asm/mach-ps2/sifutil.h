/*
 *  PlayStation 2 DMA packet utility
 *
 *  Copyright (C) 2000-2001 Sony Computer Entertainment Inc.
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

#ifdef __KERNEL__
#define PS2SIF_ALLOC_PRINT(fmt, args...) \
	printk(fmt, ## args)
#else
#ifndef PS2SIF_ALLOC_PRINT
#define PS2SIF_ALLOC_PRINT(fmt, args...) \
	fprintf(stderr, fmt, ## args)
#endif
#endif

#ifdef PS2SIF_ALLOC_DEBUG
#define PS2SIF_ALLOC_DPRINT(fmt, args...) \
	PS2SIF_ALLOC_PRINT("ps2sif_alloc: " fmt, ## args)
#else
#define PS2SIF_ALLOC_DPRINT(fmt, args...)
#endif

#define PS2SIF_ALIGN(a, n)	((__typeof__(a))(((unsigned long)(a) + (n) - 1) / (n) * (n)))
#define PS2SIF_ALLOC_BEGIN(buf, size) \
    if ((buf) != NULL) { \
      char *sif_alloc_base = (char*)(buf); \
      char *sif_alloc_ptr = (char*)(buf); \
      int limit = (size)
#define PS2SIF_ALLOC(ptr, size, align) \
    do { \
      (ptr) = (__typeof__(ptr))(sif_alloc_ptr = PS2SIF_ALIGN(sif_alloc_ptr, (align)));\
      PS2SIF_ALLOC_DPRINT("(%14s,%4d,%3d) = %p\n", #ptr,(size),(align),(ptr));\
      sif_alloc_ptr += (size); \
    } while (0)
#define PS2SIF_ALLOC_END(fmt, args...) \
      if (limit < sif_alloc_ptr - sif_alloc_base) { \
        PS2SIF_ALLOC_PRINT("*********************************\n"); \
        PS2SIF_ALLOC_PRINT("PS2SIF_ALLOC overrun %dbytes\n", \
	       sif_alloc_ptr - sif_alloc_base - limit); \
        PS2SIF_ALLOC_PRINT(fmt, ## args); \
        PS2SIF_ALLOC_PRINT("*********************************\n"); \
      } \
    }
