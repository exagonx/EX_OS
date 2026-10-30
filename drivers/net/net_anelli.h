/* =============================================================================
 * drivers/net/net_anelli.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * How big a network card's rings are, and the queue towards ip.drv: the
 * same rule for every driver (voce 41, 28 October 2026).
 *
 * ! NO NUMBER IN THE CODE (the user's rule, voce 35): until e1000.drv 0.003
 * the rings had fixed sizes (e1000 96 receive descriptors, the most that
 * fitted in the 256 KB dma_alloc gave a process), and under 2048 clients
 * the e1000 lost 858521 frames with its ring full. Now, in this order:
 *
 *     1. the driver's option (-r N / -t N), for one start;
 *     2. netrx / nettx in [kernel] of /boot/kernel.cfg (SYS_GETENV);
 *     3. from the RAM: one receive descriptor per MB, a quarter of that
 *        for sending (2048 and 512 with 2 GB: 5 MB of DMA memory).
 *
 * and always within what the card allows (the minimum and the maximum the
 * driver passes, from the datasheet), in steps the card wants (the e1000
 * wants multiples of 8). If dma_alloc cannot give the contiguous memory,
 * the driver halves and asks again (net_anelli_dimezza).
 *
 * The queue of frames already out of the ring and waiting for ip.drv
 * (NetCoda) grows when it is full (doubling), up to four rings: the card can
 * refill its ring four times while ip.drv is busy before the oldest frame
 * goes, and the oldest because a stale frame is worth less than a new one.
 *
 * -r / -t are taken in nforce.drv (-r is its Realtek PHY), so every driver
 * reads -rx N / -tx N with net_anelli_opzione; the e1000 keeps -r / -t too.
 *
 * Only for the drivers (getenv, atoi, malloc and sysconf of the libc).
 * ========================================================================== */
#ifndef NET_ANELLI_H
#define NET_ANELLI_H

#include "libc.h"
#include "net_proto.h"

/* The queue may hold this many rings of frames (see above). */
#define NET_CODA_PER_ANELLO  4u
#define NET_CODA_INIZIO      64

/* Rounds n down to a multiple of passo, within [min, max]. */
static inline unsigned int net_anelli_entro(unsigned int n, unsigned int min,
                                     unsigned int max, unsigned int passo)
{
    if (n > max) n = max;
    if (passo > 1) n -= n % passo;
    if (n < min) n = min;
    return n;
}

/* The receive descriptors: da_riga (the option, 0 = none), else netrx of
 * kernel.cfg, else one per MB of RAM. */
static inline unsigned int net_anelli_rx(unsigned int da_riga, unsigned int min,
                                  unsigned int max, unsigned int passo)
{
    const char *v = getenv("netrx");
    long        pagine;
    unsigned int n;

    if (da_riga != 0)                              n = da_riga;
    else if (v != NULL && v[0] >= '1' && v[0] <= '9') n = (unsigned int)atoi(v);
    else {
        pagine = sysconf(_SC_PHYS_PAGES);
        n = pagine > 0 ? (unsigned int)((unsigned long)pagine / 256ul) : min;
    }
    return net_anelli_entro(n, min, max, passo);
}

/* The send descriptors: da_riga, else nettx of kernel.cfg, else a quarter
 * of the receive ones. */
static inline unsigned int net_anelli_tx(unsigned int da_riga, unsigned int rx,
                                  unsigned int min, unsigned int max,
                                  unsigned int passo)
{
    const char *v = getenv("nettx");
    unsigned int n;

    if (da_riga != 0)                              n = da_riga;
    else if (v != NULL && v[0] >= '1' && v[0] <= '9') n = (unsigned int)atoi(v);
    else                                           n = rx / 4u;
    return net_anelli_entro(n, min, max, passo);
}

/* After a dma_alloc that failed: halves the larger ring. Returns 0 when
 * both are already at the minimum (nothing more to try). */
static inline int net_anelli_dimezza(unsigned int *rx, unsigned int *tx,
                              unsigned int rx_min, unsigned int tx_min,
                              unsigned int passo)
{
    if (*rx > rx_min && *rx >= *tx) {
        *rx = net_anelli_entro(*rx / 2u, rx_min, *rx, passo);
        return 1;
    }
    if (*tx > tx_min) {
        *tx = net_anelli_entro(*tx / 2u, tx_min, *tx, passo);
        return 1;
    }
    if (*rx > rx_min) {
        *rx = net_anelli_entro(*rx / 2u, rx_min, *rx, passo);
        return 1;
    }
    return 0;
}

/* -rx N and -tx N: returns 1 if argv[*i] was one of them. */
static inline int net_anelli_opzione(int argc, char **argv, int *i,
                                     unsigned int *rx, unsigned int *tx)
{
    if (*i + 1 >= argc) return 0;
    if (strcmp(argv[*i], "-rx") == 0) *rx = (unsigned int)atoi(argv[++*i]);
    else if (strcmp(argv[*i], "-tx") == 0) *tx = (unsigned int)atoi(argv[++*i]);
    else return 0;
    return 1;
}

/* For a card that takes the logarithm of the size (the pcnet): the exponent
 * of the largest power of two not above n. */
static inline unsigned int net_anelli_log2(unsigned int n)
{
    unsigned int l = 0;

    while ((n >> (l + 1)) != 0) l++;
    return l;
}

/* -----------------------------------------------------------------------------
 * The frames waiting for ip.drv
 * --------------------------------------------------------------------------- */
typedef struct {
    unsigned char *frame;           /* n frames of NET_FRAME_MAX bytes */
    unsigned int  *len;
    int            n, max;          /* places now, places at most */
    int            testa, conta;
} NetCoda;

#define NET_CODA_FRAME(c, i)  ((c)->frame + (unsigned int)(i) * NET_FRAME_MAX)

/* For a card with rings of `anello` receive descriptors. Returns 0 if there
 * is no memory: the caller says so and gives up. */
static inline int net_coda_inizia(NetCoda *c, unsigned int anello)
{
    c->max   = (int)(NET_CODA_PER_ANELLO * anello);
    c->n     = NET_CODA_INIZIO < c->max ? NET_CODA_INIZIO : c->max;
    c->testa = c->conta = 0;
    c->frame = malloc((unsigned int)c->n * NET_FRAME_MAX);
    c->len   = malloc((unsigned int)c->n * sizeof(unsigned int));
    return c->frame != NULL && c->len != NULL;
}

/* Doubles the queue, keeping the frames in order from place 0. Returns 0 at
 * the maximum or without memory (the queue stays as it was). */
static inline int net_coda_cresci(NetCoda *c)
{
    int            n = c->n * 2, i;
    unsigned char *f;
    unsigned int  *l;

    if (n > c->max) n = c->max;
    if (n <= c->n) return 0;
    f = malloc((unsigned int)n * NET_FRAME_MAX);
    l = malloc((unsigned int)n * sizeof(unsigned int));
    if (f == NULL || l == NULL) { free(f); free(l); return 0; }

    for (i = 0; i < c->conta; i++) {
        int k = (c->testa + i) % c->n;

        memcpy(f + (unsigned int)i * NET_FRAME_MAX, NET_CODA_FRAME(c, k), c->len[k]);
        l[i] = c->len[k];
    }
    free(c->frame);
    free(c->len);
    c->frame = f;
    c->len   = l;
    c->n     = n;
    c->testa = 0;
    return 1;
}

/* A place for a frame of len bytes (at most NET_FRAME_MAX): the caller fills
 * it. Full and unable to grow: the oldest goes, and *persi counts it. */
static inline unsigned char *net_coda_posto(NetCoda *c, unsigned int len,
                                            unsigned int *persi)
{
    int slot;

    if (c->conta == c->n && !net_coda_cresci(c)) {
        c->testa = (c->testa + 1) % c->n;
        c->conta--;
        (*persi)++;
    }
    slot = (c->testa + c->conta) % c->n;
    c->len[slot] = len;
    c->conta++;
    return NET_CODA_FRAME(c, slot);
}

/* The oldest frame stays in the queue until net_coda_togli. */
static inline const unsigned char *net_coda_primo(const NetCoda *c)
{
    return NET_CODA_FRAME(c, c->testa);
}

static inline unsigned int net_coda_primo_len(const NetCoda *c)
{
    return c->len[c->testa];
}

static inline void net_coda_togli(NetCoda *c)
{
    c->testa = (c->testa + 1) % c->n;
    c->conta--;
}

#endif /* NET_ANELLI_H */
