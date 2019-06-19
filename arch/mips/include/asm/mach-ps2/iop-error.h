// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) error codes
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_IOP_ERROR_H
#define __ASM_MACH_PS2_IOP_ERROR_H

#define IOP_ERRORS(E)							\
	E(BADEXCE,	50,	EINVAL,	"Bad exception")		\
	E(NOEXCE,	51,	ENOENT,	"Exception not found")		\
	E(BUSYEXCE,	52,	EBUSY,	"Exception in use")		\
									\
	E(INIRQ,	100,	EPERM,	"In IRQ context")		\
	E(BADIRQ,	101,	EINVAL,	"Bad IRQ")			\
	E(CPUINTD,	102,	EPERM,	"CPU interrupts disabled")	\
	E(INTD,		103,	EPERM,	"Interrupts disabled")		\
	E(BUSYHAND,	104,	EBUSY,	"Handler in use")		\
	E(NOHAND,	105,	ENOENT,	"Handler not found")		\
									\
	E(NOTIMER,	150,	ENOENT,	"Timer not found")		\
	E(BADTIMER,	151,	EINVAL,	"Bad timer")			\
									\
	E(BUSYUNIT,	160,	EBUSY,	"Unit in use")			\
	E(NOUNIT,	161,	ENOENT,	"Unit not found")		\
	E(NOROMDIR,	162,	ENOENT,	"ROM directory not found")	\
									\
	E(LINK,		200,	ENOEXEC, "Module linking error")	\
	E(BADOBJ,	201,	ENOEXEC, "Object not module")		\
	E(NOMOD,	202,	ENOENT,	"Module not found")		\
	E(NOENT,	203,	ENOENT,	"No such file")			\
	E(FILE,		204,	EIO,	"File error")			\
	E(BUSYMEM,	205,	EBUSY,	"Memory in use")		\
									\
	E(NOMEM,	400,	ENOMEM,	"Out of memory")		\
	E(BADATTR,	401,	EINVAL,	"Bad attribute")		\
	E(BADENTRY,	402,	EINVAL,	"Bad entry")			\
	E(BADPRIO,	403,	EINVAL,	"Bad priority")			\
	E(BADSTSZ,	404,	EINVAL,	"Bad stack size")		\
	E(BADMODE,	405,	EINVAL,	"Bad mode")			\
	E(BADTHR,	406,	EINVAL,	"Bad thread")			\
	E(NOTHR,	407,	ESRCH,	"Thread not found")		\
	E(NOSEM,	408,	ENOENT,	"Semaphore not found")		\
	E(NOEVF,	409,	ENXIO,	"ENOEVF")	/* FIXME */	\
	E(NOMBX,	410,	ENXIO,	"ENOMBX")	/* FIXME */	\
	E(NOVPL,	411,	ENXIO,	"ENOVPL")	/* FIXME */	\
	E(NOFPL,	412,	ENXIO,	"ENOFPL")	/* FIXME */	\
	E(DORM,		413,	EINVAL,	"EDORM")	/* FIXME */	\
	E(NODORM,	414,	ENOENT,	"ENODORM")	/* FIXME */	\
	E(NOSUSP,	415,	ENOENT,	"ENOSUSP")	/* FIXME */	\
	E(BADWAIT,	416,	EINVAL,	"EBADWAIT")	/* FIXME */	\
	E(NOWAIT,	417,	ENOENT,	"ENOWAIT")	/* FIXME */	\
	E(RELWAIT,	418,	EINVAL,	"ERELWAIT")	/* FIXME */	\
	E(SEMZERO,	419,	EINVAL,	"ESEMZERO")	/* FIXME */	\
	E(SEMOVF,	420,	EINVAL,	"ESEMOVF")	/* FIXME */	\
	E(EVFCOND,	421,	EINVAL,	"EEVFCOND")	/* FIXME */	\
	E(EVFMULTI,	422,	EINVAL,	"EEVFMULTI")	/* FIXME */	\
	E(EVFILPAT,	423,	EINVAL,	"EEVFILPAT")	/* FIXME */	\
	E(MBOXNOMSG,	424,	ENOENT,	"EMBOXNOMSG")	/* FIXME */	\
	E(WAITDEL,	425,	EINVAL,	"EWAITDEL")	/* FIXME */	\
	E(INVMEMBLK,	426,	EINVAL,	"EINVMEMBLK")	/* FIXME */	\
	E(INVMEMSZ,	427,	ENOSPC,	"EINVMEMSZ")	/* FIXME */

enum iop_error {
#define IOP_ERROR_ENUM(identifier, number, errno, description)		\
	IOP_E##identifier = number,
	IOP_ERRORS(IOP_ERROR_ENUM)
};

int errno_for_iop_error(int ioperr);

const char *iop_error_message(int ioperr);

#endif /* __ASM_MACH_PS2_IOP_ERROR_H */
