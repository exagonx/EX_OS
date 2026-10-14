/* =============================================================================
 * lib/include/iconv.h — character set conversion (POSIX iconv)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * In the static libc.a only (lib/libc_iconv.c), like the signals: it is for
 * ported software, and the libc.so on the floppy has no room to spare.
 *
 * Encodings: UTF-8; UTF-16, UTF-16LE, UTF-16BE, UCS-2; UTF-32, UTF-32LE,
 * UTF-32BE, UCS-4, WCHAR_T; ASCII (US-ASCII, ANSI_X3.4-1968); ISO-8859-1
 * (LATIN1); ISO-8859-15 (LATIN9); WINDOWS-1252 (CP1252); CP437 (the PC
 * console's). Names are matched ignoring case, '-' and '_'. "" means UTF-8.
 * A "//TRANSLIT" suffix on the target writes '?' for what it cannot
 * represent; "//IGNORE" drops it. Without either, it stops with EILSEQ.
 * UTF-16 and UTF-32 without an order read a BOM (big endian if there is
 * none) and write one, little endian.
 * ============================================================================= */
#ifndef EXOS_ICONV_H
#define EXOS_ICONV_H

#include "libc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *iconv_t;

iconv_t iconv_open(const char *verso, const char *da);
size_t  iconv(iconv_t cd, char **in, size_t *in_n, char **out, size_t *out_n);
int     iconv_close(iconv_t cd);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_ICONV_H */
