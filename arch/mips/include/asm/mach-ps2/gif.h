// SPDX-License-Identifier: GPL-2.0
/* FIXME */

#ifndef __ASM_PS2_GIF_H
#define __ASM_PS2_GIF_H

#include <asm/types.h>

#include <uapi/asm/gif.h>

void gif_writel_ctrl(u32 value);
void gif_write_ctrl(struct gif_ctrl value);

void gif_writel_mode(u32 value);

u32 gif_readl_stat(void);

void gif_reset(void);

void gif_stop(void);
void gif_resume(void);

bool gif_ready(void);
void gif_read(union gif_data *base_package, size_t package_count);
void gif_write(union gif_data *base_package, size_t package_count);

#endif /* __ASM_PS2_GIF_H */
