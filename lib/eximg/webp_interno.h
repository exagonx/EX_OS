/* =============================================================================
 * lib/eximg/webp_interno.h
 * EX-OS — Extensible Operating System
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * What webp.c and vp8.c share (@IMG-FORMATI, 30 September 2026).
 * ============================================================================= */
#ifndef WEBP_INTERNO_H
#define WEBP_INTERNO_H

/* Memory for the decoding in progress, from webp.c's arena: zeroed, given
 * back all together when eximg_carica ends. 0 if there is none. */
void *webp_prendi(unsigned int n);
void  webp_arena_azzera(void);

/* A lossy VP8 frame (the payload of a "VP8 " chunk): w x h ARGB pixels,
 * alpha 255, from webp_prendi. 1 if read. */
int webp_vp8(const unsigned char *d, unsigned int n,
             unsigned int *w, unsigned int *h, unsigned int **px);

/* A lossless stream; see webp.c. */
int webp_vp8l(const unsigned char *d, unsigned int n, int testa,
              unsigned int *w, unsigned int *h, unsigned int **out, int *alfa);

#endif /* WEBP_INTERNO_H */
