// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 gamepad controller
 *
 * Copyright (C) 2026 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_GAMEPAD_H
#define __ASM_MACH_PS2_GAMEPAD_H

#include <asm/types.h>

enum { GAMEPAD_PORTS = 2 };

#define GAMEPAD_DEVICES(d)						\
	d(UNDEFINED,  "undefined")					\
	d(DUALSHOCK2, "DualShock 2")

enum gamepad_device_id {
#define GAMEPAD_DEVICE_ENUM(id, name) GAMEPAD_DEVICE_ ## id,
GAMEPAD_DEVICES(GAMEPAD_DEVICE_ENUM)
};

struct gamepad_digital_pad {
	struct {
		u8 select : 1;
		u8 thumbl : 1;
		u8 thumbr : 1;
		u8 start : 1;
		u8 up : 1;
		u8 right : 1;
		u8 down : 1;
		u8 left : 1;
	};
	struct {
		u8 l2 : 1;
		u8 r2 : 1;
		u8 l1 : 1;
		u8 r1 : 1;
		u8 triangle : 1;
		u8 circle : 1;
		u8 cross : 1;
		u8 square : 1;
	};
};

struct gamepad_analogue_pad {
	u8 rx;
	u8 ry;
	u8 lx;
	u8 ly;

	u8 right;
	u8 left;
	u8 up;
	u8 down;

	u8 triangle;
	u8 circle;
	u8 cross;
	u8 square;

	u8 l1;
	u8 r1;
	u8 l2;
	u8 r2;
};

struct gamepad_controller_state {
	struct {
		u8 index;
	} port;

	struct {
		u8 mode;
	} device;

	struct {
		u8 type;
		u8 modes;
		u8 mode;
		u8 actuators;
	} model;

	union {
		struct gamepad_digital_pad pad;
		u8 byte[sizeof(struct gamepad_digital_pad)];
	} digital;

	union {
		struct gamepad_analogue_pad pad;
		u8 byte[sizeof(struct gamepad_analogue_pad)];
	} analog;
};

#define GAMEPAD_INPUT_EV_KEYS		\
	BTN_SELECT,			\
	BTN_THUMBL,			\
	BTN_THUMBR,			\
	BTN_START,			\
	BTN_DPAD_UP,			\
	BTN_DPAD_RIGHT,			\
	BTN_DPAD_DOWN,			\
	BTN_DPAD_LEFT,			\
					\
	BTN_TL2,			\
	BTN_TR2,			\
	BTN_TL,				\
	BTN_TR,				\
	BTN_NORTH,	/* triangle */	\
	BTN_EAST,	/* circle */	\
	BTN_SOUTH,	/* cross */	\
	BTN_WEST,	/* square */

/**
 * enum iop_gamepad_ops - IOP gamepad remote operations
 * @rop_rumble: activate rumble for a given controller
 */
enum iop_gamepad_rops {
	gamepad_rop_rumble = 0,
};

struct gamepad_sif_opt {
	union {
		u32 raw;
		struct {
			u32 op : 3;
			u32 data : 29;
		};
	};
};

struct gamepad_cmd_rumble {
	union {
		u32 raw;
		struct {
			u32 small : 8;
			u32 large : 8;
			u32 index : 4;
			u32 : 12;
		};
	};
};

#endif /* __ASM_MACH_PS2_GAMEPAD_H */
