/* =============================================================================
 * exwin/bin/filemgr/filemgr.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il file manager: la prima applicazione grafica di EX-OS
 *
 *     /exwin/bin/filemgr [DIRECTORY]
 *
 * ! LA PRIMA APPLICAZIONE VERA, e serve a due cose insieme: essere utile, e
 * far vedere dove il toolkit non basta. Un elenco che scorre, un doppio clic
 * che entra in una directory e una riga di stato sono il minimo con cui si
 * scopre cosa manca — e finora ha gia' detto che manca una lista a
 * scorrimento, che qui e' fatta a mano.
 *
 * ! DISEGNA DA SE' L'ELENCO invece di usare i controlli. Un controllo per
 * riga vorrebbe dire crearne e distruggerne decine a ogni cambio di
 * directory, e il toolkit non ha ancora una lista: meglio un disegno onesto
 * che una finta lista fatta di pulsanti.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

#define RIGA_H      16
#define VOCI_MAX    256
#define PERC_MAX    192

#define ID_SU       1
#define ID_APRI     2

typedef struct {
    char         nome[DIRENT_NAME_MAX];
    unsigned int dim;
    unsigned char dir;
} Voce;

static Voce         g_voce[VOCI_MAX];
static unsigned int g_n = 0, g_sel = 0, g_primo = 0;
static char         g_dir[PERC_MAX] = "/";
static ExFinestra   g_f, g_stato;
static int          g_lista_h = 0;

/* -----------------------------------------------------------------------------
 * Leggere una directory
 *
 * ! LE DIRECTORY VENGONO PRIMA, e non e' estetica: in una directory con cento
 * file, quelle in cui si vuole entrare sarebbero sparse in mezzo. E' l'unica
 * cosa che questo elenco ordina — ordinare i nomi vorrebbe dire un confronto
 * che dipende dalla lingua, e non e' il momento.
 * --------------------------------------------------------------------------- */
static void leggi(const char *percorso)
{
    DirEntry v[32];
    int start = 0, n, i;
    unsigned int d = 0;

    g_n = 0; g_sel = 0; g_primo = 0;

    while ((n = listdir_from(percorso, v, 32, start)) > 0) {
        for (i = 0; i < n && g_n < VOCI_MAX; i++) {
            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;
            strncpy(g_voce[g_n].nome, v[i].name, DIRENT_NAME_MAX - 1);
            g_voce[g_n].nome[DIRENT_NAME_MAX - 1] = '\0';
            g_voce[g_n].dim = v[i].size;
            g_voce[g_n].dir = v[i].is_dir;
            g_n++;
        }
        start += n;
        if (n < 32) break;
    }

    /* Le directory in cima, tenendo l'ordine fra pari. */
    for (i = 0; i < (int)g_n; i++)
        if (g_voce[i].dir) {
            Voce t = g_voce[i];
            int k;
            for (k = i; k > (int)d; k--) g_voce[k] = g_voce[k - 1];
            g_voce[d++] = t;
        }
}

static void stato_aggiorna(void)
{
    char s[160];

    sprintf(s, "%s  -  %u voci", g_dir, g_n);
    ex_testo_metti(g_stato, s);
}

/* Entra in una directory, o lo dice se non ci riesce. */
static void entra(const char *nome)
{
    char nuovo[PERC_MAX];

    if (strcmp(nome, "..") == 0) {
        int i = (int)strlen(g_dir);
        while (i > 1 && g_dir[i - 1] != '/') i--;
        if (i > 1) i--;
        if (i == 0) i = 1;
        g_dir[i] = '\0';
    } else {
        strcpy(nuovo, g_dir);
        if (nuovo[strlen(nuovo) - 1] != '/') strcat(nuovo, "/");
        strcat(nuovo, nome);
        strncpy(g_dir, nuovo, PERC_MAX - 1);
        g_dir[PERC_MAX - 1] = '\0';
    }

    leggi(g_dir);
    stato_aggiorna();
}

/* -----------------------------------------------------------------------------
 * Il disegno dell'elenco
 * --------------------------------------------------------------------------- */
static void lista_disegna(void)
{
    unsigned int i, righe = (unsigned int)(g_lista_h / RIGA_H);

    ex_riempi(g_f, 4, 26, 472, g_lista_h, EX_BIANCO);
    ex_riquadro_disegna(g_f, 4, 26, 472, g_lista_h, EX_GRIGIO_SC);

    for (i = 0; i < righe && g_primo + i < g_n; i++) {
        unsigned int k = g_primo + i;
        int y = 28 + (int)i * RIGA_H;
        char riga[80];

        /* ! LA SELEZIONE SI DISEGNA COME FONDO, non come colore del testo: su
         * un elenco di nomi lunghi diversi, un testo colorato non dice dove
         * finisce la riga scelta. */
        if (k == g_sel) ex_riempi(g_f, 6, y - 1, 468, RIGA_H, EX_BLU);

        if (g_voce[k].dir) sprintf(riga, "[%s]", g_voce[k].nome);
        else               sprintf(riga, " %-24s %8u", g_voce[k].nome, g_voce[k].dim);

        ex_scrivi(g_f, 8, y, riga, (k == g_sel) ? EX_BIANCO : EX_NERO);
    }
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_SU)   { entra(".."); break; }
        if (wp == ID_APRI) {
            if (g_sel < g_n && g_voce[g_sel].dir) entra(g_voce[g_sel].nome);
            break;
        }
        return 0;

    case EXM_TASTO:
        /* ! I TASTI SPECIALI STANNO DA 0x100 IN SU, apposta per non poterli
         * confondere con un carattere: il servizio 'kbd' li consegna gia'
         * cosi', e qui non si tocca nessuna mappa di tastiera. */
        if (wp == 0x0101 /* giu' */) { if (g_sel + 1 < g_n) g_sel++; }
        else if (wp == 0x0100 /* su */) { if (g_sel > 0) g_sel--; }
        else if ((wp & 0xFFFF) == '\n' || (wp & 0xFFFF) == '\r') {
            if (g_sel < g_n && g_voce[g_sel].dir) entra(g_voce[g_sel].nome);
        } else return 0;

        /* La finestra scorre dietro alla selezione, o si sceglie una riga che
         * non si vede. */
        {
            unsigned int righe = (unsigned int)(g_lista_h / RIGA_H);
            if (g_sel < g_primo) g_primo = g_sel;
            if (g_sel >= g_primo + righe) g_primo = g_sel - righe + 1;
        }
        break;

    case EXM_MOUSE_GIU: {
        int y = EX_Y(lp);
        if (y >= 26 && y < 26 + g_lista_h) {
            unsigned int k = g_primo + (unsigned int)((y - 28) / RIGA_H);
            if (k < g_n) g_sel = k;
        } else return 0;
        break;
    }

    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    lista_disegna();
    ex_aggiorna(f);
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    if (argc >= 2) { strncpy(g_dir, argv[1], PERC_MAX - 1); g_dir[PERC_MAX-1] = '\0'; }

    g_f = ex_crea("finestra", "File manager", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  40, 40, 480, 320, 0, 0, proc);
    if (!g_f) {
        printf("filemgr: il server a finestre non risponde.\n");
        return 1;
    }
    g_lista_h = 320 - 26 - 30;

    ex_crea("pulsante", "Su",  EX_FIGLIO,   4, 2, 50, 20, g_f, ID_SU,   0);
    ex_crea("pulsante", "Apri", EX_FIGLIO, 58, 2, 50, 20, g_f, ID_APRI, 0);
    g_stato = ex_crea("etichetta", "", EX_FIGLIO, 6, 320 - 22, 468, 16, g_f, 0, 0);

    leggi(g_dir);
    stato_aggiorna();

    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    lista_disegna();
    ex_aggiorna(g_f);

    printf("filemgr: %s, %u voci\n", g_dir, g_n);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
