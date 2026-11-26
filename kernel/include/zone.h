/* =============================================================================
 * kernel/include/zone.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Zones of files mapped without writing, loaded at the first touch (kernel
 * 0.371, voce 120). See kernel/mm/zone.c.
 * ============================================================================= */
#ifndef ZONE_H
#define ZONE_H

#include "kernel.h"
#include "filecache.h"

struct Process;

typedef struct ProcZona {
    uint32_t vstart, vend;      /* pages [vstart, vend) of the space */
    uint32_t file_off;          /* the file's offset at vstart, page aligned */
    uint32_t pg_flags;          /* PG_PRESENT | PG_USER, never PG_WRITABLE */
    int      handle;            /* the file, held open (vfs_dup) */
    FcChiave chiave;            /* its key in filecache.c, read at mmap */
} ProcZona;

/* `pagine` pages at `va`, from offset `off` of the file open on `h` (kept with
 * a vfs_dup of its own). 0, or -ENOMEM. Any zone already there is replaced. */
int  zona_aggiungi(struct Process *p, uint32_t va, uint32_t pagine, int h,
                   uint32_t off, uint32_t pg_flags);

/* The zones over [va, va + pagine pages) go, those across the edges are cut. */
void zona_togli(struct Process *p, uint32_t va, uint32_t pagine);

/* All of them (exec, exit); zone_libera also frees the list (the PCB goes). */
void zone_tutte_via(struct Process *p);
void zone_libera(struct Process *p);

/* fork: the child gets the parent's zones, files included. 0 or -ENOMEM. */
int  zone_copia(struct Process *figlio, struct Process *padre);

/* The page at `va` if it is in a zone of `p`'s group: 1 mapped now (or
 * already there), 0 not a zone page or no frame. Sleeps on the disk. */
int  zona_carica(struct Process *p, uint32_t va);

#endif /* ZONE_H */
