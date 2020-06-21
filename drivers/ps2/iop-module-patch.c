// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 input/output processor (IOP) patch
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/io.h>	/* FIXME: For dma_cache_wback */

#include <uapi/asm/inst.h>

#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/iop-module-patch.h>

static bool __init iop_patch_match(const struct iop_module_info *module)
{
	static const __initconst struct {
		u32 address;
		u32 code;
	} match[] = {
		{ 0x4c4, 0x27bdffe8 }, /*    addiu   sp,sp,-24	*/
		{ 0x4c8, 0x2c820006 }, /*    sltiu   v0,a0,6	*/
		{ 0x4cc, 0x14400003 }, /*    bnez    v0,0x4dc	*/
		{ 0x4d0, 0xafbf0010 }, /*    sw      ra,16(sp)	*/
	     /* { 0x4d4, 0x0800013e },       j       0x4f8	*/
		{ 0x4d8, 0x00001021 }, /*    move    v0,zero	*/
		{ 0x4dc, 0x00041080 }, /*    sll     v0,a0,0x2	*/
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(match); i++) {
		const u32 *code = iop_bus_to_virt(
			module->text_start + match[i].address);

		if (match[i].code != *code)
			return false;
	}

	return true;
}

static s16 __init iop_simm_insn(
	const struct iop_module_info *module, const u32 address)
{
	const struct i_format *insn = iop_bus_to_virt(
		module->text_start + address);

	return insn->simmediate;
}

static void __init iop_patch_jump_table(
	const struct iop_module_info *loadfile, const u32 patch_address)
{
	struct u_format *sltiu = iop_bus_to_virt(
		loadfile->text_start + 0x4c8);		/* sltiu v0,a0,6     */
	const s16 hi = iop_simm_insn(loadfile, 0x4e0);  /* lui   at,0x0      */
	const s16 lo = iop_simm_insn(loadfile, 0x4e8);  /* lw    v0,7112(at) */
	const u32 jump_table = (hi << 16) + lo;
	u32 *patch_entry = iop_bus_to_virt(jump_table + 6 * sizeof(u32));

	*patch_entry = patch_address;	/* Assign 7:th jump table entry. */
	sltiu->uimmediate++;	/* Increase jump table from 6 to 7 entries. */
}

static void __init iop_patch_apply(
	const struct iop_module_info *loadfile,
	const struct iop_module_info *modload,
	const u32 patch_address)
{
	union {
		u32 word[32];
		union mips_instruction insn[32];
	} patch = {
		.word = {
		[ 0] =	0x27bdffd8, /*    addiu   sp,sp,-40		*/
		[ 1] =	0xafb00018, /*    sw      s0,24(sp)		*/
		[ 2] =	0xafbf0020, /*    sw      ra,32(sp)		*/
		[ 3] =	0x00808021, /*    move    s0,a0			*/
		[ 4] =	0x8c840000, /*    lw      a0,0(a0)		*/
		[ 5] =	0x0c000000, /*    jal     <LoadModuleBuffer>	*/
		[ 6] =	0xafb1001c, /*    sw      s1,28(sp)		*/
		[ 7] =	0x3c110000, /*    lui     s1,<HI16(result)>	*/
		[ 8] =	0x04400008, /*    bltz    v0,1f			*/
		[ 9] =	0x36310000, /*    ori     s1,s1,<LO16(result)>	*/
		[10] =	0x00402021, /*    move    a0,v0			*/
		[11] =	0x26250008, /*    addiu   a1,s1,8		*/
		[12] =	0x8e060004, /*    lw      a2,4(s0)		*/
		[13] =	0x26070104, /*    addiu   a3,s0,260		*/
		[14] =	0x26280004, /*    addiu   t0,s1,4		*/
		[15] =	0x0c000000, /*    jal     <StartModule>		*/
		[16] =	0xafa80010, /*    sw      t0,16(sp)		*/
		[17] =	0xae220000, /*1:  sw      v0,0(s1)		*/
		[18] =	0x02201021, /*    move    v0,s1			*/
		[19] =	0x8fbf0020, /*    lw      ra,32(sp)		*/
		[20] =	0x8fb1001c, /*    lw      s1,28(sp)		*/
		[21] =	0x8fb00018, /*    lw      s0,24(sp)		*/
		[22] =	0x03e00008, /*    jr      ra			*/
		[23] =	0x27bd0028, /*    addiu   sp,sp,40		*/
		}
	};

	/*
	 * Offsets 0x248 and 0x358 are exported by rom0:MODLOAD for the
	 * functions LoadModuleBuffer (id 10) and StartModule (id 8).
	 */
	const u32 modload_load_module_buffer = modload->text_start + 0x248;
	const u32 modload_start_module = modload->text_start + 0x358;
	const u32 result_address = patch_address + 24 * sizeof(u32);

	BUILD_BUG_ON(sizeof(patch) != 128);

	patch.insn[ 5].j_format.target = modload_load_module_buffer >> 2;
	patch.insn[ 7].u_format.uimmediate = result_address >> 16;
	patch.insn[ 9].u_format.uimmediate = result_address;
	patch.insn[15].j_format.target = modload_start_module >> 2;

	memcpy(iop_bus_to_virt(patch_address), &patch, sizeof(patch));

	iop_patch_jump_table(loadfile, patch_address);
}

static __init const struct iop_module_info *find_module(const char *name)
{
	const struct iop_module_info *module;

	iop_for_each_module (module)
		if (strcmp(name, iop_module_name(module)) == 0)
			return module;

	return NULL;
}

int __init iop_module_patch(void)
{
	const struct iop_module_info *loadfile = find_module("LoadModuleByEE");
	const struct iop_module_info *modload = find_module("Moldule_File_loader");
	u32 patch_address;

	if (!loadfile || !modload || !iop_patch_match(loadfile))
		return -ENOENT;

	/* Repurpose 128 bytes of the LOADFILE entry function for the patch. */
	patch_address = loadfile->entry;

	iop_patch_apply(loadfile, modload, patch_address);

	/* FIXME: Write back or use KSEG1? */
	dma_cache_wback((unsigned long)iop_bus_to_virt(0), 2*1024*1024);

	return 0;
}
