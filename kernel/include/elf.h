/* =============================================================================
 * kernel/include/elf.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef ELF_H
#define ELF_H

#include "kernel.h"
#include "sched.h"

/* Risultato del caricamento ELF */
typedef struct {
    uint32_t entry_point;       /* Indirizzo virtuale entry point */
    uint32_t user_stack_top;    /* Top stack utente (allineato 16 byte) */
} ElfLoadResult;

int elf_load(const char *path, Process *proc, ElfLoadResult *result);

#endif /* ELF_H */
