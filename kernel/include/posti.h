/* =============================================================================
 * kernel/include/posti.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * A table of slots that grows (kernel 0.343, voce 50: the mount tables were
 * VFS 6, ext2 4, FAT 4, ISO 2, against the user's rule "no fixed ceilings").
 *
 * The same scheme as the open-file table of vfs.c (0.282): an array of
 * POINTERS to slots. When every slot is taken the array doubles; the new
 * slots come zeroed from kmalloc, the old ones keep their address, so a
 * pointer to a mounted volume (VfsMount *, Ext2Mount *) stays good for as
 * long as the system runs. The old array is left in place, not freed: a
 * reader that loaded it a moment ago must not read freed memory. A few
 * pointers lost at each doubling; the ceiling is the kernel heap.
 *
 *     static Foo **volatile g_foo;
 *     static volatile int   g_foo_n;
 *     POSTI_CRESCI(foo_cresci, Foo, g_foo, g_foo_n, 4)
 *
 * foo_cresci() gives 0, or -1 with nothing changed when memory is short.
 * The caller holds whatever lock guards the table.
 * ============================================================================= */

#ifndef POSTI_H
#define POSTI_H

#include "kernel.h"
#include "kmalloc.h"

static inline void posti_azzera(void *p, size_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = 0;
}

#define POSTI_CRESCI(nome, T, dir, n, primi)                                  \
static int nome(void)                                                         \
{                                                                             \
    int  vecchi = (n), nuovi = vecchi ? vecchi * 2 : (primi), i;               \
    T  **d = (T **)kmalloc((size_t)nuovi * sizeof(T *));                      \
                                                                              \
    if (d == NULL) return -1;                                                 \
    for (i = 0; i < nuovi; i++) {                                             \
        if (i < vecchi) { d[i] = (dir)[i]; continue; }                        \
        d[i] = (T *)kmalloc(sizeof(T));                                       \
        if (d[i] == NULL) return -1;    /* d never published: a small leak */ \
        posti_azzera(d[i], sizeof(T));                                        \
    }                                                                         \
    __sync_synchronize();                                                     \
    (dir) = d;                          /* the old array stays: see above */  \
    __sync_synchronize();                                                     \
    (n) = nuovi;                                                              \
    return 0;                                                                 \
}

#endif
