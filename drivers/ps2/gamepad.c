// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 DualShock gamepad
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>

static struct input_dev *gamepad_dev[2];
static const char *gamepad_names[ARRAY_SIZE(gamepad_dev)] = {
	"PlayStation 2 gamepad 1",
	"PlayStation 2 gamepad 2",
};

static const unsigned int ev_keys[] = {
	BTN_SELECT,
	KEY_RESERVED,
	KEY_RESERVED,
	BTN_START,
	BTN_DPAD_UP,
	BTN_DPAD_RIGHT,
	BTN_DPAD_DOWN,
	BTN_DPAD_LEFT,

	BTN_TL2,
	BTN_TR2,
	BTN_TL,
	BTN_TR,
	BTN_NORTH,	/* triangle */
	BTN_EAST,	/* circle */
	BTN_SOUTH,	/* cross */
	BTN_WEST,	/* square */
};

static void gamepad_event(const struct sif_cmd_header *header,
	const void *data, void *arg)
{
	const struct {
		u8 port, d0, d1;
	} *packet = data;
	const u32 d = packet->d0 | (packet->d1 << 8);
	size_t k;

	static u32 masks[ARRAY_SIZE(gamepad_dev)];	/* FIXME */

	if (packet->port >= ARRAY_SIZE(gamepad_dev))
		return;

	for (k = 0; k < ARRAY_SIZE(ev_keys); k++)
		if ((d ^ masks[packet->port]) & BIT(k))
			input_report_key(gamepad_dev[packet->port],
				ev_keys[k], (~d) & BIT(k));

	masks[packet->port] = d;

	input_sync(gamepad_dev[packet->port]);
}

static int __init gamepad_init(void)
{
	struct input_dev *unreg_dev = NULL;
	size_t i;
	int err;

	for (i = 0; i < ARRAY_SIZE(gamepad_dev); i++) {
		size_t k;

		unreg_dev = input_allocate_device();
		if (!unreg_dev) {
			err = -ENOMEM;
			goto init_err;
		}

		unreg_dev->name = gamepad_names[i];
		for (k = 0; k < ARRAY_SIZE(ev_keys); k++)
			input_set_capability(unreg_dev, EV_KEY, ev_keys[k]);

		err = input_register_device(unreg_dev);
		if (err)
			goto init_err;

		gamepad_dev[i] = unreg_dev;
		unreg_dev = NULL;
	}

	err = sif_request_cmd(SIF_CMD_GAMEPAD, gamepad_event, NULL);
	if (err)
		goto init_err;

	err = iop_module_request("gamepad", 0x0100, NULL);
	if (err < 0)
		goto init_err;

	return 0;

init_err:
	sif_request_cmd(SIF_CMD_GAMEPAD, NULL, NULL);

	if (unreg_dev)
		input_free_device(unreg_dev);

	for (i = 0; i < ARRAY_SIZE(gamepad_dev); i++)
		if (gamepad_dev[i])
			input_unregister_device(gamepad_dev[i]);

	return err;
}

module_init(gamepad_init);

MODULE_DESCRIPTION("PlayStation 2 DualShock gamepad");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
