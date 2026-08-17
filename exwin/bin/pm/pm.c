/* =============================================================================
 * exwin/bin/pm/pm.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il program manager: scrivania, barra delle applicazioni, menu di avvio
 *
 *     /exwin/bin/pm            la scrivania
 *     /exwin/bin/pm -s FILE    con un'immagine di sfondo
 *
 * ! STA IN /exwin E NON IN /bin, ED E' UNA DECISIONE, NON UN VEZZO. I
 * programmi di /bin si lanciano da una shell e parlano con un terminale;
 * questi vogliono il server a finestre, e lanciati da una shell senza server
 * non fanno niente. Tenerli mescolati vorrebbe dire un `ls /bin` in cui meta'
 * dei nomi non si puo' usare li' dove si sta guardando. La stessa ragione per
 * cui i driver stanno in /dev e non in /bin.
 *
 * ! E L'ELENCO DELLE APPLICAZIONI E' UN FILE, NON UNA TABELLA COMPILATA.
 * /exwin/lib/applicazioni.txt: aggiungerne una e' una riga. Un elenco dentro
 * il binario vorrebbe dire rifare il program manager per ogni applicazione
 * nuova — e chi installa un programma non ha i sorgenti.
 *
 * ! LA BARRA STA SOPRA A TUTTO (EX_SOPRA). Se una finestra qualunque potesse
 * coprirla, l'unico modo di tornare al menu sarebbe spostare quella finestra —
 * e con una finestra a schermo intero non si potrebbe affatto.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

#define BARRA_H     28
#define MENU_W      220
#define VOCE_H      24
#define APP_MAX     16

#define ID_AVVIO    1
#define ID_VOCE     100     /* ID_VOCE + n = la voce n del menu */
#define ID_ESCI     90
#define ID_SPEGNI   91


typedef struct {
    char nome[32];
    char percorso[96];
} App;

static App          g_app[APP_MAX];
static unsigned int g_app_n = 0;
static unsigned int g_da_cdrom = 0;

static ExFinestra g_barra, g_menu = 0;
static unsigned int g_sw, g_sh;

/* -----------------------------------------------------------------------------
 * L'elenco delle applicazioni
 *
 * ! UNA RIGA SBAGLIATA SI SALTA, NON FERMA TUTTO. Un file di elenco e' una
 * cosa che si modifica a mano: un refuso non deve lasciare senza scrivania chi
 * lo ha fatto, deve solo far mancare quella voce.
 * --------------------------------------------------------------------------- */
static void taglia(char *s)
{
    int i = (int)strlen(s);

    while (i > 0 && (s[i-1] == ' ' || s[i-1] == '\t' ||
                     s[i-1] == '\n' || s[i-1] == '\r')) s[--i] = '\0';
}

static void applicazioni_leggi(const char *percorso)
{
    int fd = open(percorso, O_RDONLY);

    if (fd >= 0 && strncmp(percorso, "/cdrom", 6) == 0) g_da_cdrom = 1;
    char buf[2048];
    int n, i, r = 0;
    char riga[160];

    if (fd < 0) return;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    for (i = 0; i <= n; i++) {
        if (buf[i] != '\n' && buf[i] != '\0') {
            if (r + 1 < (int)sizeof(riga)) riga[r++] = buf[i];
            continue;
        }
        riga[r] = '\0';
        r = 0;

        {
            char *barra = strchr(riga, '|');
            char *p;

            if (riga[0] == '#' || riga[0] == '\0' || !barra) continue;
            if (g_app_n >= APP_MAX) break;

            *barra = '\0';
            p = barra + 1;
            while (*p == ' ' || *p == '\t') p++;

            taglia(riga);
            taglia(p);
            if (riga[0] == '\0' || p[0] == '\0') continue;

            strncpy(g_app[g_app_n].nome, riga, sizeof(g_app[0].nome) - 1);
            strncpy(g_app[g_app_n].percorso, p, sizeof(g_app[0].percorso) - 1);
            g_app_n++;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Il menu di avvio
 *
 * ! E' UNA FINESTRA, NON UN DISEGNO SULLA BARRA. Cosi' sta sopra alle altre
 * senza casi particolari, si chiude distruggendola, e i clic sulle sue voci
 * arrivano come EXM_COMANDO — cioe' con lo stesso meccanismo di tutto il
 * resto, invece che con un calcolo di coordinate a mano.
 * --------------------------------------------------------------------------- */
static long menu_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp);

static void menu_chiudi(void)
{
    if (g_menu) { ex_distruggi(g_menu); g_menu = 0; }
}

static void menu_apri(void)
{
    unsigned int i;
    int h;

    if (g_menu) { menu_chiudi(); return; }   /* premuto due volte: si chiude */

    /* ! «ESCI» E «SPEGNI» CI SONO ANCHE SENZA APPLICAZIONI. Un menu che si
     * rifiutasse di aprirsi perche' l'elenco e' vuoto lascerebbe senza modo
     * di uscire dalla grafica — e uscire e' proprio cio' che si vuole fare
     * quando non c'e' niente da avviare. */
    h = (int)(g_app_n * VOCE_H) + 8 + 2 * VOCE_H + 6;

    g_menu = ex_crea("finestra", "", EX_BORDO | EX_SOPRA,
                     4, (int)g_sh - BARRA_H - h - 2, MENU_W, h, 0, 0, menu_proc);
    if (!g_menu) return;

    for (i = 0; i < g_app_n; i++)
        ex_crea("pulsante", g_app[i].nome, EX_FIGLIO,
                4, 4 + (int)i * VOCE_H, MENU_W - 8, VOCE_H - 2,
                g_menu, ID_VOCE + i, 0);

    /* Una riga a separare le applicazioni da cio' che spegne le cose: sono
     * due categorie diverse, e un clic sbagliato costa molto di piu' da una
     * parte che dall'altra. */
    ex_crea("separatore", "", EX_FIGLIO,
            6, 6 + (int)g_app_n * VOCE_H, MENU_W - 12, 2, g_menu, 0, 0);

    ex_crea("pulsante", "Esci", EX_FIGLIO,
            4, 10 + (int)g_app_n * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_ESCI, 0);
    ex_crea("pulsante", "Spegni", EX_FIGLIO,
            4, 10 + (int)(g_app_n + 1) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_SPEGNI, 0);

    ex_procedura_base(g_menu, EXM_DISEGNA, 0, 0);
}

static void avvia(unsigned int n)
{
    char *argv[2];

    if (n >= g_app_n) return;

    argv[0] = g_app[n].percorso;
    argv[1] = 0;

    /* ! SE NON PARTE SI DICE, e non si resta zitti: un menu in cui premere una
     * voce non fa niente e non spiega niente e' peggio di un menu senza quella
     * voce. Il messaggio va sulla seriale perche' qui non c'e' un terminale a
     * cui dirlo — la scrivania e' l'unica cosa a video. */
    if (spawn_ex(argv[0], argv, 0, 0, 0) < 0) {
        char m[160];
        sprintf(m, "pm: non riesco ad avviare %s", argv[0]);
        log_seriale(m);
    }
}

static long menu_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO) {
        menu_chiudi();

        /* ! «ESCI» CHIUDE SOLO LA SCRIVANIA, NON IL SERVER. Il server e' di
         * chi lo ha avviato — `exwin`, o kernel.cfg — e potrebbe avere altre
         * finestre aperte: chiuderlo da qui vorrebbe dire portarsi via il
         * lavoro di qualcun altro. Chi torna alla shell trova la console di
         * testo con Alt+Fn, che e' li' apposta. */
        if (wp == ID_ESCI) {
            log_seriale("pm: uscita chiesta dal menu");
            ex_esci(0);
            return 0;
        }

        /* ! SPEGNERE SINCRONIZZA I DISCHI, e lo fa il kernel: qui si chiede e
         * basta. Se rende, vuol dire che ha rifiutato — e allora si dice,
         * invece di lasciare una scrivania che sembra aver ignorato il
         * comando. */
        if (wp == ID_SPEGNI) {
            log_seriale("pm: spegnimento chiesto dal menu");
            reboot(EXOS_RB_POWEROFF);
            log_seriale("pm: il kernel ha rifiutato lo spegnimento");
            return 0;
        }

        avvia(wp - ID_VOCE);
        return 0;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

/* -----------------------------------------------------------------------------
 * La barra
 * --------------------------------------------------------------------------- */
static long barra_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO && wp == ID_AVVIO) { menu_apri(); return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExMsg m;
    const char *sfondo = 0;
    int i;

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) sfondo = argv[++i];

    ex_schermo(&g_sw, &g_sh);
    if (g_sw == 0) {
        printf("pm: il server a finestre non risponde, o lo schermo e' in testo\n");
        return 1;
    }

    /* ! SI CERCA IN DUE POSTI, E L'ORDINE CONTA. Su un sistema installato
     * l'albero sta in /exwin; avviando dal CD sta sotto /cdrom, perche' il
     * kernel monta il lettore come radice solo quando non c'e' il floppy.
     * Cercare solo il primo vorrebbe dire una scrivania senza applicazioni
     * ogni volta che si prova il CD — e il menu vuoto non dice perche'. */
    applicazioni_leggi("/exwin/lib/applicazioni.txt");
    if (g_app_n == 0) applicazioni_leggi("/cdrom/exwin/lib/applicazioni.txt");

    /* ! E I PERCORSI DELLE APPLICAZIONI SEGUONO L'ELENCO. Se l'elenco l'abbiamo
     * trovato sul CD, anche i programmi stanno li': un elenco che dice
     * /exwin/bin/filemgr letto da /cdrom fa premere una voce che non avvia
     * niente. */
    if (g_app_n && g_da_cdrom) {
        unsigned int k;
        for (k = 0; k < g_app_n; k++)
            if (strncmp(g_app[k].percorso, "/exwin/", 7) == 0) {
                char t[96];
                strcpy(t, "/cdrom");
                strncat(t, g_app[k].percorso, sizeof(t) - 8);
                strncpy(g_app[k].percorso, t, sizeof(g_app[0].percorso) - 1);
            }
    }

    /* La scrivania: una finestra come le altre, con lo stile che la tiene
     * sotto. E' il motivo per cui uno sfondo non e' un caso a parte. */
    {
        ExFinestra scr = ex_crea("finestra", "", EX_SFONDO,
                                 0, 0, (int)g_sw, (int)g_sh - BARRA_H, 0, 0, 0);
        if (!scr) {
            /* ! IL CONSIGLIO E' «exwin», NON IL DRIVER A MANO, e la
             * differenza non e' comodita': wserver.drv avviato cosi' nasce
             * sulla console della shell e le contende la tastiera. E' exwin
             * che lo fa ripartire su una console sua. Un messaggio che
             * suggerisce il comando sbagliato costa piu' di un messaggio che
             * non dice niente. */
            printf("pm: il server a finestre non risponde.\n");
            printf("    Avvialo con:  exwin\n");
            return 1;
        }
        ex_riempi(scr, 0, 0, (int)g_sw, (int)g_sh - BARRA_H, EX_BLU);
        if (sfondo && !ex_immagine(scr, sfondo, 0, 0)) {
            char msg[160];
            sprintf(msg, "pm: %s: formato non riconosciuto", sfondo);
            log_seriale(msg);
        }
        ex_aggiorna(scr);
    }

    g_barra = ex_crea("finestra", "", EX_SOPRA,
                      0, (int)g_sh - BARRA_H, (int)g_sw, BARRA_H,
                      0, 0, barra_proc);
    if (!g_barra) return 1;

    ex_crea("pulsante", "Avvio", EX_FIGLIO, 2, 2, 70, BARRA_H - 4,
            g_barra, ID_AVVIO, 0);
    ex_crea("etichetta", "EX-OS", EX_FIGLIO, (int)g_sw - 56, 6, 50, 16,
            g_barra, 0, 0);

    ex_procedura_base(g_barra, EXM_DISEGNA, 0, 0);

    printf("pm: scrivania attiva, %u applicazioni nel menu\n", g_app_n);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
