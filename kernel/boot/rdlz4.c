/* =============================================================================
 * kernel/boot/rdlz4.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il sistema di base compresso (@RDBOOT passo 1b, 22 ottobre 2026)
 *
 * ! LO DECOMPRIME IL KERNEL, NON STAGE 2. Stage 2 lavora in modo reale con
 * ventisette KB in tutto: porta in memoria il file cosi' com'e', a 10 MB, e
 * qui - prima che il PMM prenda possesso della memoria - lo si espande a
 * RD_ESPANSO (6 MB). Da li' in poi rb_addr/rb_byte descrivono il volume
 * ESPANSO, e chi viene dopo (PMM, livello a blocchi) non sa che e' stato
 * compresso.
 *
 * ! IL FORMATO E' 'EXLZ' + UN BLOCCO LZ4, il contratto con tools/lz4exos.py
 * che lo scrive: +0 'EXLZ', +4 byte dell'originale, +8 byte del blocco, +12
 * zero, +16 il blocco. Un file senza la firma resta com'e': e' un volume non
 * compresso, che si usa direttamente come prima del passo 1b. A +12 la somma
 * dei byte dell'originale: un letterale cambiato per strada LZ4 non lo vede.
 *
 * ! OGNI LETTURA E SCRITTURA E' CONTROLLATA. Un blocco rovinato dal BIOS non
 * deve scrivere fuori dallo spazio dell'espanso ne' leggere oltre il file:
 * qui la memoria non e' ancora di nessuno e un errore non lo vedrebbe niente.
 * Se il blocco non torna ESATTAMENTE alla lunghezza dichiarata, il volume non
 * si usa: meglio un avvio senza sistema di base che uno con file rotti.
 * ============================================================================= */

#include "kernel.h"
#include "rdlz4.h"

#define EXLZ_FIRMA    0x5A4C5845u      /* 'EXLZ' */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Un blocco LZ4. Rende i byte scritti, o -1 se il blocco e' malformato. */
int lz4_blocco(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t cap)
{
    const uint8_t *ip = src, *ie = src + n;
    uint8_t       *op = dst, *oe = dst + cap;

    for (;;) {
        uint32_t tok, lit, len, off;

        if (ip >= ie) return -1;
        tok = *ip++;

        lit = tok >> 4;
        if (lit == 15) {
            uint8_t b;
            do {
                if (ip >= ie) return -1;
                b = *ip++;
                lit += b;
            } while (b == 255);
        }
        if ((uint32_t)(ie - ip) < lit || (uint32_t)(oe - op) < lit) return -1;
        while (lit--) *op++ = *ip++;

        if (ip == ie) break;              /* l'ultima sequenza: solo letterali */

        if (ie - ip < 2) return -1;
        off = (uint32_t)ip[0] | ((uint32_t)ip[1] << 8);
        ip += 2;
        if (off == 0 || off > (uint32_t)(op - dst)) return -1;

        len = tok & 15;
        if (len == 15) {
            uint8_t b;
            do {
                if (ip >= ie) return -1;
                b = *ip++;
                len += b;
            } while (b == 255);
        }
        len += 4;
        if ((uint32_t)(oe - op) < len) return -1;
        {
            const uint8_t *m = op - off;
            while (len--) *op++ = *m++;   /* byte per byte: m e op si sovrappongono */
        }
    }
    return (int)(op - dst);
}

static void senza(BootInfo *info)
{
    info->rb_addr = 0;
    info->rb_byte = 0;
}

void rd_decomprimi(BootInfo *info)
{
    const uint8_t *s;
    uint32_t       orig, comp;
    int            r;

    if (info == NULL || info->rb_firma != RB_FIRMA) return;
    if (info->rb_addr == 0 || info->rb_byte < 16) return;

    s = (const uint8_t *)DA_FISICO(info->rb_addr);
    if (le32(s) != EXLZ_FIRMA) return;    /* non compresso: si usa com'e' */

    orig = le32(s + 4);
    comp = le32(s + 8);
    /* ! L'ESPANSO NON DEVE TOCCARE IL COMPRESSO: lo si legge mentre si
     * scrive. Stage 2 lo mette sopra RD_ESPANSO + RD_ESPANSO_MAX. */
    if (comp > info->rb_byte - 16 || orig == 0 || orig > RD_ESPANSO_MAX ||
        (info->rb_addr < RD_ESPANSO + orig &&
         info->rb_addr + info->rb_byte > RD_ESPANSO)) {
        klog(LOG_ERROR, "RD: rdboot compresso con misure impossibili (%u -> %u byte, "
                        "file di %u a 0x%08x): non lo uso", comp, orig,
                        info->rb_byte, info->rb_addr);
        senza(info);
        return;
    }

    r = lz4_blocco(s + 16, comp, (uint8_t *)DA_FISICO(RD_ESPANSO), orig);
    if (r != (int)orig) {
        klog(LOG_ERROR, "RD: rdboot compresso ROTTO (decompressi %d byte su %u): "
                        "parto senza il sistema di base", r, orig);
        senza(info);
        return;
    }

    {
        const uint8_t *d = (const uint8_t *)DA_FISICO(RD_ESPANSO);
        uint32_t       somma = 0, i;

        for (i = 0; i < orig; i++) somma += d[i];
        if (somma != le32(s + 12)) {
            klog(LOG_ERROR, "RD: rdboot compresso ROTTO (somma 0x%08x, attesa 0x%08x): "
                            "parto senza il sistema di base", somma, le32(s + 12));
            senza(info);
            return;
        }
    }

    klog(LOG_INFO, "RD: rdboot decompresso: %u -> %u byte a 0x%08x",
         comp + 16, orig, RD_ESPANSO);
    info->rb_addr = RD_ESPANSO;
    info->rb_byte = orig;
}
