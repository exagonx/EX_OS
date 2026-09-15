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
EX_VERSIONE("scarica", "0.005");

/* ! IL TETTO LO METTE CHI SCARICA, NON IL SERVER. Un megabyte tiene qualunque
 * pagina di testo; se non basta si tronca e si dice, invece di far decidere a
 * chi sta dall'altra parte quanta memoria prendere qui.
 *
 * ! E VALE SOLO PER CIO' CHE FINISCE A SCHERMO, dal 15 settembre 2026. Con un
 * file di destinazione questo buffer e' una finestra sul flusso: vedi
 * verso_file() qui sotto. */
#define BUF_MAX     (1024u * 1024u)

static unsigned char g_buf[BUF_MAX];

/* =============================================================================
 * IL VERSO — quando si sa gia' dove va a finire, non serve tenerlo in mano
 *
 * ! IL DIFETTO ERA UN FILE MEZZO SALVATO. `scarica <url> <file>` accumulava
 * tutto in g_buf e poi lo scriveva: oltre il megabyte usciva «TRONCATA: il
 * buffer era piccolo» e sul disco restava meta' file, con un nome che diceva di
 * essere quello giusto. Adesso ogni pezzo va sul disco appena arriva.
 *
 * ! IL FILE SI CANCELLA SE LA RICHIESTA NON FINISCE BENE. Meta' file con il
 * nome giusto e' peggio di nessun file: il nome e' quello che si guarda.
 * ========================================================================== */
static int  g_fd      = -1;
static long g_scritti = 0;
static int  g_guaio   = 0;

static int verso_file(void *dato, const unsigned char *d, unsigned int n)
{
    unsigned int fatti = 0;

    (void)dato;
    while (fatti < n) {
        int k = (int)write(g_fd, d + fatti, n - fatti);

        if (k <= 0) { g_guaio = 1; return 0; }
        fatti += (unsigned int)k;
    }
    g_scritti += (long)n;
    return 1;
}

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
    printf("uso: scarica [-i] [-tempi] [-da N] <url> [file]\n\n");
    printf("  scarica http://esempio.it/           stampa la pagina\n");
    printf("  scarica http://esempio.it/ pag.html  la salva\n");
    printf("  scarica -i http://esempio.it/        solo l'esito\n");
    printf("  scarica -tempi https://esempio.it/   i tempi della stretta\n");
    printf("  scarica -da 1000 <url>               chiede dal byte 1000\n\n");
    /* ! -da SERVE A PROVARE LA RIPRESA A MANO. Chi la usa per davvero e'
     * netupdate, che finisce un .new rimasto a meta'; qui sta perche' una
     * cosa che non si puo' chiedere da riga di comando non si prova, e una
     * cosa che non si prova non si sa se funziona. Il server risponde 206 e
     * manda da li' in poi; se invece risponde 200 vuol dire che il Range non
     * lo sa fare e sta mandando tutto. */
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
    unsigned long da = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)      solo_info = 1;
        else if (strcmp(argv[i], "-tempi") == 0) tempi = 1;
        else if (strcmp(argv[i], "-da") == 0 && i + 1 < argc)
            da = (unsigned long)strtoul(argv[++i], 0, 10);
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else if (!url)                       url = argv[i];
        else if (!dove)                      dove = argv[i];
    }

    if (!url) { uso(); return 1; }

    /* ! SI CHIEDE SUBITO PRIMA DELLA RICHIESTA, perche' vale per una sola:
     * exhttp_prendi() se lo prende e lo azzera. Vedi exhttp_da. */
    if (da > 0) exhttp_da(da);

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

    /* ! CON UNA DESTINAZIONE NON C'E' PIU' NESSUN TETTO. Fino al 15 settembre
     * 2026 il corpo si accumulava tutto in g_buf, un megabyte, e quel che
     * avanzava si perdeva: usciva «TRONCATA: il buffer era piccolo» e il file
     * salvato era mezzo. Adesso, quando si sa gia' dove va a finire, ogni pezzo
     * ci va appena arriva e il buffer torna a essere quel che deve essere —
     * una finestra sul flusso, non il posto dove sta il file.
     *
     * ! SENZA DESTINAZIONE IL TETTO RESTA, ed e' giusto cosi': `scarica <url>`
     * senza file stampa la pagina a schermo, e per stamparla bisogna averla. */
    if (dove && !solo_info) {
        g_fd = open(dove, O_WRONLY | O_CREAT | O_TRUNC);
        if (g_fd < 0) { printf("scarica: non riesco a creare %s\n", dove); return 1; }
        g_scritti = 0;
        g_guaio   = 0;
        exhttp_verso(verso_file, 0);
    }

    if (!exhttp_prendi(url, g_buf, sizeof(g_buf), &e)) {
        if (g_fd >= 0) { exhttp_verso(0, 0); close(g_fd); g_fd = -1; remove(dove); }
        printf("scarica: %s\n", e.errore[0] ? e.errore : "non riuscito");
        return 1;
    }

    if (g_fd >= 0) exhttp_verso(0, 0);

    ms = uptime_ms() - t0;

    /* ! LA CAUSA NON SI INDOVINA: LA DICE exhttp. Qui c'era scritto «TRONCATA:
     * il buffer era piccolo», che e' uno dei due casi e non l'altro — un corpo
     * interrotto a meta' rete avrebbe accusato il buffer, mandando a guardare
     * nel posto sbagliato. */
    printf("scarica: %d, %s, %u byte in %u,%u s\n", e.codice,
           e.tipo[0] ? e.tipo : "(nessun tipo)",
           g_fd >= 0 ? (unsigned int)g_scritti : e.byte,
           ms / 1000u, (ms % 1000u) / 100u);

    if (e.troncata)
        printf("         ! TRONCATA: %s\n",
               e.errore[0] ? e.errore : "il buffer era piccolo");

    /* ! SI DICE DOVE SI E' FINITI, se non e' dove si voleva andare. Con una
     * redirezione seguita in silenzio, chi guarda una pagina inattesa non ha
     * modo di sapere che l'indirizzo non e' piu' quello che aveva scritto. */
    if (e.salti > 0)
        printf("         dopo %d redirezion%s: %s\n", e.salti,
               e.salti == 1 ? "e" : "i", e.finale);

    if (solo_info) return 0;

    if (dove) {
        /* Il file e' stato scritto mentre arrivava: qui resta da chiuderlo e
         * da dire com'e' andata. */
        close(g_fd);
        g_fd = -1;

        if (g_guaio) {
            printf("scarica: la scrittura si e' fermata dopo %ld byte. "
                   "Disco pieno?\n", g_scritti);
            return 1;
        }
        printf("         salvata in %s (%ld byte)\n", dove, g_scritti);
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
