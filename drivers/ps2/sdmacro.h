/*
 *  PlayStation 2 Sound driver
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

#ifndef PS2SDMACRO_H
#define PS2SDMACRO_H

/*
 * MIX
 */
#define SD_MMIX_SINER  (1 <<  0)
#define SD_MMIX_SINEL  (1 <<  1)
#define SD_MMIX_SINR   (1 <<  2)
#define SD_MMIX_SINL   (1 <<  3)
#define SD_MMIX_MINER  (1 <<  4)
#define SD_MMIX_MINEL  (1 <<  5)
#define SD_MMIX_MINR   (1 <<  6)
#define SD_MMIX_MINL   (1 <<  7)
#define SD_MMIX_MSNDER (1 <<  8)
#define SD_MMIX_MSNDEL (1 <<  9)
#define SD_MMIX_MSNDR  (1 << 10)
#define SD_MMIX_MSNDL  (1 << 11)

/*
 * transfer
 */
#define SD_TRANS_MODE_WRITE 0
#define SD_TRANS_MODE_READ  1
#define SD_TRANS_MODE_STOP  2
#define SD_TRANS_MODE_WRITE_FROM 3

#define SD_TRANS_BY_DMA     (0x0<<3)
#define SD_TRANS_BY_IO      (0x1<<3)

#define SD_BLOCK_ONESHOT (0<<4) 
#define SD_BLOCK_LOOP (1<<4) 

#define SD_TRANS_STATUS_WAIT  1
#define SD_TRANS_STATUS_CHECK 0

/*
 * 32bit mode: with `channel' argument
 */
#define SD_BLOCK_MEMIN0 (1<<2) 
#define SD_BLOCK_MEMIN1 (1<<3) 
#define SD_BLOCK_NORMAL (0<<4) 

#endif /* PS2SDMACRO_H */
