/*
 *  PlayStation 2 Game Controller driver
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

#include <asm/io.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/isapnp.h>
#include <linux/stddef.h>
#include <linux/delay.h>

#include <linux/kernel.h>
#include <linux/input.h>

#include <linux/ps2/pad.h>

#include "pad.h"
#include "padcall.h"


#define PS2JS_REFRESH	HZ/50	/* Time between joystick polls [20 ms] */
#define PS2JS_AXES 	2
#define PS2JS_PADS 	10
#define PS2JS_LENS 	18
#define DIGITAL     	0
#define ANALOG      	1

char ps2name[PS2PAD_MAXNPADS][64];
struct ps2js *ps2js_ptr[PS2PAD_MAXNPADS];

static unsigned char ps2js_abs[] = { ABS_X, ABS_Y};
static short ps2js_btn_pad[] = { BTN_TL2, BTN_TR2, BTN_TL, BTN_TR, 
				 BTN_X, BTN_Y, BTN_B, BTN_A,  
				 BTN_START, BTN_SELECT };

struct ps2js {
	struct timer_list timer;
	long data;

	struct input_dev *dev;
	int reads;
	int used;
	unsigned char id;
	unsigned char length;
        int axes_X;
        int axes_Y;
        int analog_switch;
        int port;
        int slot;
};


static void ps2js_read(struct ps2js *ps2js)
{
	struct input_dev *dev = ps2js->dev;
	int j, res;
	u_char js_data[PS2PAD_DATASIZE];
	int new_dig_X, new_dig_Y;


	res = ps2padlib_GetState(ps2js->port,ps2js->slot);
	if (res != PadStateFindCTP1 && res != PadStateStable) {
		return;
	}

	res = ps2padlib_Read(ps2js->port, ps2js->slot, js_data);


	/* input key */
	for (j = 0; j < 8; j++)
		input_report_key(dev, ps2js_btn_pad[j], (~js_data[3] & (1 << j)) ? 1:0);
	input_report_key(dev, BTN_START,  (~js_data[2] & 0x08)?1:0);
	input_report_key(dev, BTN_SELECT, (~js_data[2] & 0x01)?1:0);
	

	/* input abs */
	new_dig_X = 128 + !(js_data[2] & 0x20) * 127 - !(js_data[2] & 0x80) * 128;
	new_dig_Y = 128 + !(js_data[2] & 0x40) * 127 - !(js_data[2] & 0x10) * 128;

	if ((js_data[1] & 0xf) <= 1) {     // digital input
	  	if (ps2js->analog_switch == ANALOG) {    // analog --> digital 
		  new_dig_X = 128;
		  new_dig_Y = 128;
		}
		input_report_abs(dev, ABS_X, new_dig_X);
		input_report_abs(dev, ABS_Y, new_dig_Y);
	  
		ps2js->axes_X = new_dig_X;
		ps2js->axes_Y = new_dig_Y;
		ps2js->analog_switch = DIGITAL;
	}
	else {                             // analog input
	  	if ((new_dig_X != ps2js->axes_X) || (new_dig_Y != ps2js->axes_Y)) {
			input_report_abs(dev, ABS_X, new_dig_X);
			input_report_abs(dev, ABS_Y, new_dig_Y);
		}
		else {
			input_report_abs(dev, ABS_X, js_data[6]);
			input_report_abs(dev, ABS_Y, js_data[7]);
		}

		ps2js->axes_X = new_dig_X;
		ps2js->axes_Y = new_dig_Y;
		ps2js->analog_switch = ANALOG;
	}
}


/*
 * ps2js_timer() reads and analyzes joystick data.
 */

static void ps2js_timer(struct timer_list *t)
{
	struct ps2js *ps2js = from_timer(ps2js, t, timer);

	ps2js->reads++;
	ps2js_read(ps2js);
	mod_timer(&ps2js->timer, jiffies + PS2JS_REFRESH);
}


static int ps2js_open(struct input_dev *dev)
{
	struct ps2js *ps2js = input_get_drvdata(dev);
	int i;

	if (!ps2js->used++) {
		mod_timer(&ps2js->timer, jiffies + PS2JS_REFRESH);	
		for (i = 0; i < PS2JS_AXES; i++)
			ps2js->dev->abs[ps2js_abs[i]] = 128;
	}
	/* TBD: MOD_INC_USE_COUNT; */

	return 0;
}


static void ps2js_close(struct input_dev *dev)
{
	struct ps2js *ps2js = input_get_drvdata(dev);

	/* TBD: MOD_DEC_USE_COUNT; */
	if (!--ps2js->used)
		del_timer(&ps2js->timer);
}


int __init ps2js_init(void)
{
	int i, npads;
	struct ps2js *ps2js;
	
	for (npads = 0; npads < ps2pad_npads; npads++) {
		if (!(ps2js_ptr[npads] = kmalloc(sizeof(struct ps2js), 
						 GFP_KERNEL)))
			return -ENOMEM;
		ps2js = ps2js_ptr[npads];
		memset(ps2js, 0, sizeof(struct ps2js));
		
		timer_setup(&ps2js->timer, ps2js_timer, 0);

		ps2js->port = ps2pad_pads[npads].port;
		ps2js->slot = ps2pad_pads[npads].slot;
			
		ps2js->data = (long) ps2js;
		ps2js->length = PS2JS_LENS;
		ps2js->dev = input_allocate_device();
		if (ps2js->dev == NULL) {
			printk(KERN_ERR "ps2js: Out of memory.\n");
			for (i = 0; i < npads; i++) {
				input_free_device(ps2js_ptr[i]->dev);
				kfree(ps2js_ptr[i]);
			}
			kfree(ps2js);
			return -ENOMEM;
		}
		input_set_drvdata(ps2js->dev, ps2js);
		ps2js->dev->open = ps2js_open;
		ps2js->dev->close = ps2js_close;
		ps2js->dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

		sprintf(ps2name[npads], "PS2PAD JoyStick(controller %d)",
			npads + 1);
		ps2js->dev->name = ps2name[npads];

		for (i = 0; i < BITS_TO_LONGS(KEY_MAX); i++)
			ps2js->dev->key[i] = 0;

		for (i = 0; i < PS2JS_AXES; i++)
			set_bit(ps2js_abs[i], ps2js->dev->absbit);

		for (i = 0; i < PS2JS_PADS; i++)
			set_bit(ps2js_btn_pad[i], ps2js->dev->keybit);

		for (i = 0; i < PS2JS_AXES; i++) {
  			ps2js->dev->abs[ps2js_abs[i]] = 128;
			ps2js->dev->absmax[ps2js_abs[i]] = 255;
			ps2js->dev->absmin[ps2js_abs[i]] = 1;
			ps2js->dev->absfuzz[ps2js_abs[i]] = 4;
			ps2js->dev->absflat[ps2js_abs[i]] = 0;
		}
		
		input_register_device(ps2js->dev);
	}
	return (0);
}


void
ps2js_cleanup(void)
{
	int npads;

	for (npads = 0; npads < ps2pad_npads; npads++) {
		input_unregister_device(ps2js_ptr[npads]->dev);
		input_free_device(ps2js_ptr[npads]->dev);
		ps2js_ptr[npads]->dev = NULL;
		kfree(ps2js_ptr[npads]);
	}
}

module_init(ps2js_init);
module_exit(ps2js_cleanup);

MODULE_AUTHOR("Sony Computer Entertainment Inc.");
MODULE_DESCRIPTION("PlayStation 2 Joystick driver");
MODULE_LICENSE("GPL");
