/* =============================================================================
 * kernel/include/condivise.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The shared objects (ET_DYN) in one copy for the whole machine (kernel
 * 0.351, voce 105): see kernel/loader/condivise.c.
 * ============================================================================= */
#ifndef CONDIVISE_H
#define CONDIVISE_H

#include "kernel.h"
#include "idt.h"        /* InterruptFrame */

struct Process;

/* Where a shared object landed in a process. */
typedef struct {
    uint32_t base;      /* the address of its vaddr 0 */
    uint32_t inizio;    /* first mapped page (base + the lowest PT_LOAD) */
    uint32_t misura;    /* bytes from inizio to the end of the last page */
    uint32_t entry;     /* base + e_entry */
} SoEsito;

/* Maps the ET_DYN open on VFS handle `h` into `proc`: the read-only pages are
 * the cache's own frames, the writable ones a copy of them (or a promise for
 * pure .bss). `dove` != 0 puts the lowest page there (the interpreter);
 * 0 takes the space from the top of the heap, as mmap does. 0 or -errno. */
int32_t so_mappa(int h, const char *nome, struct Process *proc, uint32_t dove,
                 SoEsito *out);

/* SYS_SO_MAPPA (274): ebx = fd of a shared object open for reading,
 * ecx = SoEsito * filled in. 0 or -errno. */
int32_t sys_so_mappa_gruppo(InterruptFrame *f);

#endif /* CONDIVISE_H */
