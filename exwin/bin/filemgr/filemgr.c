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
 * Il file manager, a due aree
 *
 *     /exwin/bin/filemgr [DIRECTORY]
 *
 *     +----------------------------------------------------------+
 *     | File   Comandi   Info                                    |
 *     +---------------------+------------------------------------+
 *     | - /                 | [bin]                              |
 *     |   + bin             | [dev]                              |
 *     |   - exwin           |  leggimi.txt          1024         |
 *     |     + bin           |                                    |
 *     +---------------------+------------------------------------+
 *     | /exwin  -  4 voci                                        |
 *     +----------------------------------------------------------+
 *
 * ! A SINISTRA C'E' DOVE SI E', A DESTRA COSA C'E'. Con una lista sola le due
 * domande si rispondono a turno: per sapere dov'e' un file bisogna risalire, e
 * risalendo si perde di vista il file. E' la ragione per cui ogni file manager
 * mai scritto ha due aree, e non e' una questione di gusto.
 *
 * ! L'ALBERO NON E' UN CONTROLLO NUOVO DEL TOOLKIT, E' UNA LISTA CON DENTRO
 * L'INDENTAZIONE. Un «controllo albero» vorrebbe dire nodi, figli, un modello
 * da tenere aggiornato e un disegno tutto suo dentro exwin.so — cioe' un pezzo
 * di toolkit che UNA sola applicazione usa. Qui l'albero e' un vettore di nodi
 * in ORDINE DI VISUALIZZAZIONE: espandere vuol dire infilare i figli subito
 * dopo il padre, chiudere vuol dire toglierli. La lista non sa che sia un
 * albero, e non deve saperlo.
 *
 * ! E IL PERCORSO DI UN NODO SI RICOSTRUISCE ALL'INDIETRO, senza puntatori al
 * padre. Con l'inserimento e la rimozione in mezzo al vettore, un indice del
 * padre sarebbe da correggere in tutti i nodi ogni volta — cioe' il difetto
 * che aspetta. Il livello, invece, non cambia mai: risalire cercando il primo
 * nodo di livello minore e' O(n) e non si puo' sbagliare.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "kbd_proto.h"

#define VOCI_MAX    512
#define NODI_MAX    128
#define PERC_MAX    192
#define PROFONDITA  8       /* quanto in giu' vanno copia ricorsiva e ricerca */

#define FIN_W       700
#define FIN_H       440
#define MENU_H      20
#define BASSO       24
#define ALBERO_W    240

#define ID_ALBERO    1
#define ID_ELENCO    2

#define ID_APRI      10
#define ID_SU        11
#define ID_AGGIORNA  12
#define ID_ESCI      13

#define ID_COPIA     20
#define ID_COPIADIR  21
#define ID_CERCA     22

#define ID_ISTRUZIONI 30
#define ID_INFO       31

/* -----------------------------------------------------------------------------
 * L'albero, a sinistra
 *
 * ! IL VETTORE E' GIA' L'ORDINE IN CUI SI VEDE. Non c'e' una struttura ad
 * albero da percorrere per disegnarla: c'e' l'elenco delle righe visibili, e
 * il livello di ognuna. E' la stessa rappresentazione che usa la lista del
 * toolkit — una riga di testo per riga — quindi fra il modello e cio' che si
 * vede non c'e' niente da tenere d'accordo.
 * --------------------------------------------------------------------------- */
typedef struct {
    char          nome[DIRENT_NAME_MAX];
    unsigned char liv;          /* 0 = la radice */
    unsigned char aperto;       /* 1 = i figli sono qui sotto */
} Nodo;

static Nodo         g_nodo[NODI_MAX];
static unsigned int g_nodi = 0;

/* L'elenco di destra. Si tiene solo cio' che la lista non sa: per ogni voce,
 * se e' una directory — perche' e' quello che decide cosa fa l'Invio. */
static unsigned char g_dir_flag[VOCI_MAX];
static char          g_nome[VOCI_MAX][DIRENT_NAME_MAX];
static unsigned int  g_voci = 0;

/* ! DOPO UNA RICERCA I NOMI SONO PERCORSI INTERI, e va segnato: senza questo
 * l'Invio su un risultato cercherebbe il file dentro la directory corrente,
 * che e' proprio quella in cui non sta. */
static int g_da_ricerca = 0;

static char       g_dir[PERC_MAX] = "/";
static ExFinestra g_f, g_stato, g_albero, g_elenco, g_menu;
static char       g_avviso[120] = "";

/* =============================================================================
 * I percorsi
 *
 * ! UNA FUNZIONE SOLA CHE ATTACCA UN NOME A UNA DIRECTORY, e non uno strcat
 * scritto a mano ogni volta. Le due cose da sbagliare sono sempre le stesse —
 * la barra doppia quando la directory e' «/», e il traboccamento — e scritte
 * in sei punti si sbagliano in almeno uno.
 * ============================================================================= */
/* =============================================================================
 * ! UN NOME DI FILE PUO' ESSERE LUNGO 255 BYTE, E LA RIGA NE TIENE 80. Qui
 * c'era uno `sprintf(riga, "[%s]", nome)` su un buffer di 80: con un nome
 * lungo scriveva oltre la fine dello stack. Non e' mai successo perche' i nomi
 * di prova sono corti — che e' il modo in cui questi difetti restano nascosti
 * per mesi. La riga la costruisce questa, che tronca invece di traboccare.
 * ============================================================================= */
static void riga_voce(char *out, unsigned int max, const char *nome, int e_dir)
{
    unsigned int i = 0;

    if (max < 4) { out[0] = '\0'; return; }

    out[i++] = e_dir ? '[' : ' ';
    out[i] = '\0';

    strncat(out, nome, max - 3);        /* -3: la quadra e lo zero finale */
    if (e_dir) strncat(out, "]", max - strlen(out) - 1);
}

static void unisci(char *out, unsigned int max, const char *dir, const char *nome)
{
    unsigned int l;

    strncpy(out, dir, max - 1);
    out[max - 1] = '\0';
    l = (unsigned int)strlen(out);

    if (l == 0 || out[l - 1] != '/') {
        if (l + 1 < max) { out[l] = '/'; out[l + 1] = '\0'; }
    }
    strncat(out, nome, max - strlen(out) - 1);
}

static void percorso_nodo(int i, char *out, unsigned int max)
{
    int catena[NODI_MAX];
    int n = 0, k;
    int liv;

    out[0] = '\0';
    if (i < 0 || i >= (int)g_nodi) { strcpy(out, "/"); return; }

    /* ! SI RISALE CERCANDO IL PRIMO NODO DI LIVELLO MINORE, all'indietro. E'
     * corretto perche' i figli stanno SEMPRE subito dopo il padre: il primo
     * nodo di livello n-1 che si incontra tornando indietro e' per forza il
     * padre, e non ci sono puntatori da tenere aggiornati. */
    liv = (int)g_nodo[i].liv;
    catena[n++] = i;
    for (k = i - 1; k >= 0 && liv > 0 && n < NODI_MAX; k--)
        if ((int)g_nodo[k].liv == liv - 1) { catena[n++] = k; liv--; }

    for (k = n - 1; k >= 0; k--) {
        if (g_nodo[catena[k]].nome[0] == '\0') continue;   /* la radice */
        unisci(out, max, out, g_nodo[catena[k]].nome);
    }
    if (out[0] == '\0') strcpy(out, "/");
}

/* =============================================================================
 * Espandere e chiudere
 * ============================================================================= */
static void albero_chiudi(int i)
{
    int j = i + 1;

    if (i < 0 || i >= (int)g_nodi) return;

    while (j < (int)g_nodi && g_nodo[j].liv > g_nodo[i].liv) j++;
    if (j > i + 1) {
        memmove(&g_nodo[i + 1], &g_nodo[j],
                (unsigned int)((int)g_nodi - j) * sizeof(Nodo));
        g_nodi -= (unsigned int)(j - i - 1);
    }
    g_nodo[i].aperto = 0;
}

static void albero_espandi(int i)
{
    char      perc[PERC_MAX];
    DirEntry  v[8];
    int       start = 0, n, k, ins;
    unsigned char liv;

    if (i < 0 || i >= (int)g_nodi || g_nodo[i].aperto) return;

    percorso_nodo(i, perc, sizeof(perc));
    liv = (unsigned char)(g_nodo[i].liv + 1);
    ins = i + 1;

    /* ! IL BLOCCO E' DA OTTO, NON DA SEDICI. Un DirEntry sono 264 byte: un
     * blocco da sedici sono 4 KB di stack per chiamata, e questa funzione la
     * chiama anche chi copia una directory intera scendendo di livello in
     * livello. Otto bastano e costano la meta'. */
    while ((n = listdir_from(perc, v, 8, start)) > 0) {
        for (k = 0; k < n && g_nodi < NODI_MAX; k++) {
            if (!v[k].is_dir) continue;
            if (v[k].name[0] == '.' &&
                (v[k].name[1] == '\0' ||
                 (v[k].name[1] == '.' && v[k].name[2] == '\0'))) continue;

            memmove(&g_nodo[ins + 1], &g_nodo[ins],
                    (g_nodi - (unsigned int)ins) * sizeof(Nodo));
            g_nodi++;

            memset(&g_nodo[ins], 0, sizeof(Nodo));
            strncpy(g_nodo[ins].nome, v[k].name, DIRENT_NAME_MAX - 1);
            g_nodo[ins].nome[DIRENT_NAME_MAX - 1] = '\0';
            g_nodo[ins].liv = liv;
            ins++;
        }
        start += n;
        if (n < 8) break;
    }
    g_nodo[i].aperto = 1;
}

/* Riempie la lista di sinistra con i nodi, indentati. */
static void albero_mostra(void)
{
    unsigned int i;
    unsigned int scelta = ex_lista_scelta(g_albero);

    ex_lista_svuota(g_albero);

    for (i = 0; i < g_nodi; i++) {
        char riga[80];
        unsigned int k, p = 0;

        for (k = 0; k < g_nodo[i].liv && p + 2 < sizeof(riga); k++) {
            riga[p++] = ' '; riga[p++] = ' ';
        }
        /* ! IL SEGNO DICE SE C'E' ALTRO SOTTO, e non se il nodo E' una
         * directory: sono tutte directory. «+» vuol dire «non l'ho ancora
         * guardata dentro», «-» vuol dire «e' aperta». Chi apre una directory
         * vuota vede il segno cambiare e nessun figlio comparire, che e'
         * l'unica risposta onesta: era vuota. */
        if (p + 2 < sizeof(riga)) {
            riga[p++] = g_nodo[i].aperto ? '-' : '+';
            riga[p++] = ' ';
        }
        riga[p] = '\0';
        strncat(riga, g_nodo[i].nome[0] ? g_nodo[i].nome : "/",
                sizeof(riga) - strlen(riga) - 1);
        ex_lista_aggiungi(g_albero, riga);
    }

    if (scelta < g_nodi) ex_lista_scegli(g_albero, scelta);
}

/* =============================================================================
 * L'elenco di destra
 * ============================================================================= */
static void leggi(const char *percorso)
{
    DirEntry v[8];
    int start = 0, n, i;
    unsigned int quante = 0, d = 0;
    static unsigned int dim[VOCI_MAX];

    ex_lista_svuota(g_elenco);
    g_da_ricerca = 0;

    while ((n = listdir_from(percorso, v, 8, start)) > 0) {
        for (i = 0; i < n && quante < VOCI_MAX; i++) {
            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;
            strncpy(g_nome[quante], v[i].name, DIRENT_NAME_MAX - 1);
            g_nome[quante][DIRENT_NAME_MAX - 1] = '\0';
            dim[quante]        = v[i].size;
            g_dir_flag[quante] = v[i].is_dir;
            quante++;
        }
        start += n;
        if (n < 8) break;
    }

    /* ! LE DIRECTORY VENGONO PRIMA, e non e' estetica: in una directory con
     * cento file, quelle in cui si vuole entrare sarebbero sparse in mezzo.
     * E' l'unica cosa che questo elenco ordina — ordinare i nomi vorrebbe dire
     * un confronto che dipende dalla lingua, e non e' il momento. */
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

    for (i = 0; i < (int)quante; i++) {
        char riga[80];

        if (g_dir_flag[i]) {
            riga_voce(riga, sizeof(riga), g_nome[i], 1);
        } else {
            char corto[40];

            strncpy(corto, g_nome[i], sizeof(corto) - 1);
            corto[sizeof(corto) - 1] = '\0';
            sprintf(riga, " %-24s %8u", corto, dim[i]);
        }
        ex_lista_aggiungi(g_elenco, riga);
    }
    g_voci = quante;
}

static void stato_aggiorna(void)
{
    char s[220];

    if (g_avviso[0]) sprintf(s, "%s  -  %s", g_dir, g_avviso);
    else             sprintf(s, "%s  -  %u voci", g_dir, g_voci);

    ex_testo_metti(g_stato, s);
}

static void ridisegna(void)
{
    stato_aggiorna();
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);
}

/* =============================================================================
 * Andare da qualche parte
 *
 * ! UNA SOLA FUNZIONE PER \xabADESSO SIAMO QUI\xbb, chiamata dall'albero, dall'elenco
 * e da «Su». Tre strade che portano nello stesso posto e non passano dalla
 * stessa funzione si scollegano appena una delle tre cambia — e la prima a
 * cambiare sara' quella che qualcuno usa meno, quindi il difetto si vedra' per
 * ultimo.
 * ============================================================================= */
static void vai(const char *percorso)
{
    strncpy(g_dir, percorso, PERC_MAX - 1);
    g_dir[PERC_MAX - 1] = '\0';
    leggi(g_dir);
}

/* Trova nell'albero il nodo che corrisponde a `perc`, espandendo per strada.
 * Rende -1 se non ci si arriva. */
static int albero_apri_fino_a(const char *perc)
{
    int  i = 0;                 /* si parte dalla radice */
    char pezzo[DIRENT_NAME_MAX];
    unsigned int p = 0, k;

    while (perc[p] == '/') p++;

    while (perc[p]) {
        unsigned int q = 0;

        while (perc[p] && perc[p] != '/' && q < sizeof(pezzo) - 1)
            pezzo[q++] = perc[p++];
        pezzo[q] = '\0';
        while (perc[p] == '/') p++;
        if (!pezzo[0]) break;

        albero_espandi(i);

        /* Fra i figli diretti di `i` si cerca quello che si chiama cosi'. */
        {
            int trovato = -1;

            for (k = (unsigned int)i + 1; k < g_nodi; k++) {
                if (g_nodo[k].liv <= g_nodo[i].liv) break;
                if (g_nodo[k].liv == g_nodo[i].liv + 1 &&
                    strcmp(g_nodo[k].nome, pezzo) == 0) { trovato = (int)k; break; }
            }
            if (trovato < 0) return -1;
            i = trovato;
        }
    }
    return i;
}

/* =============================================================================
 * Aprire un file: lo si passa all'editor
 *
 * ! SI CERCA IN DUE POSTI, E L'ORDINE CONTA: su un sistema installato l'albero
 * sta in /exwin, avviando dal CD sta sotto /cdrom. Stessa regola del program
 * manager, per la stessa ragione.
 * ============================================================================= */
static void apri_file(const char *percorso)
{
    static const char *editori[2] = {
        "/exwin/bin/edit", "/cdrom/exwin/bin/edit"
    };
    static char copia[PERC_MAX];
    char *argv[3];
    int i;

    strncpy(copia, percorso, PERC_MAX - 1);
    copia[PERC_MAX - 1] = '\0';

    argv[1] = copia;
    argv[2] = 0;

    for (i = 0; i < 2; i++) {
        argv[0] = (char *)editori[i];
        if (spawn_ex(editori[i], argv, 0, 0, 0) >= 0) {
            sprintf(g_avviso, "aperto con l'editor: %s", copia);
            return;
        }
    }
    strcpy(g_avviso, "l'editor non si trova: /exwin/bin/edit");
}

/* =============================================================================
 * Copiare
 *
 * ! IL BUFFER E' UNO SOLO E STA QUI FUORI, non nello stack: copia_albero()
 * scende di livello in livello, e un buffer da un kilobyte per chiamata
 * sarebbe un kilobyte per ogni directory di profondita'. Non e' rientrante, e
 * non deve esserlo: qui c'e' un solo copiatore per volta.
 * ============================================================================= */
static char g_buf[1024];

static int copia_file(const char *da, const char *a)
{
    int fa, fb, n;

    fa = open(da, O_RDONLY, 0);
    if (fa < 0) return 0;

    fb = open(a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fb < 0) { close(fa); return 0; }

    while ((n = (int)read(fa, g_buf, sizeof(g_buf))) > 0)
        if ((int)write(fb, g_buf, (unsigned int)n) != n) {
            close(fa); close(fb);
            return 0;
        }

    close(fa);
    close(fb);
    return 1;
}

/* Rende quanti file ha copiato, o -1 se qualcosa e' andato storto. */
static int copia_albero(const char *da, const char *a, int giu)
{
    DirEntry v[8];
    int start = 0, n, i, fatti = 0;

    /* ! IL TETTO ALLA PROFONDITA' NON E' PRUDENZA ESAGERATA. Questa funzione
     * ricorre, e ogni livello si porta dietro il suo blocco di DirEntry: senza
     * un tetto, una directory profonda o — peggio — un anello nel filesystem
     * finirebbe lo stack. Fermarsi dicendolo e' meglio che morire. */
    if (giu > PROFONDITA) return -1;

    if (mkdir(a, 0755) < 0 && errno != EEXIST) return -1;

    while ((n = listdir_from(da, v, 8, start)) > 0) {
        for (i = 0; i < n; i++) {
            char sorg[PERC_MAX], dest[PERC_MAX];
            int r;

            if (v[i].name[0] == '.' &&
                (v[i].name[1] == '\0' ||
                 (v[i].name[1] == '.' && v[i].name[2] == '\0'))) continue;

            unisci(sorg, sizeof(sorg), da, v[i].name);
            unisci(dest, sizeof(dest), a,  v[i].name);

            if (v[i].is_dir) {
                r = copia_albero(sorg, dest, giu + 1);
                if (r < 0) return -1;
                fatti += r;
            } else {
                if (!copia_file(sorg, dest)) return -1;
                fatti++;
            }
        }
        start += n;
        if (n < 8) break;
    }
    return fatti;
}

/* La voce scelta a destra, col suo percorso intero. Rende 0 se non c'e'. */
static int scelta_destra(char *out, unsigned int max, int *e_dir)
{
    unsigned int s = ex_lista_scelta(g_elenco);

    if (s >= g_voci) return 0;
    *e_dir = g_dir_flag[s];

    /* Dopo una ricerca il nome E' gia' il percorso: vedi g_da_ricerca. */
    if (g_da_ricerca) {
        strncpy(out, g_nome[s], max - 1);
        out[max - 1] = '\0';
    } else {
        unisci(out, max, g_dir, g_nome[s]);
    }
    return 1;
}

static void comando_copia(int con_directory)
{
    char sorg[PERC_MAX], dest[PERC_MAX];
    int  e_dir = 0, n;

    if (!scelta_destra(sorg, sizeof(sorg), &e_dir)) {
        strcpy(g_avviso, "non c'e' niente di scelto a destra");
        return;
    }

    if (e_dir && !con_directory) {
        strcpy(g_avviso, "e' una directory: usa il comando Copia directory");
        return;
    }
    if (!e_dir && con_directory) {
        strcpy(g_avviso, "e' un file: usa il comando Copia");
        return;
    }

    /* ! LA DESTINAZIONE SI CHIEDE, E ARRIVA GIA' SCRITTA. Il dialogo parte dal
     * percorso di partenza: quasi sempre si vuole lo stesso nome in un'altra
     * directory, e riscrivere un nome lungo per intero e' il modo piu' facile
     * di sbagliarlo. */
    strncpy(dest, sorg, PERC_MAX - 1);
    dest[PERC_MAX - 1] = '\0';

    if (!ex_dlg_salva(dest, PERC_MAX)) {
        strcpy(g_avviso, "copia annullata");
        return;
    }

    if (strcmp(sorg, dest) == 0) {
        strcpy(g_avviso, "sorgente e destinazione sono lo stesso percorso");
        return;
    }

    if (con_directory) {
        n = copia_albero(sorg, dest, 0);
        if (n < 0) sprintf(g_avviso, "copia interrotta: %s", strerror(errno));
        else       sprintf(g_avviso, "copiati %d file in %s", n, dest);
    } else {
        if (copia_file(sorg, dest)) sprintf(g_avviso, "copiato in %s", dest);
        else sprintf(g_avviso, "non riesco a copiare: %s", strerror(errno));
    }

    leggi(g_dir);
}

/* =============================================================================
 * Cercare
 *
 * ! I RISULTATI VANNO NELL'AREA DI DESTRA, non in una finestra nuova. Un
 * elenco di risultati e' un elenco di file: farne un posto a parte vorrebbe
 * dire un secondo modo di aprirli, di copiarli e di guardarli. Cosi' invece un
 * risultato si apre con l'Invio come qualunque altra voce — e per far tornare
 * l'elenco vero basta scegliere una directory a sinistra.
 * ============================================================================= */
static void cerca_giu(const char *dir, const char *pezzo, int giu,
                      unsigned int *quanti)
{
    DirEntry v[8];
    int start = 0, n, i;

    if (giu > PROFONDITA || *quanti >= VOCI_MAX) return;

    while ((n = listdir_from(dir, v, 8, start)) > 0) {
        for (i = 0; i < n && *quanti < VOCI_MAX; i++) {
            char perc[PERC_MAX];

            if (v[i].name[0] == '.' &&
                (v[i].name[1] == '\0' ||
                 (v[i].name[1] == '.' && v[i].name[2] == '\0'))) continue;

            unisci(perc, sizeof(perc), dir, v[i].name);

            if (strstr(v[i].name, pezzo) != 0) {
                strncpy(g_nome[*quanti], perc, DIRENT_NAME_MAX - 1);
                g_nome[*quanti][DIRENT_NAME_MAX - 1] = '\0';
                g_dir_flag[*quanti] = v[i].is_dir;
                (*quanti)++;
            }

            if (v[i].is_dir) cerca_giu(perc, pezzo, giu + 1, quanti);
        }
        start += n;
        if (n < 8) break;
    }
}

static void comando_cerca(void)
{
    static char pezzo[64] = "";
    unsigned int quanti = 0, i;

    if (!ex_dlg_riga("Cerca", "Parte del nome da cercare, qui sotto:",
                     pezzo, sizeof(pezzo))) {
        strcpy(g_avviso, "ricerca annullata");
        return;
    }
    if (!pezzo[0]) {
        strcpy(g_avviso, "non hai scritto niente da cercare");
        return;
    }

    cerca_giu(g_dir, pezzo, 0, &quanti);

    ex_lista_svuota(g_elenco);
    for (i = 0; i < quanti; i++) {
        char riga[80];

        riga_voce(riga, sizeof(riga), g_nome[i], g_dir_flag[i]);
        ex_lista_aggiungi(g_elenco, riga);
    }

    g_voci = quanti;
    g_da_ricerca = 1;
    sprintf(g_avviso, "%s: %u trovati sotto %s", pezzo, quanti, g_dir);
}

/* =============================================================================
 * Le scelte
 * ============================================================================= */
static void scegli_albero(int con_invio)
{
    unsigned int s = ex_lista_scelta(g_albero);
    char perc[PERC_MAX];

    if (s >= g_nodi) return;

    percorso_nodo((int)s, perc, sizeof(perc));
    vai(perc);

    /* ! IL CLIC MOSTRA, L'INVIO ESPANDE. Sono due desideri diversi e vanno
     * distinti: chi scorre l'albero con le frecce vuole vedere il contenuto
     * cambiare a destra senza che l'albero gli si apra sotto le mani, e chi
     * batte Invio ha chiesto proprio di scendere. */
    if (!con_invio) return;

    if (g_nodo[s].aperto) albero_chiudi((int)s);
    else                  albero_espandi((int)s);
    albero_mostra();
    ex_lista_scegli(g_albero, s);
}

static void scegli_elenco(int con_invio)
{
    char perc[PERC_MAX];
    int  e_dir = 0;

    if (!con_invio) return;
    if (!scelta_destra(perc, sizeof(perc), &e_dir)) return;

    if (!e_dir) { apri_file(perc); return; }

    /* Una directory scelta a destra si apre a destra E si apre a sinistra:
     * sono la stessa directory, e vederla in un posto solo vorrebbe dire un
     * albero che dice una cosa e un elenco che ne dice un'altra. */
    vai(perc);
    {
        int nodo = albero_apri_fino_a(perc);

        if (nodo >= 0) {
            albero_espandi(nodo);
            albero_mostra();
            ex_lista_scegli(g_albero, (unsigned int)nodo);
        } else {
            albero_mostra();
        }
    }
}

/* =============================================================================
 * La disposizione, che cambia con la finestra
 * ============================================================================= */
static void disponi(int w, int h)
{
    int alt = h - MENU_H - 4 - BASSO;

    if (alt < 40) alt = 40;

    ex_sposta(g_albero, 4, MENU_H + 4);
    ex_misura(g_albero, ALBERO_W, alt);

    ex_sposta(g_elenco, ALBERO_W + 10, MENU_H + 4);
    ex_misura(g_elenco, w - ALBERO_W - 14, alt);

    ex_sposta(g_stato, 6, h - 22);
    ex_misura(g_stato, w - 12, 16);
}

static void istruzioni(void)
{
    ex_dlg_avviso("Istruzioni",
                  "A sinistra l'albero, a destra il contenuto.  Le frecce "
                  "scelgono, Tab passa da un'area all'altra, Invio espande "
                  "una directory o apre un file.  F10 apre i menu.  "
                  "Copia chiede dove mettere quello che e' scelto a destra; "
                  "Cerca guarda sotto la directory corrente.");
}

static void informazioni(void)
{
    ex_dlg_avviso("Informazioni su",
                  "Il file manager di EX-OS, sul toolkit ExWin.  L'albero non "
                  "e' un controllo nuovo: e' una lista con dentro "
                  "l'indentazione, e i nodi stanno in un vettore nell'ordine "
                  "in cui si vedono.");
}

/* =============================================================================
 * La procedura
 * ============================================================================= */
static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        g_avviso[0] = '\0';

        /* ! DALLE LISTE ARRIVA ANCHE COME, non solo COSA: lp dice se e'
         * arrivato dall'Invio o dal clic. Senza, un clic per guardare e un
         * Invio per entrare sarebbero indistinguibili — e l'albero si
         * aprirebbe sotto le dita di chi voleva solo dare un'occhiata. */
        if (wp == ID_ALBERO) { scegli_albero(EX_DA_INVIO(lp)); break; }
        if (wp == ID_ELENCO) { scegli_elenco(EX_DA_INVIO(lp)); break; }

        if (wp == ID_APRI)     { scegli_elenco(1); break; }
        if (wp == ID_AGGIORNA) { leggi(g_dir);     break; }
        if (wp == ID_ESCI)     { ex_esci(0); return 0; }
        if (wp == ID_SU) {
            char su[PERC_MAX];
            int i = (int)strlen(g_dir);

            strncpy(su, g_dir, PERC_MAX - 1);
            su[PERC_MAX - 1] = '\0';
            while (i > 1 && su[i - 1] != '/') i--;
            if (i > 1) i--;
            if (i == 0) i = 1;
            su[i] = '\0';
            vai(su);
            {
                int nodo = albero_apri_fino_a(su);
                if (nodo >= 0) ex_lista_scegli(g_albero, (unsigned int)nodo);
            }
            break;
        }

        if (wp == ID_COPIA)    { comando_copia(0); break; }
        if (wp == ID_COPIADIR) { comando_copia(1); break; }
        if (wp == ID_CERCA)    { comando_cerca();  break; }

        if (wp == ID_ISTRUZIONI) { istruzioni();   break; }
        if (wp == ID_INFO)       { informazioni(); break; }
        return 0;

    case EXM_TASTO:
        /* ! Tab PASSA DA UN'AREA ALL'ALTRA, e lo fa gia' il toolkit: qui non
         * c'e' niente da scrivere. Resta questo ramo perche' le scorciatoie
         * dei comandi sono dell'applicazione — un menu non cattura i tasti. */
        if (wp & KBD_MOD_CTRL) {
            unsigned int c = wp & KBD_KEY_MASK;

            if (c == 'c' || c == 'C') { comando_copia(0); break; }
            if (c == 'f' || c == 'F') { comando_cerca();  break; }
            if (c == 'q' || c == 'Q') { ex_esci(0); return 0; }
        }
        return ex_procedura_base(f, msg, wp, lp);

    case EXM_MISURA:
        disponi(EX_X(lp), EX_Y(lp));
        break;

    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    if (argc >= 2) {
        strncpy(g_dir, argv[1], PERC_MAX - 1);
        g_dir[PERC_MAX - 1] = '\0';
    }

    g_f = ex_crea("finestra", "File manager",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("filemgr: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Apri\tInvio",   ID_APRI);
    ex_menu_voce(g_menu, "File", "Su",            ID_SU);
    ex_menu_voce(g_menu, "File", "Aggiorna",      ID_AGGIORNA);
    ex_menu_voce(g_menu, "File", "-",             0);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q",  ID_ESCI);

    ex_menu_voce(g_menu, "Comandi", "Copia\tCtrl+C",   ID_COPIA);
    ex_menu_voce(g_menu, "Comandi", "Copia directory", ID_COPIADIR);
    ex_menu_voce(g_menu, "Comandi", "-",               0);
    ex_menu_voce(g_menu, "Comandi", "Cerca\tCtrl+F",   ID_CERCA);

    ex_menu_voce(g_menu, "Info", "Istruzioni",      ID_ISTRUZIONI);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);

    g_albero = ex_crea("lista", "", EX_FIGLIO,
                       4, MENU_H + 4, ALBERO_W,
                       FIN_H - MENU_H - 4 - BASSO, g_f, ID_ALBERO, 0);
    g_elenco = ex_crea("lista", "", EX_FIGLIO,
                       ALBERO_W + 10, MENU_H + 4, FIN_W - ALBERO_W - 14,
                       FIN_H - MENU_H - 4 - BASSO, g_f, ID_ELENCO, 0);
    if (!g_albero || !g_elenco) {
        printf("filemgr: non riesco a creare le due aree\n");
        return 1;
    }

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, FIN_W - 12, 16, g_f, 0, 0);

    /* La radice: il nodo senza nome, da cui discende ogni percorso. */
    memset(&g_nodo[0], 0, sizeof(Nodo));
    g_nodi = 1;
    albero_espandi(0);

    {
        int nodo = albero_apri_fino_a(g_dir);

        albero_mostra();
        if (nodo >= 0) ex_lista_scegli(g_albero, (unsigned int)nodo);
    }

    leggi(g_dir);

    /* ! IL FUOCO ALL'ALBERO, ESPLICITAMENTE. Senza andrebbe al primo controllo
     * creato che lo accetta — che e' comunque l'albero, ma per caso: il giorno
     * che si aggiunge un pulsante prima, le frecce smetterebbero di muovere
     * qualcosa senza che nessuno abbia toccato l'albero. */
    ex_fuoco(g_albero);

    ridisegna();
    printf("filemgr: %s, %u voci\n", g_dir, g_voci);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

/* =============================================================================
 * QUELLO CHE MANCA, DICHIARATO
 *
 * ! NIENTE SPOSTA, NIENTE CANCELLA, NIENTE RINOMINA. Copiare non distrugge
 * niente; le altre tre si', e una conferma sbagliata cancella il lavoro di
 * qualcuno. Ci vogliono, ma ci vogliono con la domanda giusta davanti.
 *
 * ! LA COPIA NON DICE A CHE PUNTO E'. Copiando una directory grossa la
 * finestra resta ferma finche' non ha finito: il ciclo dei messaggi e' fermo
 * dentro copia_albero(). Per farlo si dovrebbe copiare un pezzo per giro del
 * ciclo, e allora servirebbe uno stato del lavoro in corso.
 *
 * ! LA RICERCA SCENDE AL MASSIMO DI OTTO LIVELLI e si ferma a 512 risultati.
 * Sono due tetti dichiarati, non due limiti scoperti dopo: la funzione ricorre
 * e ogni livello si porta dietro il suo blocco di DirEntry.
 *
 * ! E L'ALBERO TIENE 128 NODI IN TUTTO, aperti insieme. Espandendo mezzo disco
 * si smette di aggiungerne: e' un vettore, non una lista, e un vettore ha una
 * fine.
 * ============================================================================= */
