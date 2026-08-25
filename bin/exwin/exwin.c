/* =============================================================================
 * bin/exwin/exwin.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * exwin — accende l'interfaccia grafica
 *
 *     exwin            server e scrivania sulla prossima console
 *     exwin -c 2       su una console scelta
 *     exwin -s FILE    con un'immagine di sfondo
 *
 * ! QUESTO STA IN /bin E NON IN /exwin/bin, e non e' una svista. Lo si digita
 * da una shell, prima che la grafica esista: metterlo insieme alle
 * applicazioni grafiche vorrebbe dire cercarlo dove si arriva solo dopo
 * averlo eseguito.
 *
 * ! LA GRAFICA NASCE SU UN'ALTRA CONSOLE, ed e' tutta la ragione di questo
 * comando. La shell da cui si digita resta viva sulla sua: con Alt+Fn si passa
 * dall'una all'altra, e se il server muore si torna al prompt invece che a uno
 * schermo fermo. Avviare il server sulla PROPRIA console vorrebbe dire due
 * programmi che si contendono lo schermo e la tastiera.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `exwin -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("exwin", "0.001");

/* ! I PERCORSI SI CERCANO IN DUE POSTI. Su un sistema installato l'albero sta
 * nella radice; avviando dal CD sta sotto /cdrom. Cercare solo il primo
 * vorrebbe dire un comando che non funziona proprio quando si prova il CD. */
static const char *trova(const char *a, const char *b)
{
    if (access(a, 0) == 0) return a;
    if (access(b, 0) == 0) return b;
    return 0;
}

int main(int argc, char **argv)
{
    const char *server, *pm, *sfondo = 0;
    ConsoleInfo ci;
    VideoInfo   v;
    char        c_arg[8];
    char       *sv[6], *pv[4];
    int i, console = -1, n, partenza = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) console = atoi(argv[++i]);
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) sfondo = argv[++i];
        else if (strcmp(argv[i], "-h") == 0) {
            printf("uso: exwin [-c CONSOLE] [-s IMMAGINE] [opzioni del server]\n");
            printf("  accende server grafico e scrivania su un'altra console.\n");
            return 0;
        }
    }

    /* ! LO SCHERMO DEV'ESSERE GIA' IN GRAFICA, e non lo si puo' cambiare da
     * qui: la modalita' la imposta Stage 2 con il BIOS, prima del modo
     * protetto. Dirlo adesso, con il rimedio, evita di far scoprire il
     * problema a un server che muore senza spiegazioni. */
    if (video_info(&v) != 0 || v.larghezza == 0) {
        printf("exwin: lo schermo e' in modo TESTO.\n");
        printf("       Scegli una risoluzione e riavvia:\n");
        printf("         /dev/svga.drv 800x600   (poi  reboot)\n");
        return 1;
    }

    /* ! NON PIU' /dev/wserver.drv, dal 19 agosto 2026. Il server non e' un
     * driver: mappa il framebuffer con fb_map(), che non chiede privilegi.
     * Finche' stava in /dev — che e' di root — un utente normale non poteva
     * eseguirlo, quindi non poteva avere una scrivania. */
    server = trova("/exwin/bin/wserver", "/cdrom/exwin/bin/wserver");
    pm     = trova("/exwin/bin/pm",    "/cdrom/exwin/bin/pm");

    if (!server) { printf("exwin: /exwin/bin/wserver non trovato\n"); return 1; }
    if (!pm)     { printf("exwin: /exwin/bin/pm non trovato\n"); return 1; }

    /* =====================================================================
     * ! L'ULTIMA CONSOLE, NON LA PROSSIMA.
     *
     * Prima si prendeva «quella dopo la mia»: lanciando exwin dalla prima si
     * finiva sulla seconda, cioe' in mezzo alle console di testo. Chi lavora
     * in testo si ritrovava Alt+F2 occupato dalla grafica senza averlo
     * chiesto, e le console utili diventavano tre.
     *
     * L'ultima e' di casa: Alt+F1..F4 restano di chi scrive, la grafica sta in
     * fondo. E siccome e' l'ULTIMA e non un numero scritto qui, il giorno che
     * le console diventassero sei questo file non cambia.
     * ===================================================================== */
    if (console_info(&ci) != 0) {
        printf("exwin: non so su che console sono\n");
        return 1;
    }
    partenza = (int)ci.mia;

    if (console < 0) {
        console = (int)ci.totale - 1;
        if (console == partenza) {
            printf("exwin: c'e' una sola console: la grafica coprirebbe la shell\n");
            return 1;
        }
    }

    sprintf(c_arg, "%d", console);

    n = 0;
    sv[n++] = (char *)server;
    sv[n++] = "-c";
    sv[n++] = c_arg;
    sv[n++] = "-t";     /* la console e' sua: la tastiera pure */

    /* ! CIO' CHE exwin NON RICONOSCE LO PASSA AL SERVER, invece di rifiutarlo.
     * Le opzioni del server sono sue e cambiano con lui: elencarle anche qui
     * vorrebbe dire una seconda verita' accanto a quella vera, e le due
     * divergono alla prima opzione aggiunta. Cosi' `exwin -nommx` arriva dove
     * deve senza che questo file sappia cosa voglia dire. */
    for (i = 1; i < argc && n < (int)(sizeof(sv)/sizeof(sv[0])) - 1; i++) {
        if (strcmp(argv[i], "-c") == 0) { i++; continue; }
        if (strcmp(argv[i], "-s") == 0) { i++; continue; }
        sv[n++] = argv[i];
    }
    sv[n]   = 0;

    /* ! L'AMBIENTE VA PASSATO A MANO: `envp` nullo vuol dire ambiente VUOTO,
     * non «eredita». Da qui nasce tutta la scrivania, quindi qui si decide se
     * le applicazioni grafiche sapranno chi le sta usando — a cominciare da
     * HOME. Vedi lo stesso commento in wserver.c e in pm.c. */
    if (spawn_ex(sv[0], sv, environ, 0, 0) < 0) {
        printf("exwin: non riesco ad avviare %s\n", server);
        return 1;
    }

    /* ! SI ASPETTA CHE IL SERVER SI REGISTRI, invece di sperarci. La
     * scrivania lo cerca per nome e ha una sua attesa, ma partire mentre il
     * server sta ancora mappando il framebuffer vuol dire tre secondi di
     * niente sullo schermo senza che si capisca cosa stia succedendo. */
    usleep(600000);

    n = 0;
    pv[n++] = (char *)pm;
    if (sfondo) { pv[n++] = "-s"; pv[n++] = (char *)sfondo; }
    pv[n] = 0;

    if (spawn_ex(pv[0], pv, environ, 0, 0) < 0) {
        printf("exwin: non riesco ad avviare %s\n", pm);
        return 1;
    }

    /* ! CI SI VA, NON SI DICE COME ANDARCI. Stampare «Alt+F5 per andarci» e
     * lasciare l'utente davanti alla shell era una schermata di istruzioni al
     * posto della cosa chiesta: chi digita `exwin` vuole la grafica, adesso.
     * Il messaggio resta, ma sulla console da cui si e' partiti, cosi' e' li'
     * ad aspettare quando si torna. */
    printf("exwin: grafica accesa sulla console %d.\n", console + 1);
    printf("       Alt+F%d per tornare qui.\n", partenza + 1);

    if (console_switch((unsigned int)console) != 0)
        printf("exwin: non riesco a passare alla console %d: vacci con Alt+F%d\n",
               console + 1, console + 1);
    return 0;
}
