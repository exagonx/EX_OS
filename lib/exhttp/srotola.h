/* =============================================================================
 * lib/exhttp/srotola.h
 * EX-OS — Extensible Operating System
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 * Srotola: the body of an answer with Content-Encoding gzip, deflate, br or
 * zstd, decoded as it arrives (debug/implementazione_BROWSER002.txt, P2).
 *
 * ! THE DECODERS ARE NOT OURS AND ARE NOT LINKED IN: zlib (libz.so.1),
 * Zstandard (libzstd.so.1) and Brotli (libbrotlidec.so.1), the EX-OS ports of
 * tools/<name>-exos, opened with dlopen the first time they are wanted. One
 * that is not there is a coding that is not asked for; exhttp.so has no new
 * DT_NEEDED and starts wherever it started before.
 *
 * ! ONLY exhttp.so HAS THIS FILE. netupdate compiles exhttp.c into itself
 * and not this: exhttp.c names these functions as weak symbols, and a
 * program without them never decodes (and never asks: see exhttp_srotola).
 * ============================================================================= */
#ifndef SROTOLA_H
#define SROTOLA_H

typedef struct Srotola Srotola;

/* Receives the decoded bytes. 0 = taken; anything else stops the decoding,
 * and srotola_dai returns SROTOLA_FERMATO. */
typedef int (*SrotolaScrivi)(void *dato, const unsigned char *d, unsigned int n);

#define SROTOLA_OK        0
#define SROTOLA_ROTTO   (-1)     /* corrupt data, or a window too large      */
#define SROTOLA_FERMATO (-2)     /* the receiver said no                     */

/* The codings that can be decoded here, as the value of Accept-Encoding:
 * "gzip, deflate, br, zstd" or less, "" if no library could be opened. */
const char *srotola_codifiche(void);

/* A decoder for the Content-Encoding value `codifica`. NULL with *rotto = 0
 * when there is nothing to decode ("", "identity"); NULL with *rotto = 1
 * when it is a coding this cannot decode (unknown, a list of two, or its
 * library missing). */
Srotola *srotola_apri(const char *codifica, int *rotto);

/* Feeds `n` bytes of the body as it came; the decoded ones go to `f`. */
int srotola_dai(Srotola *s, const unsigned char *in, unsigned int n,
                SrotolaScrivi f, void *dato);

/* 1 when the compressed stream has reached its own end. A body that ends
 * before that is a truncated body, whatever Content-Length said. */
int srotola_finito(const Srotola *s);

/* The name of the coding, for messages ("br"). */
const char *srotola_nome(const Srotola *s);

void srotola_chiudi(Srotola *s);

#endif /* SROTOLA_H */
