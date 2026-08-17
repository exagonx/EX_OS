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

/* ! UN BLOCCO DI STRINGHE, non un vettore di puntatori: i nomi stanno uno dopo
 * l'altro separati dallo zero finale, nello stesso ordine della tabella. Un
 * vettore di 322 puntatori sarebbero 1288 byte in ogni programma per dire una
 * cosa che le stringhe dicono gia' da se'. */
extern const char         __libc_ponti_nomi[];

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
    const char *nome;
    unsigned int i;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0)
        muori("libc: non trovo la libreria condivisa /lib/libc.so\n");

    nome = __libc_ponti_nomi;

    for (i = 0; i < __libc_ponti_quanti; i++) {
        void *p = exlib_simbolo(t, nome);

        /* ! UN NOME CHE MANCA SI DICE, E SI DICE QUALE. E' il solo modo di
         * accorgersi che la libc installata e' piu' vecchia del programma —
         * l'unico guaio che la risoluzione per nome non impedisce. Senza
         * questo controllo il puntatore resterebbe zero e il programma
         * cadrebbe piu' tardi, in un punto che non c'entra niente. */
        if (p == 0) {
            scrivi("libc: la libreria condivisa non ha la funzione: ");
            scrivi(nome);
            muori("\n      La libc installata e' piu' vecchia di questo programma.\n");
        }
        __libc_ponti_tabella[i] = p;

        while (*nome) nome++;       /* al nome dopo, oltre lo zero finale */
        nome++;
    }
}
