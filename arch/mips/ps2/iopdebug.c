/*
 *  PlayStation 2 IOP Debugging using printk
 *
 *  Copyright (C) 2015-2015 Rick Gaiser
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

#include <linux/init.h>
#include <linux/module.h>

#include <asm/mach-ps2/sifdefs.h>
#include <asm/mach-ps2/sbios.h>
#include <asm/mach-ps2/iopmodules.h>


#define USE_MESSAGE_BUFFER
#define IOP_MAX_MSG_LEN 100


typedef struct {
       struct t_SifCmdHeader sifcmd;
       char text[IOP_MAX_MSG_LEN];
} iop_text_data_t;

#ifdef USE_MESSAGE_BUFFER
static char text[IOP_MAX_MSG_LEN];
static int writepos = 0;
static int msg_count = 0;
#endif


static void iopdebug_printk(iop_text_data_t *data, void *arg)
{
#ifdef USE_MESSAGE_BUFFER
	int i = 0;

	msg_count++;
	while (data->text[i] != 0) {
		char c = data->text[i++];
		text[writepos++] = c;

		/* Invalid message */
		if (i >= IOP_MAX_MSG_LEN-1) {
			pr_err("iopdebug: string not terminated\n");
			writepos = 0;
			break;
		}

		if (writepos >= IOP_MAX_MSG_LEN-1) {
			pr_err("iopdebug: overflow\n");
			writepos = 0;
		}

		if (c == '\n') {
			text[writepos] = 0;
			writepos = 0;

			pr_info("IOP%d: %s", msg_count, text);
		}
	}
#else
	pr_info("IOP: %s", data->text);
#endif
}

int __init iopdebug_init(void)
{
	/* Add SIF command handler for incoming debug messages */
        if (ps2sif_addcmdhandler(0x10, iopdebug_printk, NULL) < 0) {
		pr_err("iopdebug: Failed to add command handler.\n");
		return -1;
        }

	if (load_module_firmware("ps2/eedebug.irx", 0) < 0) {
		pr_err("iopdebug: loading ps2/eedebug.irx failed\n");
		return -1;
	}

	return 0;
}
