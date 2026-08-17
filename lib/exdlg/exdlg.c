/* =============================================================================
 * lib/exdlg/exdlg.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I dialoghi «Apri» e «Salva con nome»
 *
 * ! IL CICLO DEI MESSAGGI E' ANNIDATO, non separato. Dentro c'e' lo stesso
 * ex_prendi_msg()/ex_smista() dell'applicazione, che continua a consegnare gli
 * eventi anche alle ALTRE finestre: senza, aprire un dialogo lascerebbe il
 * resto del programma senza ridisegni finche' non lo si chiude, e chi guarda
 * penserebbe che si e' bloccato.
 *
 * ! NON E' MODALE DAVVERO, ed e' dichiarato. Il server a finestre non sa cosa
 * sia una finestra modale: non c'e' modo di dirgli «non consegnare clic a
 * quella finestra la' finche' c'e' questa». Il dialogo sta sopra e prende il
 * fuoco, ma se si clicca la finestra sotto, quella risponde. Renderlo modale
 * per davvero vuole un bit nel protocollo — e finche' non c'e', e' meglio
 * scriverlo qui che far finta.
 *
 * ! L'ELENCO E' DISEGNATO A MANO, come nel file manager e nell'editor. E' la
 * TERZA volta, ed e' la conferma che al toolkit manca un controllo lista: qui
 * pero' la ripetizione finisce, perche' d'ora in poi chi vuole scegliere un
 * file chiama questa funzione invece di riscriverne una sua.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"

#define DLG_W       420
#define DLG_H       300
#define RIGA_H      16
#define VOCI_MAX    256
#define PERC_MAX    192

#define AREA_X      6
#define AREA_Y      26
#define AREA_W      (DLG_W - 12)
#define AREA_H      192

#define ID_OK       1
#define ID_ANNULLA  2
#define ID_SU       3

typedef struct {
    char          nome[DIRENT_NAME_MAX];
    unsigned char dir;
} Voce;

static Voce         g_voce[VOCI_MAX];
static unsigned int g_n, g_sel, g_primo;
static char         g_dir[PERC_MAX];
static char         g_nome[DIRENT_NAME_MAX];

static ExFinestra   g_f, g_casella;
static int          g_fatto;        /* 0 = ancora aperto, 1 = OK, 2 = annullato */
static int          g_salva;        /* 1 = dialogo di salvataggio */

/* -----------------------------------------------------------------------------
 * Leggere la directory. Le directory in cima, come nel file manager: in una
 * directory con cento file, quelle in cui si vuole entrare sarebbero sparse.
 * --------------------------------------------------------------------------- */
static void leggi(void)
{
    DirEntry v[32];
    int start = 0, n, i;
    unsigned int d = 0;

    g_n = 0; g_sel = 0; g_primo = 0;

    while ((n = listdir_from(g_dir, v, 32, start)) > 0) {
        for (i = 0; i < n && g_n < VOCI_MAX; i++) {
            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;
            strncpy(g_voce[g_n].nome, v[i].name, DIRENT_NAME_MAX - 1);
            g_voce[g_n].nome[DIRENT_NAME_MAX - 1] = '\0';
            g_voce[g_n].dir = v[i].is_dir;
            g_n++;
        }
        start += n;
        if (n < 32) break;
    }

    for (i = 0; i < (int)g_n; i++)
        if (g_voce[i].dir) {
            Voce t = g_voce[i];
            int k;
            for (k = i; k > (int)d; k--) g_voce[k] = g_voce[k - 1];
            g_voce[d++] = t;
        }
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
        if (g_dir[strlen(g_dir) - 1] != '/') strcat(g_dir, "/");
        strncat(g_dir, nome, PERC_MAX - strlen(g_dir) - 1);
    }
    leggi();
}

/* Unisce directory corrente e nome in un percorso solo. */
static void componi(char *out, unsigned int max)
{
    unsigned int l;

    strncpy(out, g_dir, max - 1);
    out[max - 1] = '\0';
    l = (unsigned int)strlen(out);
    if (l && out[l - 1] != '/') { strcat(out, "/"); l++; }
    strncat(out, g_nome, max - l - 1);
}

static void lista_disegna(void)
{
    unsigned int i, righe = (unsigned int)(AREA_H / RIGA_H);
    char riga[96];

    ex_riempi(g_f, AREA_X, AREA_Y, AREA_W, AREA_H, EX_BIANCO);
    ex_riquadro_disegna(g_f, AREA_X, AREA_Y, AREA_W, AREA_H, EX_GRIGIO_SC);

    for (i = 0; i < righe && g_primo + i < g_n; i++) {
        unsigned int k = g_primo + i;
        int y = AREA_Y + 2 + (int)i * RIGA_H;

        if (k == g_sel) ex_riempi(g_f, AREA_X + 2, y - 1, AREA_W - 4, RIGA_H, EX_BLU);

        if (g_voce[k].dir) sprintf(riga, "[%s]", g_voce[k].nome);
        else               sprintf(riga, " %s", g_voce[k].nome);

        ex_scrivi(g_f, AREA_X + 4, y, riga, (k == g_sel) ? EX_BIANCO : EX_NERO);
    }

    /* Il percorso corrente, sotto l'elenco: senza, si sceglie un nome senza
     * sapere in quale directory si e' finiti. */
    ex_riempi(g_f, AREA_X, AREA_Y + AREA_H + 4, AREA_W, 16, EX_GRIGIO);
    ex_scrivi(g_f, AREA_X + 2, AREA_Y + AREA_H + 4, g_dir, EX_NERO);
}

static void ridisegna(void)
{
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    lista_disegna();
    ex_aggiorna(g_f);
}

/* Il nome scelto va nella casella: e' li' che l'utente lo vede e lo corregge. */
static void nome_metti(const char *s)
{
    strncpy(g_nome, s, DIRENT_NAME_MAX - 1);
    g_nome[DIRENT_NAME_MAX - 1] = '\0';
    ex_testo_metti(g_casella, g_nome);
}

static void conferma(void)
{
    const char *t = ex_testo_prendi(g_casella);

    /* ! SI PRENDE QUELLO CHE C'E' NELLA CASELLA, non quello che era
     * selezionato: l'utente puo' averlo battuto a mano, ed e' l'unico modo di
     * dare un nome a un file che ancora non esiste. */
    if (t && t[0]) nome_metti(t);
    if (g_nome[0] == '\0') return;      /* niente nome, niente conferma */

    g_fatto = 1;
}

static void scegli(void)
{
    if (g_sel >= g_n) return;

    if (g_voce[g_sel].dir) {
        entra(g_voce[g_sel].nome);
        nome_metti("");
        return;
    }
    nome_metti(g_voce[g_sel].nome);
    conferma();
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_OK)      { conferma();      break; }
        if (wp == ID_ANNULLA) { g_fatto = 2; return 0; }
        if (wp == ID_SU)      { entra(".."); nome_metti(""); break; }
        return 0;

    case EXM_TASTO:
        if (wp == 0x0101)      { if (g_sel + 1 < g_n) g_sel++; }
        else if (wp == 0x0100) { if (g_sel > 0) g_sel--; }
        else if ((wp & 0xFFFF) == '\n' || (wp & 0xFFFF) == '\r') {
            /* ! INVIO NELLA CASELLA CONFERMA, INVIO SULL'ELENCO ENTRA. Sono la
             * stessa cosa vista da due posti: si conferma quello che si vede
             * scritto, e se e' una directory ci si entra. */
            const char *t = ex_testo_prendi(g_casella);
            if (t && t[0]) { nome_metti(t); conferma(); }
            else           scegli();
            if (g_fatto) return 0;
        } else {
            /* Tutto il resto (le lettere) lo gestisce la casella di testo. */
            long r = ex_procedura_base(f, msg, wp, lp);
            ridisegna();
            return r;
        }
        {
            unsigned int righe = (unsigned int)(AREA_H / RIGA_H);
            if (g_sel < g_primo) g_primo = g_sel;
            if (g_sel >= g_primo + righe) g_primo = g_sel - righe + 1;
        }
        break;

    case EXM_MOUSE_GIU: {
        int y = EX_Y(lp);
        if (y >= AREA_Y && y < AREA_Y + AREA_H) {
            unsigned int k = g_primo + (unsigned int)((y - AREA_Y - 2) / RIGA_H);
            if (k < g_n) {
                g_sel = k;
                if (!g_voce[k].dir) nome_metti(g_voce[k].nome);
            }
        } else {
            long r = ex_procedura_base(f, msg, wp, lp);
            ridisegna();
            return r;
        }
        break;
    }

    case EXM_CHIUDI:
        g_fatto = 2;
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il dialogo, che e' uno solo con due titoli
 * --------------------------------------------------------------------------- */
static int dialogo(char *percorso, unsigned int max, int salva)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          x, y, i;

    g_salva = salva;
    g_fatto = 0;

    /* La directory di partenza viene da cio' che il chiamante propone. Un
     * dialogo che ricomincia sempre da / farebbe ripercorrere ogni volta la
     * stessa strada. */
    if (percorso && percorso[0]) {
        strncpy(g_dir, percorso, PERC_MAX - 1);
        g_dir[PERC_MAX - 1] = '\0';
        i = (int)strlen(g_dir);
        while (i > 1 && g_dir[i - 1] != '/') i--;
        nome_metti(g_dir + i);
        if (i > 1) i--;
        if (i == 0) i = 1;
        g_dir[i] = '\0';
    } else {
        strcpy(g_dir, "/");
        g_nome[0] = '\0';
    }

    /* In mezzo allo schermo: un dialogo nell'angolo si cerca. */
    ex_schermo(&sw, &sh);
    x = sw > DLG_W ? (int)(sw - DLG_W) / 2 : 0;
    y = sh > DLG_H ? (int)(sh - DLG_H) / 2 : 0;

    g_f = ex_crea("finestra", salva ? "Salva con nome" : "Apri",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA,
                  x, y, DLG_W, DLG_H, 0, 0, proc);
    if (g_f == 0) return 0;

    /* ! LA CASELLA SI CREA PER PRIMA, E NON E' L'ORDINE DI CIO' CHE SI VEDE.
     * ExWin da' il fuoco al PRIMO controllo creato che lo accetta, e non ha
     * (ancora) un modo pubblico di spostarlo: creando prima il pulsante «Su»,
     * il fuoco restava li' e tutto quello che si batteva finiva in un pulsante
     * che i tasti non li usa. Il dialogo sembrava sordo — e chi lo prova pensa
     * alla tastiera, non all'ordine delle chiamate.
     *
     * Quando il toolkit avra' un ex_fuoco(), queste tre righe torneranno
     * nell'ordine in cui si leggono. */
    g_casella = ex_crea("testo", "", EX_FIGLIO,
                        AREA_X + 92, AREA_Y + AREA_H + 22,
                        AREA_W - 92, 22, g_f, 0, 0);

    ex_crea("pulsante", "Su", EX_FIGLIO, AREA_X, 2, 44, 20, g_f, ID_SU, 0);
    ex_crea("etichetta", salva ? "Salva come:" : "Nome:", EX_FIGLIO,
            AREA_X, AREA_Y + AREA_H + 26, 90, 16, g_f, 0, 0);

    ex_crea("pulsante", salva ? "Salva" : "Apri", EX_FIGLIO,
            DLG_W - 180, DLG_H - 32, 80, 24, g_f, ID_OK, 0);
    ex_crea("pulsante", "Annulla", EX_FIGLIO,
            DLG_W - 92, DLG_H - 32, 80, 24, g_f, ID_ANNULLA, 0);

    ex_testo_metti(g_casella, g_nome);
    leggi();
    ridisegna();

    /* ! IL CICLO ANNIDATO. ex_prendi_msg() rende 0 solo se qualcuno ha chiamato
     * ex_esci(), cioe' se l'applicazione intera se ne sta andando: allora si
     * esce anche di qui, e si esce senza risultato. */
    while (!g_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(g_f);
    g_f = 0;

    if (g_fatto != 1) return 0;

    componi(percorso, max);
    return 1;
}

int ex_dlg_apri(char *percorso, unsigned int max)  { return dialogo(percorso, max, 0); }
int ex_dlg_salva(char *percorso, unsigned int max) { return dialogo(percorso, max, 1); }

/* -----------------------------------------------------------------------------
 * L'avviso: una finestra, un testo, un pulsante
 * --------------------------------------------------------------------------- */
static int g_av_fatto;

static long av_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO || msg == EXM_CHIUDI) { g_av_fatto = 1; return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

int ex_dlg_avviso(const char *titolo, const char *testo)
{
    ExFinestra   f;
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          x, y;

    ex_schermo(&sw, &sh);
    x = sw > 360 ? (int)(sw - 360) / 2 : 0;
    y = sh > 120 ? (int)(sh - 120) / 2 : 0;

    g_av_fatto = 0;
    f = ex_crea("finestra", titolo ? titolo : "Avviso",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA,
                x, y, 360, 120, 0, 0, av_proc);
    if (f == 0) return 1;

    ex_crea("etichetta", testo ? testo : "", EX_FIGLIO, 12, 24, 336, 16, f, 0, 0);
    ex_crea("pulsante", "OK", EX_FIGLIO, 140, 64, 80, 24, f, 1, 0);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(f);

    while (!g_av_fatto && ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(f);
    return 1;
}
