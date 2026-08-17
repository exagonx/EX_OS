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

/* =============================================================================
 * IL FORMATO ELF — la definizione unica
 *
 * ! Stava dentro loader/elf.c. E' salita qui il 17 agosto 2026 perche' i
 * lettori sono diventati due: elf.c per i programmi e lib.c per le librerie
 * condivise. Ricopiarla sarebbe stato ripetere, lo stesso giorno, l'errore
 * che aveva appena tolto redirezioni e ambiente alla shell per tre giorni.
 * ============================================================================= */
#define ELF_MAGIC0      0x7F
#define ELF_MAGIC1      'E'
#define ELF_MAGIC2      'L'
#define ELF_MAGIC3      'F'

/* e_ident indici */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4   /* 1=32bit, 2=64bit */
#define EI_DATA         5   /* 1=little-endian, 2=big-endian */
#define EI_VERSION      6   /* Deve essere 1 */

/* Valori e_type */
#define ET_EXEC         2   /* Eseguibile */
#define ET_DYN          3   /* Shared object / PIE */

/* Valore e_machine */
#define EM_386          3   /* Intel i386 */

/* Tipi Program Header (p_type) */
#define PT_NULL         0
#define PT_LOAD         1   /* Segmento da caricare */
#define PT_DYNAMIC      2   /* Informazioni dynamic linker */
#define PT_INTERP       3   /* Percorso dynamic linker */
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_TLS          7   /* Immagine di partenza delle variabili __thread */

/* Flag Program Header (p_flags) */
#define PF_X            0x1     /* Execute */
#define PF_W            0x2     /* Write */
#define PF_R            0x4     /* Read */

/* ELF Header (52 byte) */
typedef struct PACKED {
    uint8_t  e_ident[16];   /* Magic + class + data + version + padding */
    uint16_t e_type;        /* ET_EXEC o ET_DYN */
    uint16_t e_machine;     /* EM_386 */
    uint32_t e_version;     /* 1 */
    uint32_t e_entry;       /* Entry point virtuale */
    uint32_t e_phoff;       /* Offset program header table */
    uint32_t e_shoff;       /* Offset section header table (non usato) */
    uint32_t e_flags;       /* Flags specifici architettura */
    uint16_t e_ehsize;      /* Dimensione ELF header (52) */
    uint16_t e_phentsize;   /* Dimensione ogni program header (32) */
    uint16_t e_phnum;       /* Numero program headers */
    uint16_t e_shentsize;   /* Dimensione ogni section header */
    uint16_t e_shnum;       /* Numero section headers */
    uint16_t e_shstrndx;    /* Indice section header con nomi sezioni */
} Elf32Header;

/* Program Header (32 byte) */
typedef struct PACKED {
    uint32_t p_type;        /* Tipo segmento (PT_LOAD, ecc.) */
    uint32_t p_offset;      /* Offset nel file */
    uint32_t p_vaddr;       /* Indirizzo virtuale destinazione */
    uint32_t p_paddr;       /* Indirizzo fisico (ignorato) */
    uint32_t p_filesz;      /* Dimensione nel file */
    uint32_t p_memsz;       /* Dimensione in memoria (>= p_filesz, diff = BSS) */
    uint32_t p_flags;       /* Permessi: PF_R, PF_W, PF_X */
    uint32_t p_align;       /* Allineamento (deve essere potenza di 2) */
} Elf32Phdr;

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

/* =============================================================================
 * LIBRERIE CONDIVISE — kernel/loader/lib.c
 *
 * Carica la libreria (una volta per tutto il sistema) e la aggancia a `proc`.
 * Rende 0 e riempie *out_tabella con l'indirizzo della tabella di
 * esportazione, oppure un -errno. Il perche' sta tutto in lib.c.
 * ============================================================================= */
int32_t lib_apri(const char *percorso, Process *proc, uint32_t *out_tabella);

#endif /* ELF_H */
