/* =============================================================================
 * lib/exlib/exlib.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il risolutore: quaranta righe fra un programma e una libreria condivisa
 *
 * ! E' PICCOLO APPOSTA. Questo pezzo finisce dentro OGNI applicazione che usa
 * una libreria — e' l'unica cosa che non si puo' condividere, perche' serve
 * proprio a raggiungere cio' che si condivide. Ogni riga qui dentro e' pagata
 * da tutti.
 *
 * ! E NON DIPENDE DALLA libc. Niente printf, niente strcmp, niente malloc: il
 * confronto dei nomi e la syscall se li fa da se'. Cosi' puo' essere usato
 * anche da un programma che la libc non ce l'ha — e soprattutto non obbliga
 * chi lo include a portarsi dietro mezza libreria per chiedere un indirizzo.
 * ============================================================================= */

#include "exlib.h"

#define SYS_LIB_APRI    248

static inline int syscall1(int n, unsigned int a)
{
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a) : "memory");
    return r;
}

/* Il confronto dei nomi, fatto in casa: vedi la nota in testa al file. */
static int uguale(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

const ExLibTesta *exlib_apri(const char *percorso)
{
    int               r;
    const ExLibTesta *t;

    if (percorso == 0 || percorso[0] == '\0') return 0;

    r = syscall1(SYS_LIB_APRI, (unsigned int)percorso);
    if (r <= 0) return 0;

    t = (const ExLibTesta *)(unsigned int)r;

    /* ! SI CONTROLLA COSA E' STATO MAPPATO, non ci si fida dell'indirizzo. Il
     * kernel garantisce che quelle pagine ci siano, non che contengano una
     * tabella: un ELF qualunque messo in /exwin/lib avrebbe del codice li',
     * e leggerlo come una struttura darebbe un `n` enorme e un giro sui nomi
     * dentro memoria che non e' fatta di stringhe. */
    if (t->magia != EXLIB_MAGIA) return 0;

    /* ! UNA VERSIONE PIU' NUOVA SI RIFIUTA. I campi starebbero da un'altra
     * parte, e leggerli comunque vorrebbe dire chiamare indirizzi presi da
     * quello che capita. Meglio un programma che dice «non ci parlo» di uno
     * che salta nel vuoto. */
    if (t->versione != EXLIB_VERSIONE) return 0;

    if (t->n == 0 || t->nomi == 0 || t->indirizzi == 0) return 0;

    /* ! UNA LIBRERIA CHE HA BISOGNO DI ALTRE LIBRERIE SI AVVIA QUI. Un
     * programma ha _start, e li' c'e' un posto naturale in cui agganciare cio'
     * che gli serve. Una libreria non parte: le sue funzioni vengono chiamate
     * e basta. Se exwin.so usa la libc condivisa, i suoi ponti li deve
     * riempire qualcuno — e l'unico momento in cui si sa che exwin.so e'
     * appena stata mappata e' questo.
     *
     * Il nome e' facoltativo: una libreria che non dipende da nessuno (la libc
     * stessa) non lo esporta e qui non succede niente. */
    {
        void (*avvia)(void) = (void (*)(void))exlib_simbolo(t, "__lib_avvio");
        if (avvia) avvia();
    }

    return t;
}

const ExLibTesta *exlib_apri_fra(const char *const *percorsi, int quanti)
{
    int i;

    for (i = 0; i < quanti; i++) {
        const ExLibTesta *t = exlib_apri(percorsi[i]);
        if (t) return t;
    }
    return 0;
}

void *exlib_simbolo(const ExLibTesta *t, const char *nome)
{
    unsigned int i;

    if (t == 0 || nome == 0) return 0;

    /* ! RICERCA LINEARE, e va bene: si fa una volta per nome all'avvio, e i
     * nomi sono qualche decina. Una tabella hash costerebbe piu' codice DENTRO
     * OGNI APPLICAZIONE di quanto faccia risparmiare in un confronto solo. */
    for (i = 0; i < t->n; i++)
        if (uguale(t->nomi[i], nome)) return t->indirizzi[i];

    return 0;
}
