/*
 *  PlayStation 2 CDROM
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

#ifndef __ASM_PS2_CDROM_H
#define __ASM_PS2_CDROM_H

/*
 * read data pattern
 */
#define SCECdSecS2048		0	/* sector size 2048 */
#define SCECdSecS2328		1	/* sector size 2328 */
#define SCECdSecS2340		2	/* sector size 2340 */
#define SCECdSecS2352		0	/* sector size 2352  CD-DA read */
#define SCECdSecS2368		1	/* sector size 2368  CD-DA read */
#define SCECdSecS2448		2	/* sector size 2448  CD-DA read */

/*
 * spindle control
 */
#define SCECdSpinMax		0	/* maximum speed	*/
#define SCECdSpinNom		1	/* optimized speed	*/
#define SCECdSpinX1             2	/* x1			*/
#define SCECdSpinX2             3	/* x2			*/
#define SCECdSpinX4             4	/* x4			*/
#define SCECdSpinX12            5	/* x12			*/
#define SCECdSpinNm2           10	/* optimized speed
					   (based on current speed) */
#define SCECdSpin1p6           11	/* DVD x1.6 CLV		*/
#define SCECdSpinMx            20	/* maximum speed	*/

/*
 * read mode
 */
typedef struct sceCdRMode {
	u_char trycount;	/* trycount   */
	u_char spindlctrl;	/* spindlctrl */
	u_char datapattern;	/* datapattern */
	u_char pad;		/* pad         */
} sceCdRMode;

/*
 *      stream command
 */
#define PS2CDVD_STREAM_START	1
#define PS2CDVD_STREAM_READ	2
#define PS2CDVD_STREAM_STOP	3
#define PS2CDVD_STREAM_SEEK	4
#define PS2CDVD_STREAM_INIT	5
#define PS2CDVD_STREAM_STAT	6
#define PS2CDVD_STREAM_PAUSE	7
#define PS2CDVD_STREAM_RESUME	8
#define PS2CDVD_STREAM_SEEKF	9

/*
 * disc type
 */
#define SCECdIllgalMedia 	0xff
#define SCECdDVDV		0xfe
#define SCECdCDDA		0xfd
#define SCECdPS2DVD		0x14
#define SCECdPS2CDDA		0x13
#define SCECdPS2CD		0x12
#define SCECdPSCDDA 		0x11
#define SCECdPSCD		0x10
#define SCECdUNKNOWN		0x05
#define SCECdDETCTDVDD		0x04
#define SCECdDETCTDVDS		0x03
#define SCECdDETCTCD		0x02
#define SCECdDETCT		0x01
#define SCECdNODISC 		0x00

typedef struct ps2cdvd_dastream_command {
    u_int command;
    u_int lbn;
    u_int sectors;
    void *buf;
    struct sceCdRMode rmode;
    int result;
} ps2cdvd_dastream_command;

typedef struct ps2cdvd_subchannel {
    int stat;
    u_char data[10];
    u_char reserved[2];
} ps2cdvd_subchannel;

typedef struct ps2cdvd_read
{
    u_int lbn;
    u_int sectors;
    void *buf;
} ps2cdvd_read;

typedef struct ps2cdvd_rcbyctl {
    int param;
    int stat;
} ps2cdvd_rcbyctl;

#define PS2CDVDIO_DASTREAM	_IOWR('V', 0, ps2cdvd_dastream_command)
#define PS2CDVDIO_READSUBQ	_IOWR('V', 1, ps2cdvd_subchannel)
#define PS2CDVDIO_GETDISCTYPE	_IOR('V', 2, int)
#define PS2CDVDIO_READMODE1	_IOW('V', 3, ps2cdvd_read)
#define PS2CDVDIO_RCBYCTL	_IOWR('V', 4, ps2cdvd_rcbyctl)
#define PS2CDVDIO_CHANGETRYCNT  _IO('V', 5)

#endif /* __ASM_PS2_CDROM_H */
