/* =============================================================================
 * exwin/bin/browser/browser.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il browser: mette insieme tutto quello che e' stato costruito
 *
 *     exhttp    prende i byte dalla rete e segue le redirezioni
 *     exhtml    da testo ad albero
 *     exfont    misura e disegna il testo, anche proporzionale
 *     exwin     la finestra, la casella dell'indirizzo, i clic
 *
 * ! L'IMPAGINAZIONE MISURA IL TESTO, NON CONTA LE LETTERE, ed e' la ragione
 * per cui i font sono arrivati prima di questo file. `larghezza = lettere per
 * otto` e' vero solo col font di sistema: un motore nato su quel presupposto
 * andrebbe riscritto il giorno che si sceglie un font proporzionale, cioe'
 * subito.
 *
 * ! E SI IMPAGINA IN DUE TEMPI: prima si producono i PEZZI — un pezzo e' un
 * tratto di testo su una riga, con la sua posizione e il suo aspetto — poi si
 * disegnano solo quelli che si vedono. Impaginare a ogni disegno vorrebbe dire
 * rifare tutto il lavoro a ogni riga di scorrimento; disegnare tutto vorrebbe
 * dire dipingere migliaia di righe fuori dalla finestra.
 *
 * ! QUELLO CHE NON C'E', DICHIARATO: niente CSS, niente tabelle impaginate come
 * tabelle, niente immagini dentro il testo, niente https. Un blocco va a capo,
 * il testo scorre e si spezza, i collegamenti si vedono e si premono. E' il
 * minimo che si possa chiamare browser, e ogni pezzo mancante ha il suo posto.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exhttp.h"
#include "html.h"
#include "kbd_proto.h"

#define FIN_W       760
#define FIN_H       520

#define BARRA_H     30          /* la riga dell'indirizzo */
#define MARGINE     8

#define ID_URL      1
#define ID_VAI      2
#define ID_INDIETRO 3

/* ! I TETTI SONO DICHIARATI E NON SI CRESCE: una pagina la sceglie chi sta
 * dall'altra parte, quindi ogni numero che dipende da lei ha un limite. Una
 * pagina che sfora si vede a meta' e lo dice, che e' meglio di una macchina
 * che rallenta finche' non finisce la memoria. */
#define PAGINA_MAX  (512u * 1024u)
#define NODI_MAX    4096
#define ATTR_MAX    2048
#define ARENA_MAX   (192u * 1024u)
#define PEZZI_MAX   6000
#define LINK_MAX    512
#define STORIA_MAX  32

/* Un tratto di testo gia' collocato. */
typedef struct {
    int          x, y, w;
    unsigned int testo;         /* scostamento nell'arena del documento */
    unsigned char titolo;       /* 1 = grande e in neretto */
    short        link;          /* indice in g_link, -1 = niente */
} Pezzo;

static unsigned char g_pagina[PAGINA_MAX];
static HtmlNodo      g_nodi[NODI_MAX];
static HtmlAttr      g_attr[ATTR_MAX];
static char          g_arena[ARENA_MAX];
static HtmlDoc       g_doc;

static Pezzo         g_pez[PEZZI_MAX];
static int           g_pez_n = 0;

static char          g_link[LINK_MAX][EXHTTP_URL_MAX];
static int           g_link_n = 0;

static char          g_storia[STORIA_MAX][EXHTTP_URL_MAX];
static int           g_storia_n = 0;

static ExFinestra    g_f, g_url, g_stato;
static ExFont        g_font_testo = 0, g_font_titolo = 0;
static int           g_scorri = 0, g_altezza = 0;
static char          g_qui[EXHTTP_URL_MAX] = "";

static int area_x(void) { return MARGINE; }
static int area_y(void) { return BARRA_H + MARGINE; }
static int area_w(void) { return FIN_W - 2 * MARGINE; }
static int area_h(void) { return FIN_H - BARRA_H - 2 * MARGINE - 20; }

/* -----------------------------------------------------------------------------
 * L'impaginazione
 * --------------------------------------------------------------------------- */
static int  g_pen_x, g_pen_y, g_riga_h;
static int  g_link_ora;
static int  g_titolo_ora;

static int alt_riga(int titolo)
{
    ExFont f = titolo ? g_font_titolo : g_font_testo;
    int    h = ex_font_altezza(f);

    return h > 0 ? h + 3 : 19;
}

static void a_capo(void)
{
    /* ! UNA RIGA VUOTA NON SI ACCUMULA. Un documento indentato produce spazi
     * fra un blocco e l'altro: andando a capo per ognuno si otterrebbero
     * pagine fatte di buchi. Si va a capo solo se sulla riga c'e' qualcosa. */
    if (g_pen_x <= area_x()) return;
    g_pen_x  = area_x();
    g_pen_y += g_riga_h;
    g_riga_h = alt_riga(g_titolo_ora);
}

static void spazio_fra_blocchi(void)
{
    a_capo();
    g_pen_y += 6;
}

/* Mette una parola, andando a capo se non ci sta. */
static void parola(const char *t, unsigned int off, int n)
{
    static char cop[256];
    ExFont      f = g_titolo_ora ? g_font_titolo : g_font_testo;
    int         w, i;

    if (n <= 0) return;
    if (n > (int)sizeof(cop) - 1) n = (int)sizeof(cop) - 1;
    for (i = 0; i < n; i++) cop[i] = t[i];
    cop[n] = '\0';

    w = ex_larghezza_testo(f, cop);

    /* ! SI VA A CAPO SULLA PAROLA, NON SUL CARATTERE, ed e' cio' che rende il
     * testo leggibile: spezzare in mezzo a una parola si vede subito. Una
     * parola piu' larga della finestra si mette lo stesso e sborda — meglio
     * che sparire. */
    if (g_pen_x + w > area_x() + area_w() && g_pen_x > area_x()) a_capo();

    if (g_pez_n < PEZZI_MAX) {
        g_pez[g_pez_n].x = g_pen_x;
        g_pez[g_pez_n].y = g_pen_y;
        g_pez[g_pez_n].w = w;
        g_pez[g_pez_n].testo = off;
        g_pez[g_pez_n].titolo = (unsigned char)g_titolo_ora;
        g_pez[g_pez_n].link = (short)g_link_ora;
        g_pez_n++;
    }

    g_pen_x += w;
    if (alt_riga(g_titolo_ora) > g_riga_h) g_riga_h = alt_riga(g_titolo_ora);
}

static int blocco(const char *nome)
{
    static const char *const B[] = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li",
        "table", "tr", "pre", "blockquote", "form", "hr", "section",
        "article", "header", "footer", "nav", "aside", "main", "title", 0
    };
    int i;

    for (i = 0; B[i]; i++) {
        const char *b = B[i];
        const char *n = nome;

        while (*b && *n && *b == *n) { b++; n++; }
        if (*b == '\0' && *n == '\0') return 1;
    }
    return 0;
}

static int uguale(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

static int titolo_di(const char *n)
{
    return uguale(n, "h1") || uguale(n, "h2") || uguale(n, "h3");
}

/* ! CIO' CHE NON SI VEDE NON SI IMPAGINA: dentro <script>, <style>, <head> e
 * <title> c'e' testo che non appartiene alla pagina. Senza questo, la prima
 * cosa che si legge su un sito vero e' un chilometro di JavaScript. */
static int invisibile(const char *n)
{
    return uguale(n, "script") || uguale(n, "style") || uguale(n, "head") ||
           uguale(n, "title") || uguale(n, "meta") || uguale(n, "link");
}

static void impagina_nodo(int v)
{
    int f;

    if (v < 0) return;

    if (g_doc.nodi[v].tipo == HTML_TESTO) {
        const char  *t = html_testo(&g_doc, v);
        unsigned int base = g_doc.nodi[v].testo;
        int          i = 0;

        while (t[i]) {
            int a;

            while (t[i] == ' ') { g_pen_x += ex_larghezza_testo(
                                      g_titolo_ora ? g_font_titolo : g_font_testo,
                                      " "); i++; }
            if (!t[i]) break;
            a = i;
            while (t[i] && t[i] != ' ') i++;
            parola(t + a, base + (unsigned int)a, i - a);
        }
        return;
    }

    {
        const char *nome = html_nome(&g_doc, v);
        int         era_titolo = g_titolo_ora;
        int         era_link   = g_link_ora;

        if (invisibile(nome)) return;

        if (uguale(nome, "br")) { g_pen_x = area_x() + 1; a_capo(); return; }

        if (blocco(nome)) spazio_fra_blocchi();
        if (titolo_di(nome)) g_titolo_ora = 1;

        if (uguale(nome, "a")) {
            const char *h = html_attr(&g_doc, v, "href");

            if (h && h[0] && g_link_n < LINK_MAX) {
                unsigned int k = 0;

                while (h[k] && k < sizeof(g_link[0]) - 1) {
                    g_link[g_link_n][k] = h[k]; k++;
                }
                g_link[g_link_n][k] = '\0';
                g_link_ora = g_link_n++;
            }
        }

        for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
            impagina_nodo(f);

        g_titolo_ora = era_titolo;
        g_link_ora   = era_link;

        if (blocco(nome)) spazio_fra_blocchi();
    }
}

static void impagina(void)
{
    g_pez_n = 0;
    g_link_n = 0;
    g_pen_x = area_x();
    g_pen_y = area_y();
    g_titolo_ora = 0;
    g_link_ora = -1;
    g_riga_h = alt_riga(0);

    impagina_nodo(g_doc.radice);
    a_capo();

    g_altezza = g_pen_y - area_y() + g_riga_h;
    if (g_altezza < 1) g_altezza = 1;
}

/* -----------------------------------------------------------------------------
 * Il disegno
 * --------------------------------------------------------------------------- */
static void disegna(void)
{
    int i;

    /* ! I CONTROLLI SI RIDISEGNANO PRIMA DEL CONTENUTO, e non basta riempire
     * di grigio. Qui c'era un ex_riempi su tutta la finestra: dipingeva SOPRA
     * la casella dell'indirizzo e i pulsanti, che sono figli e stanno negli
     * stessi pixel. Il risultato era una barra sparita al primo disegno — e
     * siccome il primo disegno arriva subito, non si vedeva mai.
     *
     * ex_procedura_base riempie il fondo E ridisegna i figli: e' la stessa
     * cosa in una chiamata, e resta giusta il giorno che si aggiunge un
     * pulsante. */
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);

    ex_riempi(g_f, area_x() - 2, area_y() - 2, area_w() + 4, area_h() + 4,
              EX_BIANCO);
    ex_incavo(g_f, area_x() - 2, area_y() - 2, area_w() + 4, area_h() + 4);

    for (i = 0; i < g_pez_n; i++) {
        int y = g_pez[i].y - g_scorri;

        /* ! SI DISEGNA SOLO CIO' CHE SI VEDE. Con una pagina di migliaia di
         * righe, dipingere tutto vorrebbe dire pagare l'intero documento a
         * ogni riga di scorrimento — e per il novantanove per cento fuori
         * dalla finestra. */
        if (y + 24 < area_y() || y > area_y() + area_h()) continue;

        {
            ExFont       f = g_pez[i].titolo ? g_font_titolo : g_font_testo;
            const char  *t = g_arena + g_pez[i].testo;
            unsigned int c = (g_pez[i].link >= 0) ? EX_BLU : EX_NERO;

            /* Il testo nell'arena e' una parola sola perche' l'impaginazione
             * l'ha spezzato: si disegna fino allo spazio. */
            {
                static char cop[256];
                int k = 0;

                while (t[k] && t[k] != ' ' && k < (int)sizeof(cop) - 1) {
                    cop[k] = t[k]; k++;
                }
                cop[k] = '\0';
                ex_scrivi_con(g_f, f, g_pez[i].x, y, cop, c);

                /* ! UN COLLEGAMENTO SI SOTTOLINEA, e non basta il colore: su
                 * uno schermo a pochi colori il blu e il nero si distinguono
                 * male, e chi non li distingue non trova i collegamenti. */
                if (g_pez[i].link >= 0)
                    ex_riempi(g_f, g_pez[i].x, y + ex_font_altezza(f) - 2,
                              g_pez[i].w, 1, EX_BLU);
            }
        }
    }

    ex_aggiorna(g_f);
}

static void dico(const char *s)
{
    if (g_stato) ex_testo_metti(g_stato, s);
}

/* -----------------------------------------------------------------------------
 * Andare
 * --------------------------------------------------------------------------- */
static void vai(const char *url, int in_storia)
{
    ExHttpEsito e;
    char        msg[160];

    if (!url || !url[0]) return;

    dico("sto scaricando...");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);

    if (!exhttp_prendi(url, g_pagina, sizeof(g_pagina), &e)) {
        sprintf(msg, "%s: %s", url, e.errore[0] ? e.errore : "non riuscito");
        dico(msg);
        return;
    }

    if (in_storia && g_storia_n < STORIA_MAX && g_qui[0]) {
        strncpy(g_storia[g_storia_n], g_qui, EXHTTP_URL_MAX - 1);
        g_storia[g_storia_n][EXHTTP_URL_MAX - 1] = '\0';
        g_storia_n++;
    }

    strncpy(g_qui, e.finale, sizeof(g_qui) - 1);
    g_qui[sizeof(g_qui) - 1] = '\0';
    ex_testo_metti(g_url, g_qui);

    html_prepara(&g_doc, g_nodi, NODI_MAX, g_attr, ATTR_MAX,
                 g_arena, ARENA_MAX);
    html_analizza(&g_doc, (const char *)g_pagina, e.byte);

    g_scorri = 0;
    impagina();

    sprintf(msg, "%d, %u byte, %u nodi%s%s", e.codice, e.byte, g_doc.nodi_n,
            e.troncata ? " (pagina troncata)" : "",
            g_doc.troncato ? " (albero troncato)" : "");
    dico(msg);

    disegna();
}

/* Un collegamento premuto: si risolve contro l'indirizzo di adesso. */
static void segui(int k)
{
    char nuovo[EXHTTP_URL_MAX];

    if (k < 0 || k >= g_link_n) return;

    /* ! I COLLEGAMENTI RELATIVI SI RISOLVONO, e sono la maggioranza: «/x» o
     * «pagina.html» senza schema. La stessa regola sta in exhttp per le
     * redirezioni; qui la si applica ai collegamenti, che e' la stessa cosa
     * vista dall'altra parte. */
    if (g_link[k][0] == 'h' && g_link[k][1] == 't') {
        strncpy(nuovo, g_link[k], sizeof(nuovo) - 1);
        nuovo[sizeof(nuovo) - 1] = '\0';
    } else {
        HttpUrl u;

        if (!http_url(g_qui, &u)) return;

        strcpy(nuovo, u.cifrato ? "https://" : "http://");
        strncat(nuovo, u.host, sizeof(nuovo) - strlen(nuovo) - 1);

        if ((u.cifrato && u.porta != 443) || (!u.cifrato && u.porta != 80)) {
            char cifre[8], rov[8];
            unsigned int p = u.porta;
            int a = 0, b = 0;

            while (p) { rov[b++] = (char)('0' + (p % 10)); p /= 10; }
            while (b) cifre[a++] = rov[--b];
            cifre[a] = '\0';
            strncat(nuovo, ":", sizeof(nuovo) - strlen(nuovo) - 1);
            strncat(nuovo, cifre, sizeof(nuovo) - strlen(nuovo) - 1);
        }

        if (g_link[k][0] == '/') {
            strncat(nuovo, g_link[k], sizeof(nuovo) - strlen(nuovo) - 1);
        } else {
            char base[HTTP_PERCORSO_MAX];
            int  i;

            strncpy(base, u.percorso, sizeof(base) - 1);
            base[sizeof(base) - 1] = '\0';
            i = (int)strlen(base);
            while (i > 0 && base[i - 1] != '/') i--;
            base[i] = '\0';
            strncat(nuovo, base, sizeof(nuovo) - strlen(nuovo) - 1);
            strncat(nuovo, g_link[k], sizeof(nuovo) - strlen(nuovo) - 1);
        }
    }

    vai(nuovo, 1);
}

/* Quale collegamento sta sotto quel punto, o -1. */
static int link_sotto(int x, int y)
{
    int i;

    for (i = 0; i < g_pez_n; i++) {
        int py = g_pez[i].y - g_scorri;
        int h  = ex_font_altezza(g_pez[i].titolo ? g_font_titolo : g_font_testo);

        if (g_pez[i].link < 0) continue;
        if (x >= g_pez[i].x && x < g_pez[i].x + g_pez[i].w &&
            y >= py && y < py + h) return g_pez[i].link;
    }
    return -1;
}

static void scorri(int quanto)
{
    int max = g_altezza - area_h();

    if (max < 0) max = 0;
    g_scorri += quanto;
    if (g_scorri < 0) g_scorri = 0;
    if (g_scorri > max) g_scorri = max;
    disegna();
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    case EXM_COMANDO:
        if (wp == ID_VAI) {
            const char *t = ex_testo_prendi(g_url);

            if (t && t[0]) vai(t, 1);
            return 0;
        }
        if (wp == ID_INDIETRO) {
            if (g_storia_n > 0) {
                char indietro[EXHTTP_URL_MAX];

                g_storia_n--;
                strncpy(indietro, g_storia[g_storia_n], sizeof(indietro) - 1);
                indietro[sizeof(indietro) - 1] = '\0';
                vai(indietro, 0);
            }
            return 0;
        }
        return 0;

    case EXM_TASTO: {
        unsigned int c = wp & 0xFFFF;

        if (c == '\n' || c == '\r') {
            const char *t = ex_testo_prendi(g_url);

            if (t && t[0]) vai(t, 1);
            return 0;
        }
        if (c == KBD_K_DOWN)  { scorri(24);  return 0; }
        if (c == KBD_K_UP)    { scorri(-24); return 0; }
        if (c == KBD_K_PGDN)  { scorri(area_h() - 24);  return 0; }
        if (c == KBD_K_PGUP)  { scorri(-(area_h() - 24)); return 0; }
        if (c == KBD_K_HOME)  { g_scorri = 0; disegna(); return 0; }
        return ex_procedura_base(f, msg, wp, lp);
    }

    case EXM_MOUSE_GIU: {
        int k = link_sotto(EX_X(lp), EX_Y(lp));

        if (k >= 0) { segui(k); return 0; }
        return 0;
    }

    case EXM_DISEGNA:
        disegna();
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

int main(int argc, char **argv)
{
    ExMsg m;

    g_f = ex_crea("finestra", "Navigatore", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("browser: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    /* ! IL FONT E' PROPORZIONALE SE C'E', ALTRIMENTI QUELLO DI SISTEMA, e non
     * si muore per un file mancante: ex_font_apri rende 0, che E' il font di
     * sistema. Una pagina con un carattere diverso e' meglio di un browser che
     * non parte. */
    g_font_testo  = ex_font_apri("/exwin/font/LiberationSerif-Regular.ttf", 15);
    g_font_titolo = ex_font_apri("/exwin/font/LiberationSans-Bold.ttf", 22);

    ex_crea("pulsante", "<", EX_FIGLIO, MARGINE, 4, 26, 22,
            g_f, ID_INDIETRO, 0);
    g_url = ex_crea("testo", "", EX_FIGLIO, MARGINE + 32, 4,
                    FIN_W - 2 * MARGINE - 32 - 56, 22, g_f, ID_URL, 0);
    ex_crea("pulsante", "Vai", EX_FIGLIO, FIN_W - MARGINE - 50, 4, 50, 22,
            g_f, ID_VAI, 0);

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      MARGINE, FIN_H - 18, FIN_W - 2 * MARGINE, 16, g_f, 0, 0);

    ex_fuoco(g_url);
    dico("scrivi un indirizzo e premi Invio. https non ancora: manca il TLS.");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    disegna();

    if (argc >= 2) { ex_testo_metti(g_url, argv[1]); vai(argv[1], 0); }

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
