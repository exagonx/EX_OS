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
 * tabelle, niente https. Un blocco va a capo, il testo scorre e si spezza, i
 * collegamenti si vedono e si premono, le immagini si scaricano e si
 * collocano. E' il minimo che si possa chiamare browser, e ogni pezzo mancante
 * ha il suo posto.
 *
 * ! LE IMMAGINI ARRIVANO DOPO IL TESTO, ED E' LA DECISIONE CHE CONTA QUI. La
 * pagina si impagina e si disegna con le sole parole; solo allora si scarica
 * un'immagine per volta, e a ognuna che arriva si reimpagina e si ridisegna.
 * Prenderle prima vorrebbe dire una finestra vuota finche' l'ultima non
 * risponde — e una che non risponde costa otto secondi da sola, cioe' una
 * pagina con cinque immagini morte non si vedrebbe per quaranta secondi.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exlib.h"
#include "eximg.h"
#include "exhttp.h"
#include "html.h"
#include "css.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"

#define FIN_W       760
#define FIN_H       520

#define BARRA_H     30          /* la riga dell'indirizzo */
#define MARGINE     8

#define ID_URL      1
#define ID_VAI      2
#define ID_INDIETRO 3
#define ID_INFO     4

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

/* ! E I TETTI DELLE IMMAGINI SONO TRE, PERCHE' TRE SONO LE COSE CHE LA PAGINA
 * SCEGLIE: quante ne mette, quanto pesa ognuna, e quanti pixel diventano una
 * volta decodificate. Il terzo e' quello che conta davvero: centoventotto
 * chilobyte di PNG possono essere quattromila per tremila pixel, cioe'
 * quarantotto megabyte su una macchina che ne ha trentadue. */
/* ! I TETTI DEL FOGLIO DI STILE, con la stessa regola di tutto il resto: li
 * sceglie chi apre la pagina, non la pagina. Un sito con diecimila regole non
 * deve poter decidere quanta memoria prendere qui — si applica quello che ci
 * sta e si dice che il resto e' stato lasciato fuori. */
#define CSS_REGOLE_MAX  600
#define CSS_DICH_MAX    2000
#define CSS_ARENA_MAX   (24u * 1024u)
#define CSS_FOGLI_MAX   4       /* quanti <link rel=stylesheet> si seguono */

#define IMM_MAX      12
#define IMM_BYTE_MAX (128u * 1024u)     /* il file di UNA immagine  */
#define IMM_PX_TOT   (512u * 1024u)     /* i pixel tenuti, IN TUTTO */

/* Un tratto di testo — o un'immagine — gia' collocato. */
/* ! IL PEZZO PORTA IL CARATTERE E IL COLORE GIA' SCELTI, e non piu' un «e' un
 * titolo si'/no». Con i fogli di stile il carattere non e' piu' una di due
 * possibilita': dipende da `font-weight`, `font-style` e `font-size`, che
 * cambiano elemento per elemento. Deciderlo durante l'impaginazione — dove lo
 * stile e' gia' calcolato — e lasciare al disegno solo il compito di usarlo
 * tiene la scelta in un posto solo. */
typedef struct {
    int          x, y, w;
    unsigned int testo;         /* scostamento nell'arena del documento */
    ExFont       font;          /* il carattere, gia' scelto            */
    unsigned int colore;        /* ARGB, gia' deciso                    */
    short        h;             /* solo per le immagini: la loro altezza */
    short        link;          /* indice in g_link, -1 = niente         */
    short        img;           /* indice in g_imm, -1 = e' testo        */
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

/* -----------------------------------------------------------------------------
 * Le immagini
 *
 * ! L'IDENTITA' DI UN'IMMAGINE E' IL SUO NODO, non la sua posizione: la pagina
 * si reimpagina a ogni immagine che arriva, e alla seconda impaginazione la
 * prima immagine deve ritrovarsi, non riscaricarsi. Gli indici dei nodi non si
 * muovono finche' l'albero e' quello.
 *
 * ! E I PIXEL SONO NOSTRI, NON DI eximg: si decodifica, si copia nella misura
 * con cui si disegnera', e il bitmap naturale si restituisce SUBITO. Tenerlo
 * vorrebbe dire lasciar scegliere alla pagina quanta memoria prendere.
 * --------------------------------------------------------------------------- */
typedef struct {
    int           nodo;             /* il nodo <img> dentro g_doc              */
    unsigned int  dich_w, dich_h;   /* width= e height=, 0 se non ci sono      */
    unsigned int  w, h;             /* la misura con cui si disegna            */
    unsigned int *px;               /* ARGB, nostri: free() li restituisce     */
    unsigned char stato;            /* 0 da prendere, 1 presa, 2 rinunciata    */
    char          src[EXHTTP_URL_MAX];
} Imm;

/* -----------------------------------------------------------------------------
 * I riquadri di sfondo
 *
 * ! UNO SFONDO NON E' UN PEZZO, E' CIO' CHE STA SOTTO I PEZZI, quindi vive in
 * un elenco suo e si disegna PRIMA di tutto il testo. Metterlo fra i pezzi
 * vorrebbe dire dipingere sopra le parole gia' scritte ogni volta che un
 * blocco colorato viene dopo — e l'ordine dei pezzi e' quello del documento,
 * che non ha niente a che fare con la profondita'.
 *
 * ! E LA MISURA SI SA SOLO QUANDO IL BLOCCO E' FINITO: si segna la y d'inizio
 * entrando e si chiude il riquadro uscendo.
 * --------------------------------------------------------------------------- */
#define SFONDI_MAX  256

typedef struct {
    int          x, y, w, h;
    unsigned int colore;
} Sfondo;

static Sfondo g_sfondi[SFONDI_MAX];
static int    g_sfondi_n = 0;

static CssRegola     g_css_reg[CSS_REGOLE_MAX];
static CssDich       g_css_dich[CSS_DICH_MAX];
static char          g_css_arena[CSS_ARENA_MAX];
static CssFoglio     g_css;

static Imm           g_imm[IMM_MAX];
static int           g_imm_n = 0;
static unsigned int  g_imm_px = 0;      /* quanti pixel si stanno tenendo */
static unsigned char g_imm_buf[IMM_BYTE_MAX];

static int  (*g_img_carica)(const unsigned char *, unsigned int, EximgBitmap *);
static void (*g_img_libera)(EximgBitmap *);

static int area_x(void) { return MARGINE; }
static int area_y(void) { return BARRA_H + MARGINE; }
static int area_w(void) { return FIN_W - 2 * MARGINE; }
static int area_h(void) { return FIN_H - BARRA_H - 2 * MARGINE - 20; }

/* eximg.so si apre una volta sola, e una volta sola si rinuncia.
 *
 * ! IL PERCORSO E' DOPPIO come per ogni libreria: su un sistema installato sta
 * in /exwin/lib, avviando dal CD sotto /cdrom.
 *
 * ! E SE NON C'E' NON SI MUORE: un browser senza eximg mostra il testo, che e'
 * la maggior parte di una pagina. E' la stessa scelta che fa il toolkit in
 * ex_immagine, per la stessa ragione. */
static int eximg_pronta(void)
{
    static const char *const dove[] = {
        "/exwin/lib/eximg.so",
        "/cdrom/exwin/lib/eximg.so"
    };
    static int cercata = 0;

    if (!cercata) {
        const ExLibTesta *t;

        cercata = 1;    /* prima del tentativo: non si ricerca a ogni immagine */
        t = exlib_apri_fra(dove, (int)(sizeof dove / sizeof dove[0]));
        if (t) {
            g_img_carica = (int (*)(const unsigned char *, unsigned int,
                                    EximgBitmap *))
                           exlib_simbolo(t, "eximg_carica");
            g_img_libera = (void (*)(EximgBitmap *))
                           exlib_simbolo(t, "eximg_libera");
        }
    }

    /* Uno dei due senza l'altro vuol dire una eximg.so piu' vecchia di questo
     * browser: si rinuncia invece di chiamare un indirizzo nullo. */
    return g_img_carica != 0 && g_img_libera != 0;
}

static void imm_libera_tutte(void)
{
    int i;

    for (i = 0; i < g_imm_n; i++)
        if (g_imm[i].px) { free(g_imm[i].px); g_imm[i].px = 0; }

    g_imm_n  = 0;
    g_imm_px = 0;
}

/* Le cifre in testa, e basta.
 *
 * ! «80%» NON E' OTTANTA PIXEL, quindi rende 0 — cioe' «non l'hanno detto» —
 * invece di collocare un'immagine larga ottanta punti. Le percentuali vogliono
 * la larghezza del contenitore, che qui non esiste: meglio la misura vera. */
static unsigned int numero(const char *s)
{
    unsigned int v = 0;
    int          viste = 0;

    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (unsigned int)(*s - '0');
        s++;
        viste = 1;
    }
    if (!viste || *s == '%') return 0;
    return v;
}

/* Trova l'immagine di questo nodo, o la registra. Rende -1 se non c'e' posto. */
static int imm_indice(int nodo, const char *src)
{
    int i;

    for (i = 0; i < g_imm_n; i++)
        if (g_imm[i].nodo == nodo) return i;

    if (g_imm_n >= IMM_MAX) return -1;

    i = g_imm_n++;
    g_imm[i].nodo   = nodo;
    g_imm[i].dich_w = numero(html_attr(&g_doc, nodo, "width"));
    g_imm[i].dich_h = numero(html_attr(&g_doc, nodo, "height"));
    g_imm[i].w      = 0;
    g_imm[i].h      = 0;
    g_imm[i].px     = 0;
    g_imm[i].stato  = 0;
    strncpy(g_imm[i].src, src, sizeof(g_imm[i].src) - 1);
    g_imm[i].src[sizeof(g_imm[i].src) - 1] = '\0';
    return i;
}

/* Da quanto e' l'immagine a quanto se ne disegna: `width`/`height` se ci sono,
 * altrimenti la misura naturale — e in ogni caso dentro l'area.
 *
 * ! LE PROPORZIONI SI TENGONO ANCHE QUANDO SI RIDUCE, e non e' vezzo: una
 * fotografia schiacciata si riconosce peggio di una fotografia piccola. */
static void misura(const Imm *im, unsigned int nw, unsigned int nh,
                   unsigned int *pw, unsigned int *ph)
{
    unsigned int w  = im->dich_w, h = im->dich_h;
    unsigned int aw = (unsigned int)area_w(), ah = (unsigned int)area_h();

    *pw = 0;
    *ph = 0;
    if (nw == 0 || nh == 0) return;

    if (!w && !h)  { w = nw; h = nh; }
    else if (!h)   { h = nh * w / nw; }
    else if (!w)   { w = nw * h / nh; }

    /* ! IL TETTO E' LA FINESTRA. Una pagina che dichiara width=4000 non deve
     * poter chiedere quattromila colonne di pixel. */
    if (w > aw && w) { h = h * aw / w; w = aw; }
    if (h > ah && h) { w = w * ah / h; h = ah; }

    if (w < 1) w = 1;
    if (h < 1) h = 1;

    *pw = w;
    *ph = h;
}

/* Copia nella misura voluta, col vicino piu' vicino. */
static unsigned int *ridimensiona(const EximgBitmap *bm,
                                  unsigned int w, unsigned int h)
{
    unsigned int *d = (unsigned int *)malloc(w * h * sizeof(unsigned int));
    unsigned int  y;

    if (!d) return 0;

    for (y = 0; y < h; y++) {
        const unsigned int *s = bm->px +
                                (y * bm->altezza / h) * bm->larghezza;
        unsigned int       *r = d + y * w;
        unsigned int        x;

        for (x = 0; x < w; x++) r[x] = s[x * bm->larghezza / w];
    }
    return d;
}

/* -----------------------------------------------------------------------------
 * L'impaginazione
 * --------------------------------------------------------------------------- */
static int  g_pen_x, g_pen_y, g_riga_h;
static int  g_link_ora;

/* Lo stile dell'elemento dentro cui stiamo impaginando adesso. I nodi di testo
 * non hanno uno stile proprio: usano quello del padre, che e' questo. */
static CssStile g_stile_ora;

/* -----------------------------------------------------------------------------
 * La riga e i rientri
 *
 * ! I MARGINI SINISTRO E DESTRO RESTRINGONO LA RIGA, e vanno tenuti come stato
 * dell'impaginazione perche' si ACCUMULANO: un `blockquote` dentro un altro
 * rientra due volte. Il valore si mette entrando nell'elemento e si rimette
 * com'era uscendone — la stessa disciplina del collegamento in corso.
 *
 * ! E L'ALLINEAMENTO NON SI PUO' APPLICARE MENTRE SI SCRIVE, ed e' la ragione
 * per cui serve `g_riga_primo`: per centrare una riga bisogna sapere quanto e'
 * larga, e lo si sa solo quando e' finita. Si segna dove la riga comincia, e
 * al momento di andare a capo si spostano tutti i pezzi che ci stanno dentro.
 * --------------------------------------------------------------------------- */
static int g_marg_sx, g_marg_dx;    /* rientri di adesso, in pixel */
static int g_riga_primo;            /* primo pezzo della riga in corso */

/* ! MENTRE SI MISURA NON SI ALLINEA, e non e' un dettaglio: la prima passata
 * delle tabelle impagina ogni cella su tutta la riga per vedere quanto e'
 * larga, e la larghezza si legge dal bordo destro dei pezzi. Con un `th`
 * centrato — e il foglio predefinito li centra tutti — quei pezzi finiscono in
 * mezzo alla riga, e la misura torna larga quanto meta' pagina invece che
 * quanto la parola. Il risultato erano tre colonne quasi uguali, con quella
 * lunga stretta e quella di due lettere larghissima.
 *
 * L'allineamento e' una decisione su DOVE mettere una riga; la misura chiede
 * QUANTO occupa. Tenerli separati e' la correzione, non un caso particolare. */
static int g_misura = 0;

static int riga_x(void) { return area_x() + g_marg_sx; }
static int riga_w(void)
{
    int w = area_w() - g_marg_sx - g_marg_dx;

    /* Una pagina che dichiara margini enormi non deve poter produrre una riga
     * di larghezza negativa: si stringe fino a un minimo e li' ci si ferma. */
    return w < 40 ? 40 : w;
}

/* -----------------------------------------------------------------------------
 * La riserva dei caratteri
 *
 * ! UN CARATTERE SI APRE UNA VOLTA SOLA E SI TIENE. Con i fogli di stile la
 * faccia non e' piu' una di due: `font-weight`, `font-style` e `font-size` la
 * scelgono elemento per elemento, e aprire un TrueType costa — il file si
 * legge, il contenitore si analizza, la cache dei glifi si riempie. Senza
 * questa riserva una pagina con dieci corpi diversi aprirebbe dieci volte lo
 * stesso file.
 *
 * ! E IL TETTO E' DICHIARATO: oltre, si ripiega sul carattere di sistema invece
 * di continuare ad aprirne. Le combinazioni le sceglie la pagina.
 * --------------------------------------------------------------------------- */
#define FONT_MAX    12
#define CORPO_MIN   6
#define CORPO_MAX   72

typedef struct {
    unsigned char neretto, corsivo, fisso;
    short         corpo;
    ExFont        f;
} FontVoce;

static FontVoce g_font[FONT_MAX];
static int      g_font_n = 0;

/* ! IL CARATTERE A LARGHEZZA FISSA E' UNA FAMIGLIA, NON UN CORPO: dentro <pre>
 * e <code> gli spazi devono valere quanto le lettere, o l'incolonnamento — che
 * e' l'unica ragione per cui quel testo e' preformattato — non si vede. */
static ExFont font_per(int neretto, int corsivo, int fisso, int corpo)
{
    static const char *const FACCIA[8] = {
        "/exwin/font/LiberationSerif-Regular.ttf",
        "/exwin/font/LiberationSerif-Bold.ttf",
        "/exwin/font/LiberationSerif-Italic.ttf",
        "/exwin/font/LiberationSerif-BoldItalic.ttf",
        "/exwin/font/LiberationMono-Regular.ttf",
        "/exwin/font/LiberationMono-Bold.ttf",
        "/exwin/font/LiberationMono-Italic.ttf",
        "/exwin/font/LiberationMono-BoldItalic.ttf"
    };
    int i, k;

    if (corpo < CORPO_MIN) corpo = CORPO_MIN;
    if (corpo > CORPO_MAX) corpo = CORPO_MAX;
    neretto = neretto ? 1 : 0;
    corsivo = corsivo ? 1 : 0;
    fisso   = fisso   ? 1 : 0;

    for (i = 0; i < g_font_n; i++)
        if (g_font[i].neretto == neretto && g_font[i].corsivo == corsivo &&
            g_font[i].fisso == fisso && g_font[i].corpo == (short)corpo)
            return g_font[i].f;

    if (g_font_n >= FONT_MAX) return g_font_testo;

    k = neretto + corsivo * 2 + fisso * 4;
    g_font[g_font_n].f = ex_font_apri(FACCIA[k], corpo);

    /* ! ex_font_apri RENDE 0 SE IL FILE NON C'E', e zero E' il font di sistema:
     * si mette in riserva lo stesso, cosi' non si torna a cercarlo a ogni
     * parola. Una pagina con un carattere diverso e' meglio di una pagina
     * lenta. */
    g_font[g_font_n].neretto = (unsigned char)neretto;
    g_font[g_font_n].corsivo = (unsigned char)corsivo;
    g_font[g_font_n].fisso   = (unsigned char)fisso;
    g_font[g_font_n].corpo   = (short)corpo;
    return g_font[g_font_n++].f;
}

/* Dentro quanti <pre>/<code> siamo: e' un contatore e non un si'/no, perche'
 * si annidano — <pre> con dentro <code> e' comunissimo. */
static int g_fisso = 0;

/* Il carattere che tocca allo stile di adesso. */
static ExFont font_di(const CssStile *st)
{
    int neretto = (st->grassetto == 1);
    int corsivo = (st->corsivo == 1);
    int corpo   = (st->corpo == CSS_MISURA_NO) ? 15 : st->corpo;

    if (!neretto && !corsivo && !g_fisso && corpo == 15) return g_font_testo;
    return font_per(neretto, corsivo, g_fisso > 0, corpo);
}

static unsigned int colore_di(const CssStile *st)
{
    return (st->colore == CSS_NIENTE) ? EX_NERO : st->colore;
}

static int alt_riga_f(ExFont f)
{
    int h = ex_font_altezza(f);

    return h > 0 ? h + 3 : 19;
}

/* Sposta i pezzi della riga appena finita, se non e' allineata a sinistra. */
static void allinea_riga(void)
{
    int avanzo, dx, i;

    if (g_misura) return;
    if (g_stile_ora.allineamento != CSS_ALL_CENTRO &&
        g_stile_ora.allineamento != CSS_ALL_DX) return;
    if (g_riga_primo >= g_pez_n) return;

    avanzo = (riga_x() + riga_w()) - g_pen_x;
    if (avanzo <= 0) return;

    dx = (g_stile_ora.allineamento == CSS_ALL_CENTRO) ? avanzo / 2 : avanzo;
    for (i = g_riga_primo; i < g_pez_n; i++) g_pez[i].x += dx;
}

static void a_capo(void)
{
    /* ! UNA RIGA VUOTA NON SI ACCUMULA. Un documento indentato produce spazi
     * fra un blocco e l'altro: andando a capo per ognuno si otterrebbero
     * pagine fatte di buchi. Si va a capo solo se sulla riga c'e' qualcosa. */
    if (g_pen_x <= riga_x()) return;

    allinea_riga();

    g_pen_x  = riga_x();
    g_pen_y += g_riga_h;
    g_riga_h = alt_riga_f(font_di(&g_stile_ora));
    g_riga_primo = g_pez_n;
}

/* Lo spazio sopra e sotto un blocco. ! IL MARGINE DICHIARATO SOSTITUISCE IL
 * PREDEFINITO, non ci si somma: `margin-top: 0` deve poter togliere lo spazio,
 * e sommando non lo toglierebbe mai. */
static void spazio_blocco(int quale)
{
    int m = g_stile_ora.margine[quale];

    a_capo();
    g_pen_y += (m == CSS_MISURA_NO) ? 6 : m;
}


/* Mette una parola, andando a capo se non ci sta. */
static void parola(const char *t, unsigned int off, int n)
{
    static char cop[256];
    ExFont      f = font_di(&g_stile_ora);
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
    if (g_pen_x + w > riga_x() + riga_w() && g_pen_x > riga_x()) a_capo();

    if (g_pez_n < PEZZI_MAX) {
        g_pez[g_pez_n].x = g_pen_x;
        g_pez[g_pez_n].y = g_pen_y;
        g_pez[g_pez_n].w = w;
        g_pez[g_pez_n].testo = off;
        g_pez[g_pez_n].font = f;
        g_pez[g_pez_n].colore = colore_di(&g_stile_ora);
        g_pez[g_pez_n].h = 0;
        g_pez[g_pez_n].link = (short)g_link_ora;
        g_pez[g_pez_n].img = -1;
        g_pez_n++;
    }

    g_pen_x += w;
    if (alt_riga_f(f) > g_riga_h) g_riga_h = alt_riga_f(f);
}

/* Un testo intero, spezzato in parole. Serve al testo dei nodi e al testo di
 * ripiego di un'immagine, che e' la stessa cosa: parole nell'arena. */
/* =============================================================================
 * IL TESTO CHE NON VIENE DAL DOCUMENTO
 *
 * ! I SEGNI DELLE LISTE NON STANNO NELLA PAGINA, e un pezzo pero' sa indicare
 * solo un punto dell'arena del documento. Si scrivono percio' IN CODA a
 * quell'arena, dopo il segno lasciato da html_analizza: e' memoria che il
 * browser possiede gia' e che nessun altro tocca piu'.
 *
 * ! E SI RIPARTE DAL SEGNO A OGNI IMPAGINAZIONE, o la coda crescerebbe di un
 * giro per volta — e la pagina si reimpagina a ogni immagine che arriva.
 * ========================================================================== */
static unsigned int g_arena_doc = 0;    /* dove finisce il testo del documento */

static unsigned int genera(const char *s)
{
    unsigned int inizio = g_doc.arena_n, i = 0;

    while (s[i]) i++;
    if (g_doc.arena_n + i + 1 > ARENA_MAX) return 0;

    for (i = 0; s[i]; i++) g_arena[g_doc.arena_n++] = s[i];
    g_arena[g_doc.arena_n++] = '\0';
    return inizio;
}

static void parole(const char *t, unsigned int base)
{
    int i = 0;

    while (t[i]) {
        int a;

        /* ! DENTRO <pre> LO SPAZIO E' TESTO, e l'a capo pure: e' tutta la
         * ragione per cui quel tag esiste. Fuori, una sequenza di spazi e di a
         * capo vale uno spazio solo — che e' la regola dell'HTML. */
        if (g_fisso > 0) {
            if (t[i] == '\n') { g_pen_x = riga_x() + 1; a_capo(); i++; continue; }
            if (t[i] == '\r') { i++; continue; }
            if (t[i] == ' ' || t[i] == '\t') {
                int quanti = (t[i] == '\t') ? 8 : 1;

                g_pen_x += quanti * ex_larghezza_testo(font_di(&g_stile_ora), " ");
                i++;
                continue;
            }
            a = i;
            while (t[i] && t[i] != ' ' && t[i] != '\t' &&
                   t[i] != '\n' && t[i] != '\r') i++;
            parola(t + a, base + (unsigned int)a, i - a);
            continue;
        }

        while (t[i] == ' ') {
            g_pen_x += ex_larghezza_testo(font_di(&g_stile_ora), " ");
            i++;
        }
        if (!t[i]) break;
        a = i;
        while (t[i] && t[i] != ' ') i++;
        parola(t + a, base + (unsigned int)a, i - a);
    }
}

/* Un'immagine gia' decodificata: si colloca come una parola molto grande. */
static void pezzo_immagine(int k)
{
    int w = (int)g_imm[k].w;
    int h = (int)g_imm[k].h;

    if (g_pen_x + w > riga_x() + riga_w() && g_pen_x > riga_x()) a_capo();

    if (g_pez_n < PEZZI_MAX) {
        g_pez[g_pez_n].x = g_pen_x;
        g_pez[g_pez_n].y = g_pen_y;
        g_pez[g_pez_n].w = w;
        g_pez[g_pez_n].testo = 0;
        g_pez[g_pez_n].font = g_font_testo;
        g_pez[g_pez_n].colore = EX_NERO;
        g_pez[g_pez_n].h = (short)h;
        g_pez[g_pez_n].link = (short)g_link_ora;
        g_pez[g_pez_n].img = (short)k;
        g_pez_n++;
    }

    g_pen_x += w;

    /* ! LA RIGA CRESCE FINO ALL'IMMAGINE, altrimenti la riga dopo le passa
     * sopra: l'altezza di una riga e' quella del suo pezzo piu' alto. */
    if (h + 3 > g_riga_h) g_riga_h = h + 3;
}

static int blocco(const char *nome)
{
    static const char *const B[] = {
        "p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li",
        "pre", "blockquote", "form", "hr", "section",
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

/* ! CIO' CHE NON SI VEDE NON SI IMPAGINA: dentro <script>, <style>, <head> e
 * <title> c'e' testo che non appartiene alla pagina. Senza questo, la prima
 * cosa che si legge su un sito vero e' un chilometro di JavaScript. */
static int invisibile(const char *n)
{
    return uguale(n, "script") || uguale(n, "style") || uguale(n, "head") ||
           uguale(n, "title") || uguale(n, "meta") || uguale(n, "link");
}

/* impagina_nodo e impagina_tabella si chiamano a vicenda: una tabella contiene
 * del contenuto qualunque, e quel contenuto puo' contenere un'altra tabella. */
static void impagina_nodo(int v, const CssStile *ered);

/* =============================================================================
 * IL FOGLIO PREDEFINITO — quello che il browser porta con se'
 *
 * ! I TAG CHE «CAMBIANO L'ASPETTO» SONO DIVENTATI CSS, E NON E' UN GIRO PIU'
 * LUNGO: e' cio' che li rende sovrascrivibili. Prima `h1` era grande e in
 * neretto perche' c'era un `if` nell'impaginazione, e nessuna pagina poteva
 * dire altrimenti. Adesso e' una regola come le altre, con l'origine piu'
 * bassa della cascata: la pagina che vuole un `h1` piccolo lo ottiene.
 *
 * ! ED E' ANCHE COME SONO ARRIVATI <b>, <i>, <strong> ed <em>, che prima non
 * c'erano: cinque righe qui invece di cinque casi nel motore.
 * ============================================================================= */
static const char CSS_DI_SISTEMA[] =
    "h1 { font-size: 22px; font-weight: bold }"
    "h2 { font-size: 19px; font-weight: bold }"
    "h3 { font-size: 17px; font-weight: bold }"
    "h4, h5, h6 { font-weight: bold }"
    "b, strong { font-weight: bold }"
    "i, em, cite, var { font-style: italic }"
    "a { color: #0000ee }"
    "blockquote { margin-left: 32px; margin-right: 16px }"
    "ul, ol, dd { margin-left: 28px }"
    "center { text-align: center }"
    "th { font-weight: bold; text-align: center }";

/* =============================================================================
 * LE TABELLE — e sono l'unico posto che vuole DUE passate
 *
 * ! LA LARGHEZZA DI UNA COLONNA NON SI SA FINCHE' NON SI E' GUARDATO OGNI
 * CONTENUTO DI QUELLA COLONNA, ed e' tutta la difficolta': il resto della
 * pagina si impagina in avanti, una parola dopo l'altra, senza tornare
 * indietro. Qui no. Prima si misura ogni cella come se avesse tutta la riga a
 * disposizione, poi si decide quanto e' larga ogni colonna, e solo allora si
 * impagina davvero.
 *
 * ! LA PRIMA PASSATA PRODUCE PEZZI CHE SI BUTTANO, e vanno buttati per
 * davvero: si segna dove arrivava `g_pez_n` e ci si torna. Lasciarli
 * vorrebbe dire disegnare due volte ogni cella, la seconda nel posto giusto e
 * la prima dove capita.
 *
 * ! E SE LE COLONNE NON CI STANNO SI RESTRINGONO IN PROPORZIONE, non si
 * lasciano sbordare: una tabella piu' larga della finestra e' la cosa che
 * rende illeggibili le pagine vere, e qui non c'e' lo scorrimento
 * orizzontale per rimediare.
 *
 * ! QUELLO CHE NON C'E', DICHIARATO: niente `colspan`/`rowspan` — una cella
 * che ne chiede uno occupa una colonna sola e le altre restano al loro posto,
 * che e' storto ma non disallinea il resto — e niente bordi.
 * ============================================================================= */
#define TAB_COL_MAX     10
#define TAB_RIG_MAX     120
#define TAB_LIV_MAX     3       /* tabelle dentro tabelle */
#define TAB_SPAZIO      8       /* fra una colonna e l'altra */

static int g_tab_liv = 0;

static int e_riga(const char *n)  { return uguale(n, "tr"); }
static int e_cella(const char *n) { return uguale(n, "td") || uguale(n, "th"); }

/* Raccoglie le `tr` di questa tabella, saltando thead/tbody/tfoot e fermandosi
 * davanti a una tabella annidata — le sue righe sono sue. */
static void raccogli_righe(int v, int *righe, int *n)
{
    int f;

    for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
        const char *nome;

        if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
        nome = html_nome(&g_doc, f);

        if (uguale(nome, "table")) continue;
        if (e_riga(nome)) { if (*n < TAB_RIG_MAX) righe[(*n)++] = f; continue; }
        raccogli_righe(f, righe, n);
    }
}

/* Impagina `nodo` dentro una colonna, e rende l'altezza che ha occupato.
 * Con `prova` a 1 i pezzi si buttano e si rende invece la larghezza usata. */
static int impagina_in_colonna(int nodo, const CssStile *ered,
                               int x, int y, int w, int prova, int *alt)
{
    int era_sx = g_marg_sx, era_dx = g_marg_dx;
    int era_px = g_pen_x, era_py = g_pen_y;
    int era_rh = g_riga_h, era_rp = g_riga_primo;
    int primo  = g_pez_n, primo_sf = g_sfondi_n;
    int era_mis = g_misura;
    int larga  = 0, i, f;

    g_misura = prova;

    g_marg_sx = x - area_x();
    g_marg_dx = (area_x() + area_w()) - (x + w);
    if (g_marg_sx < 0) g_marg_sx = 0;
    if (g_marg_dx < 0) g_marg_dx = 0;

    g_pen_x      = riga_x();
    g_pen_y      = y;
    g_riga_h     = alt_riga_f(font_di(ered));
    g_riga_primo = g_pez_n;

    /* I figli della cella, non la cella: `td` non e' un blocco, e trattarlo da
     * tale aggiungerebbe uno stacco dentro ogni casella. */
    for (f = g_doc.nodi[nodo].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
        impagina_nodo(f, ered);
    a_capo();

    *alt = g_pen_y - y;
    if (*alt < g_riga_h) *alt = g_riga_h;

    for (i = primo; i < g_pez_n; i++) {
        int destra = g_pez[i].x + g_pez[i].w - x;

        if (destra > larga) larga = destra;
    }

    if (prova) { g_pez_n = primo; g_sfondi_n = primo_sf; }
    g_misura = era_mis;

    g_marg_sx = era_sx; g_marg_dx = era_dx;
    g_pen_x = era_px;   g_pen_y = era_py;
    g_riga_h = era_rh;  g_riga_primo = era_rp;
    return larga;
}

static void impagina_tabella(int v, const CssStile *mio)
{
    int      righe[TAB_RIG_MAX], n_righe = 0;
    int      largh[TAB_COL_MAX];
    int      n_col = 0, r, c, somma = 0, disp, alt;
    int      x0, y0;

    raccogli_righe(v, righe, &n_righe);
    if (n_righe == 0 || g_tab_liv >= TAB_LIV_MAX) {
        int f;

        /* Niente righe, o troppo annidata: si impagina come un blocco
         * qualunque, che e' cio' che si faceva prima delle tabelle. */
        for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
            impagina_nodo(f, mio);
        return;
    }

    g_tab_liv++;
    for (c = 0; c < TAB_COL_MAX; c++) largh[c] = 0;

    /* --- prima passata: quanto vorrebbe essere larga ogni colonna --------- */
    for (r = 0; r < n_righe; r++) {
        int f;

        c = 0;
        for (f = g_doc.nodi[righe[r]].primo_figlio; f >= 0;
             f = g_doc.nodi[f].prossimo) {
            CssStile sc;
            int      w, a;

            if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
            if (!e_cella(html_nome(&g_doc, f))) continue;
            if (c >= TAB_COL_MAX) break;

            css_calcola(&g_css, &g_doc, f, mio, &sc);
            w = impagina_in_colonna(f, &sc, riga_x(), 0, riga_w(), 1, &a);
            if (w > largh[c]) largh[c] = w;
            c++;
        }
        if (c > n_col) n_col = c;
    }

    if (n_col == 0) { g_tab_liv--; return; }

    /* --- la distribuzione ------------------------------------------------- */
    for (c = 0; c < n_col; c++) {
        if (largh[c] < 12) largh[c] = 12;
        somma += largh[c];
    }
    disp = riga_w() - (n_col - 1) * TAB_SPAZIO;
    if (disp < n_col * 12) disp = n_col * 12;

    if (somma > disp) {
        /* Si stringe in proporzione: chi voleva piu' spazio ne perde di piu'. */
        int resto = disp;

        for (c = 0; c < n_col; c++) {
            int w = (c == n_col - 1) ? resto : (int)((long)largh[c] * disp / somma);

            if (w < 12) w = 12;
            largh[c] = w;
            resto -= w;
            if (resto < 0) resto = 0;
        }
    }

    /* --- seconda passata: si impagina per davvero ------------------------- */
    a_capo();
    y0 = g_pen_y;

    for (r = 0; r < n_righe; r++) {
        int f, alt_riga_tab = 0;

        x0 = riga_x();
        c  = 0;
        for (f = g_doc.nodi[righe[r]].primo_figlio; f >= 0;
             f = g_doc.nodi[f].prossimo) {
            CssStile sc;

            if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
            if (!e_cella(html_nome(&g_doc, f))) continue;
            if (c >= n_col) break;

            css_calcola(&g_css, &g_doc, f, mio, &sc);

            /* Lo sfondo della cella si segna PRIMA, con l'altezza rimessa a
             * posto quando la riga e' finita: e' lo stesso giro dei blocchi. */
            if (sc.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                g_sfondi[g_sfondi_n].x = x0;
                g_sfondi[g_sfondi_n].y = y0;
                g_sfondi[g_sfondi_n].w = largh[c];
                g_sfondi[g_sfondi_n].h = 0;
                g_sfondi[g_sfondi_n].colore = sc.sfondo;
                g_sfondi_n++;
            }

            impagina_in_colonna(f, &sc, x0, y0, largh[c], 0, &alt);
            if (alt > alt_riga_tab) alt_riga_tab = alt;

            x0 += largh[c] + TAB_SPAZIO;
            c++;
        }

        /* Gli sfondi di questa riga prendono adesso la loro altezza vera. */
        {
            int k;

            for (k = g_sfondi_n - 1; k >= 0; k--) {
                if (g_sfondi[k].y != y0 || g_sfondi[k].h != 0) continue;
                g_sfondi[k].h = alt_riga_tab;
            }
        }

        y0 += alt_riga_tab;
    }

    g_pen_y      = y0;
    g_pen_x      = riga_x();
    g_riga_h     = alt_riga_f(font_di(mio));
    g_riga_primo = g_pez_n;
    g_tab_liv--;
}

static void impagina_nodo(int v, const CssStile *ered)
{
    int f;

    if (v < 0) return;

    /* ! UN NODO DI TESTO NON HA UNO STILE SUO: prende quello del padre, che e'
     * esattamente cio' che `ered` porta. Calcolargliene uno vorrebbe dire far
     * corrispondere dei selettori a qualcosa che selettore non ha. */
    if (g_doc.nodi[v].tipo == HTML_TESTO) {
        g_stile_ora = *ered;
        parole(html_testo(&g_doc, v), g_doc.nodi[v].testo);
        return;
    }

    {
        const char *nome = html_nome(&g_doc, v);
        int         era_link = g_link_ora;
        int         era_sx = g_marg_sx, era_dx = g_marg_dx;
        int         sfondo_mio = -1;
        CssStile    mio;
        int         e_blocco;

        if (invisibile(nome)) return;

        css_calcola(&g_css, &g_doc, v, ered, &mio);

        /* ! `display: none` TOGLIE ANCHE I FIGLI, e va fatto qui prima di
         * qualunque altra cosa: e' cosi' che i siti veri nascondono i menu che
         * si aprono col mouse. Impaginarli lo stesso vorrebbe dire una pagina
         * piena di voci che non dovrebbero vedersi. */
        if (mio.display == CSS_DISPLAY_NIENTE) return;

        g_stile_ora = mio;

        /* ! LA TABELLA HA UNA STRADA SUA, e va presa PRIMA della logica dei
         * blocchi: quella impagina i figli uno dietro l'altro, che e'
         * esattamente cio' che una tabella non deve fare. */
        if (uguale(nome, "table")) {
            spazio_blocco(0);
            impagina_tabella(v, &mio);
            g_link_ora = era_link;
            spazio_blocco(2);
            return;
        }

        if (uguale(nome, "br")) { g_pen_x = riga_x() + 1; a_capo(); return; }

        /* ! <hr> E' UNA RIGA, non uno stacco piu' grande: finora era solo un
         * blocco vuoto, cioe' un po' d'aria in mezzo alla pagina — e chi
         * scrive <hr> vuole vedere il segno che separa. Si disegna come uno
         * sfondo alto due pixel, perche' e' esattamente cio' che e'. */
        if (uguale(nome, "hr")) {
            spazio_blocco(0);
            if (g_sfondi_n < SFONDI_MAX) {
                g_sfondi[g_sfondi_n].x = riga_x();
                g_sfondi[g_sfondi_n].y = g_pen_y + 2;
                g_sfondi[g_sfondi_n].w = riga_w();
                g_sfondi[g_sfondi_n].h = 2;
                g_sfondi[g_sfondi_n].colore = EX_OMBRA;
                g_sfondi_n++;
            }
            g_pen_y += 6;
            g_riga_h = alt_riga_f(font_di(&mio));
            g_riga_primo = g_pez_n;
            spazio_blocco(2);
            return;
        }

        if (uguale(nome, "img")) {
            const char *src = html_attr(&g_doc, v, "src");
            const char *alt;
            int         k = (src && src[0]) ? imm_indice(v, src) : -1;

            if (k >= 0 && g_imm[k].px) { pezzo_immagine(k); return; }

            /* ! FINCHE' L'IMMAGINE NON C'E' SI LEGGE IL SUO `alt`, ed e'
             * esattamente il motivo per cui quell'attributo esiste. Il valore
             * sta gia' nell'arena del documento, quindi si impagina con le
             * stesse parole di tutto il resto. */
            alt = html_attr(&g_doc, v, "alt");
            if (alt && alt[0])
                parole(alt, (unsigned int)(alt - g_doc.arena));
            return;
        }

        /* L'elenco dei blocchi resta la regola di base; `display` la
         * sovrascrive nei due versi, che e' a cosa serve. */
        e_blocco = blocco(nome);
        if (mio.display == CSS_DISPLAY_BLOCCO) e_blocco = 1;
        if (mio.display == CSS_DISPLAY_INLINE) e_blocco = 0;

        if (e_blocco) {
            spazio_blocco(0);

            /* ! I RIENTRI SI SOMMANO A QUELLI DI FUORI: un blocco dentro un
             * altro rientra due volte, ed e' cosi' che si vedono le citazioni
             * annidate. Valgono solo sui blocchi — un margine su un pezzo di
             * testo in linea non ha un lato a cui attaccarsi. */
            if (mio.margine[3] != CSS_MISURA_NO) g_marg_sx += mio.margine[3];
            if (mio.margine[1] != CSS_MISURA_NO) g_marg_dx += mio.margine[1];
            if (g_marg_sx < 0) g_marg_sx = 0;
            if (g_marg_dx < 0) g_marg_dx = 0;

            g_pen_x = riga_x();
            g_riga_primo = g_pez_n;

            if (mio.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                sfondo_mio = g_sfondi_n++;
                g_sfondi[sfondo_mio].x = riga_x();
                g_sfondi[sfondo_mio].y = g_pen_y;
                g_sfondi[sfondo_mio].w = riga_w();
                g_sfondi[sfondo_mio].h = 0;
                g_sfondi[sfondo_mio].colore = mio.sfondo;
            }
        }

        /* ! IL SEGNO DI UNA VOCE DIPENDE DALLA LISTA CHE LA CONTIENE, e la
         * lista si trova risalendo: <li> non sa da solo se e' puntato o
         * numerato. Per <ol> serve anche la POSIZIONE, cioe' quanti <li> lo
         * precedono fra i fratelli — e si contano li', non con un contatore
         * globale, o due liste annidate si darebbero i numeri a vicenda. */
        if (uguale(nome, "li")) {
            int su = g_doc.nodi[v].padre;
            int numerata = 0;

            while (su >= 0) {
                const char *n = html_nome(&g_doc, su);

                if (uguale(n, "ol")) { numerata = 1; break; }
                if (uguale(n, "ul")) break;
                su = g_doc.nodi[su].padre;
            }

            if (numerata) {
                int  quanti = 1, f2;
                char seg[16];
                int  q = 0, cifre[8], nc = 0;

                for (f2 = g_doc.nodi[su].primo_figlio; f2 >= 0 && f2 != v;
                     f2 = g_doc.nodi[f2].prossimo)
                    if (g_doc.nodi[f2].tipo == HTML_ELEMENTO &&
                        uguale(html_nome(&g_doc, f2), "li")) quanti++;

                while (quanti > 0) { cifre[nc++] = quanti % 10; quanti /= 10; }
                while (nc > 0) seg[q++] = (char)('0' + cifre[--nc]);
                seg[q++] = '.';
                seg[q] = '\0';
                parola(seg, genera(seg), q);
            } else {
                /* Un pallino, non un asterisco: e' il segno che ci si aspetta,
                 * e il carattere c'e' in tutte le facce Liberation. */
                parola("-", genera("-"), 1);
            }
            g_pen_x += ex_larghezza_testo(font_di(&mio), " ");
        }

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

        if (uguale(nome, "pre") || uguale(nome, "code") ||
            uguale(nome, "tt")  || uguale(nome, "kbd") ||
            uguale(nome, "samp")) g_fisso++;

        for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
            impagina_nodo(f, &mio);
            g_stile_ora = mio;      /* i figli l'hanno cambiato: si rimette */
        }

        if (uguale(nome, "pre") || uguale(nome, "code") ||
            uguale(nome, "tt")  || uguale(nome, "kbd") ||
            uguale(nome, "samp")) g_fisso--;

        g_link_ora = era_link;

        if (e_blocco) {
            a_capo();
            if (sfondo_mio >= 0) {
                int fine = g_pen_y + ((g_pen_x > riga_x()) ? g_riga_h : 0);

                g_sfondi[sfondo_mio].h = fine - g_sfondi[sfondo_mio].y;
                if (g_sfondi[sfondo_mio].h < 1) g_sfondi[sfondo_mio].h = 1;
            }
            g_marg_sx = era_sx;
            g_marg_dx = era_dx;
            g_pen_x = riga_x();
            g_riga_primo = g_pez_n;
            spazio_blocco(2);
        }
    }
}

static void impagina(void)
{
    g_pez_n = 0;
    g_link_n = 0;
    g_fisso = 0;
    g_doc.arena_n = g_arena_doc;    /* si butta il testo generato dal giro prima */
    g_marg_sx = g_marg_dx = 0;
    g_riga_primo = 0;
    g_sfondi_n = 0;
    g_pen_x = area_x();
    g_pen_y = area_y();
    g_link_ora = -1;
    css_stile_vuoto(&g_stile_ora);
    g_riga_h = alt_riga_f(g_font_testo);

    {
        CssStile radice;

        css_stile_vuoto(&radice);
        impagina_nodo(g_doc.radice, &radice);
    }
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

    /* ! GLI SFONDI PRIMA DI TUTTO IL RESTO, e ritagliati a mano all'area come
     * le immagini: ex_riempi ritaglia alla FINESTRA, non al documento. */
    for (i = 0; i < g_sfondi_n; i++) {
        int y = g_sfondi[i].y - g_scorri;
        int h = g_sfondi[i].h;

        if (y + h < area_y() || y > area_y() + area_h()) continue;
        if (y < area_y()) { h -= area_y() - y; y = area_y(); }
        if (y + h > area_y() + area_h()) h = area_y() + area_h() - y;
        if (h > 0)
            ex_riempi(g_f, g_sfondi[i].x, y, g_sfondi[i].w, h,
                      g_sfondi[i].colore);
    }

    for (i = 0; i < g_pez_n; i++) {
        int y  = g_pez[i].y - g_scorri;
        int ph = g_pez[i].img >= 0 ? g_pez[i].h : 24;

        /* ! SI DISEGNA SOLO CIO' CHE SI VEDE. Con una pagina di migliaia di
         * righe, dipingere tutto vorrebbe dire pagare l'intero documento a
         * ogni riga di scorrimento — e per il novantanove per cento fuori
         * dalla finestra. */
        if (y + ph < area_y() || y > area_y() + area_h()) continue;

        /* ! UN'IMMAGINE SI RITAGLIA A MANO, e non e' pignoleria: ex_pixmap
         * ritaglia alla FINESTRA, non all'area del documento, quindi
         * un'immagine alta trecento pixel scorsa in su dipingerebbe sopra la
         * casella dell'indirizzo. Il testo se la cava perche' e' alto venti
         * punti e sborda di poco; un'immagine no. */
        if (g_pez[i].img >= 0) {
            const Imm *im    = &g_imm[g_pez[i].img];
            int        cima  = y;
            int        salta = 0;
            int        alta  = (int)im->h;

            if (!im->px) continue;

            if (cima < area_y()) {
                salta = area_y() - cima;
                cima  = area_y();
                alta -= salta;
            }
            if (cima + alta > area_y() + area_h())
                alta = area_y() + area_h() - cima;

            if (alta > 0)
                ex_pixmap(g_f, g_pez[i].x, cima, (int)im->w, alta,
                          im->px + (unsigned int)salta * im->w, im->w);
            continue;
        }

        {
            ExFont       f = g_pez[i].font;
            const char  *t = g_arena + g_pez[i].testo;
            unsigned int c = g_pez[i].colore;

            /* Il testo nell'arena e' una parola sola perche' l'impaginazione
             * l'ha spezzato: si disegna fino allo spazio. */
            {
                static char cop[256];
                int k = 0;

                /* ! CI SI FERMA A QUALUNQUE BIANCO, non al solo spazio.
                 * L'impaginazione ha gia' spezzato il testo in parole e il
                 * pezzo punta all'inizio di una: quello che segue nell'arena
                 * appartiene alla parola dopo. Fermarsi al solo ' ' bastava
                 * finche' gli a capo non arrivavano fin qui — dentro <pre>
                 * arrivano, e venivano DISEGNATI, cioe' un rettangolino in
                 * coda a ogni riga. */
                while (t[k] && t[k] != ' ' && t[k] != '\n' &&
                       t[k] != '\r' && t[k] != '\t' &&
                       k < (int)sizeof(cop) - 1) {
                    cop[k] = t[k]; k++;
                }
                cop[k] = '\0';
                ex_scrivi_con(g_f, f, g_pez[i].x, y, cop, c);

                /* ! UN COLLEGAMENTO SI SOTTOLINEA, e non basta il colore: su
                 * uno schermo a pochi colori il blu e il nero si distinguono
                 * male, e chi non li distingue non trova i collegamenti. */
                if (g_pez[i].link >= 0)
                    ex_riempi(g_f, g_pez[i].x, y + ex_font_altezza(f) - 2,
                              g_pez[i].w, 1, c);
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
 * Gli indirizzi relativi
 *
 * ! I RIFERIMENTI RELATIVI SI RISOLVONO, e sono la maggioranza: «/x» o
 * «pagina.html» senza schema. La stessa regola sta in exhttp per le
 * redirezioni; qui la si applica a quello che scrive la pagina — i
 * collegamenti e le immagini, che sono la stessa cosa vista da due parti.
 * Averla in una funzione sola vuol dire che il giorno che sbaglia, sbaglia in
 * un posto solo.
 * --------------------------------------------------------------------------- */
static int risolvi(const char *rif, char *out, unsigned int max)
{
    HttpUrl u;

    if (!rif || !rif[0] || max < 2) return 0;

    if (rif[0] == 'h' && rif[1] == 't') {
        strncpy(out, rif, max - 1);
        out[max - 1] = '\0';
        return 1;
    }

    /* ! UNO SCHEMA CHE NON E' http NON SI SEGUE, e va riconosciuto PRIMA di
     * trattarlo da percorso relativo: «data:image/png;base64,...» attaccato in
     * coda all'indirizzo di adesso produrrebbe una richiesta lunga un
     * chilometro verso il sito sbagliato. I due punti prima di qualunque «/»
     * sono uno schema. */
    {
        const char *c = rif;

        while (*c && *c != ':' && *c != '/' && *c != '?' && *c != '#') c++;
        if (*c == ':') return 0;
    }

    if (!http_url(g_qui, &u)) return 0;

    /* ! «//host/x» E' UN INDIRIZZO SENZA SCHEMA, non un percorso: vuol dire
     * «lo stesso schema della pagina». Le immagini dei siti veri sono scritte
     * cosi' molto piu' spesso dei collegamenti. */
    if (rif[0] == '/' && rif[1] == '/') {
        strcpy(out, u.cifrato ? "https:" : "http:");
        strncat(out, rif, max - strlen(out) - 1);
        return 1;
    }

    strcpy(out, u.cifrato ? "https://" : "http://");
    strncat(out, u.host, max - strlen(out) - 1);

    if ((u.cifrato && u.porta != 443) || (!u.cifrato && u.porta != 80)) {
        char cifre[8], rov[8];
        unsigned int p = u.porta;
        int a = 0, b = 0;

        while (p) { rov[b++] = (char)('0' + (p % 10)); p /= 10; }
        while (b) cifre[a++] = rov[--b];
        cifre[a] = '\0';
        strncat(out, ":", max - strlen(out) - 1);
        strncat(out, cifre, max - strlen(out) - 1);
    }

    if (rif[0] == '/') {
        strncat(out, rif, max - strlen(out) - 1);
    } else {
        char base[HTTP_PERCORSO_MAX];
        int  i;

        strncpy(base, u.percorso, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        i = (int)strlen(base);
        while (i > 0 && base[i - 1] != '/') i--;
        base[i] = '\0';
        strncat(out, base, max - strlen(out) - 1);
        strncat(out, rif, max - strlen(out) - 1);
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * La cache su disco
 *
 * ! IL POSTO NON SI SCRIVE NEL CODICE, SI RICAVA DA `HOME`. La casa e' /root
 * solo per root e /home/<utente> per tutti gli altri — la regola sta in
 * bin/login/login.c — e un percorso costante nel sorgente funzionerebbe per
 * una persona sola. Da li' in giu' la convenzione e' $HOME/.app/<programma>/,
 * cioe' per noi $HOME/.app/browser/cache.
 *
 * ! E SE NON SI PUO' SCRIVERE NON SI MUORE. Avviando da CD la radice e' in
 * sola lettura e la directory non si crea: il browser lavora in memoria
 * esattamente come prima, e lo dice una volta sola invece di riprovarci a
 * ogni immagine.
 *
 * ! IL NOME DEL FILE E' L'IMPRONTA DELL'INDIRIZZO, MA A DECIDERE E'
 * L'INDIRIZZO SCRITTO DENTRO. Un'impronta a 32 bit ogni tanto collide, e una
 * collisione servirebbe l'immagine SBAGLIATA — che e' un difetto silenzioso,
 * il peggiore che una cache possa avere. Con l'indirizzo nella testa del file
 * una collisione diventa semplicemente un buco: si riscarica e si riscrive.
 *
 * ! ED E' UNA DIRECTORY TEMPORANEA, quindi si svuota all'avvio. Il guadagno
 * che conta e' dentro la sessione — la stessa <img> ripetuta, e «indietro» che
 * non ripassa dalla rete — e una cache che sopravvive ai riavvii vorrebbe una
 * politica di scadenza che qui non c'e'.
 * --------------------------------------------------------------------------- */
#define CACHE_MAX_BYTE  (4u * 1024u * 1024u)   /* quanto si scrive per sessione */
#define CACHE_PERC_MAX  192
#define CACHE_PULIZIA   128                    /* nomi per giro di svuotamento */

typedef struct {
    char         magia[12];
    unsigned int byte;
    char         url[EXHTTP_URL_MAX];
} CacheTesta;

static char         g_cache[CACHE_PERC_MAX] = "";
static unsigned int g_cache_scritti = 0;

/* Il percorso del file di una risorsa: otto cifre esadecimali piu' «.dat»,
 * che sta anche in un nome 8.3 se la casa dell'utente e' su FAT. */
static void cache_nome(const char *url, char *out, unsigned int max)
{
    static const char cifre[] = "0123456789abcdef";
    unsigned int      h = 2166136261u;      /* FNV-1a */
    unsigned int      i;
    char              nome[16];

    while (*url) { h ^= (unsigned char)*url++; h *= 16777619u; }

    for (i = 0; i < 8; i++) nome[i] = cifre[(h >> ((7 - i) * 4)) & 0xFu];
    nome[8] = '.'; nome[9] = 'd'; nome[10] = 'a'; nome[11] = 't';
    nome[12] = '\0';

    strncpy(out, g_cache, max - 1);
    out[max - 1] = '\0';
    strncat(out, "/", max - strlen(out) - 1);
    strncat(out, nome, max - strlen(out) - 1);
}

/* ! SI CANCELLA SOLO QUELLO CHE ABBIAMO SCRITTO NOI, e il riconoscimento e' il
 * nome: otto cifre esadecimali e «.dat». Svuotare una directory cancellando
 * tutto quello che ci si trova dentro e' come si perdono i file di qualcun
 * altro il giorno che il percorso e' sbagliato di un livello. */
static int cache_nostro(const char *n)
{
    int i;

    for (i = 0; i < 8; i++)
        if (!((n[i] >= '0' && n[i] <= '9') || (n[i] >= 'a' && n[i] <= 'f')))
            return 0;

    return n[8] == '.' && n[9] == 'd' && n[10] == 'a' && n[11] == 't' &&
           n[12] == '\0';
}

static void cache_svuota(void)
{
    static char nomi[CACHE_PULIZIA][16];
    int         giri;

    if (!g_cache[0]) return;

    /* ! I NOMI SI RACCOLGONO PRIMA E SI CANCELLANO POI. Cancellare mentre si
     * scorre una directory vuol dire cambiare sotto i piedi la cosa che si sta
     * scorrendo, e quanto sia grave dipende dal filesystem — cioe' e' un
     * difetto che si presenta su un disco e non sull'altro. */
    for (giri = 0; giri < 32; giri++) {
        DIR           *d = opendir(g_cache);
        struct dirent *e;
        int            n = 0, i;

        if (!d) return;
        while (n < CACHE_PULIZIA && (e = readdir(d)) != 0) {
            if (!cache_nostro(e->d_name)) continue;
            strncpy(nomi[n], e->d_name, sizeof(nomi[0]) - 1);
            nomi[n][sizeof(nomi[0]) - 1] = '\0';
            n++;
        }
        closedir(d);

        for (i = 0; i < n; i++) {
            char p[CACHE_PERC_MAX + 24];

            strncpy(p, g_cache, sizeof(p) - 1);
            p[sizeof(p) - 1] = '\0';
            strncat(p, "/", sizeof(p) - strlen(p) - 1);
            strncat(p, nomi[i], sizeof(p) - strlen(p) - 1);
            unlink(p);
        }

        if (n < CACHE_PULIZIA) return;      /* la directory e' finita */
    }
}

/* Crea $HOME/.app/browser/cache. Si chiama una volta, all'avvio. */
static void cache_prepara(void)
{
    const char *casa = getenv("HOME");
    char        p[CACHE_PERC_MAX];
    int         i;

    static const char *const passi[] = { "/.app", "/browser", "/cache" };

    g_cache[0] = '\0';

    if (!casa || !casa[0]) {
        printf("browser: HOME non c'e', niente cache su disco.\n");
        return;
    }
    if (strlen(casa) + 24 >= sizeof(p)) {
        printf("browser: HOME troppo lungo, niente cache su disco.\n");
        return;
    }

    strcpy(p, casa);

    /* ! LE BARRE FINALI SI TOLGONO TUTTE, COMPRESA QUELLA DELLA RADICE, o
     * «/» piu' «/.app» diventa «//.app». Su POSIX le due barre portano allo
     * stesso posto, ma il percorso finisce stampato nei messaggi e scritto
     * nella variabile: uno che si legge male e' uno che si cerca male. */
    i = (int)strlen(p);
    while (i > 0 && p[i - 1] == '/') p[--i] = '\0';

    /* ! mkdir NON FA LA CATENA, e EEXIST non e' un errore: e' il caso normale
     * dalla seconda volta in poi. */
    for (i = 0; i < 3; i++) {
        strncat(p, passi[i], sizeof(p) - strlen(p) - 1);
        if (mkdir(p, i == 2 ? 0700 : 0755) != 0 && errno != EEXIST) {
            printf("browser: niente cache in %s (%s), lavoro in memoria.\n",
                   p, strerror(errno));
            return;
        }
    }

    strncpy(g_cache, p, sizeof(g_cache) - 1);
    g_cache[sizeof(g_cache) - 1] = '\0';

    cache_svuota();
    printf("browser: cache in %s\n", g_cache);
}

/* Rende 1 e riempie `buf` se la risorsa c'e' ed e' proprio la sua. */
static int cache_leggi(const char *url, unsigned char *buf, unsigned int max,
                       unsigned int *quanti)
{
    CacheTesta t;
    char       p[CACHE_PERC_MAX + 24];
    int        fd, n;

    if (!g_cache[0]) return 0;

    cache_nome(url, p, sizeof(p));
    fd = open(p, O_RDONLY);
    if (fd < 0) return 0;

    n = (int)read(fd, &t, sizeof(t));
    if (n != (int)sizeof(t)) { close(fd); return 0; }

    t.magia[sizeof(t.magia) - 1] = '\0';
    t.url[sizeof(t.url) - 1]     = '\0';

    if (!uguale(t.magia, "EXCACHE1") || t.byte == 0 || t.byte > max ||
        !uguale(t.url, url)) { close(fd); return 0; }

    n = (int)read(fd, buf, t.byte);
    close(fd);
    if (n != (int)t.byte) return 0;

    *quanti = t.byte;
    return 1;
}

static void cache_scrivi(const char *url, const unsigned char *dati,
                         unsigned int n)
{
    CacheTesta t;
    char       p[CACHE_PERC_MAX + 24];
    int        fd, bene;

    if (!g_cache[0] || n == 0) return;

    /* ! QUANDO IL TETTO E' PIENO SI SMETTE DI SCRIVERE MA SI CONTINUA A
     * LEGGERE. Una cache che si svuota da sola a meta' navigazione sarebbe
     * peggio di nessuna cache: ogni pagina ricomincerebbe da zero senza che si
     * capisca perche'. */
    if (g_cache_scritti + n > CACHE_MAX_BYTE) return;

    cache_nome(url, p, sizeof(p));
    fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd < 0) return;

    memset(&t, 0, sizeof(t));
    strcpy(t.magia, "EXCACHE1");
    t.byte = n;
    strncpy(t.url, url, sizeof(t.url) - 1);

    bene = (write(fd, &t, sizeof(t)) == (ssize_t)sizeof(t) &&
            write(fd, dati, n) == (ssize_t)n);
    close(fd);

    /* Una voce scritta a meta' e' peggio di una voce assente: si toglie. */
    if (bene) g_cache_scritti += n;
    else      unlink(p);
}

/* -----------------------------------------------------------------------------
 * Prendere le immagini
 * --------------------------------------------------------------------------- */

/* Una sola immagine: la scarica, la decodifica, se la copia. Rende 1 se ce
 * l'ha fatta — e su 0 non ha lasciato niente in giro. */
static int imm_prendi(int k)
{
    Imm         *im = &g_imm[k];
    ExHttpEsito  e;
    EximgBitmap  bm;
    char         url[EXHTTP_URL_MAX];
    unsigned int n = 0;
    unsigned int w, h;

    if (!eximg_pronta()) return 0;
    if (!risolvi(im->src, url, sizeof(url))) return 0;

    /* ! LA CACHE SI GUARDA PRIMA DELLA RETE, e si ripaga gia' dentro una
     * pagina sola: due <img> con lo stesso `src` erano due richieste, e nella
     * prova lo si vedeva nel log del server. */
    if (!cache_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n)) {
        if (!exhttp_prendi(url, g_imm_buf, sizeof(g_imm_buf), &e)) return 0;
        if (e.codice != 200 || e.byte == 0) return 0;

        /* ! UN FILE TRONCATO NON SI PROVA A DECODIFICARE: un PNG a meta' non
         * e' un PNG piu' piccolo, e' un file rotto — e il decodificatore lo
         * scoprirebbe dopo aver allocato. E non si mette in cache, o il
         * troncamento diventerebbe permanente. */
        if (e.troncata) return 0;

        n = e.byte;
        cache_scrivi(url, g_imm_buf, n);
    }

    if (!g_img_carica(g_imm_buf, n, &bm)) return 0;

    misura(im, bm.larghezza, bm.altezza, &w, &h);

    /* ! IL TETTO SI CONTROLLA PRIMA DI COPIARE, e vale sulla SOMMA: dodici
     * immagini che stanno ciascuna nell'area sono lo stesso dodici volte
     * l'area. */
    if (w == 0 || h == 0 || g_imm_px + w * h > IMM_PX_TOT) {
        g_img_libera(&bm);
        return 0;
    }

    im->px = ridimensiona(&bm, w, h);
    g_img_libera(&bm);
    if (!im->px) return 0;

    im->w = w;
    im->h = h;
    g_imm_px += w * h;
    return 1;
}

/* Tutte quelle che l'impaginazione ha trovato, una per volta. */
static void immagini_prendi(void)
{
    char msg[160];
    int  k;

    for (k = 0; k < g_imm_n; k++) {
        if (g_imm[k].stato != 0) continue;

        sprintf(msg, "immagine %d di %d...", k + 1, g_imm_n);
        dico(msg);

        if (!imm_prendi(k)) {
            /* ! QUELLA CHE NON ARRIVA SI SALTA E BASTA: al suo posto resta il
             * suo `alt`, e la pagina va avanti. Un browser che si ferma sulla
             * prima immagine irraggiungibile non mostra mai niente. */
            g_imm[k].stato = 2;
            continue;
        }

        g_imm[k].stato = 1;

        /* ! SI REIMPAGINA A OGNI IMMAGINE, e il testo si sposta sotto gli
         * occhi: e' il prezzo di mostrare le parole prima dei pixel, e si paga
         * volentieri. */
        impagina();
        disegna();
    }
}

/* -----------------------------------------------------------------------------
 * Raccogliere i fogli di stile
 *
 * ! L'ORDINE E' QUELLO DEL DOCUMENTO, ED E' META' DELLA CASCATA: a parita' di
 * peso vince l'ultima regola letta, quindi leggerle nell'ordine sbagliato
 * cambia il risultato. Gli indici dei nodi sono gia' in ordine di documento —
 * html.c li assegna mentre analizza — quindi basta un giro dritto sul vettore,
 * senza ricorsione.
 *
 * ! E I FOGLI ESTERNI SI ASPETTANO, al contrario delle immagini. Un'immagine
 * che arriva dopo sposta il testo e si vede arrivare; un foglio di stile che
 * arrivasse dopo cambierebbe TUTTA la pagina sotto gli occhi — colori, corpi,
 * cose che spariscono. Meglio aspettare quei pochi decimi, con un tetto
 * dichiarato di quanti seguirne.
 * --------------------------------------------------------------------------- */
static void raccogli_css(void)
{
    int i, presi = 0;

    css_prepara(&g_css, g_css_reg, CSS_REGOLE_MAX, g_css_dich, CSS_DICH_MAX,
                g_css_arena, CSS_ARENA_MAX);
    css_analizza(&g_css, CSS_DI_SISTEMA, sizeof(CSS_DI_SISTEMA) - 1,
                 CSS_ORIGINE_SISTEMA);

    for (i = 0; i < (int)g_doc.nodi_n; i++) {
        const char *nome;

        if (g_doc.nodi[i].tipo != HTML_ELEMENTO) continue;
        nome = html_nome(&g_doc, i);

        if (uguale(nome, "style")) {
            int f;

            for (f = g_doc.nodi[i].primo_figlio; f >= 0;
                 f = g_doc.nodi[f].prossimo) {
                const char  *t;
                unsigned int n = 0;

                if (g_doc.nodi[f].tipo != HTML_TESTO) continue;
                t = html_testo(&g_doc, f);
                while (t[n]) n++;
                css_analizza(&g_css, t, n, CSS_ORIGINE_FOGLIO);
            }
            continue;
        }

        if (uguale(nome, "link") && presi < CSS_FOGLI_MAX) {
            const char  *rel  = html_attr(&g_doc, i, "rel");
            const char  *href = html_attr(&g_doc, i, "href");
            char         url[EXHTTP_URL_MAX];
            unsigned int n = 0;
            int          e_foglio = 0, a;

            if (!rel || !href || !href[0]) continue;

            /* «stylesheet» puo' stare in mezzo ad altre parole e in qualunque
             * cassa: si cerca dentro invece di confrontare tutto. */
            for (a = 0; rel[a] && rel[a+1] && rel[a+2] && rel[a+3] &&
                        rel[a+4] && rel[a+5]; a++) {
                if ((rel[a]   | 32) == 's' && (rel[a+1] | 32) == 't' &&
                    (rel[a+2] | 32) == 'y' && (rel[a+3] | 32) == 'l' &&
                    (rel[a+4] | 32) == 'e' && (rel[a+5] | 32) == 's') {
                    e_foglio = 1; break;
                }
            }
            if (!e_foglio) continue;
            if (!risolvi(href, url, sizeof(url))) continue;

            /* ! SI RIUSA IL BUFFER DELLE IMMAGINI, e si puo': i fogli si
             * prendono PRIMA della prima impaginazione, le immagini dopo, e
             * fra le due cose non c'e' sovrapposizione. Un buffer in piu' da
             * centoventotto chilobyte non si paga per niente. */
            if (!cache_leggi(url, g_imm_buf, sizeof(g_imm_buf), &n)) {
                ExHttpEsito e;

                dico("foglio di stile...");
                if (!exhttp_prendi(url, g_imm_buf, sizeof(g_imm_buf), &e))
                    continue;
                if (e.codice != 200 || e.byte == 0) continue;
                n = e.byte;
                if (!e.troncata) cache_scrivi(url, g_imm_buf, n);
            }

            css_analizza(&g_css, (const char *)g_imm_buf, n,
                         CSS_ORIGINE_FOGLIO);
            presi++;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Andare
 * --------------------------------------------------------------------------- */
/* ! «INDIETRO» SI SERVE DALLA CACHE, TUTTO IL RESTO VA IN RETE, ed e' la
 * distinzione che rende una cache di pagine accettabile: tornare indietro deve
 * mostrare la pagina che si e' vista, mentre battere un indirizzo o premere un
 * collegamento e' una richiesta nuova e vuole la pagina di adesso. Una cache
 * che risponde anche a quelle mostrerebbe notizie vecchie senza dirlo. */
static void vai(const char *url, int in_storia, int usa_cache)
{
    ExHttpEsito  e;
    char         msg[160];
    unsigned int n = 0;
    int          da_cache = 0;

    if (!url || !url[0]) return;

    dico("sto scaricando...");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);

    memset(&e, 0, sizeof(e));

    if (usa_cache && cache_leggi(url, g_pagina, sizeof(g_pagina), &n)) {
        e.codice = 200;
        e.byte   = n;
        strncpy(e.finale, url, sizeof(e.finale) - 1);
        da_cache = 1;
    } else if (!exhttp_prendi(url, g_pagina, sizeof(g_pagina), &e)) {
        sprintf(msg, "%s: %s", url, e.errore[0] ? e.errore : "non riuscito");
        dico(msg);
        return;
    } else if (!e.troncata) {
        /* ! LA CHIAVE E' `finale`, NON L'INDIRIZZO CHIESTO, perche' e' li' che
         * il contenuto sta davvero: dopo una redirezione i due sono diversi, e
         * `indietro` cerchera' proprio `finale` — e' quello che finisce nella
         * storia. */
        cache_scrivi(e.finale, g_pagina, e.byte);
    }

    if (in_storia && g_storia_n < STORIA_MAX && g_qui[0]) {
        strncpy(g_storia[g_storia_n], g_qui, EXHTTP_URL_MAX - 1);
        g_storia[g_storia_n][EXHTTP_URL_MAX - 1] = '\0';
        g_storia_n++;
    }

    strncpy(g_qui, e.finale, sizeof(g_qui) - 1);
    g_qui[sizeof(g_qui) - 1] = '\0';
    ex_testo_metti(g_url, g_qui);

    /* ! LE IMMAGINI DELLA PAGINA DI PRIMA SE NE VANNO QUI, prima che l'albero
     * cambi: dopo html_analizza gli indici dei nodi sono di un altro documento
     * e non vogliono piu' dire niente. */
    imm_libera_tutte();

    html_prepara(&g_doc, g_nodi, NODI_MAX, g_attr, ATTR_MAX,
                 g_arena, ARENA_MAX);
    html_analizza(&g_doc, (const char *)g_pagina, e.byte);
    g_arena_doc = g_doc.arena_n;    /* da qui in poi c'e' il testo generato */

    g_scorri = 0;
    raccogli_css();
    impagina();

    sprintf(msg, "%d, %u byte, %u nodi%s%s%s%s", e.codice, e.byte, g_doc.nodi_n,
            da_cache ? " (dalla cache)" : "",
            e.troncata ? " (pagina troncata)" : "",
            g_doc.troncato ? " (albero troncato)" : "",
            g_css.troncato ? " (stile troncato)" : "");
    dico(msg);

    disegna();

    /* Il testo si vede: adesso, e solo adesso, si va a prendere il resto. */
    immagini_prendi();
    dico(msg);
}

/* Un collegamento premuto: si risolve contro l'indirizzo di adesso. */
static void segui(int k)
{
    char nuovo[EXHTTP_URL_MAX];

    if (k < 0 || k >= g_link_n) return;
    if (!risolvi(g_link[k], nuovo, sizeof(nuovo))) return;

    vai(nuovo, 1, 0);
}

/* Quale collegamento sta sotto quel punto, o -1. */
static int link_sotto(int x, int y)
{
    int i;

    for (i = 0; i < g_pez_n; i++) {
        int py = g_pez[i].y - g_scorri;
        int h  = g_pez[i].img >= 0 ? g_pez[i].h
                                   : ex_font_altezza(g_pez[i].font);

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

            if (t && t[0]) vai(t, 1, 0);
            return 0;
        }
        if (wp == ID_INFO) {
            char t[640];

            exinfo_testo(t, sizeof(t), "Navigatore",
                         "Il browser di EX-OS.  Mette insieme exhttp per la "
                         "rete, exhtml per l'albero, excss per i fogli di "
                         "stile, eximg per le immagini e i font per misurare "
                         "e disegnare il testo.  Niente JavaScript, niente "
                         "https.");
            ex_dlg_avviso("Informazioni su", t);
            return 0;
        }
        if (wp == ID_INDIETRO) {
            if (g_storia_n > 0) {
                char indietro[EXHTTP_URL_MAX];

                g_storia_n--;
                strncpy(indietro, g_storia[g_storia_n], sizeof(indietro) - 1);
                indietro[sizeof(indietro) - 1] = '\0';
                vai(indietro, 0, 1);
            }
            return 0;
        }
        return 0;

    case EXM_TASTO: {
        unsigned int c = wp & 0xFFFF;

        if (c == '\n' || c == '\r') {
            const char *t = ex_testo_prendi(g_url);

            if (t && t[0]) vai(t, 1, 0);
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

    /* ! I DUE PULSANTI DI DESTRA SI MISURANO DALLA DESTRA, non dalla
     * sinistra: cosi' aggiungerne uno sposta solo il campo dell'indirizzo, che
     * e' l'unico pezzo che puo' restringersi senza diventare inutile. */
    ex_crea("pulsante", "?", EX_FIGLIO, FIN_W - MARGINE - 24, 4, 24, 22,
            g_f, ID_INFO, 0);
    ex_crea("pulsante", "Vai", EX_FIGLIO, FIN_W - MARGINE - 24 - 4 - 44, 4,
            44, 22, g_f, ID_VAI, 0);
    g_url = ex_crea("testo", "", EX_FIGLIO, MARGINE + 32, 4,
                    FIN_W - MARGINE - 24 - 4 - 44 - 4 - (MARGINE + 32), 22,
                    g_f, ID_URL, 0);

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      MARGINE, FIN_H - 18, FIN_W - 2 * MARGINE, 16, g_f, 0, 0);

    cache_prepara();

    ex_fuoco(g_url);
    dico("scrivi un indirizzo e premi Invio. https non ancora: manca il TLS.");
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    disegna();

    if (argc >= 2) { ex_testo_metti(g_url, argv[1]); vai(argv[1], 0, 0); }

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
