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

/* Carica l'eseguibile SU RICHIESTA: i segmenti si annotano e le pagine
 * arrivano al primo accesso. E' il modo giusto per i programmi. */
int elf_load(const char *path, Process *proc, ElfLoadResult *result);

/* =============================================================================
 * Come elf_load, ma impegnando `stack_extra` byte di stack IN PIU' dei
 * USER_STACK_INIT soliti.
 *
 * ! SERVE PERCHE' argv NON STA NELLO STACK CHE CRESCE SU FAULT. Le stringhe
 * della riga di comando le scrive sys_spawn con paging_get_physical, cioe'
 * leggendo la tabella delle pagine del figlio dall'esterno: una pagina non
 * ancora mappata non genera nessun fault che possa mapparla — non c'e'
 * nessun processo in esecuzione a cui il fault possa capitare — e la
 * scrittura fallisce e basta. Chi cresce su richiesta e' lo stack DEL
 * PROGRAMMA una volta partito; quello che gli si mette sotto i piedi prima
 * di farlo partire dev'esserci gia'.
 *
 * Otto kilobyte bastavano finche' la riga di comando piu' lunga era quella
 * di collect2. Una `ar rcs libfb.a <200 oggetti>` sono diecimila byte di soli
 * percorsi, e senza questo parametro il caricamento riusciva e lo spawn
 * moriva dopo, in spawn_write_user, con «stack non mappato per argv[137]»:
 * un messaggio che parla di paginazione mentre il difetto e' un tetto.
 * ============================================================================= */
int elf_load_argv(const char *path, Process *proc, ElfLoadResult *result,
                  uint32_t stack_extra);

/* Carica tutto in RAM subito. Serve a chi non puo' permettersi di
 * dipendere dal filesystem mentre gira — i driver. Vedi elf.c. */
int elf_load_residente(const char *path, Process *proc, ElfLoadResult *result);

#endif /* ELF_H */
