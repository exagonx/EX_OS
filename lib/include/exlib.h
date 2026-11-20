/* =============================================================================
 * lib/include/exlib.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * OPTIONAL LIBRARIES — open one, ask it for a name, carry on without it
 *
 * The EX-OS libraries (exwin.so, eximg.so, exfont.so, ...) are ordinary ELF
 * shared objects on the shared libc /lib/ld-exos.so (voce 105, 19 November
 * 2026). A program that cannot live without one names it at link time
 * (DT_NEEDED) and calls it like any function. These three calls are for the
 * OPTIONAL ones: a program that can show a BMP without eximg.so, or draw with
 * the bitmap font without exfont.so, opens them when it needs them.
 *
 * They are dlopen and dlsym under another name (lib/libc.c), and the handle
 * is dlopen's. Until 19 November 2026 a library was an ET_EXEC at its own
 * address whose entry point was a table of names (ExLibTesta), mapped by the
 * kernel (SYS_LIB_APRI): that is gone, and so are the table and its macro.
 *
 * ! A NAME CAN BE MISSING, and the caller MUST check: it is how a program
 * finds out that the library installed is older than itself.
 * ============================================================================= */

#ifndef EXLIB_H
#define EXLIB_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque: a dlopen handle. */
typedef struct ExLibTesta ExLibTesta;

/* The library at `percorso` (absolute, or a name searched as dlopen does),
 * or 0. Twice is harmless: the same handle. */
const ExLibTesta *exlib_apri(const char *percorso);

/* A name in it, or 0. */
void             *exlib_simbolo(const ExLibTesta *t, const char *nome);

/* The first of `percorsi` that opens: /exwin/lib on an installed system,
 * /cdrom/exwin/lib when started from the CD. */
const ExLibTesta *exlib_apri_fra(const char *const *percorsi, int quanti);

#ifdef __cplusplus
}
#endif

#endif /* EXLIB_H */
