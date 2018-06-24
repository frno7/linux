/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <asm/processor.h>
#include <asm/reboot.h>

#include "reset.h"

static void jz4740_halt(void)
{
	cpu_relax_forever();
}

void jz4740_reset_init(void)
{
	_machine_halt = jz4740_halt;
}
