// SPDX-License-Identifier: GPL-2.0

/*
 * The short loop bug under certain conditions causes loops to execute only
 * once or twice. The Gnu assembler (GAS) has the following note about it:
 *
 *     On the R5900 short loops need to be fixed by inserting a NOP in the
 *     branch delay slot.
 *
 *     The short loop bug under certain conditions causes loops to execute
 *     only once or twice. We must ensure that the assembler never
 *     generates loops that satisfy all of the following conditions:
 *
 *     - a loop consists of less than or equal to six instructions
 *       (including the branch delay slot);
 *     - a loop contains only one conditional branch instruction at the
 *       end of the loop;
 *     - a loop does not contain any other branch or jump instructions;
 *     - a branch delay slot of the loop is not NOP (EE 2.9 or later).
 *
 *     We need to do this because of a hardware bug in the R5900 chip.
 */

#include <stdlib.h>

#include "check.h"
#include "elf32.h"
#include "file.h"
#include "inst.h"
#include "print.h"

bool inst_branch_offset(int *offset, const union mips_instruction inst)
{
	if (inst.i_format.opcode == beq_op  ||
	    inst.i_format.opcode == beql_op ||
	    inst.i_format.opcode == bne_op  ||
	    inst.i_format.opcode == bnel_op) {
		if (offset)
			*offset = inst.i_format.simmediate;
		return true;
	}

	if (inst.i_format.opcode == bcond_op) {
		if (inst.i_format.rt == bgez_op    ||
		    inst.i_format.rt == bgezal_op  ||
		    inst.i_format.rt == bgezall_op ||
		    inst.i_format.rt == bgezl_op   ||
		    inst.i_format.rt == bltz_op    ||
		    inst.i_format.rt == bltzal_op  ||
		    inst.i_format.rt == bltzall_op ||
		    inst.i_format.rt == bltzl_op) {
			if (offset)
				*offset = inst.i_format.simmediate;
			return true;
		}
	}

	if (inst.i_format.rt == 0) {
		if ((inst.i_format.opcode == bgtz_op) ||
		    (inst.i_format.opcode == bgtzl_op) ||
		    (inst.i_format.opcode == blez_op) ||
		    (inst.i_format.opcode == blezl_op)) {
			if (offset)
				*offset = inst.i_format.simmediate;
			return true;
		}
	}

	return false;
}

bool inst_branch(const union mips_instruction inst)
{
	return inst_branch_offset(NULL, inst);
}

bool inst_jump(const union mips_instruction inst)
{
	if (inst.j_format.opcode == j_op ||
	    inst.j_format.opcode == jal_op)
		return true;

	if (inst.r_format.opcode == spec_op &&
	    inst.r_format.rt == 0 &&
	    inst.r_format.re == 0 &&
	    inst.r_format.func == jalr_op)
		return true;

	if (inst.r_format.opcode == spec_op &&
	    inst.r_format.rt == 0 &&
	    inst.r_format.rd == 0 &&
	    inst.r_format.re == 0 &&
	    inst.r_format.func == jr_op)
		return true;

	return false;
}

static bool inst_nop(const size_t i,
	const union mips_instruction *inst, const size_t inst_count)
{
	if (inst_count <= i)
		return false;

	// pr_info("%s %08x\n", __func__, inst[i].word);

	return inst[i].word == 0;
}

static bool inst_branch_or_jump(const ssize_t i,
	const union mips_instruction *inst, const size_t inst_count)
{
	if (i < 0 || inst_count <= i)
		return false;

	// pr_info("%s %08x\n", __func__, inst[i].word);

	return inst_branch(inst[i]) || inst_jump(inst[i]);
}

static bool short_loop_erratum(const size_t i,
	const union mips_instruction *inst, const size_t inst_count)
{
	int offset;

	if (inst_count <= i)
		return false;

	if (!inst_branch_offset(&offset, inst[i]))
		return false;

#if 0
	if (-10 <= offset && offset <= 0) {
		if (inst_nop(i + 1, inst, inst_count))
			printf("with NOP %d!\n", offset);
		else
			printf("not NOP %d!\n", offset);
	}
#endif

	if (offset <= -6 || 0 <= offset)
		return false;

	if (inst_nop(i + 1, inst, inst_count))
		return false;

	for (int k = offset + 1; k < 0; k++)
		if (inst_branch_or_jump((ssize_t)i + k, inst, inst_count))
			return false;

	// pr_info("\t%08x %d\n", inst[i].word, offset);

	return true;
}

static void pr_short_loop_erratum(const size_t i,
	const union mips_instruction *inst, const size_t inst_count,
	Elf_Shdr *shdr, Elf_Ehdr *ehdr, struct file file)
{
	int offset;

	if (inst_count <= i)
		return;

	if (!inst_branch_offset(&offset, inst[i]))
		return;

	printf("erratum shortloop path %s\n", file.path);

	for (int k = offset + 1; k <= 1; k++) {
		const ssize_t j = (ssize_t)i + k;
		const u32 addr = shdr->sh_addr + j * sizeof(*inst);

		printf("code %08x ", addr);

		if (j < 0 || inst_count <= j)
			printf(" -        -");
		else
			printf("%2d %08x", k, inst[j].word);

		printf("\n");
	}

	exit(EXIT_FAILURE);
}

static void check_text_section(Elf_Shdr *shdr,
	Elf_Ehdr *ehdr, struct file file)
{
	const union mips_instruction *inst =
		elf_ent_for_offset(shdr->sh_offset, ehdr);
	const size_t inst_count = shdr->sh_size / sizeof(*inst);
	size_t branch_count = 0;

	pr_info("section name %s\n", elf_section_name(shdr, ehdr));
	pr_info("section instruction count %zu\n", inst_count);

	for (size_t i = 0; i < inst_count; i++) {
		if (short_loop_erratum(i, inst, inst_count))
			pr_short_loop_erratum(i, inst, inst_count,
				shdr, ehdr, file);

		if (inst_branch(inst[i]))
			branch_count++;
	}

	pr_info("section branch count %zu\n", branch_count);
}

void check_file(struct file file)
{
	Elf_Ehdr *ehdr = file.data;
	Elf_Shdr *shdr;

	pr_info("check %s\n", file.path);

	if (!elf_identify(file.data, file.size)) {
		fprintf(stderr, "%s: not a valid ELF object\n", file.path);
		exit(EXIT_FAILURE);
	}

	elf_for_each_section_type (shdr, ehdr, SHT_PROGBITS)
		if (shdr->sh_flags & SHF_EXECINSTR)
			check_text_section(shdr, ehdr, file);
}
