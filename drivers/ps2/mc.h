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

#ifndef PS2MC_H
#define PS2MC_H

typedef struct {
	struct {
		unsigned char Resv2,Sec,Min,Hour;
		unsigned char Day,Month;
		unsigned short Year;
	} _Create;
	struct {
		unsigned char Resv2,Sec,Min,Hour;
		unsigned char Day,Month;
		unsigned short Year;
	} _Modify;
	unsigned FileSizeByte;
	unsigned short AttrFile;
	unsigned short Reserve1;
	unsigned Reserve2[2];
	unsigned char EntryName[32];
} McDirEntry __attribute__((aligned (64)));

#define McMaxFileDiscr		3
#define McMaxPathLen		1023

#define McRDONLY		0x0001
#define McWRONLY		0x0002
#define McRDWR			0x0003
#define McCREAT			0x0200

#define McFileInfoCreate	0x01
#define McFileInfoModify	0x02
#define McFileInfoAttr		0x04

#define McFileAttrReadable	0x0001
#define McFileAttrWriteable	0x0002
#define McFileAttrExecutable	0x0004
#define McFileAttrSubdir	0x0020

#define McTZONE			(9 * 60 * 60)

#endif /* PS2MC_H */
