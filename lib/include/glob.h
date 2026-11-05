/* =============================================================================
 * lib/include/glob.h
 * EX-OS -- Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <glob.h> -- the paths a shell pattern names (5 November 2026, Valkey's
 * "include /dir/<star>.conf", <star> the asterisk). The code is in lib/libc_glob.c, static libc only
 * (ported software). The flags are POSIX's; the numbers are not glibc's, and
 * need not be: they only travel between a program and its own libc.
 * ! KEEP IDENTICAL to the copy in lib/libc_glob.c.
 * ============================================================================= */
#ifndef EXOS_GLOB_H
#define EXOS_GLOB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t gl_pathc;    /* how many paths */
    char **gl_pathv;    /* the paths, after gl_offs NULLs, ending with NULL */
    size_t gl_offs;     /* NULLs to leave first (GLOB_DOOFFS) */
} glob_t;

#define GLOB_ERR        0x01    /* stop at a directory that cannot be read */
#define GLOB_MARK       0x02    /* a '/' after each directory */
#define GLOB_NOSORT     0x04
#define GLOB_DOOFFS     0x08
#define GLOB_NOCHECK    0x10    /* no match: the pattern itself */
#define GLOB_APPEND     0x20
#define GLOB_NOESCAPE   0x40

#define GLOB_NOSPACE    1
#define GLOB_ABORTED    2
#define GLOB_NOMATCH    3
#define GLOB_ABEND      GLOB_ABORTED

int  glob(const char *modello, int flags, int (*errfunc)(const char *, int), glob_t *g);
void globfree(glob_t *g);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_GLOB_H */
