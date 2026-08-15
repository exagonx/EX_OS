/* =============================================================================
 * tools/iso/prova-gcc.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * La prova della CATENA INTERA dentro EX-OS: preprocessore, cc1,
 * assemblatore, collegatore, e un programma che parte e stampa.
 *
 *     gcc -B/cdrom/bin/ -I/cdrom/exos/include /cdrom/prova-gcc.c -o /disco/pg
 *     /disco/pg
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' NON BASTAVA prova-cc1.c
 *
 * Quello risponde a una domanda sola — cc1 legge un .c e scrive un .s — e
 * la risponde bene proprio perche' non ha nessun #include e non chiama
 * nessuna funzione. Ma un compilatore che compila e basta non serve a
 * niente: fra "cc1 produce assembly" e "ho un programma che gira" ci sono
 * quattro processi, un file temporaneo per ognuno, la ricerca degli
 * header e il collegamento con crt0, libgcc e libc.
 *
 * Ognuno di quei pezzi puo' mancare per conto suo, e ognuno da' un errore
 * che sembra colpa di un altro. Questo file li tocca tutti insieme, e se
 * stampa vuol dire che nessuno manca.
 *
 * -----------------------------------------------------------------------------
 * Cosa tocca, e perche' proprio questo
 *
 *   - <stdio.h> e <string.h>: la ricerca degli header, cioe' il pezzo che
 *     prova-cc1.c evita di proposito;
 *   - printf con %s, %d e %lld: chiama la libc, quindi il collegamento con
 *     libc.a e' avvenuto davvero — un binario che si collega ma non trova
 *     printf non arriva a stamparlo;
 *   - una divisione fra long long: e' una delle poche cose che il
 *     compilatore NON sa fare con un'istruzione, e che chiama __divdi3 in
 *     libgcc.a. Se libgcc non e' stato collegato, il difetto si vede qui e
 *     solo qui;
 *   - una struttura restituita per valore, dove l'ABI di ritorno si vede;
 *   - un array su cui gira memcpy, che sulla nostra libc e' una funzione
 *     vera e non un builtin del compilatore.
 *
 * ! IL RISULTATO E' UN NUMERO NOTO, non "sembra giusto". 385 e' la somma
 * dei quadrati da 1 a 10, ed e' scritto qui sotto perche' un programma che
 * stampa un valore sbagliato senza che nessuno sappia quale fosse quello
 * giusto e' una prova che non prova niente.
 * ============================================================================= */

#include <stdio.h>
#include <string.h>

struct coppia { int a; long long b; };

static const char saluto[] = "EX-OS";

static struct coppia calcola(int n)
{
    struct coppia r;
    long long     s = 0;
    int           i;

    for (i = 1; i <= n; i++) s += (long long)i * i;

    r.a = (int)sizeof(saluto) - 1;
    r.b = s;
    return r;
}

int main(void)
{
    struct coppia c = calcola(10);
    char          nome[16];
    long long     q;

    memcpy(nome, saluto, sizeof(saluto));

    /* __divdi3 di libgcc: una divisione a 64 bit per un valore che il
     * compilatore non conosce a priori non si risolve con uno shift. */
    q = c.b / (long long)(c.a + 1);

    printf("La catena intera dentro %s\n\n", nome);
    printf("  somma dei quadrati 1..10 : %lld   (atteso 385)\n", c.b);
    printf("  lunghezza del nome       : %d     (atteso 5)\n", c.a);
    printf("  divisione a 64 bit       : %lld   (atteso 64)\n", q);

    if (c.b != 385 || c.a != 5 || q != 64) {
        printf("\nUn valore non torna: la catena ha prodotto un programma\n");
        printf("che si collega ma calcola male.\n");
        return 1;
    }

    printf("\nCompilato, assemblato e collegato qui dentro.\n");
    return 0;
}
