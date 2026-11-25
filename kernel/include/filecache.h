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
paddr_t  fc_pagina(int h, uint32_t idx);

/* Gives back up to `pagine` frames that only the cache holds (nobody maps
 * them now): they can be read again from the file. Returns how many. */
uint32_t fc_cedi(uint32_t pagine);

#endif /* FILECACHE_H */
