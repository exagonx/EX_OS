/* =============================================================================
 * lib/include/elf.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <elf.h> — i tipi e le strutture del formato ELF a 32 bit, quello dei
 * programmi di EX-OS. Sono quelli della specifica (System V ABI): nomi e
 * campi uguali a glibc, perche' il codice di terzi li scrive cosi'. Gecko
 * ci mette il suo identificativo di build in una nota (Elf32_Nhdr).
 * ============================================================================= */

#ifndef EXOS_ELF_H
#define EXOS_ELF_H

#include <stdint.h>

typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Word;

#define EI_NIDENT   16
#define EI_CLASS    4
#define EI_DATA     5
#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define ET_EXEC     2
#define ET_DYN      3
#define EM_386      3

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

#define PT_NULL     0
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_PHDR     6
#define PT_TLS      7

typedef struct {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

typedef struct {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off  sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

#define STT_NOTYPE  0
#define STT_OBJECT  1
#define STT_FUNC    2
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define ELF32_ST_BIND(i)  ((i) >> 4)
#define ELF32_ST_TYPE(i)  ((i) & 0xf)

typedef struct {
    Elf32_Word    st_name;
    Elf32_Addr    st_value;
    Elf32_Word    st_size;
    unsigned char st_info;
    unsigned char st_other;
    Elf32_Half    st_shndx;
} Elf32_Sym;

#define NT_VERSION  1

typedef struct {
    Elf32_Word n_namesz;
    Elf32_Word n_descsz;
    Elf32_Word n_type;
} Elf32_Nhdr;

#endif /* EXOS_ELF_H */
