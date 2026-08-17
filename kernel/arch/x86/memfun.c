/* =============================================================================
 * kernel/arch/x86/memfun.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Le quattro funzioni che il compilatore puo' chiamare da solo
 *
 * ! -ffreestanding NON VUOL DIRE «nessuna funzione di libreria». Vuol dire che
 * non c'e' una libreria standard COMPLETA, ma il compilatore resta libero di
 * generare chiamate a memcpy, memmove, memset e memcmp per conto suo — per una
 * copia di struttura, per l'azzeramento di un array locale, per un confronto.
 * Sono le quattro che ogni ambiente freestanding deve fornire.
 *
 * ! SONO COMPARSE CAMBIANDO LA CPU DI BASE, il 17 agosto 2026. Con
 * -march=i686 (il default) GCC apriva quelle copie in istruzioni; con
 * -march=pentium-mmx ha deciso che una chiamata conviene, e il collegamento
 * del kernel si e' fermato su
 *
 *     undefined reference to `memcpy'
 *
 * Il codice del kernel non era cambiato di una riga. **Non era un difetto del
 * kernel: era una scelta del compilatore che nessuno gli aveva impedito.**
 *
 * ! E NON SI SCRIVONO «VELOCI». Una copia a parole da 32 bit e' quattro volte
 * meglio di una a byte e costa cinque righe; oltre si entra nel campo delle
 * copie allineate e non allineate, dove un errore e' silenzioso e si vede
 * altrove. Se un giorno serviranno piu' veloci — MMX copia otto byte per volta
 * — si misurera' prima quanto pesano davvero.
 * ============================================================================= */

#include "kernel.h"

void *memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    /* A parole quando entrambi sono allineati: e' il caso normale, perche' chi
     * copia strutture copia cose allineate. */
    if ((((uint32_t)d | (uint32_t)s) & 3u) == 0) {
        uint32_t       *dw = (uint32_t *)d;
        const uint32_t *sw = (const uint32_t *)s;

        while (n >= 4u) { *dw++ = *sw++; n -= 4u; }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }

    while (n--) *d++ = *s++;
    return dst;
}

/* ! memmove DEVE FUNZIONARE ANCHE QUANDO LE DUE ZONE SI SOVRAPPONGONO, ed e'
 * l'unica differenza con memcpy: copiando in avanti su una sovrapposizione si
 * riscrivono i byte che non si sono ancora letti. */
void *memmove(void *dst, const void *src, uint32_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0) return dst;

    if (d < s) return memcpy(dst, src, n);

    d += n;
    s += n;
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t  v = (uint8_t)c;

    if ((((uint32_t)d) & 3u) == 0) {
        uint32_t  parola = ((uint32_t)v << 24) | ((uint32_t)v << 16) |
                           ((uint32_t)v << 8)  |  (uint32_t)v;
        uint32_t *dw = (uint32_t *)d;

        while (n >= 4u) { *dw++ = parola; n -= 4u; }
        d = (uint8_t *)dw;
    }

    while (n--) *d++ = v;
    return dst;
}

int memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;

    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}
