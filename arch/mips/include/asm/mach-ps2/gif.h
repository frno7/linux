// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer interface (GIF)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef __ASM_MACH_PS2_GIF_H
#define __ASM_MACH_PS2_GIF_H

#include <asm/types.h>

#include <uapi/asm/gif.h>

#define GIF_CTRL	0x10003000	/* (W) GIF control */
#define GIF_MODE	0x10003010	/* (W) GIF mode */
#define GIF_STAT	0x10003020	/* (R) GIF status */

/*
 * These GIF registers are accessible only when stopped by the
 * &gif_ctrl.pse flag.
 */

#define GIF_TAG0	0x10003040	/* (R) GIF tag 31:0 */
#define GIF_TAG1	0x10003050	/* (R) GIF tag 63:32 */
#define GIF_TAG2	0x10003060	/* (R) GIF tag 95:54 */
#define GIF_TAG3	0x10003070	/* (R) GIF tag 127:96 */
#define GIF_CNT		0x10003080	/* (R) GIF transfer status counter */
#define GIF_P3CNT	0x10003090	/* (R) PATH3 transfer status counter */
#define GIF_P3TAG	0x100030a0	/* (R) PATH3 GIF tag value */

/**
 * struct gif_ctrl - GIF control register
 * @rst: GIF reset
 * @pse: temporary transfer stop
 *
 * Writing 1 to PSE temporarily stops GIF transfers and makes it possible
 * to read GIF registers for debugging. Writing 0 to PSE resumes transfers.
 */
struct gif_ctrl {
	u32 rst : 1;
	u32 : 2;
	u32 pse : 1;
	u32 : 28;
};

/**
 * gif_writel_ctrl - write word to the GIF_CTRL register
 * @value - 32-bit word to write
 */
void gif_writel_ctrl(u32 value);

/**
 * gif_write_ctrl - write structure to the GIF_CTRL register
 * @value - structure to write
 */
void gif_write_ctrl(struct gif_ctrl value);

/**
 * gif_reset- reset the GIF
 *
 * The reset includes a delay of 100 us.
 */
void gif_reset(void);

/**
 * gif_wait - is the GIF ready to transfer data?
 *
 * FIXME: Move to ps2fb.c
 *
 * Return: %true if ready to transfer data, otherwise %false
 */
bool gif_wait(void);

void gif_write(union gif_data *base_package, size_t package_count);

#endif /* __ASM_MACH_PS2_GIF_H */
