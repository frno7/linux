// SPDX-License-Identifier: GPL-2.0
/*
 * Executable and linkable format (ELF)
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "elf.h"

bool elf_offset_in_section(Elf_Off offset, const Elf_Shdr *shdr)
{
	return shdr->sh_offset <= offset &&
		offset < shdr->sh_offset + shdr->sh_size;
}

bool elf_addr_in_section(Elf_Addr addr, const Elf_Shdr *shdr)
{
	return shdr->sh_addr <= addr &&
		addr < shdr->sh_addr + shdr->sh_size;

}

/**
 * elf_ent_for_offset - pointer given an ELF offset
 * @offset: ELF offset
 * @ehdr: ELF header of module
 *
 * Return: pointer for a given ELF offset
 */
void *elf_ent_for_offset(Elf_Off offset, Elf_Ehdr *ehdr)
{
	return &((u8 *)ehdr)[offset];
}

void *elf_ent_for_addr(Elf_Addr addr, Elf_Ehdr *ehdr)
{
	Elf_Shdr *shdr;

	elf_for_each_section (shdr, ehdr)
		if (shdr->sh_type == SHT_PROGBITS &&
		    elf_addr_in_section(addr, shdr)) {
			u8 *offset = elf_ent_for_offset(shdr->sh_offset, ehdr);
			return &offset[addr - shdr->sh_addr];
		}

	return NULL;
}

Elf_Addr elf_addr_for_ent(void *ent, Elf_Ehdr *ehdr)
{
	Elf_Off offset = (u8 *)ent - (u8 *)ehdr;
	Elf_Shdr *shdr;

	elf_for_each_section (shdr, ehdr)
		if (elf_offset_in_section(offset, shdr))
			return offset - shdr->sh_offset + shdr->sh_addr;

	return -1;
}

/**
 * elf_strings - base of ELF module string table
 * @ehdr: ELF header of module
 *
 * Return: pointer to base of ELF module table
 */
char *elf_strings(Elf_Ehdr *ehdr)
{
	if (ehdr->e_shstrndx == SHN_UNDEF)
		return NULL;

	Elf_Shdr *shdr = &elf_first_section(ehdr)[ehdr->e_shstrndx];

	return elf_ent_for_offset(shdr->sh_offset, ehdr);
}

/**
 * elf_section_name - name of section
 * @shdr: header of section to provide name for
 * @ehdr: ELF header of module for section
 *
 * Return: name of given section
 */
char *elf_section_name(Elf_Shdr *shdr, Elf_Ehdr *ehdr)
{
	return &elf_strings(ehdr)[shdr->sh_name];
}

/**
 * elf_first_section - first ELF section
 * @ehdr: ELF header of module
 *
 * Return: pointer to the first ELF section, or %NULL if it does not exist
 */
Elf_Shdr *elf_first_section(Elf_Ehdr *ehdr)
{
	return ehdr->e_shnum ? elf_ent_for_offset(ehdr->e_shoff, ehdr) : NULL;
}

/**
 * elf_next_section - next ELF section
 * @shdr: header of current section
 * @ehdr: ELF header of module
 *
 * Return: section following the current section, or %NULL
 */
Elf_Shdr *elf_next_section(Elf_Shdr *shdr, Elf_Ehdr *ehdr)
{
	Elf_Shdr *next = &shdr[1];
	Elf_Shdr *past = &elf_first_section(ehdr)[ehdr->e_shnum];

	return next == past ? NULL: next;
}

void *elf_first_ent(Elf_Shdr *shdr, Elf_Ehdr *ehdr)
{
	return shdr->sh_size >= shdr->sh_entsize ?
		elf_ent_for_offset(shdr->sh_offset, ehdr) : NULL;
}

void *elf_next_ent(void *ent, Elf_Shdr *shdr, Elf_Ehdr *ehdr)
{
	u8 *this = ent;
	u8 *next = &this[shdr->sh_entsize];
	u8 *past = elf_ent_for_offset(shdr->sh_offset + shdr->sh_size, ehdr);

	return &next[shdr->sh_entsize] > past ? NULL : next;
}

char *elf_symbol_for_addr(Elf_Addr addr, Elf_Ehdr *ehdr)
{
	Elf_Shdr *shdr;
	Elf_Sym *sym;

	elf_for_each_sym (sym, shdr, ehdr)
		if (sym->st_value == addr) {
			Elf_Shdr *strhdr =
				&elf_first_section(ehdr)[shdr->sh_link];
			char *strings =
				elf_ent_for_offset(strhdr->sh_offset, ehdr);

			return &strings[sym->st_name];
		}

	return NULL;
}

/**
 * elf_identify - does the buffer contain an ELF object?
 * @buffer: pointer to data to identify
 * @size: size in bytes of buffer
 *
 * Return: %true if the buffer looks like an ELF object, otherwise %false
 */
bool elf_identify(void *buffer, size_t size)
{
	const Elf_Ehdr *ehdr = buffer;

	if (size < sizeof(*ehdr))
		return false;

	return ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
	       ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
	       ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
	       ehdr->e_ident[EI_MAG3] == ELFMAG3 &&
	       ehdr->e_ident[EI_VERSION] == EV_CURRENT;
}
