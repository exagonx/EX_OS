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
 * Il file manager
 *
 *     /exwin/bin/filemgr [DIRECTORY]
 *
 * ! L'ELENCO NON E' PIU' DISEGNATO A MANO. Fino al 17 agosto 2026 questo file
 * si disegnava le righe, la barra della scelta e lo scorrimento da se': una
 * sessantina di righe che facevano quello che oggi fa il controllo «lista» di
 * ExWin. Le stesse sessanta righe stavano anche nel dialogo Apri/Salva, e in
 * forma appena diversa nell'editor — tre volte la stessa cosa.
 *
 * Quello che resta qui e' cio' che e' DAVVERO del file manager: leggere una
 * directory, mettere le directory in cima, decidere cosa fare quando si sceglie
 * qualcosa. Il come si vede e come si scorre e' del toolkit.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

#define VOCI_MAX    512
#define PERC_MAX    192

#define ID_SU       1
#define ID_APRI     2
#define ID_LISTA    3

#define FIN_W       480
#define FIN_H       320
#define LISTA_Y     26
#define LISTA_H     (FIN_H - LISTA_Y - 30)

/* ! SI TIENE SOLO CIO' CHE LA LISTA NON SA. La lista conserva il testo e la
 * riga scelta; noi dobbiamo ricordare, per ogni voce, se e' una directory —
 * perche' e' quello che decide cosa succede all'Invio. */
static unsigned char g_dir_flag[VOCI_MAX];
static char          g_nome[VOCI_MAX][DIRENT_NAME_MAX];

static char       g_dir[PERC_MAX] = "/";
static ExFinestra g_f, g_stato, g_lista;
static char       g_avviso[96] = "";

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
    unsigned int quante = 0, d = 0;
    static unsigned int dim[VOCI_MAX];

    ex_lista_svuota(g_lista);

    while ((n = listdir_from(percorso, v, 32, start)) > 0) {
        for (i = 0; i < n && quante < VOCI_MAX; i++) {
            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;
            strncpy(g_nome[quante], v[i].name, DIRENT_NAME_MAX - 1);
            g_nome[quante][DIRENT_NAME_MAX - 1] = '\0';
            dim[quante]      = v[i].size;
            g_dir_flag[quante] = v[i].is_dir;
            quante++;
        }
        start += n;
        if (n < 32) break;
    }

    /* Le directory in cima, tenendo l'ordine fra pari. */
    for (i = 0; i < (int)quante; i++)
        if (g_dir_flag[i]) {
            char t[DIRENT_NAME_MAX];
            unsigned int td = dim[i];
            int k;

            strcpy(t, g_nome[i]);
            for (k = i; k > (int)d; k--) {
                strcpy(g_nome[k], g_nome[k - 1]);
                dim[k] = dim[k - 1];
                g_dir_flag[k] = g_dir_flag[k - 1];
            }
            strcpy(g_nome[d], t);
            dim[d] = td;
            g_dir_flag[d] = 1;
            d++;
        }

    /* E adesso nella lista, che ci pensa lei a mostrarle e a scorrerle. */
    for (i = 0; i < (int)quante; i++) {
        char riga[80];

        if (g_dir_flag[i]) sprintf(riga, "[%s]", g_nome[i]);
        else               sprintf(riga, " %-24s %8u", g_nome[i], dim[i]);

        ex_lista_aggiungi(g_lista, riga);
    }
}

static void stato_aggiorna(void)
{
    char s[200];

    if (g_avviso[0])
        sprintf(s, "%s  -  %s", g_dir, g_avviso);
    else
        sprintf(s, "%s  -  %u voci", g_dir, ex_lista_quante(g_lista));

    ex_testo_metti(g_stato, s);
}

/* -----------------------------------------------------------------------------
 * Aprire un file: lo si passa all'editor
 *
 * ! SI CERCA IN DUE POSTI, E L'ORDINE CONTA: su un sistema installato l'albero
 * sta in /exwin, avviando dal CD sta sotto /cdrom. Stessa regola del program
 * manager, per la stessa ragione.
 * --------------------------------------------------------------------------- */
static void apri_file(const char *nome)
{
    static const char *editori[2] = {
        "/exwin/bin/edit", "/cdrom/exwin/bin/edit"
    };
    static char percorso[PERC_MAX];
    char *argv[3];
    int i;

    strcpy(percorso, g_dir);
    if (percorso[strlen(percorso) - 1] != '/') strcat(percorso, "/");
    strncat(percorso, nome, PERC_MAX - strlen(percorso) - 1);

    argv[1] = percorso;
    argv[2] = 0;

    for (i = 0; i < 2; i++) {
        argv[0] = (char *)editori[i];
        if (spawn_ex(editori[i], argv, 0, 0, 0) >= 0) {
            sprintf(g_avviso, "aperto con l'editor: %s", nome);
            return;
        }
    }
    strcpy(g_avviso, "l'editor non si trova: /exwin/bin/edit");
}

static void entra(const char *nome)
{
    if (strcmp(nome, "..") == 0) {
        int i = (int)strlen(g_dir);
        while (i > 1 && g_dir[i - 1] != '/') i--;
        if (i > 1) i--;
        if (i == 0) i = 1;
        g_dir[i] = '\0';
    } else {
        char nuovo[PERC_MAX];

        strcpy(nuovo, g_dir);
        if (nuovo[strlen(nuovo) - 1] != '/') strcat(nuovo, "/");
        strcat(nuovo, nome);
        strncpy(g_dir, nuovo, PERC_MAX - 1);
        g_dir[PERC_MAX - 1] = '\0';
    }

    leggi(g_dir);
}

/* «Apri», Invio e il clic fanno la stessa cosa, e dipende da cosa e' scelto.
 * Una sola funzione perche' tre strade identiche si scollegano appena una
 * delle tre cambia. */
static void scegli(void)
{
    unsigned int s = ex_lista_scelta(g_lista);

    if (s >= ex_lista_quante(g_lista)) return;
    if (g_dir_flag[s]) entra(g_nome[s]);
    else               apri_file(g_nome[s]);
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        g_avviso[0] = '\0';
        if (wp == ID_SU)    { entra(".."); break; }
        if (wp == ID_APRI)  { scegli();    break; }

        /* ! LA LISTA MANDA IL SUO id COME UN PULSANTE, e non e' un caso: Invio
         * e clic sono la stessa decisione presa in due modi, e chi la riceve
         * non deve imparare un secondo meccanismo. Ma qui NON si apre: un clic
         * che entrasse subito in una directory renderebbe impossibile
         * scegliere senza aprire. Si apre con Invio, «Apri» o il pulsante. */
        if (wp == ID_LISTA) break;
        return 0;

    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    stato_aggiorna();
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    if (argc >= 2) {
        strncpy(g_dir, argv[1], PERC_MAX - 1);
        g_dir[PERC_MAX - 1] = '\0';
    }

    g_f = ex_crea("finestra", "File manager", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  40, 40, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("filemgr: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    ex_crea("pulsante", "Su",   EX_FIGLIO,  4, 2, 50, 20, g_f, ID_SU,   0);
    ex_crea("pulsante", "Apri", EX_FIGLIO, 58, 2, 50, 20, g_f, ID_APRI, 0);

    g_lista = ex_crea("lista", "", EX_FIGLIO,
                      4, LISTA_Y, FIN_W - 8, LISTA_H, g_f, ID_LISTA, 0);
    if (!g_lista) {
        printf("filemgr: non riesco a creare l'elenco\n");
        return 1;
    }

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, FIN_W - 12, 16, g_f, 0, 0);

    /* ! IL FUOCO ALLA LISTA, ESPLICITAMENTE. Senza andrebbe al pulsante «Su»,
     * che e' il primo controllo creato che lo accetta, e le frecce non
     * muoverebbero niente. Prima che ex_fuoco() esistesse l'unico modo era
     * creare i controlli in un ordine che non e' quello in cui si leggono. */
    ex_fuoco(g_lista);

    leggi(g_dir);
    stato_aggiorna();

    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);

    printf("filemgr: %s, %u voci\n", g_dir, ex_lista_quante(g_lista));

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
