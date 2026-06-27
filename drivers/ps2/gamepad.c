// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Dual Shock gamepad
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <asm/mach-ps2/gamepad.h>
#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/sif.h>

static const unsigned int ev_keys[] = { GAMEPAD_INPUT_EV_KEYS };

struct gamepad_controllers {
	struct gamepad_controller {
		struct input_dev *dev;
		char name[64];

		enum {
			GAMEPAD_CMD_NONE,
			GAMEPAD_CMD_REGISTER,
			GAMEPAD_CMD_UNREGISTER,
		} cmd;
		struct work_struct work;

		struct {
			int index;
		} port;

		struct gamepad_controller_digital {
			u32 mask;
		} digital;

		struct gamepad_controller_state state;
	} ctrl[GAMEPAD_PORTS];
};

static bool gamepad_device_mode_is_digital(
	const struct gamepad_controller_state *state)
{
	return state->device.mode == 0x41;
}

static bool gamepad_device_mode_is_analog(
	const struct gamepad_controller_state *state)
{
	return state->device.mode == 0x73 ||
	       state->device.mode == 0x79;
}

static bool gamepad_device_is_dualshock(
	const struct gamepad_controller_state *state)
{
	return (gamepad_device_mode_is_digital(state) ||
		gamepad_device_mode_is_analog(state)) &&
	       state->model.type == 1 &&
	       state->model.modes == 2 &&
	       state->model.actuators == 2;
}

static bool gamepad_device_is_dualshock2(
	const struct gamepad_controller_state *state)
{
	return (gamepad_device_mode_is_digital(state) ||
		gamepad_device_mode_is_analog(state)) &&
	       state->model.type == 3 &&
	       state->model.modes == 2 &&
	       state->model.actuators == 2;
}

static enum gamepad_device_id gamepad_device_id(
	const struct gamepad_controller_state *state)
{
	return gamepad_device_is_dualshock(state)  ? GAMEPAD_DEVICE_DUALSHOCK  :
	       gamepad_device_is_dualshock2(state) ? GAMEPAD_DEVICE_DUALSHOCK2 :
						     GAMEPAD_DEVICE_UNDEFINED;
}

static const char *gamepad_device_name(
	const struct gamepad_controller_state *state)
{
	switch (gamepad_device_id(state)) {
	default:
#define GAMEPAD_DEVICE_CASE(id, name)					\
	case GAMEPAD_DEVICE_ ## id: return name;
GAMEPAD_DEVICES(GAMEPAD_DEVICE_CASE)
	}
}

static inline int port_id(const struct gamepad_controller *ctrl)
{
	return 1 + ctrl->port.index;
}

struct gamepad_rumble_data { struct gamepad_controller *ctrl; };

static int gamepad_rumble(struct input_dev *dev,
	void *data, struct ff_effect *effect)
{
	struct gamepad_rumble_data *rumble_data = data;
	struct gamepad_controller *ctrl = rumble_data->ctrl;

	switch (effect->type) {
	case FF_RUMBLE: {
		const struct gamepad_sif_opt opt = {
			.op = gamepad_rop_rumble,
			.data = (struct gamepad_cmd_rumble) {
				.small = effect->u.rumble.weak_magnitude > 0,
				.large = effect->u.rumble.strong_magnitude >> 8,
				.index = ctrl->port.index,
			}.raw,
		};

		int err = sif_cmd_opt(SIF_CMD_GAMEPAD, opt.raw, NULL, 0);
		if (err < 0)
			pr_err("%s: sif_cmd_opt failed with %d\n", __func__, err);
		break;
	}
	default:
		break;
	}

	return 0;
}

static void register_dualshock(struct gamepad_controller *ctrl)
{
	input_set_abs_params(ctrl->dev, ABS_X,  -128, 127, 0, 0);
	input_set_abs_params(ctrl->dev, ABS_Y,  -128, 127, 0, 0);
	input_set_abs_params(ctrl->dev, ABS_RX, -128, 127, 0, 0);
	input_set_abs_params(ctrl->dev, ABS_RY, -128, 127, 0, 0);
}

static void register_rumble(struct gamepad_controller *ctrl)
{
	int err;

	/*
	 * ml_ff_destroy() calls kfree() on rumble_data,
	 * so it must be allocated with kmalloc.
	 */
	struct gamepad_rumble_data *rumble_data =
		kmalloc(sizeof(struct gamepad_rumble_data), GFP_KERNEL);
	if (!rumble_data) {
		pr_err("%s: kmalloc failed\n", __func__);
		return;
	}
	*rumble_data = (struct gamepad_rumble_data) { .ctrl = ctrl };

	input_set_capability(ctrl->dev, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(ctrl->dev, rumble_data, gamepad_rumble);
	if (err < 0)
		pr_err("gamepad: input_ff_create_memless failed with %d\n", err);
}

static bool gamepad_register(struct gamepad_controller *ctrl)
{
	const int device_id = gamepad_device_id(&ctrl->state);
	size_t k;
	int err;

	if (ctrl->dev)
		return true;

	ctrl->dev = input_allocate_device();
	if (!ctrl->dev) {
		pr_err("gamepad: Failed to allocate device\n");
		goto err;
	}

	if (device_id != GAMEPAD_DEVICE_UNDEFINED)
		snprintf(ctrl->name, ARRAY_SIZE(ctrl->name),
			"PlayStation 2 gamepad port %d %s",
			port_id(ctrl), gamepad_device_name(&ctrl->state));
	else
		snprintf(ctrl->name, ARRAY_SIZE(ctrl->name),
			"PlayStation 2 gamepad port %d", port_id(ctrl));
	ctrl->dev->name = ctrl->name;

	for (k = 0; k < ARRAY_SIZE(ev_keys); k++)
		input_set_capability(ctrl->dev, EV_KEY, ev_keys[k]);

	if (gamepad_device_is_dualshock(&ctrl->state) ||
	    gamepad_device_is_dualshock2(&ctrl->state))
		register_dualshock(ctrl);

	if (ctrl->state.model.actuators > 0)
		register_rumble(ctrl);

	err = input_register_device(ctrl->dev);
	if (err) {
		pr_err("gamepad: Failed to register device\n");
		goto err;
	}

	return true;

err:
	input_free_device(ctrl->dev);

	ctrl->dev = NULL; // FIXME: Clear rest

	return false;
}

static void gamepad_unregister(struct gamepad_controller *ctrl)
{
	if (!ctrl->dev)
		return;

	input_unregister_device(ctrl->dev);

	ctrl->dev = NULL;
	ctrl->name[0] = '\0';
	ctrl->digital = (struct gamepad_controller_digital) { };
	ctrl->state   = (struct gamepad_controller_state)   { };
}

static void gamepad_work(struct work_struct *work)
{
	struct gamepad_controller *ctrl =
		container_of(work, struct gamepad_controller, work);

	switch (ctrl->cmd) {
	case GAMEPAD_CMD_NONE:                                 break;
	case GAMEPAD_CMD_REGISTER:   gamepad_register(ctrl);   break;
	case GAMEPAD_CMD_UNREGISTER: gamepad_unregister(ctrl); break;
	}

	ctrl->cmd = GAMEPAD_CMD_NONE;
}

static inline bool gamepad_device_connected(
	const struct gamepad_controller_state *state)
{
	return state->device.mode != 0;
}

static void report_dualshock2(struct gamepad_controller *ctrl,
	const struct gamepad_controller_state *state)
{
	if (!gamepad_device_mode_is_analog(state))
		return;

	input_report_abs(ctrl->dev, ABS_X,  (int)state->analog.pad.lx - 128);
	input_report_abs(ctrl->dev, ABS_Y,  (int)state->analog.pad.ly - 128);
	input_report_abs(ctrl->dev, ABS_RX, (int)state->analog.pad.rx - 128);
	input_report_abs(ctrl->dev, ABS_RY, (int)state->analog.pad.ry - 128);
}

static void gamepad_event(const struct sif_cmd_header *header, void *arg)
{
	struct gamepad_controllers *ctrls = arg;

	const struct gamepad_controller_state *state = sif_cmd_payload(header);
	const u32 dm = state->digital.byte[0] |
		      (state->digital.byte[1] << 8);
	size_t k;

	struct gamepad_controller *ctrl;

	if (state->port.index >= GAMEPAD_PORTS) {
		pr_err("gamepad: Port %d is out of bounds %d\n",
			1 + state->port.index, GAMEPAD_PORTS);
		return;
	}
	ctrl = &ctrls->ctrl[state->port.index];

	if (ctrl->cmd) {
		pr_warn("gamepad: Port %d event skipped, command %d is already queued\n",
			port_id(ctrl), ctrl->cmd);
		return;
	}
	if (!ctrl->dev) {
		if (gamepad_device_connected(state)) {
			ctrl->cmd = GAMEPAD_CMD_REGISTER;
			ctrl->state = *state;

			schedule_work(&ctrl->work);
		}
		return;
	}
	if (!gamepad_device_connected(state)) {
		ctrl->cmd = GAMEPAD_CMD_UNREGISTER;
		schedule_work(&ctrl->work);
		return;
	}

	for (k = 0; k < ARRAY_SIZE(ev_keys); k++)
		if ((dm ^ ctrl->digital.mask) & BIT(k))
			input_report_key(ctrl->dev,
				ev_keys[k], (~dm) & BIT(k));

	if (gamepad_device_is_dualshock2(state))
		report_dualshock2(ctrl, state);

	ctrl->digital.mask = dm;
	ctrl->state = *state;

	input_sync(ctrl->dev);

	return;
}

static int __init gamepad_init(void)
{
	static struct gamepad_controllers ctrls = { };

	int err;
	int i;

	for (i = 0; i < GAMEPAD_PORTS; i++) {
		ctrls.ctrl[i].port.index = i;

		INIT_WORK(&ctrls.ctrl[i].work, gamepad_work);
	}

	err = sif_request_cmd(SIF_CMD_GAMEPAD, gamepad_event, &ctrls);
	if (err)
		goto err;

	err = iop_module_request("gamepad", 0x0100, NULL);
	if (err < 0)
		goto err;

	return 0;

err:
	sif_request_cmd(SIF_CMD_GAMEPAD, NULL, NULL);

	for (i = 0; i < GAMEPAD_PORTS; i++)
		cancel_work_sync(&ctrls.ctrl[i].work);

	return err;
}

module_init(gamepad_init);

MODULE_DESCRIPTION("PlayStation 2 gamepad");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL");
