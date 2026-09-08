/* =============================================================================
 * bin/scarica/scarica.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * scarica — prende una pagina e la mette da qualche parte
 *
 * ! ESISTE PER PROVARE L'HTTP PRIMA CHE CI SIA UN BROWSER, e non e' un
 * ripiego: fra «i conti sulle intestazioni tornano» e «si vede una pagina» ci
 * sono l'impaginazione e il disegno, cioe' due pezzi grossi in cui un difetto
 * dell'HTTP si confonderebbe con un difetto loro. Qui l'HTTP e' l'unica cosa
 * che puo' sbagliare.
 *
 * ! E RESTA UTILE DOPO. Un sistema che sa prendere un file da un URL e metterlo
 * su disco ha un modo in piu' di installare qualcosa, e non dipende dal fatto
 * che la grafica sia accesa.
 *
 *     scarica <url>                    lo stampa
 *     scarica <url> <file>             lo salva
 *     scarica -i <url>                 solo le intestazioni che contano
 *     scarica -tempi <url>             quanto e' costato ogni passo del TLS
 * ============================================================================= */

#include "libc.h"
#include "exhttp.h"

/* +0.001 a ogni modifica: `scarica -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("scarica", "0.003");

/* ! IL TETTO LO METTE CHI SCARICA, NON IL SERVER. Un megabyte tiene qualunque
 * pagina di testo; se non basta si tronca e si dice, invece di far decidere a
 * chi sta dall'altra parte quanta memoria prendere qui. */
#define BUF_MAX     (1024u * 1024u)

static unsigned char g_buf[BUF_MAX];

/* =============================================================================
 * QUANTO COSTA OGNI PASSO DELLA STRETTA
 *
 * ! IL GANCIO C'ERA DAL 3 SETTEMBRE, E NESSUNO L'AVEVA USATO PER MISURARE.
 * exhttp_passo() chiama chi vuole saperlo fra un passo e l'altro della stretta
 * TLS: il browser lo registra e ne fa una riga di stato, che e' il motivo per
 * cui e' nato. Ma fra due chiamate c'e' esattamente il lavoro di quel passo,
 * quindi lo stesso gancio MISURA — e serviva a rispondere a @DIF-TLS, dove il
 * costo del pezzo piu' lungo era scritto da mesi senza che nessuno l'avesse
 * cronometrato. Bastava attaccargli un orologio invece di una frase.
 *
 * ! IL NUMERO SU UNA RIGA E' IL TEMPO PER ARRIVARE FIN LI', non quello del
 * passo che la riga nomina. Ed e' cosi' che va letto: «concordo il segreto
 * 30 ms» vuol dire che lo scambio di chiave e' costato 30 ms, perche' fra il
 * passo prima e questo non c'e' altro che lui.
 *
 * ! TRE RIGHE CONTENGONO ANCHE ALTRO, e vanno lette come un tetto, non come
 * un conto. «preparo la chiave» si porta dietro il DNS, la connessione e il
 * magazzino delle CA letto da disco; «il server ha risposto» e «leggo i
 * certificati» aspettano dei pacchetti. Le altre sono conto puro, e sono
 * quelle che @DIF-TLS voleva sapere.
 *
 * ! «un anello della catena» SI RIPETE, ed e' l'unica riga che lo fa: la
 * catena si verifica un anello per volta, e ogni riga porta il costo di
 * quello PRIMA. La prima delle sue righe e' quindi quasi zero — si e' appena
 * cominciato — e l'ultima porta il costo dell'ultimo anello ricevuto. Su
 * «connessione cifrata» finisce la firma che manca all'appello: quella della
 * RADICE, cercata nel magazzino.
 *
 * ! LA RISOLUZIONE E' DIECI MILLISECONDI (uptime_ms avanza col PIT a 100 Hz):
 * un passo che dice 0 e' sotto i dieci, non e' gratis.
 * ========================================================================== */
static unsigned int g_t_passo;

static int passo_tempo(void *dato, const char *cosa)
{
    unsigned int ora = uptime_ms();

    (void)dato;
    printf("  %-22s %4u ms\n", cosa, ora - g_t_passo);
    g_t_passo = ora;
    return 1;                       /* 0 annullerebbe la stretta */
}

static void uso(void)
{
    printf("uso: scarica [-i] [-tempi] <url> [file]\n\n");
    printf("  scarica http://esempio.it/           stampa la pagina\n");
    printf("  scarica http://esempio.it/ pag.html  la salva\n");
    printf("  scarica -i http://esempio.it/        solo l'esito\n");
    printf("  scarica -tempi https://esempio.it/   i tempi della stretta\n\n");
    /* ! QUESTA RIGA HA DETTO UNA BUGIA PER SETTIMANE: «https non ancora,
     * manca il TLS». Il TLS c'e' da agosto e sta dentro exhttp.so, che e'
     * proprio la libreria che questo programma carica — bastava provarlo.
     * Chi legge l'aiuto di un comando gli crede piu' che al codice. */
    printf("Segue fino a %d redirezioni. https funziona: il TLS sta in\n",
           EXHTTP_SALTI_MAX);
    printf("/exwin/lib/exhttp.so, che scarica carica quando le serve.\n");
}

int main(int argc, char **argv)
{
    ExHttpEsito  e;
    const char  *url = 0, *dove = 0;
    unsigned int t0, ms;
    int          solo_info = 0, tempi = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)      solo_info = 1;
        else if (strcmp(argv[i], "-tempi") == 0) tempi = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else if (!url)                       url = argv[i];
        else if (!dove)                      dove = argv[i];
    }

    if (!url) { uso(); return 1; }

    if (tempi) {
        printf("tempi della stretta (ms per ARRIVARE a ogni passo)\n");
        g_t_passo = uptime_ms();
        exhttp_passo(passo_tempo, 0);
    }

    /* ! QUANTO C'E' VOLUTO SI STAMPA SEMPRE, e non e' un vezzo. La velocita'
     * di questo programma e' il modo in cui si misura lo stack TCP da dentro
     * il sistema (@DIF-TCPBUF: 759 KB in 8 secondi con la finestra a 4 KB, in
     * 25 con quella a 16). Finora quel numero si prendeva col cronometro di
     * chi guardava, che e' il modo in cui una misura diventa un'opinione. */
    t0 = uptime_ms();

    if (!exhttp_prendi(url, g_buf, sizeof(g_buf), &e)) {
        printf("scarica: %s\n", e.errore[0] ? e.errore : "non riuscito");
        return 1;
    }

    ms = uptime_ms() - t0;

    printf("scarica: %d, %s, %u byte in %u,%u s%s\n", e.codice,
           e.tipo[0] ? e.tipo : "(nessun tipo)", e.byte,
           ms / 1000u, (ms % 1000u) / 100u,
           e.troncata ? " (TRONCATA: il buffer era piccolo)" : "");

    /* ! SI DICE DOVE SI E' FINITI, se non e' dove si voleva andare. Con una
     * redirezione seguita in silenzio, chi guarda una pagina inattesa non ha
     * modo di sapere che l'indirizzo non e' piu' quello che aveva scritto. */
    if (e.salti > 0)
        printf("         dopo %d redirezion%s: %s\n", e.salti,
               e.salti == 1 ? "e" : "i", e.finale);

    if (solo_info) return 0;

    if (dove) {
        int fd = open(dove, O_WRONLY | O_CREAT | O_TRUNC);
        unsigned int fatti = 0;

        if (fd < 0) { printf("scarica: non riesco a creare %s\n", dove); return 1; }

        while (fatti < e.byte) {
            int k = (int)write(fd, g_buf + fatti, e.byte - fatti);

            if (k <= 0) break;
            fatti += (unsigned int)k;
        }
        close(fd);

        if (fatti != e.byte) {
            printf("scarica: scritti %u byte su %u\n", fatti, e.byte);
            return 1;
        }
        printf("         salvata in %s\n", dove);
        return 0;
    }

    /* Sullo schermo, e non con printf: il corpo puo' contenere degli zeri, e
     * printf si fermerebbe li'. */
    {
        unsigned int fatti = 0;

        while (fatti < e.byte) {
            int k = (int)write(1, g_buf + fatti, e.byte - fatti);

            if (k <= 0) break;
            fatti += (unsigned int)k;
        }
    }
    return 0;
}
