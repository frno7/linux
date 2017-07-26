/*
 *  PlayStation 2 CD/DVD driver
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

#ifndef PS2LIBCDVD_H
#define PS2LIBCDVD_H

/*
 * error code
 */
#define SCECdErFAIL		-1	/* can't get error code		*/
#define SCECdErNO		0x00	/* No Error			*/
#define SCECdErEOM		0x32	/* End of Media			*/
#define SCECdErTRMOPN		0x31	/* tray was opened while reading */
#define SCECdErREAD		0x30	/* read error			*/
#define SCECdErPRM		0x22	/* invalid parameter		*/
#define SCECdErILI		0x21	/* illegal length		*/
#define SCECdErIPI		0x20	/* illegal address		*/
#define SCECdErCUD		0x14	/* not appropreate for current disc */
#define SCECdErNORDY		0x13    /* not ready			*/
#define SCECdErNODISC		0x12	/* no disc			*/
#define SCECdErOPENS		0x11	/* tray is open			*/
#define SCECdErCMD		0x10	/* not supported command	*/
#define SCECdErABRT		0x01	/* aborted			*/

/*
 * spinup result
 */
#define SCECdComplete	0x02	/* Command Complete 	  */
#define SCECdNotReady	0x06	/* Drive Not Ready	  */

/*
 * media mode
 */
#define SCECdCD         1
#define SCECdDVD        2

/*
 * tray request
 */
#define SCECdTrayOpen   0       /* Tray Open  */
#define SCECdTrayClose  1       /* Tray Close */
#define SCECdTrayCheck  2       /* Tray Check */

#endif /* ! PS2LIBCDVD_H */
