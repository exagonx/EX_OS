/* =============================================================================
 * exwin/bin/edit/edit.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'editor di testo grafico
 *
 *     /exwin/bin/edit [FILE]
 *
 * ! NON E' /bin/gfedit CON LE FINESTRE. gfedit vive su una console in modo
 * raw, si disegna da se' i menu a tendina e possiede lo schermo intero; qui lo
 * schermo e' di chi lo compone, i tasti arrivano solo quando la finestra ha il
 * fuoco, e non c'e' niente da spegnere quando si esce. Sono due programmi con
 * lo stesso scopo e due strutture diverse: fonderli vorrebbe dire un ramo `if`
 * a ogni riga che tocca l'input o il disegno.
 *
 * ! L'AREA DI TESTO E' DISEGNATA A MANO, come l'elenco del file manager e per
 * la stessa ragione: il toolkit non ha ancora un controllo multiriga. E'
 * la seconda applicazione che lo chiede, ed e' il segnale che quel controllo
 * va scritto — vedi la nota in fondo.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "kbd_proto.h"

/* --- La finestra ---------------------------------------------------------- */
#define FIN_W       640
#define FIN_H       420

#define AREA_X      4
#define AREA_Y      26
#define AREA_W      (FIN_W - 8)         /* 632 */
#define AREA_H      368                 /* multiplo di RIGA_H: 23 righe piene */

#define CAR_W       8                   /* il font e' 8x16, non si negozia */
#define RIGA_H      16
#define COLONNE     ((AREA_W - 4) / CAR_W)      /* 78 */
#define RIGHE_VIS   (AREA_H / RIGA_H)           /* 23 */

/* --- Il testo -------------------------------------------------------------
 *
 * ! LIMITI FISSI, E SONO UNA CONSEGUENZA NON UNA PIGRIZIA. L'allocatore di
 * EX-OS e' a bump su sbrk e free() non restituisce niente: righe riallocate a
 * ogni tasto premuto perderebbero memoria per sempre. Sono gli stessi numeri
 * di /bin/gfedit, per la stessa ragione.
 * --------------------------------------------------------------------------- */
#define RIGHE_MAX   512
#define COL_MAX     200
#define PERC_MAX    192

#define ID_SALVA      1
#define ID_NUOVO      2
#define ID_RICARICA   3
#define ID_APRI       4
#define ID_SALVACOME  5

static char         g_riga[RIGHE_MAX][COL_MAX];
static unsigned int g_righe = 1;
static unsigned int g_cy = 0, g_cx = 0;     /* il cursore, in righe e colonne */
static unsigned int g_top = 0, g_left = 0;  /* la prima riga e colonna visibili */

static char g_perc[PERC_MAX] = "";
static int  g_mod = 0;          /* modificato dopo l'ultimo salvataggio */
static int  g_parziale = 0;     /* letto SOLO IN PARTE: non si salva */
static int  g_chiedi_uscita = 0;
static char g_avviso[96] = "";

static ExFinestra g_f, g_stato;

/* -----------------------------------------------------------------------------
 * Caricare
 *
 * ! SI LEGGE A PEZZI E SI SPEZZA STRADA FACENDO, senza un buffer grande quanto
 * il file: un file da mezzo mega non deve chiedere mezzo mega di memoria che
 * poi non si puo' restituire.
 *
 * ! UN FILE PIU' GRANDE DEI LIMITI SI CARICA IN PARTE E IL SALVATAGGIO SI
 * BLOCCA. Salvare quello che si e' letto vorrebbe dire CANCELLARE il resto del
 * file dell'utente senza averlo mai mostrato: e' il modo piu' silenzioso che
 * un editor abbia di distruggere dei dati.
 * --------------------------------------------------------------------------- */
static void svuota(void)
{
    g_righe = 1;
    g_riga[0][0] = '\0';
    g_cy = g_cx = g_top = g_left = 0;
    g_mod = 0;
    g_parziale = 0;
}

static int carica(const char *percorso)
{
    char buf[512];
    int  fd, n, i;
    unsigned int col = 0;

    svuota();

    fd = open(percorso, O_RDONLY, 0);
    if (fd < 0) return 0;               /* non c'e': e' un file nuovo */

    while ((n = (int)read(fd, buf, sizeof buf)) > 0) {
        for (i = 0; i < n; i++) {
            char c = buf[i];

            if (c == '\r') continue;    /* i fine-riga di DOS non si vedono */

            if (c == '\n') {
                g_riga[g_righe - 1][col] = '\0';
                if (g_righe >= RIGHE_MAX) { g_parziale = 1; goto fine; }
                g_righe++;
                col = 0;
                continue;
            }

            if (col < COL_MAX - 1) {
                g_riga[g_righe - 1][col++] = c;
            } else {
                /* La riga e' piu' lunga del massimo: il resto si perde, e chi
                 * salva cancellerebbe quel resto. */
                g_parziale = 1;
            }
        }
    }
    g_riga[g_righe - 1][col] = '\0';

fine:
    close(fd);
    return 1;
}

/* -----------------------------------------------------------------------------
 * Salvare, e chiedere dove
 *
 * ! LE DUE FUNZIONI SI CHIAMANO A VICENDA, e non e' un giro infinito: salva()
 * chiama salva_come() solo quando il nome MANCA, e salva_come() ne mette uno
 * prima di richiamare salva(). Due passaggi al massimo, e il caso «annullato»
 * si ferma prima.
 * --------------------------------------------------------------------------- */
static int salva(void);

static int salva_come(void)
{
    char nuovo[PERC_MAX];

    strncpy(nuovo, g_perc, PERC_MAX - 1);
    nuovo[PERC_MAX - 1] = '\0';

    if (!ex_dlg_salva(nuovo, PERC_MAX)) {
        strcpy(g_avviso, "salvataggio annullato");
        return 0;
    }

    strncpy(g_perc, nuovo, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';
    return salva();
}

static int g_chiedi_apri = 0;

static void apri_con_dialogo(void)
{
    char nuovo[PERC_MAX];

    /* ! IL LAVORO NON SALVATO SI DIFENDE COME ALLA CHIUSURA: il primo «Apri»
     * avvisa, il secondo procede. Un dialogo con «si'/no» sarebbe piu' chiaro,
     * ma ExDlg ne ha uno solo con un pulsante solo — e inventare un «ha detto
     * di si'» che nessuno ha detto e' peggio di chiedere due volte. */
    if (g_mod && !g_chiedi_apri) {
        g_chiedi_apri = 1;
        strcpy(g_avviso, "modificato: premi «Apri» di nuovo per perdere le modifiche");
        return;
    }
    g_chiedi_apri = 0;

    strncpy(nuovo, g_perc, PERC_MAX - 1);
    nuovo[PERC_MAX - 1] = '\0';

    if (!ex_dlg_apri(nuovo, PERC_MAX)) {
        strcpy(g_avviso, "apertura annullata");
        return;
    }

    strncpy(g_perc, nuovo, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';

    if (carica(g_perc)) sprintf(g_avviso, "aperto: %u righe", g_righe);
    else                strcpy(g_avviso, "non c'era: file nuovo");
}

static int salva(void)
{
    int fd;
    unsigned int i;

    if (g_parziale) {
        strcpy(g_avviso, "letto solo in parte: salvare cancellerebbe il resto");
        return 0;
    }
    /* ! SENZA NOME SI CHIEDE, invece di rifiutare. Fino al 17 agosto 2026 qui
     * c'era un messaggio che diceva di riavviare il programma con un argomento:
     * era l'unica cosa possibile finche' non esisteva un dialogo. Adesso
     * esiste, sta in una libreria condivisa, e la chiede anche il file
     * manager. */
    if (g_perc[0] == '\0') return salva_come();

    fd = open(g_perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        sprintf(g_avviso, "non riesco a scrivere %s", g_perc);
        return 0;
    }

    for (i = 0; i < g_righe; i++) {
        unsigned int l = (unsigned int)strlen(g_riga[i]);
        if (l && write(fd, g_riga[i], l) != (ssize_t)l) {
            close(fd);
            strcpy(g_avviso, "scrittura interrotta: il file e' incompleto");
            return 0;
        }
        if (write(fd, "\n", 1) != 1) {
            close(fd);
            strcpy(g_avviso, "scrittura interrotta: il file e' incompleto");
            return 0;
        }
    }
    close(fd);

    g_mod = 0;
    sprintf(g_avviso, "salvato: %u righe", g_righe);
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il disegno
 * --------------------------------------------------------------------------- */
static void stato_aggiorna(void)
{
    char s[200];
    const char *nome = g_perc[0] ? g_perc : "(senza nome)";

    if (g_avviso[0])
        sprintf(s, "%s  -  %s", nome, g_avviso);
    else
        sprintf(s, "%s%s  -  riga %u/%u  col %u%s",
                g_mod ? "*" : "", nome,
                g_cy + 1, g_righe, g_cx + 1,
                g_parziale ? "  [PARZIALE: non si salva]" : "");

    ex_testo_metti(g_stato, s);
}

static void area_disegna(void)
{
    unsigned int i;

    ex_riempi(g_f, AREA_X, AREA_Y, AREA_W, AREA_H, EX_BIANCO);
    ex_riquadro_disegna(g_f, AREA_X, AREA_Y, AREA_W, AREA_H, EX_GRIGIO_SC);

    for (i = 0; i < RIGHE_VIS && g_top + i < g_righe; i++) {
        const char  *r = g_riga[g_top + i];
        unsigned int l = (unsigned int)strlen(r);
        char         vis[COLONNE + 1];
        unsigned int k;
        int          y = AREA_Y + 2 + (int)i * RIGA_H;

        /* La finestra sul testo: da g_left, larga COLONNE. */
        for (k = 0; k < COLONNE && g_left + k < l; k++) {
            char c = r[g_left + k];
            /* ! IL TAB SI MOSTRA COME UNO SPAZIO E RESTA UN TAB NEL FILE.
             * Espanderlo davvero vorrebbe dire che una colonna sullo schermo
             * non e' piu' un carattere nel testo, e allora il cursore, il clic
             * del mouse e la lunghezza della riga direbbero tre cose diverse.
             * Espanderlo ALLA LETTURA sarebbe peggio: cambierebbe il file di
             * chi lo apre soltanto. */
            vis[k] = (c == '\t') ? ' ' : c;
        }
        vis[k] = '\0';

        if (vis[0]) ex_scrivi(g_f, AREA_X + 2, y, vis, EX_NERO);
    }

    /* Il cursore: un blocco pieno col carattere ridisegnato sopra in bianco.
     * Una barretta di un pixel su un font 8x16 si perde. */
    if (g_cy >= g_top && g_cy < g_top + RIGHE_VIS &&
        g_cx >= g_left && g_cx < g_left + COLONNE) {
        int cx = AREA_X + 2 + (int)(g_cx - g_left) * CAR_W;
        int cy = AREA_Y + 2 + (int)(g_cy - g_top) * RIGA_H;
        char sotto[2];

        ex_riempi(g_f, cx, cy, CAR_W, RIGA_H, EX_BLU);

        sotto[0] = g_riga[g_cy][g_cx];
        sotto[1] = '\0';
        if (sotto[0] == '\t') sotto[0] = ' ';
        if (sotto[0]) ex_scrivi(g_f, cx, cy, sotto, EX_BIANCO);
    }
}

/* La vista insegue il cursore, in tutt'e due le direzioni: senza, le frecce
 * muoverebbero un cursore che non si vede. */
static void vista_seguo(void)
{
    if (g_cy < g_top)                  g_top = g_cy;
    if (g_cy >= g_top + RIGHE_VIS)     g_top = g_cy - RIGHE_VIS + 1;
    if (g_cx < g_left)                 g_left = g_cx;
    if (g_cx >= g_left + COLONNE)      g_left = g_cx - COLONNE + 1;
}

static void ridisegna(void)
{
    stato_aggiorna();
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    area_disegna();
    ex_aggiorna(g_f);
}

/* -----------------------------------------------------------------------------
 * Le modifiche al testo
 * --------------------------------------------------------------------------- */
static unsigned int lung(unsigned int y)
{
    return (unsigned int)strlen(g_riga[y]);
}

static void inserisci(char c)
{
    char        *r = g_riga[g_cy];
    unsigned int l = lung(g_cy), i;

    if (l >= COL_MAX - 1) {
        strcpy(g_avviso, "riga piena");
        return;
    }
    for (i = l + 1; i > g_cx; i--) r[i] = r[i - 1];
    r[g_cx] = c;
    g_cx++;
    g_mod = 1;
}

static void spezza(void)
{
    unsigned int i;

    if (g_righe >= RIGHE_MAX) {
        strcpy(g_avviso, "troppe righe");
        return;
    }
    for (i = g_righe; i > g_cy + 1; i--) strcpy(g_riga[i], g_riga[i - 1]);

    strcpy(g_riga[g_cy + 1], g_riga[g_cy] + g_cx);
    g_riga[g_cy][g_cx] = '\0';

    g_righe++;
    g_cy++;
    g_cx = 0;
    g_mod = 1;
}

/* Toglie il carattere PRIMA del cursore; a inizio riga unisce con quella sopra. */
static void cancella_indietro(void)
{
    unsigned int i;

    if (g_cx > 0) {
        char *r = g_riga[g_cy];
        unsigned int l = lung(g_cy);
        for (i = g_cx - 1; i < l; i++) r[i] = r[i + 1];
        g_cx--;
        g_mod = 1;
        return;
    }
    if (g_cy == 0) return;

    {
        unsigned int sopra = lung(g_cy - 1);
        if (sopra + lung(g_cy) >= COL_MAX - 1) {
            strcpy(g_avviso, "le due righe insieme sarebbero troppo lunghe");
            return;
        }
        strcat(g_riga[g_cy - 1], g_riga[g_cy]);
        for (i = g_cy; i + 1 < g_righe; i++) strcpy(g_riga[i], g_riga[i + 1]);
        g_righe--;
        g_cy--;
        g_cx = sopra;
        g_mod = 1;
    }
}

/* Toglie il carattere SOTTO il cursore: e' il Backspace della riga dopo. */
static void cancella_avanti(void)
{
    if (g_cx < lung(g_cy)) {
        char *r = g_riga[g_cy];
        unsigned int i, l = lung(g_cy);
        for (i = g_cx; i < l; i++) r[i] = r[i + 1];
        g_mod = 1;
        return;
    }
    if (g_cy + 1 >= g_righe) return;

    g_cy++;
    g_cx = 0;
    cancella_indietro();
}

/* -----------------------------------------------------------------------------
 * La procedura della finestra
 * --------------------------------------------------------------------------- */
static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    unsigned int c;

    switch (msg) {
    case EXM_COMANDO:
        g_avviso[0] = '\0';
        if (wp == ID_SALVA) { salva(); break; }
        if (wp == ID_NUOVO) {
            svuota();
            g_perc[0] = '\0';
            break;
        }
        if (wp == ID_RICARICA) {
            if (g_perc[0]) carica(g_perc);
            else strcpy(g_avviso, "niente da ricaricare: non c'e' un file");
            break;
        }
        if (wp == ID_APRI)      { apri_con_dialogo(); break; }
        if (wp == ID_SALVACOME) { salva_come();       break; }
        return 0;

    case EXM_TASTO:
        g_avviso[0] = '\0';
        g_chiedi_uscita = 0;
        c = wp & KBD_KEY_MASK;

        /* ! LE SCORCIATOIE PRIMA DEI CARATTERI. Il servizio 'kbd' tiene i
         * modificatori in un campo separato, quindi Ctrl+S arriva come 's' con
         * il bit di Ctrl acceso: senza guardare il bit per primo, un Ctrl+S
         * infilerebbe una «s» nel testo. */
        if (wp & KBD_MOD_CTRL) {
            if (c == 's' || c == 'S') { salva(); break; }
            if (c == 'q' || c == 'Q') {
                if (g_mod) {
                    strcpy(g_avviso, "modificato: Ctrl+Q di nuovo per uscire senza salvare");
                    g_chiedi_uscita = 1;
                    break;
                }
                ex_esci(0);
                return 0;
            }
            return 0;
        }

        switch (c) {
        case KBD_K_UP:    if (g_cy > 0) g_cy--;                  break;
        case KBD_K_DOWN:  if (g_cy + 1 < g_righe) g_cy++;        break;
        case KBD_K_LEFT:
            if (g_cx > 0) g_cx--;
            else if (g_cy > 0) { g_cy--; g_cx = lung(g_cy); }
            break;
        case KBD_K_RIGHT:
            if (g_cx < lung(g_cy)) g_cx++;
            else if (g_cy + 1 < g_righe) { g_cy++; g_cx = 0; }
            break;
        case KBD_K_HOME:  g_cx = 0;                              break;
        case KBD_K_END:   g_cx = lung(g_cy);                     break;
        case KBD_K_PGUP:
            g_cy = (g_cy > RIGHE_VIS) ? g_cy - RIGHE_VIS : 0;
            break;
        case KBD_K_PGDN:
            g_cy += RIGHE_VIS;
            if (g_cy >= g_righe) g_cy = g_righe - 1;
            break;
        case KBD_K_DEL:   cancella_avanti();                     break;
        case '\b':        cancella_indietro();                   break;
        case '\n':
        case '\r':        spezza();                              break;
        default:
            /* ! SOLO I CARATTERI STAMPABILI E IL TAB. I tasti speciali stanno
             * da 0x100 in su apposta per non poterli confondere con un
             * carattere; infilarne uno nel testo darebbe un file con dentro un
             * valore che nessun altro programma sa leggere. */
            if (c == '\t' || (c >= 0x20 && c < 0x7F)) inserisci((char)c);
            else return 0;
            break;
        }

        /* Il cursore non puo' stare oltre la fine della riga in cui e' finito. */
        if (g_cx > lung(g_cy)) g_cx = lung(g_cy);
        break;

    case EXM_MOUSE_GIU: {
        int x = EX_X(lp), y = EX_Y(lp);
        unsigned int r, k;

        if (x < AREA_X || x >= AREA_X + AREA_W ||
            y < AREA_Y || y >= AREA_Y + AREA_H) return 0;

        g_avviso[0] = '\0';
        r = g_top + (unsigned int)((y - AREA_Y - 2) / RIGA_H);
        k = g_left + (unsigned int)((x - AREA_X - 2) / CAR_W);

        if (r >= g_righe) r = g_righe - 1;
        g_cy = r;
        g_cx = (k > lung(r)) ? lung(r) : k;
        break;
    }

    case EXM_CHIUDI:
        /* ! UNA DOMANDA SENZA FINESTRA DI DIALOGO. Non c'e' un dialogo modale
         * nel toolkit, e chiudere in silenzio un testo modificato e' il modo
         * piu' facile di perdere il lavoro di qualcuno: la prima chiusura
         * avvisa, la seconda esce. */
        if (g_mod && !g_chiedi_uscita) {
            g_chiedi_uscita = 1;
            strcpy(g_avviso, "modificato: chiudi di nuovo per uscire senza salvare");
            break;
        }
        ex_esci(0);
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    vista_seguo();
    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    svuota();

    if (argc >= 2) {
        strncpy(g_perc, argv[1], PERC_MAX - 1);
        g_perc[PERC_MAX - 1] = '\0';
    }

    g_f = ex_crea("finestra", "Editor", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  30, 30, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("edit: il server a finestre non risponde.\n");
        return 1;
    }

    ex_crea("pulsante", "Nuovo",    EX_FIGLIO,   4, 2, 60, 20, g_f, ID_NUOVO,      0);
    ex_crea("pulsante", "Apri",     EX_FIGLIO,  68, 2, 60, 20, g_f, ID_APRI,       0);
    ex_crea("pulsante", "Salva",    EX_FIGLIO, 132, 2, 60, 20, g_f, ID_SALVA,      0);
    ex_crea("pulsante", "Salva come", EX_FIGLIO, 196, 2, 96, 20, g_f, ID_SALVACOME, 0);
    ex_crea("pulsante", "Ricarica", EX_FIGLIO, 296, 2, 74, 20, g_f, ID_RICARICA,   0);
    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, AREA_W - 4, 16, g_f, 0, 0);

    if (g_perc[0]) {
        if (carica(g_perc))
            printf("edit: %s, %u righe%s\n", g_perc, g_righe,
                   g_parziale ? " (PARZIALE)" : "");
        else
            printf("edit: %s non c'e': file nuovo\n", g_perc);
    } else {
        printf("edit: file nuovo, senza nome\n");
    }

    ridisegna();

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

/* =============================================================================
 * QUELLO CHE MANCA, DICHIARATO
 *
 * ! «APRI» E «SALVA CON NOME» ADESSO CI SONO, e non stanno qui dentro: sono
 * ex_dlg_apri() ed ex_dlg_salva() della libreria condivisa ExDlg. E' il motivo
 * per cui quella libreria e' separata da ExWin — la usano l'editor e il file
 * manager, e chi non apre file non se la porta dietro.
 *
 * ! QUELLO CHE MANCA ANCORA E' UN DIALOGO CON «SI'/NO». ExDlg ne ha uno con un
 * pulsante solo, quindi «vuoi perdere le modifiche?» qui si chiede facendo
 * premere due volte lo stesso pulsante. Funziona, e si vede che e' un ripiego.
 *
 * ! NIENTE ANNULLAMENTO. /bin/gfedit ce l'ha, a giornale di operazioni; qui
 * si aggiunge quando il testo smette di cambiare forma.
 *
 * ! NIENTE SELEZIONE NE' APPUNTI, per la stessa ragione: gli appunti sono un
 * servizio che in EX-OS non esiste ancora, e una selezione che non si puo'
 * copiare da nessuna parte serve a poco.
 *
 * ! E LA COSA CHE QUESTO PROGRAMMA HA DETTO SUL TOOLKIT: manca un controllo
 * multiriga. E' la SECONDA applicazione che si disegna il contenuto a mano
 * (la prima e' l'elenco del file manager), e due volte vuol dire che il pezzo
 * mancante e' nel toolkit, non nelle applicazioni.
 * ============================================================================= */
