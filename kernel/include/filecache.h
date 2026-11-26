/* =============================================================================
 * kernel/include/filecache.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The pages of a file in one copy for the whole machine (kernel 0.369,
 * debug/bug_FIREFOX001 3.2). See filecache.c.
 * ============================================================================= */
#ifndef FILECACHE_H
#define FILECACHE_H

#include "kernel.h"

/* The frame of page `idx` of the file open on VFS handle `h`, shared by
 * every process that maps it, with one reference for the caller (give it
 * back with pmm_free_page). Only WHOLE pages: 0 for the last partial page,
 * past the end, a file with no identity, or no RAM - the caller then reads
 * a private page as before. */
/* The key of a file in the cache (0.371): read once (vfs_fstat), then the
 * lookups need no VFS. */
typedef struct {
    uint32_t ident, dim;
    int64_t  mtime;
} FcChiave;

int      fc_chiave(int h, FcChiave *k);

/* The page if the cache has it (count + 1), else 0. Never sleeps, no VFS. */
paddr_t  fc_c_e(const FcChiave *k, uint32_t idx);

/* Page idx (count + 1), reading the missing pages around it inside [a, z)
 * in one vfs_read. 0 = not a whole page, or no RAM / read error. */
paddr_t  fc_leggi(const FcChiave *k, int h, uint32_t idx, uint32_t a, uint32_t z);

paddr_t  fc_pagina(int h, uint32_t idx);

/* A page of the file for this caller alone: the bytes the file has at page
 * `idx` (the last partial page, or any page of a file with no identity),
 * zeros after them; all zeros past the end. 0 = no RAM, or a read error. */
paddr_t  fc_pagina_privata(int h, uint32_t idx);

/* Gives back up to `pagine` frames that only the cache holds (nobody maps
 * them now): they can be read again from the file. Returns how many. */
uint32_t fc_cedi(uint32_t pagine);

#endif /* FILECACHE_H */
