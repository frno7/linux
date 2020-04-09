// SPDX-License-Identifier: GPL-2.0
/*
 * Executable and linkable format (ELF)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#ifndef R5900CHECK_ELF_H
#define R5900CHECK_ELF_H

#include "types.h"

/**
 * elf_for_each_section - iterate over all ELF sections
 * @shdr: &struct elf32_shdr loop cursor
 * @ehdr: ELF header of module to iterate
 */
#define elf_for_each_section(shdr, ehdr)				\
	for ((shdr) = elf_first_section((ehdr));			\
	     (shdr);							\
	     (shdr) = elf_next_section((shdr), (ehdr)))

Elf_Shdr *elf_first_section(Elf_Ehdr *ehdr);
Elf_Shdr *elf_next_section(Elf_Shdr *shdr, Elf_Ehdr *ehdr);

#define elf_for_each_section_type(shdr, ehdr, type)			\
	elf_for_each_section ((shdr), (ehdr))				\
		if ((shdr)->sh_type != (type)) continue; else

#define elf_for_each_rel(shdr, ehdr)					\
	elf_for_each_section_type ((shdr), (ehdr), SHT_REL)

#define elf_for_each_symtab(shdr, ehdr)					\
	elf_for_each_section_type ((shdr), (ehdr), SHT_SYMTAB)

#define elf_for_each_sym(sym, shdr, ehdr)				\
	elf_for_each_symtab ((shdr), (ehdr))				\
		elf_for_each_ent ((sym), (shdr), (ehdr))

#define elf_for_each_ent(ent, shdr, ehdr)				\
	for ((ent) = elf_first_ent((shdr), (ehdr));			\
	     (ent);							\
	     (ent) = elf_next_ent((ent), (shdr), (ehdr)))

void *elf_first_ent(Elf_Shdr *shdr, Elf_Ehdr *ehdr);
void *elf_next_ent(void *ent, Elf_Shdr *shdr, Elf_Ehdr *ehdr);

bool elf_offset_in_section(Elf_Off offset, const Elf_Shdr *shdr);
bool elf_addr_in_section(Elf_Addr addr, const Elf_Shdr *shdr);

void *elf_ent_for_offset(Elf_Off offset, Elf_Ehdr *ehdr);
void *elf_ent_for_addr(Elf_Addr addr, Elf_Ehdr *ehdr);
Elf_Addr elf_addr_for_ent(void *ent, Elf_Ehdr *ehdr);

char *elf_strings(Elf_Ehdr *ehdr);

char *elf_section_name(Elf_Shdr *shdr, Elf_Ehdr *ehdr);

char *elf_symbol_for_addr(Elf_Addr addr, Elf_Ehdr *ehdr);

bool elf_identify(void *buffer, size_t size);

#endif /* R5900CHECK_ELF_H */
