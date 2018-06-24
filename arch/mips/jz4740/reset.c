// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
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
