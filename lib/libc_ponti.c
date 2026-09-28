/* =============================================================================
 * lib/libc_ponti.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Chi riempie i ponti verso libc.so, e lo fa prima di tutto il resto
 *
 * ! QUESTO FILE NON PUO' USARE LA LIBC, e non e' una regola di stile: e' la
 * definizione stessa del suo compito. Ogni funzione della libc, in un
 * programma collegato a quella condivisa, e' un salto attraverso un puntatore
 * che PRIMA di questa funzione vale zero. Una `strcmp()` qui dentro sarebbe un
 * salto a zero — e il fault indicherebbe strcmp, non l'avvio.
 *
 * Per questo il confronto dei nomi e la scrittura del messaggio d'errore sono
 * fatti a mano con le syscall dirette, e per questo la ricerca sta in
 * lib/exlib/exlib.c, che e' scritto con la stessa regola.
 *
 * ! E NON PUO' USARE NEMMENO memcpy IMPLICITAMENTE. GCC ha il diritto di
 * trasformare una copia di struttura o l'inizializzazione di un array in una
 * chiamata a memcpy anche con -ffreestanding: qui dentro non si copiano
 * strutture ne' si inizializzano array locali, si assegnano solo puntatori.
 * ============================================================================= */

#include "exlib.h"

#define SYS_WRITE   4
#define SYS_EXIT    1

/* Li genera tools/genlibc.py, insieme ai ponti veri. */
extern void              *__libc_ponti_tabella[];
extern const unsigned int __libc_ponti_quanti;

/* ! LE IMPRONTE, NON I NOMI (28 settembre 2026): quattro byte per funzione,
 * nello stesso ordine della tabella. Prima c'era un blocco con tutti i nomi in
 * fila, circa 6 KB in OGNI programma — e il floppy li pagava una volta per
 * programma: sessanta funzioni nuove nella libc lo facevano traboccare. Il
 * nome lo tiene libc.so; qui se ne calcola l'impronta e si confronta. Vedi
 * tools/genlibc.py, che rifiuta di costruire se due nomi ne hanno una uguale.
 *
 * ! E VALE IN TUTT'E DUE I SENSI: libc.so esporta ancora i NOMI, quindi un
 * programma vecchio (coi nomi) su una libc nuova funziona, e un programma
 * nuovo su una libc vecchia pure — l'impronta la calcola chi si avvia. */
extern const unsigned int __libc_ponti_impronte[];

/* FNV-1a a 32 bit: la stessa di tools/genlibc.py, riga per riga. */
static unsigned int impronta(const char *s)
{
    unsigned int h = 2166136261u;

    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static const char *const g_dove[] = {
    "/lib/libc.so",
    "/cdrom/lib/libc.so",
    "/exwin/lib/libc.so"
};

static void scrivi(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    __asm__ volatile ("int $0x80" :: "a"(SYS_WRITE), "b"(2), "c"(s), "d"(n)
                      : "memory");
}

static void scrivi_esa(unsigned int v)
{
    char t[9];
    int  i;

    for (i = 7; i >= 0; i--) { t[i] = "0123456789abcdef"[v & 15]; v >>= 4; }
    t[8] = 0;
    scrivi(t);
}

static void muori(const char *s)
{
    scrivi(s);
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT), "b"(1) : "memory");
    for (;;) { }
}

/* =============================================================================
 * __libc_ponti_avvia — la prima riga di _libc_start
 *
 * Sostituisce la versione weak e vuota di lib/libc_avvio.c: un programma
 * collegato alla libc STATICA prende quella, uno collegato alla condivisa
 * prende questa. E' l'unico interruttore fra i due modi, e sta nel
 * collegamento, non in un #ifdef sparso.
 * ============================================================================= */
void __libc_ponti_avvia(void)
{
    const ExLibTesta *t;
    unsigned int      i, e;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0)
        muori("libc: non trovo la libreria condivisa /lib/libc.so\n");

    for (i = 0; i < __libc_ponti_quanti; i++) __libc_ponti_tabella[i] = 0;

    /* Un giro sulle funzioni che libc.so offre: di ognuna l'impronta, e il
     * suo indirizzo va nel posto che la chiede. 445 per 445 confronti di
     * interi: niente, rispetto a leggere il file. */
    for (e = 0; e < t->n; e++) {
        unsigned int h = impronta(t->nomi[e]);

        for (i = 0; i < __libc_ponti_quanti; i++)
            if (__libc_ponti_impronte[i] == h)
                __libc_ponti_tabella[i] = t->indirizzi[e];
    }

    /* ! UNA FUNZIONE CHE MANCA SI DICE, e si dice quale — con l'impronta,
     * perche' il nome qui non c'e' piu': impronta() di tools/genlibc.py sui
     * nomi della libc lo ritrova. E' il solo modo di accorgersi che la libc
     * installata e' piu' vecchia del programma. */
    for (i = 0; i < __libc_ponti_quanti; i++)
        if (__libc_ponti_tabella[i] == 0) {
            scrivi("libc: la libreria condivisa non ha la funzione con impronta ");
            scrivi_esa(__libc_ponti_impronte[i]);
            muori("\n      La libc installata e' piu' vecchia di questo programma.\n");
        }
}
