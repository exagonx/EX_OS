/* =============================================================================
 * tools/iso/prova-cc1.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il programma piu' piccolo che dica qualcosa a cc1, da compilare DENTRO
 * EX-OS:
 *
 *     cc1 /prova-cc1.c -O2 -o /prova-cc1.s
 *
 * -----------------------------------------------------------------------------
 * ! NESSUN #include, E NON E' PER PIGRIZIA
 *
 * Questo file serve a rispondere a UNA domanda: cc1 riesce a partire,
 * leggere un sorgente e scrivere assembly su EX-OS? Nient'altro.
 *
 * Un solo #include ne farebbe due domande insieme — "cc1 funziona?" e
 * "cc1 trova gli header al posto giusto?" — e quando fallisse non si
 * saprebbe a quale delle due ha risposto no. La ricerca degli header e'
 * un problema di percorsi (--prefix=/exos, la catena di -isystem), che si
 * affronta dopo e da solo.
 *
 * ! SI CHIAMA cc1 E NON gcc. `gcc` e' il DRIVER: lancia il preprocessore,
 * poi cc1, poi as, poi ld — quattro processi, e su EX-OS ognuno di quei
 * lanci e' una spawn() che puo' fallire per conto suo. cc1 da solo prende
 * un .c e sputa un .s: e' il pezzo che fa il lavoro, ed e' il pezzo che
 * va provato per primo.
 *
 * -----------------------------------------------------------------------------
 * Cosa c'e' dentro, e perche' proprio questo
 *
 * Poche righe, ma scelte per far girare i pezzi di cc1 che si rompono per
 * primi se qualcosa manca nella libc del bersaglio:
 *
 *   - una funzione con un ciclo, cosi' passa dall'ottimizzatore e non
 *     solo dal parser;
 *   - un `long long`, che tira dentro le routine a 64 bit di libgcc;
 *   - una stringa costante, che finisce in .rodata e fa lavorare
 *     l'assemblatore di sezioni;
 *   - una struttura restituita per valore, che e' il caso in cui
 *     l'ABI di ritorno si vede davvero.
 * ============================================================================= */

struct coppia { int a; long long b; };

static const char saluto[] = "EX-OS";

struct coppia calcola(int n)
{
    struct coppia r;
    long long     s = 0;
    int           i;

    for (i = 1; i <= n; i++) s += (long long)i * i;

    r.a = (int)saluto[0];
    r.b = s;
    return r;
}

int main(void)
{
    struct coppia c = calcola(10);
    return (int)(c.b & 0x7F) + c.a;
}
