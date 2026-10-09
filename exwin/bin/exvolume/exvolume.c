/* =============================================================================
 * exwin/bin/exvolume/exvolume.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il pannello dei volumi (10 ottobre 2026)
 *
 * Lo apre l'icona accanto all'orologio, nella barra. Una riga per ogni uscita
 * e per ogni ingresso che la scheda audio dichiara, con due cursori: il
 * canale sinistro e il destro. Quante righe, e come si chiamano, lo dice il
 * driver (exsuono_mix_leggi): su una scheda semplice ce n'e' una, «Volume».
 *
 *     exvolume             il pannello
 *     exvolume -applica    rimette i volumi salvati e finisce, senza finestra:
 *                          lo lancia la scrivania alla partenza
 *
 * ! IL DRIVER NON RICORDA NIENTE DA UN AVVIO ALL'ALTRO, e non deve: e' un
 * servizio di sistema, e i volumi sono una scelta di chi usa la macchina.
 * Stanno in $HOME/.exwin/config/volume.cfg, una riga per voce col suo NOME -
 * non col numero, che cambia se cambia la scheda o il driver.
 *
 * ! IL CURSORE MOSTRA QUEL CHE LA SCHEDA HA FATTO, non quel che si e' chiesto.
 * Ogni movimento torna indietro dal driver col valore vero: una scheda coi
 * due canali legati li muove insieme, una presa col solo «acceso/spento»
 * salta da 0 a 100, e il pannello lo fa vedere invece di mentire.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exsuono.h"
#include "kbd_proto.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("exvolume", VERSIONE_APP);

#define VOCI_MAX    16
#define FIN_W       524
#define RIGA_H      24
#define BARRA_H     28          /* la barra della scrivania, in fondo allo schermo */

#define ID_INSIEME  10
#define ID_CURSORE  100         /* 100 + 2*voce (sinistra), 101 + 2*voce (destra) */

static ExWindow     g_f, g_insieme_c;
static ExWindow     g_cs[VOCI_MAX], g_cd[VOCI_MAX], g_num[VOCI_MAX];
static ExSuonoVoce  g_v[VOCI_MAX];
static int          g_n = 0;
static int          g_insieme = 1;      /* i due canali si muovono insieme */
static int          g_sporco = 0;       /* c'e' qualcosa da salvare */

/* -----------------------------------------------------------------------------
 * Il file delle scelte
 * --------------------------------------------------------------------------- */
static const char *file_cfg(int crea)
{
    static char f[240];
    const char *casa = getenv("HOME");
    char        d[200];

    if (!casa || !casa[0] || strcmp(casa, "/") == 0)
        casa = (getuid() == 0) ? "/root" : "";
    if (crea) {
        if (casa[0]) mkdir(casa, 0700);
        snprintf(d, sizeof(d), "%s/.exwin", casa);        mkdir(d, 0755);
        snprintf(d, sizeof(d), "%s/.exwin/config", casa); mkdir(d, 0755);
    }
    snprintf(f, sizeof(f), "%s/.exwin/config/volume.cfg", casa);
    return f;
}

static void salva(void)
{
    FILE *f = fopen(file_cfg(1), "w");
    int   i;

    g_sporco = 0;
    if (!f) return;
    fprintf(f, "# I volumi scelti dal pannello (exvolume). nome = sinistra,destra\n");
    fprintf(f, "insieme = %s\n", g_insieme ? "si" : "no");
    for (i = 0; i < g_n; i++)
        fprintf(f, "%s = %u,%u\n", g_v[i].nome, g_v[i].sin, g_v[i].des);
    fclose(f);
}

/* Legge il file e, per ogni riga che nomina una voce che c'e', la porta a quei
 * valori. Rende quante ne ha messe. */
static int carica_e_applica(void)
{
    FILE *f = fopen(file_cfg(0), "r");
    char  riga[160];
    int   messe = 0;

    if (!f) return 0;
    while (fgets(riga, sizeof(riga), f)) {
        char *u = strrchr(riga, '='), *nome = riga, *val, *fine;
        unsigned int s, d;
        int i;

        if (riga[0] == '#' || !u) continue;
        *u = 0; val = u + 1;
        while (*nome == ' ') nome++;
        fine = nome + strlen(nome);
        while (fine > nome && (fine[-1] == ' ' || fine[-1] == '\t')) *--fine = 0;
        while (*val == ' ') val++;

        if (strcmp(nome, "insieme") == 0) { g_insieme = (val[0] != 'n'); continue; }

        s = (unsigned int)strtol(val, &fine, 10);
        d = (*fine == ',') ? (unsigned int)strtol(fine + 1, 0, 10) : s;
        for (i = 0; i < g_n; i++) {
            if (strcmp(g_v[i].nome, nome) != 0) continue;
            if (exsuono_mix_metti((unsigned int)i, s, d, &g_v[i]) >= 0) messe++;
            break;
        }
    }
    fclose(f);
    return messe;
}

/* Le voci della scheda, com'e' adesso. Rende quante sono. */
static int leggi_voci(void)
{
    int tot, i;

    g_n = 0;
    tot = exsuono_mix_leggi(0, &g_v[0]);
    if (tot <= 0) return 0;
    if (tot > VOCI_MAX) tot = VOCI_MAX;
    for (i = 1; i < tot; i++)
        if (exsuono_mix_leggi((unsigned int)i, &g_v[i]) < 0) break;
    g_n = i;
    return g_n;
}

/* -----------------------------------------------------------------------------
 * La finestra
 * --------------------------------------------------------------------------- */
static void riga_mostra(int i)
{
    char t[24];

    ex_scroll_set_pos(g_cs[i], g_v[i].sin);
    ex_scroll_set_pos(g_cd[i], g_v[i].des);
    snprintf(t, sizeof(t), "%3u %3u", g_v[i].sin, g_v[i].des);
    ex_set_text(g_num[i], t);
}

static void cursore_mosso(unsigned int id, unsigned int valore)
{
    int i = (int)(id - ID_CURSORE) / 2, destro = (int)(id - ID_CURSORE) & 1;
    unsigned int s, d;

    if (i < 0 || i >= g_n) return;
    if (valore > 100) valore = 100;
    s = g_v[i].sin; d = g_v[i].des;
    if (g_insieme)   s = d = valore;
    else if (destro) d = valore;
    else             s = valore;

    exsuono_mix_metti((unsigned int)i, s, d, &g_v[i]);
    riga_mostra(i);
    g_sporco = 1;
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_update(g_f);
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        if (g_sporco) salva();
        ex_quit(0);
        return 0;

    case EXM_TIMER:
        /* Si salva un secondo dopo l'ultimo movimento, non a ogni pixel di
         * trascinamento: e se la macchina si spegne senza chiudere il
         * pannello la scelta c'e' gia'. */
        if (g_sporco) salva();
        return 0;

    case EXM_KEY:
        if ((wp & KBD_KEY_MASK) == 27) { proc(f, EXM_CLOSE, 0, 0); return 0; }
        return ex_default_proc(f, msg, wp, lp);

    case EXM_COMMAND:
        if (wp == ID_INSIEME) {
            g_insieme = ex_is_checked(g_insieme_c);
            g_sporco = 1;
        } else if (wp >= ID_CURSORE && wp < ID_CURSORE + 2 * VOCI_MAX) {
            cursore_mosso(wp, (unsigned int)lp);
        }
        return 0;

    default:
        return ex_default_proc(f, msg, wp, lp);
    }
}

static int una_riga(int i, int y)
{
    g_cs[i] = g_cd[i] = g_num[i] = 0;
    ex_create("label", g_v[i].nome, EX_CHILD, 20, y + 3, 176, 18, g_f, 0, 0);
    ex_create("label", "S", EX_CHILD, 200, y + 3, 10, 18, g_f, 0, 0);
    g_cs[i] = ex_create("scrollbar", "", EX_CHILD, 212, y + 3, 106, 16, g_f,
                        (unsigned int)(ID_CURSORE + 2 * i), 0);
    ex_create("label", "D", EX_CHILD, 326, y + 3, 10, 18, g_f, 0, 0);
    g_cd[i] = ex_create("scrollbar", "", EX_CHILD, 338, y + 3, 106, 16, g_f,
                        (unsigned int)(ID_CURSORE + 2 * i + 1), 0);
    g_num[i] = ex_create("label", "", EX_CHILD, 452, y + 3, 64, 18, g_f, 0, 0);
    ex_scroll_set_range(g_cs[i], 100, 0);
    ex_scroll_set_range(g_cd[i], 100, 0);
    riga_mostra(i);
    return y + RIGA_H;
}

int main(int argc, char **argv)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          i, y, uscite = 0, ingressi = 0, alto, solo_applica = 0;

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-applica") == 0) solo_applica = 1;

    leggi_voci();

    if (solo_applica) {
        /* Niente scheda, niente file, niente voce con quel nome: non e' un
         * errore, e' una macchina che non ha (ancora) scelte da rimettere. */
        if (g_n > 0) carica_e_applica();
        return 0;
    }

    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("exvolume: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    /* Il file dice anche se i due canali vanno insieme; i valori veri sono
     * quelli che la scheda ha adesso, e sono gia' in g_v. */
    {
        FILE *f = fopen(file_cfg(0), "r");
        char  riga[160];

        if (f) {
            while (fgets(riga, sizeof(riga), f))
                if (strncmp(riga, "insieme", 7) == 0 && strchr(riga, '='))
                    g_insieme = (strstr(riga, "no") == 0);
            fclose(f);
        }
    }

    for (i = 0; i < g_n; i++) {
        if (g_v[i].tipo == EXSUONO_INGRESSO) ingressi++; else uscite++;
    }
    alto = 30 + (uscite ? 22 + uscite * RIGA_H : 0) + (ingressi ? 26 + ingressi * RIGA_H : 0) + 40;
    if (g_n == 0) alto = 110;

    /* In basso a destra, sopra la barra: dove sta l'icona che lo apre. */
    g_f = ex_create("window", "Volume", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    (int)sw - FIN_W - 6, (int)sh - BARRA_H - alto - 6, FIN_W, alto, 0, 0, proc);
    if (!g_f) {
        printf("exvolume: non riesco a creare la finestra\n");
        return 1;
    }

    y = 26;
    if (g_n == 0) {
        ex_create("label", "La scheda audio non risponde.", EX_CHILD, 14, y + 8, FIN_W - 28, 18, g_f, 0, 0);
        ex_create("label", "Da una console:  audio -i", EX_CHILD, 14, y + 30, FIN_W - 28, 18, g_f, 0, 0);
    } else {
        if (uscite) {
            ex_create("label", "Uscite", EX_CHILD, 10, y, 200, 18, g_f, 0, 0);
            y += 22;
            for (i = 0; i < g_n; i++) if (g_v[i].tipo != EXSUONO_INGRESSO) y = una_riga(i, y);
        }
        if (ingressi) {
            y += 4;
            ex_create("label", "Ingressi (quanto si sentono nelle uscite)", EX_CHILD, 10, y, 400, 18, g_f, 0, 0);
            y += 22;
            for (i = 0; i < g_n; i++) if (g_v[i].tipo == EXSUONO_INGRESSO) y = una_riga(i, y);
        }
        y += 8;
        g_insieme_c = ex_create("checkbox", "Sinistra e destra insieme", EX_CHILD, 10, y, 260, 20,
                                g_f, ID_INSIEME, 0);
        ex_set_checked(g_insieme_c, g_insieme);
    }

    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_update(g_f);
    ex_set_timer(g_f, 1000);

    while (ex_get_message(&m)) ex_dispatch(&m);
    if (g_sporco) salva();
    return 0;
}
