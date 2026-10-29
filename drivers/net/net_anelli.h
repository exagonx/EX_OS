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
 * The queue of frames already out of the ring and waiting for ip.drv grows
 * when it is full (doubling), up to four rings: the card can refill its
 * ring four times while ip.drv is busy before the oldest frame goes.
 *
 * Only for the drivers (getenv, atoi and sysconf of the libc).
 * ========================================================================== */
#ifndef NET_ANELLI_H
#define NET_ANELLI_H

#include "libc.h"

/* The queue may hold this many rings of frames (see above). */
#define NET_CODA_PER_ANELLO  4u

/* Rounds n down to a multiple of passo, within [min, max]. */
static unsigned int net_anelli_entro(unsigned int n, unsigned int min,
                                     unsigned int max, unsigned int passo)
{
    if (n > max) n = max;
    if (passo > 1) n -= n % passo;
    if (n < min) n = min;
    return n;
}

/* The receive descriptors: da_riga (the option, 0 = none), else netrx of
 * kernel.cfg, else one per MB of RAM. */
static unsigned int net_anelli_rx(unsigned int da_riga, unsigned int min,
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
static unsigned int net_anelli_tx(unsigned int da_riga, unsigned int rx,
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
static int net_anelli_dimezza(unsigned int *rx, unsigned int *tx,
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

#endif /* NET_ANELLI_H */
