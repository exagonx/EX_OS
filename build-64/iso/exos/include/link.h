/* =============================================================================
 * lib/include/link.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <link.h> — gli oggetti ELF caricati nel processo, con dl_iterate_phdr.
 *
 * ! I PROGRAMMI DI EX-OS SONO STATICI: non c'e' un caricatore dinamico che
 * tenga un elenco di oggetti. dl_iterate_phdr non chiama mai la funzione e
 * rende 0; chi la usa per sapere che librerie ci sono (i rapporti di memoria
 * di Gecko) riceve la verita': nessuna. Solo in libc.a.
 * ============================================================================= */

#ifndef EXOS_LINK_H
#define EXOS_LINK_H

#include "libc.h"
#include "elf.h"

#ifdef __cplusplus
extern "C" {
#endif


#define ElfW(tipo) Elf32_##tipo

struct dl_phdr_info {
    Elf32_Addr        dlpi_addr;
    const char       *dlpi_name;
    const Elf32_Phdr *dlpi_phdr;
    Elf32_Half        dlpi_phnum;
};

int dl_iterate_phdr(int (*funzione)(struct dl_phdr_info *info, size_t dim, void *dato),
                    void *dato);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_LINK_H */
