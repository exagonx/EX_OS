/* =============================================================================
 * lib/include/stdio_ext.h
 * EX-OS -- Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <stdio_ext.h> -- a look inside a FILE (18 November 2026, GNU nano). The
 * Solaris/glibc/musl set; gnulib (fseterr, fpending, freading, fpurge...)
 * uses these instead of poking the FILE's fields, which it does not know
 * on EX-OS ("Please port gnulib fseterr.c to your platform"). The code is
 * in lib/libc.c, next to clearerr.
 * ============================================================================= */
#ifndef EXOS_STDIO_EXT_H
#define EXOS_STDIO_EXT_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FSETLOCKING_QUERY     0
#define FSETLOCKING_INTERNAL  1
#define FSETLOCKING_BYCALLER  2

size_t __fbufsize(FILE *f);     /* the buffer's size */
size_t __fpending(FILE *f);     /* bytes written and not yet sent */
size_t __freadahead(FILE *f);   /* bytes read ahead and not yet consumed */
int    __freading(FILE *f);     /* read-only, or the last operation read */
int    __fwriting(FILE *f);     /* write-only, or the last operation wrote */
int    __freadable(FILE *f);
int    __fwritable(FILE *f);
int    __flbf(FILE *f);         /* line buffered */
void   __fpurge(FILE *f);       /* drops what is buffered, both ways */
void   __fseterr(FILE *f);      /* sets the error indicator */
int    __fsetlocking(FILE *f, int tipo);  /* always FSETLOCKING_INTERNAL */
void   _flushlbf(void);         /* flushes the line-buffered streams */

#ifdef __cplusplus
}
#endif

#endif
