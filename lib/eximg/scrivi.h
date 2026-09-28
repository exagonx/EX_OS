/* =============================================================================
 * lib/eximg/scrivi.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The image WRITERS: BMP and PNG (@PAINT, 28 September 2026)
 *
 * ! THEY ARE NOT IN eximg.so, AND ON PURPOSE. eximg.so is opened by the
 * toolkit in every program that shows a picture, and nobody but a paint
 * program saves one. This file is compiled INTO whoever writes, the same way
 * archivi compiles lib/exzip/deflate.c — and the PNG writer needs exactly
 * that deflate.c next to it.
 *
 * ! THE PIXELS ARE THE SAME AS eximg's: ARGB, 32 bits, EximgBitmap. What
 * eximg_carica() reads, these write back, and nothing is converted twice.
 *
 * ! THE BYTES GO OUT THROUGH A FUNCTION, not to a file descriptor: the same
 * code writes to a file on EX-OS and to a FILE* in the host test
 * (tools/prove/scrivi_prova.c), where the result is checked by a decoder that
 * is not ours.
 * ============================================================================= */
#ifndef EXIMG_SCRIVI_H
#define EXIMG_SCRIVI_H

#include "eximg.h"

/* Where the bytes go. Returns 1 if written, 0 on failure. */
typedef int (*EximgScrivi)(void *chi, const unsigned char *p, unsigned int n);

/* 24 bits, bottom-up, no compression: what every program on earth opens.
 * The alpha is dropped — BMP has no alpha that everyone agrees on.
 * 1 if everything was written. */
int eximg_scrivi_bmp(const EximgBitmap *bm, EximgScrivi scrivi, void *chi);

/* PNG, 8 bits per channel: RGB if every pixel is opaque, RGBA otherwise.
 * Each row gets the filter that makes it smallest (the usual heuristic: the
 * least sum of absolute differences), then DEFLATE.
 * 1 if everything was written, 0 on a write failure or with no memory.
 *
 * ! IT USES THE ONE DEFLATE STREAM THERE IS (see lib/exzip/deflate.h: one at
 * a time), so it must not be called while an archive is being written. */
int eximg_scrivi_png(const EximgBitmap *bm, EximgScrivi scrivi, void *chi);

/* True if the name ends in .png (any case): the format a paint program
 * chooses from the name the user typed. */
int eximg_nome_png(const char *nome);

#endif
