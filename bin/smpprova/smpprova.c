/* =============================================================================
 * bin/smpprova/smpprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * smpprova — i processori in piu' lavorano davvero? (tappa 3 dell'SMP)
 *
 *     smpprova            un giro da solo, poi due insieme, e il confronto
 *     smpprova N          N programmi insieme (da 1 a 8)
 *     smpprova N SECONDI  quanto dura ogni giro (3)
 *     smpprova -accendi   mette al lavoro i processori in attesa, da adesso e
 *                         fino al riavvio (root): per provare senza toccare
 *                         kernel.cfg
 *
 * Lancia N copie di se stesso che per qualche secondo fanno solo conti, senza
 * chiamare il kernel, e contano quanti ne hanno fatti. Con UN processore due
 * copie insieme fanno ciascuna la meta' dei conti di una sola: si dividono il
 * tempo. Con due processori che lavorano ne fanno ciascuna quanti una sola.
 * Il rapporto fra i due numeri e' la risposta, e non dipende da quanto e'
 * veloce la macchina.
 *
 * ! CONTA IL LAVORO FATTO, NON CHIEDE AL KERNEL «QUANTI PROCESSORI HAI». Quello
 * lo dice gia' hwinfo, e alle tappe 1 e 2 diceva «due» su una macchina dove
 * ne lavorava uno. Una prova che si fida della risposta non prova niente.
 * ============================================================================= */

#include "libc.h"

EX_VERSIONE("smpprova", "0.002");

/* Il lavoro: aritmetica in un ciclo, con l'orologio guardato di rado (ogni
 * chiamata di sistema e' tempo passato nel kernel, che e' di tutti). */
static volatile unsigned int g_scarico;     /* dove finisce il risultato dei conti */

static unsigned int lavora(unsigned int ms)
{
    unsigned int fine = uptime_ms() + ms, giri = 0, x = 12345u;

    for (;;) {
        unsigned int k;

        /* ! IL RISULTATO DEVE SERVIRE A QUALCOSA, o il compilatore toglie il
         * ciclo e resta solo la domanda dell'ora: la prima versione faceva
         * cinque milioni di chiamate di sistema al secondo e misurava il
         * lucchetto del kernel, non i processori. Qui x esce in una variabile
         * `volatile` a ogni giro. */
        for (k = 0; k < 200000u; k++) x = x * 1664525u + 1013904223u + (x >> 7);
        g_scarico = x;
        giri++;
        if ((int)(uptime_ms() - fine) >= 0) break;
    }
    return giri;
}

/* Lancia n figli, li aspetta, rende la somma dei loro conti. Ogni figlio dice
 * il suo conto col codice d'uscita: su EX-OS e' un intero vero, non gli otto
 * bit di Unix (vedi <sys/wait.h> in libc.h). */
static unsigned int giro(const char *io, int n, unsigned int ms, unsigned int *minimo)
{
    int      pid[8], k;
    unsigned int somma = 0;
    char     t_ms[16];

    *minimo = 0xFFFFFFFFu;
    snprintf(t_ms, sizeof(t_ms), "%u", ms);
    for (k = 0; k < n; k++) {
        char *av[4];

        av[0] = (char *)io; av[1] = (char *)"-figlio"; av[2] = t_ms; av[3] = 0;
        pid[k] = spawn(io, av);
        if (pid[k] < 0) { printf("smpprova: non riesco a lanciare %s\n", io); return 0; }
    }
    for (k = 0; k < n; k++) {
        int stato = 0;

        waitpid(pid[k], &stato, 0);
        if (stato < 0) stato = 0;
        somma += (unsigned int)stato;
        if ((unsigned int)stato < *minimo) *minimo = (unsigned int)stato;
    }
    return somma;
}

int main(int argc, char **argv)
{
    unsigned int ms = 3000, uno, min1, tanti, min_n;
    int n = 2;

    if (argc >= 3 && strcmp(argv[1], "-figlio") == 0)
        return (int)lavora((unsigned int)atoi(argv[2]));
    if (argc >= 2 && strcmp(argv[1], "-accendi") == 0) {
        int r = cpu_lavora();

        if (r < 0)       printf("smpprova: non si puo' (serve root)\n");
        else if (r == 0) printf("smpprova: non ci sono processori in attesa da mettere al lavoro\n"
                                "          (hwinfo dice quanti ce ne sono e in che stato)\n");
        else             printf("smpprova: %d processor%s in piu' adesso lavora%s. Fino al riavvio:\n"
                                "          per averlo sempre,  smp = 2  in [kernel] di /boot/kernel.cfg\n",
                                r, r == 1 ? "e" : "i", r == 1 ? "" : "no");
        return r > 0 ? 0 : 1;
    }
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("uso: smpprova [N [SECONDI]]\n");
        printf("     smpprova -accendi   i processori in attesa cominciano a lavorare,\n");
        printf("                         fino al riavvio (root)\n");
        printf("  N programmi insieme (2), ognuno fa conti per SECONDI (3).\n");
        printf("  Dice quanto lavoro fa ciascuno rispetto a uno da solo:\n");
        printf("  1.0 = un processore a testa, 0.5 = se ne dividono uno.\n");
        return 0;
    }
    if (argc >= 2) n = atoi(argv[1]);
    if (argc >= 3) ms = (unsigned int)atoi(argv[2]) * 1000u;
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    if (ms < 1000u) ms = 1000u;

    printf("smpprova: un programma da solo, %u secondi...\n", ms / 1000u);
    uno = giro(argv[0], 1, ms, &min1);
    if (uno == 0) { printf("smpprova: il giro di riferimento non ha contato niente\n"); return 1; }
    printf("  da solo        %u giri\n", uno);

    printf("smpprova: %d programmi insieme, %u secondi...\n", n, ms / 1000u);
    tanti = giro(argv[0], n, ms, &min_n);
    printf("  in %d, somma    %u giri   (il piu' lento: %u)\n", n, tanti, min_n);

    {
        /* Due cifre dopo la virgola, senza virgola mobile. */
        unsigned int tot = (tanti * 100u) / uno, cias = (min_n * 100u) / uno;

        printf("  lavoro totale  %u.%02u volte quello di uno solo\n", tot / 100u, tot % 100u);
        printf("  il piu' lento  %u.%02u del lavoro di uno solo\n", cias / 100u, cias % 100u);
        if (n >= 2 && tot >= 150u)
            printf("smpprova: PIU' PROCESSORI LAVORANO (insieme fanno piu' di uno e mezzo)\n");
        else if (n >= 2)
            printf("smpprova: LAVORA UN PROCESSORE SOLO (insieme fanno quanto uno)\n");
    }
    return 0;
}
