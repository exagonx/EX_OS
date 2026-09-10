/* =============================================================================
 * tools/iso/prova-make/somma.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * ============================================================================= */

#include "prova.h"

/* Un ciclo, cosi' il file passa dall'ottimizzatore e non solo dal parser.
 * Con n = 10 il risultato e' 385, e quel numero e' noto PRIMA di eseguire:
 * e' cio' che distingue una prova da un «sembra aver funzionato». */
long somma_quadrati(int n)
{
    long s = 0;
    int  i;

    for (i = 1; i <= n; i++) s += (long)i * i;
    return s;
}
