/* =============================================================================
 * lib/include/regex.h
 * EX-OS -- Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <regex.h> -- POSIX regular expressions (5 November 2026). The engine is in
 * lib/libc_regex.c, static libc only (ported software): BRE and ERE, the
 * classes [:alpha:]..., back references, REG_ICASE, REG_NEWLINE, REG_NOSUB;
 * leftmost-longest match. Not [. .] nor [= =] (REG_ECOLLATE): locale C.
 * The numbers are glibc's. ! KEEP IDENTICAL to the copy in lib/libc_regex.c.
 * ============================================================================= */
#ifndef EXOS_REGEX_H
#define EXOS_REGEX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int regoff_t;

typedef struct {
    size_t re_nsub;     /* the groups in the expression */
    void  *__exos_rx;   /* the compiled program */
} regex_t;

typedef struct {
    regoff_t rm_so;     /* -1: the group took no part */
    regoff_t rm_eo;
} regmatch_t;

/* regcomp */
#define REG_EXTENDED    1
#define REG_ICASE       2
#define REG_NEWLINE     4
#define REG_NOSUB       8
/* regexec */
#define REG_NOTBOL      1
#define REG_NOTEOL      2
/* the answers */
#define REG_NOMATCH     1
#define REG_BADPAT      2
#define REG_ECOLLATE    3
#define REG_ECTYPE      4
#define REG_EESCAPE     5
#define REG_ESUBREG     6
#define REG_EBRACK      7
#define REG_EPAREN      8
#define REG_EBRACE      9
#define REG_BADBR       10
#define REG_ERANGE      11
#define REG_ESPACE      12   /* also: the search took too many steps */
#define REG_BADRPT      13

#define RE_DUP_MAX      32767

int    regcomp(regex_t *re, const char *modello, int flags);
int    regexec(const regex_t *re, const char *testo, size_t nmatch, regmatch_t *pm, int eflags);
size_t regerror(int err, const regex_t *re, char *buf, size_t n);
void   regfree(regex_t *re);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_REGEX_H */
