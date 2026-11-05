/* =============================================================================
 * lib/include/fnmatch.h
 * EX-OS -- Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <fnmatch.h> -- a name against a shell pattern (5 November 2026). The code
 * is in lib/libc_glob.c, static libc only (ported software). No character
 * classes ([:alpha:]): such a set matches nothing. The numbers are glibc's.
 * ============================================================================= */
#ifndef EXOS_FNMATCH_H
#define EXOS_FNMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

#define FNM_NOMATCH     1
#define FNM_NOESCAPE    0x01
#define FNM_PATHNAME    0x02
#define FNM_PERIOD      0x04
#define FNM_CASEFOLD    0x10
#define FNM_FILE_NAME   FNM_PATHNAME

int fnmatch(const char *modello, const char *nome, int flags);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_FNMATCH_H */
