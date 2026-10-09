/* =============================================================================
 * exwin/bin/browser/browser_impagina.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'IMPAGINATO — da un albero HTML a dei pezzi che sanno dove stanno
 *
 * ! E' USCITO DA browser.c SENZA CAMBIARE UNA RIGA DI COMPORTAMENTO, ed e'
 * l'unico modo onesto di cominciare: prima si spezza il FILE, e la prova e'
 * che il navigatore disegni le stesse pagine di prima, pixel per pixel; solo
 * dopo, avendo i legami sotto gli occhi (browser_priv.h), si decide quali si
 * possono tagliare. Il contrario — cominciare dalla libreria — vuol dire
 * scoprirli uno per volta col programma a meta' del guado.
 *
 * ! E I MODULI, LE IMMAGINI E GLI SCRIPT NON CI SONO PIU', dall'8 settembre
 * 2026. Erano i tre legami che browser_priv.h nominava, e non si sono tagliati
 * spostando del codice: si sono tagliati decidendo che l'impaginato CHIEDE
 * «quanto e' largo questo pezzo estraneo» a chi glielo sa dire. Le due domande
 * stanno in browser_vista.h, chi risponde in browser_estranei.c.
 *
 * ! QUI DENTRO NON SI NOMINA PIU' NESSUN TAG CHE NON SIA IMPAGINAZIONE. Restano
 * <table>, <br>, <hr>, <a>, <li>, <pre> — cioe' come si dispone il testo — e
 * sono spariti <input>, <button>, <select>, <textarea>, <img> e <noscript>.
 * E' il metro del taglio: non quante righe se ne sono andate, ma quali parole
 * non si possono piu' scrivere qui.
 *
 * ! DI <form> RESTA IL NOME NELL'ELENCO DEI BLOCCHI, e ci resta a ragione: un
 * modulo va a capo come un <div>, e questo e' impaginare. Quel che se n'e'
 * andato e' il MODULO — l'azione, il metodo, i campi che gli appartengono —
 * che non e' una questione di righe e di margini.
 *
 * ! QUEL CHE MANCA ANCORA A UNA lib/exvista sta in browser_priv.h, ed e' la
 * lista delle variabili condivise: g_doc, g_css, g_pez, i font, la finestra.
 * Quelle sono un'altra giornata — ma sono PARAMETRI, non decisioni: nessuna di
 * loro obbliga l'impaginato a sapere che cosa sia una casella di testo.
 * ============================================================================= */
#include "browser_priv.h"
#include "browser_vista.h"

/* I globali che servono SOLO a impaginare: sono usciti da browser.c insieme
 * al codice che li usa, ed e' l'unica parte del taglio che riduce davvero il
 * numero dei legami invece di limitarsi a scriverli. */
static unsigned int  g_link_usati = 0;
static int  g_pen_x, g_pen_y, g_riga_h;
static int  g_link_ora;
static int  g_nodo_ora = -1;
static CssStile g_stile_ora;
static int g_riga_primo;            /* primo pezzo della riga in corso */
static int g_misura = 0;
static int g_fisso = 0;

static void suggerimenti(int v, CssStile *st);   /* piu' avanti, qui sotto */

/* =============================================================================
 * I FLOAT (28 settembre 2026, @EXBROWSER-HTML5)
 *
 * ! UN FLOAT E' UN RETTANGOLO, NON UN MARGINE. La prima idea — sommare la sua
 * larghezza a g_marg_sx finche' la penna non lo supera — si rompe coi blocchi:
 * ognuno salva i margini entrando e li rimette uscendo, e rimetterebbe il
 * float anche dopo che e' finito (o lo toglierebbe prima). Cosi' invece la
 * riga si chiede, alla y della penna, quali float la toccano e di quanto: e'
 * geometria, e non dipende da chi ha salvato cosa. E' come fanno Gecko e
 * tutti gli altri.
 *
 * ! E VALE ANCHE DENTRO UNA CELLA: una cella a destra di un float ha la x
 * oltre il suo bordo, e il float non la tocca — che e' giusto.
 * rx() e rw() sono riga_x() e riga_w() meno i float; qui dentro si usano
 * solo loro.
 * ========================================================================== */
#define GALLEGGIA_MAX 48
static struct { int x, w, y1, y2; unsigned char lato; } g_gal[GALLEGGIA_MAX];
static int g_gal_n = 0;

/* Quante colonne si stanno impaginando una dentro l'altra (celle, float,
 * inline-block, voci di un flex). */
static int g_in_colonna = 0;

/* ! LA RIGA SENZA IL MINIMO DI 40 PIXEL DENTRO UNA COLONNA. riga_w() non
 * scende sotto 40 perche' una pagina con margini enormi non dia righe
 * negative; ma una voce di flex larga 20 («aaa») si disegnava larga 40 e
 * avanzava di 20 — l'ultima di un space-between usciva dalla pagina, e il gap
 * si mangiava (visto con prova_disposizione.sh). In colonna la larghezza
 * l'ha decisa chi l'ha aperta, e si rispetta fino a 1. */
static int riga_w_vera(void)
{
    int w = area_w() - g_marg_sx - g_marg_dx;
    int min = g_in_colonna ? 1 : 40;
    return w < min ? min : w;
}

/* Quanto i float mangiano la riga alla y della penna, a sinistra e a destra. */
static void gal_ingombro(int *sx, int *dx)
{
    int base_sx = riga_x(), base_dx = riga_x() + riga_w_vera(), i;

    *sx = 0; *dx = 0;
    for (i = 0; i < g_gal_n; i++) {
        if (g_pen_y < g_gal[i].y1 || g_pen_y >= g_gal[i].y2) continue;
        if (g_gal[i].lato == CSS_GALLEGGIA_SX) {
            int m = g_gal[i].x + g_gal[i].w - base_sx;
            if (m > *sx && g_gal[i].x < base_dx) *sx = m;
        } else {
            int m = base_dx - g_gal[i].x;
            if (m > *dx && g_gal[i].x + g_gal[i].w > base_sx) *dx = m;
        }
    }
}

static int rx(void)
{
    int sx, dx;
    gal_ingombro(&sx, &dx);
    return riga_x() + sx;
}

static int rw(void)
{
    int sx, dx, w;
    gal_ingombro(&sx, &dx);
    w = riga_w_vera() - sx - dx;
    if (g_in_colonna) return w < 1 ? 1 : w;
    return w < 40 ? 40 : w;
}

/* ! SE FRA I FLOAT NON C'E' PIU' POSTO, SI SCENDE SOTTO IL PRIMO CHE FINISCE:
 * scrivere in una fessura di quaranta pixel darebbe una colonna di sillabe. */
static void gal_fai_posto(int serve)
{
    int giri = 0;

    while (giri++ < GALLEGGIA_MAX) {
        int sx, dx, i, sotto = -1;

        gal_ingombro(&sx, &dx);
        if (sx == 0 && dx == 0) return;
        if (riga_w_vera() - sx - dx >= serve) return;
        for (i = 0; i < g_gal_n; i++)
            if (g_pen_y >= g_gal[i].y1 && g_pen_y < g_gal[i].y2 &&
                (sotto < 0 || g_gal[i].y2 < sotto)) sotto = g_gal[i].y2;
        if (sotto < 0) return;
        g_pen_y = sotto;
    }
}

/* clear: la penna scende sotto i float dei lati chiesti. */
static void pulisci(unsigned char lati)
{
    int i;

    if (!lati) return;
    for (i = 0; i < g_gal_n; i++) {
        int lato = (g_gal[i].lato == CSS_GALLEGGIA_SX) ? CSS_PULISCI_SX : CSS_PULISCI_DX;
        if ((lati & lato) && g_gal[i].y2 > g_pen_y) g_pen_y = g_gal[i].y2;
    }
    g_pen_x = rx();
}

/* Sposta tutto quel che si e' impaginato dai pezzi `pz` e dagli sfondi `sf`
 * in poi: serve ad align-items e a position: relative, che decidono DOPO
 * dove doveva stare quel che e' gia' stato messo. */
static void sposta_da(int pz, int sf, int dx, int dy)
{
    int i;

    if (!dx && !dy) return;
    for (i = pz; i < g_pez_n; i++)    { g_pez[i].x += dx; g_pez[i].y += dy; }
    for (i = sf; i < g_sfondi_n; i++) { g_sfondi[i].x += dx; g_sfondi[i].y += dy; }
}

/* Il fondo del float piu' basso: la pagina e' alta almeno fin li'. */
static int gal_fondo(void)
{
    int i, f = 0;
    for (i = 0; i < g_gal_n; i++) if (g_gal[i].y2 > f) f = g_gal[i].y2;
    return f;
}

/* ! L'ELEMENTO CHE SI STA IMPAGINANDO COME COLONNA (un float, un inline-block,
 * un figlio di un flex) non deve rifare la sua strada quando impagina_nodo lo
 * incontra: sarebbe un float dentro se stesso, per sempre. impagina_in_colonna
 * lo scrive qui, impagina_nodo lo riconosce e lo tratta da blocco semplice. */
static int g_col_radice = -1;
/* L'elemento in position: relative che si sta impaginando: vedi impagina_nodo. */
static int g_rel_nodo = -1;

/* La larghezza che uno stile chiede, in pixel, dentro `disp` disponibili, gia'
 * comprensiva di bordi e padding (`extra`) se box-sizing e' content-box. -1 =
 * nessuna: si prende quel che c'e'. */
static int larg_risolta(const CssStile *s, int disp, int extra)
{
    int w = -1, v;
    int agg = s->scatola_bordo ? 0 : extra;

    if (s->larghezza != CSS_MISURA_NO) {
        v = (s->larghezza_perc & CSS_LARG_PERC) ? (int)((long)disp * s->larghezza / 10000) : s->larghezza;
        w = v + agg;
    }
    if (s->larghezza_max != CSS_MISURA_NO) {
        v = ((s->larghezza_perc & CSS_LARG_MAX_PERC) ? (int)((long)disp * s->larghezza_max / 10000)
                                                       : s->larghezza_max) + agg;
        if (w < 0) { if (disp > v) w = v; }
        else if (w > v) w = v;
    }
    if (s->larghezza_min != CSS_MISURA_NO) {
        v = ((s->larghezza_perc & CSS_LARG_MIN_PERC) ? (int)((long)disp * s->larghezza_min / 10000)
                                                       : s->larghezza_min) + agg;
        if (w >= 0 && w < v) w = v;
    }
    return w;
}

/* min-width in pixel, o -1: vale anche quando la larghezza e' automatica
 * (vedi larg_elemento). */
static int larg_minima(const CssStile *s, int disp, int extra)
{
    if (s->larghezza_min == CSS_MISURA_NO) return -1;
    return ((s->larghezza_perc & CSS_LARG_MIN_PERC) ? (int)((long)disp * s->larghezza_min / 10000)
                                                    : s->larghezza_min) + (s->scatola_bordo ? 0 : extra);
}

/* Bordi piu' padding di sinistra e di destra: quel che box-sizing somma. */
static int lati_extra(const CssStile *s);

/* ! IL CLIENTE PUO' NON ESSERCI, e non e' un caso di scuola: la prova
 * dell'impaginato non ha un navigatore intorno, e senza cliente deve impaginare
 * il testo e ignorare il resto invece di fermarsi. E' la stessa scelta del
 * risolutore di indirizzi in exdom — un gancio che manca da' la verita' piu'
 * vicina, non un errore. */
static const VistaCliente *g_cliente = 0;

void vista_cliente(const VistaCliente *c)
{
    g_cliente = c;
}

static ExFont font_di(const CssStile *st)
{
    int neretto = (st->grassetto == 1);
    int corsivo = (st->corsivo == 1);
    int corpo   = (st->corpo == CSS_MISURA_NO) ? CSS_CORPO_PREDEFINITO : st->corpo;
    int fam;

    /* ! IL TAG BATTE IL FOGLIO SOLO DOVE IL FOGLIO TACE. Dentro <pre> o <code>
     * il monospazio e' il valore predefinito, non un obbligo: una pagina che
     * scrive `code { font-family: sans-serif }` lo sta chiedendo davvero, e
     * quel foglio ha l'ultima parola. Ma se non dice niente, <pre> vuole il
     * monospazio — e' il motivo per cui quel tag esiste. */
    if (st->famiglia == CSS_FAM_FISSO)      fam = FAM_MONO;
    else if (st->famiglia == CSS_FAM_SANS)  fam = FAM_SANS;
    else if (st->famiglia == CSS_FAM_SERIF) fam = FAM_SERIF;
    else                                    fam = g_fisso > 0 ? FAM_MONO
                                                              : FAM_SERIF;

    if (!neretto && !corsivo && fam == FAM_SERIF && corpo == CSS_CORPO_PREDEFINITO)
        return g_font_testo;
    return font_per(neretto, corsivo, fam, corpo);
}

static unsigned int colore_di(const CssStile *st)
{
    return (st->colore == CSS_NIENTE) ? EX_BLACK : st->colore;
}

/* =============================================================================
 * I bordi e il padding del CSS (@NAV-BORDER, 28 settembre 2026)
 *
 * ! SOLO SUI BLOCCHI. Un bordo su uno <span> va spezzato fra le righe in cui
 * lo <span> cade, e l'impaginato a pezzi di parola non sa ancora dove comincia
 * e finisce una riga di un elemento: per ora li' non si disegna. Anche le
 * tabelle restano col loro `border=` (bordo_tabella qui sotto).
 * ============================================================================= */

/* Lo spessore di un lato, o 0 se il lato non si vede: senza uno stile il
 * bordo non c'e', anche con uno spessore (e' la regola del CSS). */
static int lato_bordo(const CssStile *st, int lato)
{
    int w;

    if (st->bordo_stile[lato] != 1) return 0;
    w = (st->bordo[lato] == CSS_MISURA_NO) ? 3 : st->bordo[lato];   /* medium */
    return w < 0 ? 0 : (w > 50 ? 50 : w);
}

/* Il colore di un lato; senza, quello del testo (currentcolor). */
static unsigned int colore_bordo(const CssStile *st, int lato)
{
    return (st->bordo_col[lato] == CSS_NIENTE) ? colore_di(st) : st->bordo_col[lato];
}

static int imbottitura(const CssStile *st, int lato);

static int lati_extra(const CssStile *s)
{
    return lato_bordo(s, 1) + lato_bordo(s, 3) + imbottitura(s, 1) + imbottitura(s, 3);
}

static int imbottitura(const CssStile *st, int lato)
{
    int p = st->imbottitura[lato];
    return (p == CSS_MISURA_NO || p < 0) ? 0 : (p > 200 ? 200 : p);
}

/* Un rettangolo pieno nella lista degli sfondi; rende il suo indice, o -1 se
 * la lista e' piena (e allora quel lato semplicemente non si vede). */
static int rettangolo(int x, int y, int w, int h, unsigned int colore)
{
    int i;

    if (g_sfondi_n >= SFONDI_MAX || w <= 0) return -1;
    i = g_sfondi_n++;
    g_sfondi[i].x = x; g_sfondi[i].y = y; g_sfondi[i].w = w; g_sfondi[i].h = h;
    g_sfondi[i].colore = colore;
    g_sfondi[i].bordo  = 0;
    g_sfondi[i].imm    = -1;
    return i;
}

static int alt_riga_f(ExFont f)
{
    int h = ex_font_height(f);

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

    avanzo = (rx() + rw()) - g_pen_x;
    if (avanzo <= 0) return;

    dx = (g_stile_ora.allineamento == CSS_ALL_CENTRO) ? avanzo / 2 : avanzo;
    for (i = g_riga_primo; i < g_pez_n; i++) g_pez[i].x += dx;
}

static void a_capo(void)
{
    /* ! UNA RIGA VUOTA NON SI ACCUMULA. Un documento indentato produce spazi
     * fra un blocco e l'altro: andando a capo per ognuno si otterrebbero
     * pagine fatte di buchi. Si va a capo solo se sulla riga c'e' qualcosa. */
    if (g_pen_x <= rx()) return;

    allinea_riga();

    g_pen_y += g_riga_h;
    /* ! LA x DOPO LA y: la riga nuova puo' avere un float in meno. */
    g_pen_x  = rx();
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
    /* auto in verticale vale zero: e' la regola del CSS per i blocchi */
    g_pen_y += (m == CSS_MISURA_NO) ? 6 : (m == CSS_MISURA_AUTO) ? 0 : m;
    g_pen_x = rx();
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

    w = ex_text_width(f, cop);

    /* ! SI VA A CAPO SULLA PAROLA, NON SUL CARATTERE, ed e' cio' che rende il
     * testo leggibile: spezzare in mezzo a una parola si vede subito. Una
     * parola piu' larga della finestra si mette lo stesso e sborda — meglio
     * che sparire. */
    if (g_pen_x + w > rx() + rw() && g_pen_x > rx()) a_capo();
    if (g_pen_x <= rx() && g_gal_n) { gal_fai_posto(w < 120 ? w : 120); g_pen_x = rx(); }

    if (g_pez_n < (int)g_pez_max) {
        g_pez[g_pez_n].x = g_pen_x;
        g_pez[g_pez_n].y = g_pen_y;
        g_pez[g_pez_n].w = w;
        g_pez[g_pez_n].testo = off;
        g_pez[g_pez_n].font = f;
        g_pez[g_pez_n].colore = colore_di(&g_stile_ora);
        g_pez[g_pez_n].h = 0;
        g_pez[g_pez_n].link = (short)g_link_ora;
        g_pez[g_pez_n].rif = -1;
        g_pez[g_pez_n].nodo = g_nodo_ora;
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

/* ! ZERO NON PUO' VOLER DIRE «NON C'E' POSTO», ED E' COSTATO UN DIFETTO CHE
 * SEMBRAVA UN'ALTRA COSA. Zero e' un OFFSET VALIDO — e' l'inizio dell'arena,
 * cioe' il primo testo della pagina. Con l'arena piena, il segno di una voce
 * di elenco riceveva offset 0 e veniva disegnato con quel testo: su Wikipedia
 * si vedeva «#Main page» sovrapposto a «Main page», e sembrava che
 * l'impaginazione disegnasse due volte. Non disegnava due volte: disegnava
 * una volta la cosa sbagliata.
 *
 * Adesso l'impossibile e' un valore che non puo' essere un offset, e chi
 * chiama salta il pezzo. Un elenco senza i segni e' meno di un elenco; un
 * elenco con dentro pezzi di un'altra frase e' peggio di niente. */
#define GENERA_NIENTE   ((unsigned int)-1)

/* =============================================================================
 * ! IL TESTO CHE L'IMPAGINAZIONE INVENTA HA UN'ARENA SUA, E PRIMA NO — ED E'
 * STATO IL DIFETTO CHE HA ROVINATO OGNI PAGINA CON UNO SCRIPT.
 *
 * I segni degli elenchi («-», «3.») non stanno nel documento: li fabbrica
 * l'impaginazione. Finivano nell'arena del DOCUMENTO, dopo il testo vero, e
 * `impagina()` la riavvolgeva a ogni giro per non accumularli — «si butta il
 * testo generato dal giro prima». Era giusto finche' in quell'arena scriveva
 * solo l'impaginazione.
 *
 * ! DA QUANDO C'E' JAVASCRIPT, NELLA STESSA ARENA SCRIVONO ANCHE GLI SCRIPT:
 * `innerHTML`, `textContent`, `createTextNode` chiedono a exhtml, che copia
 * li' dentro. Riavvolgere buttava via IL LORO testo mentre i nodi continuavano
 * a puntarci — e la scrittura successiva ci finiva sopra. Il sintomo era
 * cattivo: il riquadro 1 mostrava un pezzo del testo del riquadro 7, e il
 * primo elemento di una lista una briciola di un'altra frase. Sembrava un
 * difetto del motore JavaScript, e lo faceva con TUTT'E DUE i motori — che e'
 * stato il modo in cui si e' capito che il motore non c'entrava.
 *
 * ! LA CURA NON E' SPOSTARE IL SEGNAPOSTO, E' NON SCRIVERE LA'. L'arena del
 * documento e' del documento; l'impaginazione ha la sua, che si azzera a ogni
 * giro perche' quel testo dura un giro. E' la stessa regola dei buffer di chi
 * chiama applicata dentro un programma solo.
 *
 * ! L'OFFSET PORTA IL BIT PIU' ALTO ACCESO per dire da quale delle due arene
 * viene. Un'arena da un megabyte non arriva a 0x80000000 nemmeno per sbaglio,
 * quindi il bit e' libero davvero — e un pezzo continua a costare quattro byte
 * di scostamento invece di un puntatore da quattro piu' un si'/no.
 * ============================================================================= */
#define GEN_BIT     0x80000000u


static unsigned int genera(const char *s)
{
    unsigned int inizio = g_gen_n, i = 0;

    while (s[i]) i++;
    if (g_gen_n + i + 1 > GEN_MAX) {
        g_doc.troncato = 1;         /* la barra di stato lo dira' */
        return GENERA_NIENTE;
    }

    for (i = 0; s[i]; i++) g_gen[g_gen_n++] = s[i];
    g_gen[g_gen_n++] = '\0';
    return inizio | GEN_BIT;
}

/* Il testo di un pezzo, da qualunque delle due arene venga. */
static const char *testo_pezzo(unsigned int off);

int pezzo_parola(int i, char *out, int max)
{
    const char *t;
    int k = 0;

    if (i < 0 || i >= g_pez_n || g_pez[i].rif >= 0 || max < 1) { if (max > 0) out[0] = '\0'; return 0; }
    t = testo_pezzo(g_pez[i].testo);
    while (t[k] && t[k] != ' ' && t[k] != '\n' && t[k] != '\r' && t[k] != '\t' && k < max - 1) {
        out[k] = t[k]; k++;
    }
    out[k] = '\0';
    return k;
}

static const char *testo_pezzo(unsigned int off)
{
    if (off & GEN_BIT) return g_gen + (off & ~GEN_BIT);
    return g_arena + off;
}

/* Fuori da <pre>, questi quattro sono tutti «spazio». */
static int bianco(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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
            if (t[i] == '\n') { g_pen_x = rx() + 1; a_capo(); i++; continue; }
            if (t[i] == '\r') { i++; continue; }
            if (t[i] == ' ' || t[i] == '\t') {
                int quanti = (t[i] == '\t') ? 8 : 1;

                g_pen_x += quanti * ex_text_width(font_di(&g_stile_ora), " ");
                i++;
                continue;
            }
            a = i;
            while (t[i] && t[i] != ' ' && t[i] != '\t' &&
                   t[i] != '\n' && t[i] != '\r') i++;
            parola(t + a, base + (unsigned int)a, i - a);
            continue;
        }

        /* =====================================================================
         * ! GLI A CAPO SONO SPAZI, E NON LO ERANO: qui si guardava solo ' ',
         * e l'HTML fra un tag e l'altro va a capo di continuo. Il risultato si
         * leggeva su qualunque pagina vera — «Hacker Newsnew» al posto di
         * «Hacker News new» — perche' quel nodo di testo fatto di un solo a
         * capo non avanzava la penna e diventava una parola vuota.
         *
         * ! E UNA SEQUENZA DI BIANCHI VALE UNO SPAZIO SOLO, che e' la regola
         * dell'HTML: prima ogni spazio ne aggiungeva uno, quindi il testo
         * sorgente indentato apriva buchi larghi quanto il rientro.
         *
         * ! A INIZIO RIGA NON VALE NIENTE. Senza questa riga ogni paragrafo che
         * nel sorgente comincia a capo partirebbe rientrato di uno spazio, e
         * il margine sinistro della pagina sembrerebbe storto.
         * ===================================================================== */
        if (bianco(t[i])) {
            while (bianco(t[i])) i++;
            if (g_pen_x > rx())
                g_pen_x += ex_text_width(font_di(&g_stile_ora), " ");
            continue;
        }
        if (!t[i]) break;
        a = i;
        while (t[i] && !bianco(t[i])) i++;
        parola(t + a, base + (unsigned int)a, i - a);
    }
}

/* Un pezzo estraneo si colloca come una parola molto grande.
 *
 * ! ERA pezzo_immagine, E LA DIFFERENZA E' TUTTA NEI CAMPI DI `p`. Prima
 * questa funzione sapeva di collocare un'immagine: prendeva il link corrente,
 * non stringeva mai, aggiungeva tre pixel di respiro. Adesso quei tre numeri
 * glieli porta chi ha misurato il pezzo, e lei non sa piu' che cosa sta
 * mettendo giu'. Un controllo passa esattamente di qui, con numeri diversi. */
static void pezzo_estraneo(int v, const VistaPezzo *p)
{
    int w = p->w, h = p->h;

    /* ! NON SOTTO I 60 PIXEL: la misura «minima» del flex impagina in una
     * colonna di un pixel, e un pulsante stretto fino a 1 diventava la sua
     * larghezza minima — «Ricerca» di Wikipedia usciva largo 19 pixel. In una
     * colonna cosi' stretta un controllo tiene la sua misura, e sborda. */
    if (p->stringi && w > rw() && rw() >= 60) w = rw();
    if (g_pen_x + w > rx() + rw() && g_pen_x > rx()) a_capo();

    if (g_pez_n < (int)g_pez_max) {
        g_pez[g_pez_n].x = g_pen_x;
        g_pez[g_pez_n].y = g_pen_y;
        g_pez[g_pez_n].w = w;
        g_pez[g_pez_n].testo = 0;
        g_pez[g_pez_n].font = g_font_testo;
        g_pez[g_pez_n].colore = EX_BLACK;
        g_pez[g_pez_n].h = (short)h;
        g_pez[g_pez_n].link = p->nel_flusso ? (short)g_link_ora : -1;
        g_pez[g_pez_n].rif = (short)p->rif;
        g_pez[g_pez_n].nodo = v;
        g_pez_n++;
    }

    g_pen_x += w + p->aria_dx;

    /* ! LA RIGA CRESCE FINO AL PEZZO, altrimenti la riga dopo gli passa sopra:
     * l'altezza di una riga e' quella del suo pezzo piu' alto. */
    if (h + p->aria_giu > g_riga_h) g_riga_h = h + p->aria_giu;
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

int uguale(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

/* ! CIO' CHE NON SI VEDE NON SI IMPAGINA: dentro <script>, <style>, <head> e
 * <title> c'e' testo che non appartiene alla pagina. Senza questo, la prima
 * cosa che si legge su un sito vero e' un chilometro di JavaScript.
 *
 * ! E <noscript> DIPENDE DA COM'E' MESSO L'INTERRUTTORE, che e' l'unico tag
 * di questo elenco a non essere sempre uguale a se stesso. E' li' apposta per
 * chi il JavaScript non ce l'ha: mostrarlo COMUNQUE vuol dire che una pagina
 * con gli script accesi fa vedere due volte la stessa cosa — una dagli script
 * e una dal ripiego — o, peggio, fa vedere il ripiego di una pagina che gli
 * script hanno gia' costruito.
 *
 * ! E <noscript> NON E' PIU' IN QUESTO ELENCO, perche' e' l'unico che dipende
 * da una cosa che l'impaginato non ha: il motore acceso o spento. La risposta
 * la da' il cliente, con VISTA_SALTA — e questo elenco e' tornato a essere
 * quello che dice il suo nome, cioe' i tag il cui contenuto non e' pagina. */
static int invisibile(const char *n)
{
    return uguale(n, "script") || uguale(n, "style") || uguale(n, "head") ||
           uguale(n, "title") || uguale(n, "meta") || uguale(n, "link");
}

/* impagina_nodo e impagina_tabella si chiamano a vicenda: una tabella contiene
 * del contenuto qualunque, e quel contenuto puo' contenere un'altra tabella. */
static void impagina_nodo(int v, const CssStile *ered);


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
 * ! `colspan` E `rowspan` CI SONO, e cambiano piu' di quanto sembri: senza,
 * ogni tabella con un'intestazione che scavalca due colonne — cioe' quasi
 * tutte quelle vere — mandava fuori posto tutte le celle dopo di lei, non solo
 * quella. Una cella che scavalca prende la somma delle colonne che occupa piu'
 * gli spazi in mezzo; e chi scavalca delle RIGHE si tiene la sua colonna per i
 * giri successivi, che e' il motivo per cui serve una mappa di cio' che e' gia'
 * occupato invece di un semplice contatore di celle.
 *
 * ! L'ALTEZZA DI UNA CELLA CHE SCAVALCA SI DIVIDE FRA LE RIGHE che occupa, non
 * si scarica tutta sulla prima. Scaricarla sulla prima farebbe una riga alta e
 * due vuote sotto — il contrario di quel che si vede in una tabella vera.
 *
 * ! QUELLO CHE NON C'E', DICHIARATO: i bordi.
 * ============================================================================= */
#define TAB_COL_MAX     10
#define TAB_RIG_MAX     120
#define TAB_LIV_MAX     3       /* tabelle dentro tabelle */
#define TAB_SPAZIO      8       /* fra una colonna e l'altra */

static int g_tab_liv = 0;

/* Quante colonne (o righe) scavalca questa cella. Sempre almeno una, e con un
 * tetto: `colspan="9999"` esiste davvero sulle pagine vere, ed e' un modo di
 * dire «tutta la riga» che non deve poter allocare novemila colonne. */
static int quanto_scavalca(int nodo, const char *attr)
{
    const char  *a = html_attr(&g_doc, nodo, attr);
    unsigned int v;

    if (!a) return 1;
    v = numero(a);
    if (v < 1) return 1;
    if (v > TAB_COL_MAX) return TAB_COL_MAX;
    return (int)v;
}

/* ! IL BORDO LO DICE L'ATTRIBUTO, non il foglio di stile, e per il web vero e'
 * la scelta giusta: `<table border="1">` e' come si sono disegnate le tabelle
 * per vent'anni, e le pagine che lo usano sono le stesse che non hanno un CSS.
 * `border-collapse`, i colori e i lati separati non ci sono: un filo scuro
 * intorno a ogni cella e' quel che quell'attributo ha sempre voluto dire.
 *
 * ! E SI CAPPA A QUATTRO. `border="20"` esiste, ed e' una tabella fatta quasi
 * solo di bordo: chi la scrive vuole «spesso», non venti pixel per lato. */
static int bordo_tabella(int v)
{
    const char  *a = html_attr(&g_doc, v, "border");
    unsigned int b;

    if (!a) return 0;
    if (!a[0]) return 1;        /* `border` da solo vale «si'» */
    b = numero(a);
    if (b == 0) return 0;
    return b > 4 ? 4 : (int)b;
}

/* Un rettangolo da contornare, nello stesso elenco degli sfondi. */
static int bordo_metti(int x, int y, int w, int h, int spess)
{
    if (spess <= 0 || g_sfondi_n >= SFONDI_MAX) return -1;

    g_sfondi[g_sfondi_n].x      = x;
    g_sfondi[g_sfondi_n].y      = y;
    g_sfondi[g_sfondi_n].w      = w;
    g_sfondi[g_sfondi_n].h      = h;
    g_sfondi[g_sfondi_n].colore = EX_DARK_GRAY;
    g_sfondi[g_sfondi_n].bordo  = (unsigned char)spess;
    g_sfondi[g_sfondi_n].imm    = -1;
    return g_sfondi_n++;
}

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
/* ! `prova`: 0 si impagina davvero; 1 si MISURA e i pezzi si buttano; 2
 * (28 settembre 2026) si impagina DENTRO una misura di fuori, e i pezzi si
 * tengono — li buttera' lei. Con 1 anche qui, un flex dentro un flex misurato
 * buttava i suoi pezzi, la misura di fuori non vedeva niente e diceva «largo
 * 1»: su Wikipedia il logo risultava largo un pixel e la ricerca gli finiva
 * sopra.
 * ! `se_stesso` (28 settembre 2026): 0 impagina i FIGLI di `nodo`, ed e' la
 * cella di una tabella; 1 impagina `nodo` stesso, col suo sfondo, i bordi e
 * il padding, ed e' un float, un inline-block o un figlio di un flex. */
static int impagina_in_colonna_ex(int nodo, const CssStile *ered,
                                  int x, int y, int w, int prova, int *alt,
                                  int se_stesso)
{
    int era_sx = g_marg_sx, era_dx = g_marg_dx;
    int era_gal = g_gal_n, era_rad = g_col_radice;

    g_in_colonna++;
    int era_px = g_pen_x, era_py = g_pen_y;
    int era_rh = g_riga_h, era_rp = g_riga_primo;
    int primo  = g_pez_n, primo_sf = g_sfondi_n;
    int era_mis = g_misura;
    int larga  = 0, i, f;

    g_misura = prova != 0;

    g_marg_sx = x - area_x();
    g_marg_dx = (area_x() + area_w()) - (x + w);
    if (g_marg_sx < 0) g_marg_sx = 0;
    if (g_marg_dx < 0) g_marg_dx = 0;

    g_pen_x      = rx();
    g_pen_y      = y;
    g_riga_h     = alt_riga_f(font_di(ered));
    g_riga_primo = g_pez_n;

    /* I figli della cella, non la cella: `td` non e' un blocco, e trattarlo da
     * tale aggiungerebbe uno stacco dentro ogni casella. */
    if (se_stesso) {
        g_col_radice = nodo;
        impagina_nodo(nodo, ered);
    } else {
        for (f = g_doc.nodi[nodo].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo)
            impagina_nodo(f, ered);
    }
    g_col_radice = era_rad;
    a_capo();
    /* i float di dentro allungano la colonna */
    for (i = era_gal; i < g_gal_n; i++)
        if (g_gal[i].y2 > g_pen_y) g_pen_y = g_gal[i].y2;

    *alt = g_pen_y - y;
    if (*alt < g_riga_h) *alt = g_riga_h;

    for (i = primo; i < g_pez_n; i++) {
        int destra = g_pez[i].x + g_pez[i].w - x;

        if (destra > larga) larga = destra;
    }

    if (prova == 1) { g_pez_n = primo; g_sfondi_n = primo_sf; }
    /* ! I FLOAT DI DENTRO NON ESCONO DALLA COLONNA: fuori, la riga non deve
     * restringersi per un rettangolo che sta dentro una cella. */
    g_gal_n = era_gal;
    g_in_colonna--;
    g_misura = era_mis;

    g_marg_sx = era_sx; g_marg_dx = era_dx;
    g_pen_x = era_px;   g_pen_y = era_py;
    g_riga_h = era_rh;  g_riga_primo = era_rp;
    return larga;
}

static int impagina_in_colonna(int nodo, const CssStile *ered,
                               int x, int y, int w, int prova, int *alt)
{
    return impagina_in_colonna_ex(nodo, ered, x, y, w, prova, alt, 0);
}

static void impagina_tabella(int v, const CssStile *mio)
{
    int      righe[TAB_RIG_MAX], n_righe = 0;
    int      largh[TAB_COL_MAX];
    int      resta[TAB_COL_MAX], debito[TAB_COL_MAX];
    int      n_col = 0, r, c, somma = 0, disp, alt;
    int      x0, y0, bordo, y_inizio, i_bordo_tab;

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
    bordo = bordo_tabella(v);
    for (c = 0; c < TAB_COL_MAX; c++) largh[c] = 0;

    /* --- prima passata: quanto vorrebbe essere larga ogni colonna --------- */
    for (r = 0; r < n_righe; r++) {
        int f;

        c = 0;
        for (f = g_doc.nodi[righe[r]].primo_figlio; f >= 0;
             f = g_doc.nodi[f].prossimo) {
            CssStile sc;
            int      w, a, sp;

            if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
            if (!e_cella(html_nome(&g_doc, f))) continue;
            if (c >= TAB_COL_MAX) break;

            css_calcola(&g_css, &g_doc, f, mio, &sc);
            suggerimenti(f, &sc);
            w = impagina_in_colonna(f, &sc, rx(), 0, rw(), 1, &a);

            sp = quanto_scavalca(f, "colspan");
            if (c + sp > TAB_COL_MAX) sp = TAB_COL_MAX - c;

            /* ! UNA CELLA CHE SCAVALCA NON ALLARGA UNA COLONNA SOLA. La sua
             * larghezza si spalma sulle colonne che occupa, e solo per la
             * parte che quelle non hanno gia': altrimenti un titolo lungo su
             * due colonne le renderebbe larghe il doppio del necessario. */
            if (sp <= 1) {
                if (w > largh[c]) largh[c] = w;
            } else {
                int k, gia = 0;

                for (k = 0; k < sp; k++) gia += largh[c + k];
                gia += (sp - 1) * TAB_SPAZIO;
                if (w > gia) {
                    int manca = (w - gia + sp - 1) / sp;

                    for (k = 0; k < sp; k++) largh[c + k] += manca;
                }
            }
            c += sp;
        }
        if (c > n_col) n_col = c;
    }

    if (n_col == 0) { g_tab_liv--; return; }

    /* --- la distribuzione ------------------------------------------------- */
    for (c = 0; c < n_col; c++) {
        if (largh[c] < 12) largh[c] = 12;
        somma += largh[c];
    }
    disp = rw() - (n_col - 1) * TAB_SPAZIO;
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
    y_inizio = y0;

    /* Il contorno di TUTTA la tabella: si apre adesso e si chiude in fondo,
     * quando si sa quanto e' venuta alta. */
    i_bordo_tab = -1;
    if (bordo > 0) {
        int tot = 0;

        for (c = 0; c < n_col; c++) tot += largh[c];
        tot += (n_col - 1) * TAB_SPAZIO;
        i_bordo_tab = bordo_metti(rx(), y0, tot, 0, bordo);
    }

    /* ! LA MAPPA DI CIO' CHE E' GIA' OCCUPATO, ed e' tutto cio' che serve per
     * `rowspan`. `resta[c]` dice per quanti giri ancora la colonna c e' presa
     * da una cella cominciata sopra; `debito[c]` quanta altezza quella cella
     * deve ancora coprire, cosi' le righe sotto non si schiacciano. */
    {
        int k;

        for (k = 0; k < TAB_COL_MAX; k++) { resta[k] = 0; debito[k] = 0; }
    }

    for (r = 0; r < n_righe; r++) {
        int f, alt_riga_tab = 0;

        /* Quel che una cella cominciata prima pretende da QUESTA riga. */
        for (c = 0; c < n_col; c++)
            if (resta[c] > 0) {
                int q = (debito[c] + resta[c] - 1) / resta[c];

                if (q > alt_riga_tab) alt_riga_tab = q;
            }

        x0 = rx();
        c  = 0;
        for (f = g_doc.nodi[righe[r]].primo_figlio; f >= 0;
             f = g_doc.nodi[f].prossimo) {
            CssStile sc;
            int      sp, rp, largh_cella, k, i_bordo;

            if (g_doc.nodi[f].tipo != HTML_ELEMENTO) continue;
            if (!e_cella(html_nome(&g_doc, f))) continue;

            /* ! LE COLONNE GIA' PRESE SI SALTANO, e si salta anche il loro
             * spazio: e' l'unica cosa che tiene incolonnato quel che viene
             * dopo una cella che scavalca delle righe. */
            while (c < n_col && resta[c] > 0) {
                x0 += largh[c] + TAB_SPAZIO;
                c++;
            }
            if (c >= n_col) break;

            css_calcola(&g_css, &g_doc, f, mio, &sc);
            suggerimenti(f, &sc);

            sp = quanto_scavalca(f, "colspan");
            if (c + sp > n_col) sp = n_col - c;
            if (sp < 1) sp = 1;
            rp = quanto_scavalca(f, "rowspan");

            largh_cella = 0;
            for (k = 0; k < sp; k++) largh_cella += largh[c + k];
            largh_cella += (sp - 1) * TAB_SPAZIO;

            /* Lo sfondo della cella si segna PRIMA, con l'altezza rimessa a
             * posto quando la riga e' finita: e' lo stesso giro dei blocchi. */
            if (sc.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                g_sfondi[g_sfondi_n].x = x0;
                g_sfondi[g_sfondi_n].y = y0;
                g_sfondi[g_sfondi_n].w = largh_cella;
                g_sfondi[g_sfondi_n].h = 0;
                g_sfondi[g_sfondi_n].colore = sc.sfondo;
                g_sfondi[g_sfondi_n].bordo  = 0;
                g_sfondi[g_sfondi_n].imm    = -1;
                g_sfondi_n++;
            }

            i_bordo = bordo_metti(x0, y0, largh_cella, 0, bordo);

            impagina_in_colonna(f, &sc, x0, y0, largh_cella, 0, &alt);

            /* ! UNA CELLA CHE SCAVALCA CHIUDE IL SUO RIQUADRO DA SOLA, adesso:
             * il giro di fine riga qui sotto rimette l'altezza a tutto cio' che
             * e' ancora aperto su questa y, e per una cella alta tre righe
             * sarebbe l'altezza di UNA. */
            if (rp > 1) {
                int q;

                for (q = g_sfondi_n - 1; q >= 0; q--)
                    if ((q == i_bordo || g_sfondi[q].y == y0) &&
                        g_sfondi[q].h == 0 && g_sfondi[q].x == x0)
                        g_sfondi[q].h = alt;
            }

            if (rp <= 1) {
                if (alt > alt_riga_tab) alt_riga_tab = alt;
            } else {
                /* Si tiene la colonna per i giri successivi, e ci si porta
                 * dietro l'altezza che resta da coprire. */
                int primo = (alt + rp - 1) / rp;

                if (primo > alt_riga_tab) alt_riga_tab = primo;

                /* ! SI SEGNA `rp`, NON `rp - 1`, e la differenza e' un giro
                 * intero: in fondo a QUESTA riga si decrementa insieme a tutte
                 * le altre, quindi partire da rp-1 lasciava la colonna libera
                 * un giro troppo presto. Il sintomo era la cella dell'ultima
                 * riga che tornava nella colonna della cella che scavalca, e
                 * ci si scriveva sopra.
                 *
                 * Il debito e' l'altezza INTERA: quel che ogni riga copre si
                 * sottrae in fondo alla riga, sempre nello stesso posto. */
                for (k = 0; k < sp; k++) {
                    resta[c + k]  = rp;
                    debito[c + k] = alt;
                }
            }

            x0 += largh_cella + TAB_SPAZIO;
            c += sp;
        }

        /* Gli sfondi di questa riga prendono adesso la loro altezza vera. */
        {
            int k;

            for (k = g_sfondi_n - 1; k >= 0; k--) {
                if (g_sfondi[k].y != y0 || g_sfondi[k].h != 0) continue;
                g_sfondi[k].h = alt_riga_tab;
            }
        }

        /* Le celle che scavalcano hanno consumato un giro. */
        for (c = 0; c < n_col; c++)
            if (resta[c] > 0) {
                debito[c] -= alt_riga_tab;
                if (debito[c] < 0) debito[c] = 0;
                resta[c]--;
            }

        y0 += alt_riga_tab;
    }

    if (i_bordo_tab >= 0) g_sfondi[i_bordo_tab].h = y0 - y_inizio;

    g_pen_y      = y0;
    g_pen_x      = rx();
    g_riga_h     = alt_riga_f(font_di(mio));
    g_riga_primo = g_pez_n;
    g_tab_liv--;
}

/* =============================================================================
 * FLOAT, INLINE-BLOCK, FLEX (28 settembre 2026, @EXBROWSER-HTML5)
 *
 * Tutti e tre sono la stessa operazione vista da tre lati: un elemento
 * impaginato in una COLONNA sua, larga quanto chiede o quanto gli serve, e
 * poi messo da qualche parte — sul bordo (float), sulla riga come una parola
 * (inline-block), accanto ai fratelli (flex). La colonna e' quella delle celle
 * di tabella, che c'era gia' e funzionava.
 *
 * ! «QUANTO GLI SERVE» SI MISURA IMPAGINANDO PER PROVA su tutta la larghezza
 * e guardando il pezzo piu' a destra: e' la misura delle celle, e ne ha gli
 * stessi limiti (un testo lungo prende tutta la riga).
 * ========================================================================== */
static int misura_elemento(int v, const CssStile *ered, int disp, int *alt)
{
    int a = 0, w = impagina_in_colonna_ex(v, ered, rx(), 0, disp, 1, &a, 1);

    if (alt) *alt = a;
    return w < 1 ? 1 : w;
}

/* La larghezza di un elemento messo in colonna: quella chiesta, o quella che
 * gli serve, mai piu' di `disp`. */
static int larg_elemento(int v, const CssStile *mio, const CssStile *ered, int disp)
{
    int w = larg_risolta(mio, disp, lati_extra(mio));

    /* ! min-width VALE ANCHE SULLA LARGHEZZA MISURATA: `.cdx-text-input` di
     * Wikipedia ha min-width 256 e nessuna width, e il flex la stringeva a
     * 48 pixel sotto il logo. */
    if (w < 0) {
        int mn = larg_minima(mio, disp, lati_extra(mio));
        w = misura_elemento(v, ered, disp, 0);
        if (mn > w) w = mn;
    }
    if (w > disp) w = disp;
    return w < 1 ? 1 : w;
}

static void galleggia(int v, const CssStile *mio, const CssStile *ered)
{
    int w, x, alt = 0;

    a_capo();
    pulisci(mio->pulisci);
    w = larg_elemento(v, mio, ered, rw());
    gal_fai_posto(w);
    x = (mio->galleggia == CSS_GALLEGGIA_SX) ? rx() : rx() + rw() - w;
    impagina_in_colonna_ex(v, ered, x, g_pen_y, w, g_misura ? 2 : 0, &alt, 1);
    if (g_gal_n < GALLEGGIA_MAX) {
        g_gal[g_gal_n].x = x;
        g_gal[g_gal_n].w = w;
        g_gal[g_gal_n].y1 = g_pen_y;
        g_gal[g_gal_n].y2 = g_pen_y + alt;
        g_gal[g_gal_n].lato = mio->galleggia;
        g_gal_n++;
    }
    g_pen_x = rx();
    g_riga_primo = g_pez_n;
}

static void in_blocco(int v, const CssStile *mio, const CssStile *ered)
{
    int w, alt = 0, dx = css_margine(mio, 1, 0), sx = css_margine(mio, 3, 0);

    w = larg_elemento(v, mio, ered, rw());
    if (g_pen_x + sx + w > rx() + rw() && g_pen_x > rx()) a_capo();
    g_pen_x += sx;
    impagina_in_colonna_ex(v, ered, g_pen_x, g_pen_y, w, g_misura ? 2 : 0, &alt, 1);
    g_pen_x += w + dx;
    if (alt > g_riga_h) g_riga_h = alt;
}

/* Un elemento figlio di un flex va messo? Gli spazi fra un tag e l'altro no. */
static int voce_flex(int f)
{
    if (g_doc.nodi[f].tipo == HTML_TESTO) {
        const char *t = html_testo(&g_doc, f);
        while (t && *t) { if (!bianco(*t)) return 1; t++; }
        return 0;
    }
    return g_doc.nodi[f].tipo == HTML_ELEMENTO;
}

#define FLEX_VOCI 64

/* ! IL FLEX IN RIGA, e basta: i figli si affiancano, ognuno largo quanto gli
 * serve (o quanto chiede con width), lo spazio che avanza va a chi ha
 * flex-grow, e se non ci stanno si stringono in proporzione — o vanno a capo
 * con flex-wrap. Mancano justify-content, align-items, gap, order: i menu dei
 * siti si leggono gia' cosi', e quello che manca sposta, non nasconde. */
static void flessibile(int v, const CssStile *mio)
{
    int voce[FLEX_VOCI], pref[FLEX_VOCI], minimo[FLEX_VOCI], cresce[FLEX_VOCI], n = 0, f, i;
    int x0, disp, y, gcol, grig;
    CssStile st;

    a_capo();
    x0 = rx(); disp = rw(); y = g_pen_y;

    for (f = g_doc.nodi[v].primo_figlio; f >= 0 && n < FLEX_VOCI; f = g_doc.nodi[f].prossimo) {
        if (!voce_flex(f)) continue;
        if (g_doc.nodi[f].tipo == HTML_ELEMENTO) {
            css_calcola(&g_css, &g_doc, f, mio, &st);
            if (st.display == CSS_DISPLAY_NIENTE || st.visibile == 0) continue;
            if (invisibile(html_nome(&g_doc, f))) continue;
            pref[n] = larg_elemento(f, &st, mio, disp);
            cresce[n] = st.flex_cresce;
            /* chi dichiara la larghezza non scende sotto di lei */
            minimo[n] = (larg_risolta(&st, disp, lati_extra(&st)) >= 0) ? pref[n]
                      : misura_elemento(f, mio, 1, 0);
            {
                int mn = larg_minima(&st, disp, lati_extra(&st));
                if (mn > minimo[n]) minimo[n] = mn;
            }
        } else {
            pref[n] = misura_elemento(f, mio, disp, 0);
            minimo[n] = misura_elemento(f, mio, 1, 0);
            cresce[n] = 0;
        }
        if (minimo[n] > pref[n]) minimo[n] = pref[n];
        voce[n++] = f;
    }

    gcol = mio->spazio_col  == CSS_MISURA_NO ? 0 : mio->spazio_col;
    grig = mio->spazio_riga == CSS_MISURA_NO ? 0 : mio->spazio_riga;

    i = 0;
    while (i < n) {
        int j = i, somma = 0, cr = 0, alto = 0, x, k, cede = 0, spazi, libero, cnt;
        int larg[FLEX_VOCI], alti[FLEX_VOCI], pz[FLEX_VOCI + 1], sf[FLEX_VOCI + 1];
        int prima = 0, fra = 0;

        /* una riga: finche' ci stanno, o tutte se non si va a capo */
        while (j < n && (j == i || !mio->flex_a_capo || somma + gcol * (j - i) + pref[j] <= disp)) {
            somma += pref[j]; cr += cresce[j]; cede += pref[j] - minimo[j]; j++;
        }
        cnt = j - i;
        spazi = gcol * (cnt - 1);
        /* ! IL GAP SI TOGLIE PRIMA DI DIVIDERE: e' spazio che nessuna voce
         * puo' prendersi. */
        disp -= spazi;
        for (k = i; k < j; k++) {
            int w = pref[k];

            if (somma < disp && cr > 0) w += (int)((long)(disp - somma) * cresce[k] / cr);
            /* ! SI STRINGE CHI PUO' CEDERE, e mai sotto la parola piu' larga
             * che contiene (min-width: auto del CSS). Stringere tutti in
             * proporzione — il primo giro — metteva «Fai una donazione» di
             * Wikipedia in una colonna di venti pixel, e il testo usciva dalla
             * pagina. Se neanche cosi' ci stanno, sbordano: come nei browser. */
            else if (somma > disp) {
                int manca = somma - disp;
                if (cede <= manca) w = minimo[k];
                else w = pref[k] - (int)((long)manca * (pref[k] - minimo[k]) / cede);
            }
            if (w < 1) w = 1;
            larg[k - i] = w;
        }

        /* justify-content: dove va lo spazio che nessuno si e' preso */
        libero = disp;
        for (k = 0; k < cnt; k++) libero -= larg[k];
        if (libero > 0) {
            switch (mio->giustifica) {
            case CSS_GIU_FINE:    prima = libero; break;
            case CSS_GIU_CENTRO:  prima = libero / 2; break;
            case CSS_GIU_TRA:     fra = cnt > 1 ? libero / (cnt - 1) : 0; break;
            case CSS_GIU_INTORNO: fra = libero / cnt; prima = fra / 2; break;
            case CSS_GIU_UGUALE:  fra = libero / (cnt + 1); prima = fra; break;
            default: break;
            }
        }
        disp += spazi;

        x = x0 + prima;
        for (k = i; k < j; k++) {
            int alt = 0;

            pz[k - i] = g_pez_n; sf[k - i] = g_sfondi_n;
            impagina_in_colonna_ex(voce[k], mio, x, y, larg[k - i], g_misura ? 2 : 0, &alt, 1);
            alti[k - i] = alt;
            x += larg[k - i] + gcol + fra;
            if (alt > alto) alto = alt;
        }
        pz[cnt] = g_pez_n; sf[cnt] = g_sfondi_n;

        /* align-items: chi e' piu' basso della riga scende di quanto serve.
         * I pezzi sono gia' posati: si spostano, non si rifanno. */
        if (mio->allinea_voci == CSS_ALV_CENTRO || mio->allinea_voci == CSS_ALV_FINE) {
            for (k = 0; k < cnt; k++) {
                int giu = alto - alti[k];
                int q, dq = (mio->allinea_voci == CSS_ALV_CENTRO) ? giu / 2 : giu;

                for (q = pz[k]; q < pz[k + 1]; q++) g_pez[q].y += dq;
                for (q = sf[k]; q < sf[k + 1]; q++) g_sfondi[q].y += dq;
            }
        }
        y += alto;
        i = j;
        if (i < n) y += grig;
    }

    g_pen_y = y;
    g_pen_x = rx();
    g_riga_h = alt_riga_f(font_di(mio));
    g_riga_primo = g_pez_n;
}

/* ! IL TESTO «SOLO PER I LETTORI DI SCHERMO» NON SI MOSTRA. I siti lo
 * scrivono in posizione assoluta fuori dallo schermo (left: -9999px) o in un
 * riquadro di un pixel: per chi guarda non c'e'. Impaginato nel flusso — come
 * si faceva — erano righe di «Salta al contenuto» in cima a ogni pagina. */
static int nascosto_posizione(const CssStile *s)
{
    int i;

    if (s->posizione != CSS_POS_ASSOLUTA && s->posizione != CSS_POS_FISSA) return 0;
    for (i = 0; i < 4; i++)
        if (s->pos[i] != CSS_MISURA_NO && s->pos[i] <= -1000) return 1;
    if (s->larghezza != CSS_MISURA_NO && !(s->larghezza_perc & CSS_LARG_PERC) &&
        s->larghezza <= 1) return 1;
    return 0;
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
        g_nodo_ora  = g_doc.nodi[v].padre;
        parole(html_testo(&g_doc, v), g_doc.nodi[v].testo);
        return;
    }

    {
        const char *nome = html_nome(&g_doc, v);
        int         era_link = g_link_ora;
        int         era_sx = g_marg_sx, era_dx = g_marg_dx;
        int         sfondo_mio = -1;
        int         alt_cima   = -1;        /* dove comincia il contenuto del blocco */
        int         bt = 0, br = 0, bb = 0, bl = 0, pb = 0;   /* bordi e padding di sotto */
        int         i_sx = -1, i_dx = -1, box_x = 0, box_w = 0, box_y = 0;
        unsigned int col_sotto = 0;
        CssStile    mio;
        int         e_blocco;
        int         radice = (v == g_col_radice);
        int         segno_mio = 2;      /* list-style di QUESTO elemento */

        if (invisibile(nome)) return;
        if (radice) g_col_radice = -1;      /* i figli sono figli qualunque */

        css_calcola(&g_css, &g_doc, v, ered, &mio);
        /* Si legge SUBITO: il prossimo css_calcola (un figlio) lo cambia. */
        segno_mio = css_segno_lista();
        suggerimenti(v, &mio);

        /* ! `display: none` TOGLIE ANCHE I FIGLI, e va fatto qui prima di
         * qualunque altra cosa: e' cosi' che i siti veri nascondono i menu che
         * si aprono col mouse. Impaginarli lo stesso vorrebbe dire una pagina
         * piena di voci che non dovrebbero vedersi. */
        if (mio.display == CSS_DISPLAY_NIENTE) return;
        /* ! E `visibility: hidden` LO STESSO, pur tenendo il posto per la
         * specifica (28 settembre 2026): sui siti veri e' il modo dei menu a
         * comparsa, in posizione assoluta — Vector 2022 li nasconde cosi', e
         * li mostra con `:checked ~`. Tenere un buco al loro posto sarebbe
         * peggio che non tenerlo. */
        if (mio.visibile == 0) return;

        if (nascosto_posizione(&mio)) return;

        /* ! position: relative SPOSTA QUEL CHE HA GIA' IMPAGINATO, e il posto
         * resta dov'era: e' la definizione del CSS. Si impagina l'elemento
         * normalmente — una seconda volta dentro impagina_nodo, riconosciuta
         * da g_rel_nodo — e poi si spostano i suoi pezzi. */
        if (mio.posizione == CSS_POS_RELATIVA && v != g_rel_nodo &&
            (mio.pos[0] != CSS_MISURA_NO || mio.pos[1] != CSS_MISURA_NO ||
             mio.pos[2] != CSS_MISURA_NO || mio.pos[3] != CSS_MISURA_NO)) {
            int pz = g_pez_n, sf = g_sfondi_n, era = g_rel_nodo;
            int dx = mio.pos[3] != CSS_MISURA_NO ? mio.pos[3] :
                     mio.pos[1] != CSS_MISURA_NO ? -mio.pos[1] : 0;
            int dy = mio.pos[0] != CSS_MISURA_NO ? mio.pos[0] :
                     mio.pos[2] != CSS_MISURA_NO ? -mio.pos[2] : 0;

            if (radice) g_col_radice = v;       /* la seconda volta lo deve rivedere */
            g_rel_nodo = v;
            impagina_nodo(v, ered);
            g_rel_nodo = era;
            sposta_da(pz, sf, dx, dy);
            return;
        }

        /* ! PRIMA DI g_stile_ora = mio: a_capo chiude la riga del PADRE, con
         * l'allineamento del padre. */
        if (!radice && mio.galleggia != CSS_GALLEGGIA_NO) { galleggia(v, &mio, ered); return; }
        if (!radice && mio.display == CSS_DISPLAY_INBLOCCO) { in_blocco(v, &mio, ered); return; }

        /* La radice di una colonna e' un blocco, e i suoi margini verticali
         * predefiniti non si aggiungono: il posto l'ha gia' deciso chi l'ha
         * messa li'. */
        if (radice) {
            if (mio.margine[0] == CSS_MISURA_NO) mio.margine[0] = 0;
            if (mio.margine[2] == CSS_MISURA_NO) mio.margine[2] = 0;
        }

        g_stile_ora = mio;

        /* ! LA TABELLA HA UNA STRADA SUA, e va presa PRIMA della logica dei
         * blocchi: quella impagina i figli uno dietro l'altro, che e'
         * esattamente cio' che una tabella non deve fare. */
        if (uguale(nome, "table")) {
            int sf = -1;

            spazio_blocco(0);

            /* ! LO SFONDO DELLA TABELLA E' SUO, NON DELLE CELLE, e va segnato
             * qui: la strada delle tabelle salta tutta la logica dei blocchi,
             * e con lei il riquadro di sfondo. E' il caso della barra
             * arancione di Hacker News, che e' un `bgcolor` sulla <table> —
             * non sulle celle. L'altezza si rimette quando la tabella e'
             * finita, come per i blocchi. */
            if (mio.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                sf = g_sfondi_n++;
                g_sfondi[sf].x = rx();
                g_sfondi[sf].y = g_pen_y;
                g_sfondi[sf].w = rw();
                g_sfondi[sf].h = 0;
                g_sfondi[sf].colore = mio.sfondo;
                g_sfondi[sf].bordo  = 0;   /* ! la voce si riusa: senza, restava il contorno di una tabella di prima */
                g_sfondi[sf].imm    = -1;
            }

            impagina_tabella(v, &mio);

            if (sf >= 0) {
                g_sfondi[sf].h = g_pen_y - g_sfondi[sf].y;
                if (g_sfondi[sf].h < 1) g_sfondi[sf].h = 1;
            }

            g_link_ora = era_link;
            spazio_blocco(2);
            return;
        }

        if (uguale(nome, "br")) { g_pen_x = rx() + 1; a_capo(); return; }

        /* ! <hr> E' UNA RIGA, non uno stacco piu' grande: finora era solo un
         * blocco vuoto, cioe' un po' d'aria in mezzo alla pagina — e chi
         * scrive <hr> vuole vedere il segno che separa. Si disegna come uno
         * sfondo alto due pixel, perche' e' esattamente cio' che e'. */
        if (uguale(nome, "hr")) {
            spazio_blocco(0);
            if (g_sfondi_n < SFONDI_MAX) {
                g_sfondi[g_sfondi_n].x = rx();
                g_sfondi[g_sfondi_n].y = g_pen_y + 2;
                g_sfondi[g_sfondi_n].w = rw();
                g_sfondi[g_sfondi_n].h = 2;
                g_sfondi[g_sfondi_n].colore = EX_SHADOW;
                g_sfondi_n++;
            }
            g_pen_y += 6;
            g_riga_h = alt_riga_f(font_di(&mio));
            g_riga_primo = g_pez_n;
            spazio_blocco(2);
            return;
        }

        /* =====================================================================
         * E QUI SI CHIEDE, invece di sapere.
         *
         * ! IL POSTO NON E' INDIFFERENTE, ed e' quello dei controlli di prima:
         * DOPO css_calcola, dopo `display: none`, dopo le tabelle e dopo <br>
         * e <hr>. Chiedere prima vorrebbe dire un <input> con `display: none`
         * impaginato lo stesso, e il testo di ripiego di un'immagine — l'alt —
         * scritto con lo stile sbagliato, perche' `parole` prende g_stile_ora
         * e g_stile_ora e' appena stato messo.
         *
         * ! E LE QUATTRO RISPOSTE SONO UNA SOLA DOMANDA. VISTA_SALTA e'
         * l'elemento che non c'e' (un campo nascosto, un <noscript> con gli
         * script accesi): niente pezzo e nemmeno i figli. VISTA_TESTO e' il
         * ripiego di un pezzo che non e' arrivato, e si impagina con le parole
         * di tutto il resto. VISTA_PEZZO e' un rettangolo che l'impaginato
         * colloca senza sapere che cosa contenga. VISTA_NIENTE e' «non e' roba
         * mia», e si tira dritto.
         * ===================================================================== */
        if (g_cliente && g_cliente->misura) {
            VistaPezzo p;
            int        r;

            p.w = p.h = p.rif = 0;
            p.nel_flusso = p.stringi = p.aria_dx = p.aria_giu = 0;
            p.prova = g_misura;
            p.larg_css = 0;
            if (mio.larghezza != CSS_MISURA_NO) {
                int lw = larg_risolta(&mio, rw(), 0);
                p.larg_css = lw > 0 ? lw : 0;
            }
            p.testo = 0;
            p.testo_off = 0;

            r = g_cliente->misura(v, &p);

            /* ! CHI STA NEL FLUSSO DIVENTA IL NODO CORRENTE, e vale anche
             * quando non si impagina niente: e' cosi' che un'immagine dice
             * «event.target qui sono io» al pallino di un elenco che venga
             * dopo. Un controllo non lo fa, e non lo faceva nemmeno prima. */
            if (r != VISTA_NIENTE && p.nel_flusso) g_nodo_ora = v;

            if (r == VISTA_SALTA) return;
            if (r == VISTA_TESTO) {
                if (p.testo && p.testo[0]) parole(p.testo, p.testo_off);
                return;
            }
            if (r == VISTA_PEZZO) {
                /* ! LA LARGHEZZA DEL CSS VALE ANCHE PER UN CONTROLLO (28
                 * settembre 2026): la casella di ricerca di Wikipedia e'
                 * `width: 100%`, e restava di venti caratteri. Solo per i
                 * pezzi che si stringono (i controlli): un'immagine ha le
                 * sue misure, e allargarla la deformerebbe. */
                if (p.stringi) {
                    int mn = larg_minima(&mio, rw(), 0);
                    /* ! width decide, max-width e min-width limitano soltanto:
                     * il pulsante «Ricerca» ha solo un max-width, e preso
                     * per larghezza diventava largo 420 pixel. */
                    if (mio.larghezza != CSS_MISURA_NO) {
                        int w = larg_risolta(&mio, rw(), 0);
                        if (w > 0) p.w = w;
                    } else if (mio.larghezza_max != CSS_MISURA_NO) {
                        int mx = (mio.larghezza_perc & CSS_LARG_MAX_PERC)
                                 ? (int)((long)rw() * mio.larghezza_max / 10000) : mio.larghezza_max;
                        if (mx > 0 && p.w > mx) p.w = mx;
                    }
                    if (mn > 0 && p.w < mn) p.w = mn;
                }
                pezzo_estraneo(v, &p);
                return;
            }
        }

        /* L'elenco dei blocchi resta la regola di base; `display` la
         * sovrascrive nei due versi, che e' a cosa serve. */
        e_blocco = blocco(nome);
        if (mio.display == CSS_DISPLAY_BLOCCO) e_blocco = 1;
        if (mio.display == CSS_DISPLAY_INLINE) e_blocco = 0;
        if (mio.display == CSS_DISPLAY_FLEX || radice) e_blocco = 1;

        if (e_blocco) {
            if (mio.pulisci) { a_capo(); pulisci(mio.pulisci); }
            spazio_blocco(0);

            /* ! I RIENTRI SI SOMMANO A QUELLI DI FUORI: un blocco dentro un
             * altro rientra due volte, ed e' cosi' che si vedono le citazioni
             * annidate. Valgono solo sui blocchi — un margine su un pezzo di
             * testo in linea non ha un lato a cui attaccarsi. */
            g_marg_sx += css_margine(&mio, 3, 0);
            g_marg_dx += css_margine(&mio, 1, 0);
            if (g_marg_sx < 0) g_marg_sx = 0;
            if (g_marg_dx < 0) g_marg_dx = 0;

            /* ! LA LARGHEZZA E I MARGINI auto (28 settembre 2026): un blocco
             * con width o max-width piu' stretto della riga si stringe, e lo
             * spazio che avanza va ai margini auto — metta' e meta' e' il
             * centraggio (`margin: 0 auto`), tutto a sinistra lo spinge a
             * destra. Senza auto resta a sinistra. La radice di una colonna
             * la sua larghezza l'ha gia'. */
            if (!radice) {
                int disp = rw(), w = larg_risolta(&mio, disp, lati_extra(&mio));

                /* ! min-width PIU' LARGO DELLA RIGA: il blocco sborda a destra
                 * invece di stringersi (e' la regola del CSS). */
                if (w < 0) {
                    int mn = larg_minima(&mio, disp, lati_extra(&mio));
                    if (mn > disp) g_marg_dx -= mn - disp;
                }
                if (w >= 0 && w < disp) {
                    int avanzo = disp - w, sposta = 0;
                    int a_sx = mio.margine[3] == CSS_MISURA_AUTO;
                    int a_dx = mio.margine[1] == CSS_MISURA_AUTO;

                    if (a_sx && a_dx) sposta = avanzo / 2;
                    else if (a_sx)    sposta = avanzo;
                    g_marg_sx += sposta;
                    g_marg_dx += avanzo - sposta;
                }
            }

            g_pen_x = rx();
            g_riga_primo = g_pez_n;

            alt_cima = g_pen_y;         /* da qui si misura `height` */

            /* ! UN RIQUADRO DI SFONDO ANCHE PER LA SOLA IMMAGINE (10 ottobre
             * 2026): un logo o un'icona sono quasi sempre un elemento senza
             * colore e senza testo, con un'immagine di sfondo e una misura. */
            if ((mio.sfondo != CSS_NIENTE || mio.sf_url) && g_sfondi_n < SFONDI_MAX) {
                sfondo_mio = g_sfondi_n++;
                g_sfondi[sfondo_mio].x = rx();
                g_sfondi[sfondo_mio].y = g_pen_y;
                g_sfondi[sfondo_mio].w = rw();
                g_sfondi[sfondo_mio].h = 0;
                g_sfondi[sfondo_mio].colore = mio.sfondo;
                g_sfondi[sfondo_mio].bordo  = 0;   /* ! la voce si riusa: senza, restava il contorno di una tabella di prima */
                g_sfondi[sfondo_mio].imm    = -1;
                if (mio.sf_url && g_cliente && g_cliente->sfondo) {
                    /* cover e 100% vogliono l'immagine larga quanto il riquadro */
                    int larg = (mio.sf_mw == CSS_SF_COPRE || mio.sf_mw == -200) ? rw()
                             : (mio.sf_mw > 0 ? mio.sf_mw : 0);

                    g_sfondi[sfondo_mio].imm     = (short)g_cliente->sfondo(v, &mio, larg);
                    g_sfondi[sfondo_mio].imm_rip = mio.sf_ripeti;
                    g_sfondi[sfondo_mio].imm_px  = mio.sf_px;
                    g_sfondi[sfondo_mio].imm_py  = mio.sf_py;
                }
            }

            /* I BORDI E IL PADDING (@NAV-BORDER, 28 settembre 2026). Il
             * riquadro e' quello dello sfondo; i lati sono rettangoli pieni
             * della stessa lista, e il contenuto rientra di bordo piu' padding
             * come rientra dei margini. Quelli di sinistra e di destra hanno
             * l'altezza solo a fine blocco, quello di sotto nasce li'. */
            bt = lato_bordo(&mio, 0); br = lato_bordo(&mio, 1);
            bb = lato_bordo(&mio, 2); bl = lato_bordo(&mio, 3);
            box_x = rx(); box_w = rw(); box_y = g_pen_y;
            if (bt) rettangolo(box_x, box_y, box_w, bt, colore_bordo(&mio, 0));
            if (bl) i_sx = rettangolo(box_x, box_y, bl, 0, colore_bordo(&mio, 3));
            if (br) i_dx = rettangolo(box_x + box_w - br, box_y, br, 0, colore_bordo(&mio, 1));
            col_sotto = colore_bordo(&mio, 2);
            pb = imbottitura(&mio, 2);
            g_marg_sx += bl + imbottitura(&mio, 3);
            g_marg_dx += br + imbottitura(&mio, 1);
            g_pen_y   += bt + imbottitura(&mio, 0);
            g_pen_x = rx();
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

            /* ! `list-style: none` TOGLIE IL SEGNO (4 ottobre 2026). I menu
             * dei siti veri sono elenchi senza segno — e Tailwind lo toglie a
             * TUTTI gli elenchi della pagina — e qui ogni voce di menu usciva
             * con un trattino davanti. Si eredita: se la voce non dice niente
             * vale quel che dice la lista che la contiene. */
            if (su >= 0) {
                CssStile della_lista;
                int      primo = g_doc.nodi[su].primo_figlio;

                css_calcola(&g_css, &g_doc, su, 0, &della_lista);
                if (segno_mio == 2) segno_mio = css_segno_lista();

                /* Le voci di un elenco in fila (display: flex) senza segno:
                 * il sito le stacca con un `gap` o con dei margini che qui
                 * non sempre ci sono, e due voci attaccate si leggono come
                 * una parola. Uno spazio davanti a ogni voce dopo la prima. */
                while (primo >= 0 && g_doc.nodi[primo].tipo != HTML_ELEMENTO)
                    primo = g_doc.nodi[primo].prossimo;
                if (segno_mio == 0 && della_lista.display == CSS_DISPLAY_FLEX &&
                    !della_lista.flex_colonna && primo != v)
                    g_pen_x += ex_text_width(font_di(&mio), "  ");
            }

            if (segno_mio == 0) {
                /* niente segno, e niente spazio per lui */
            } else if (numerata) {
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
                {
                    unsigned int off = genera(seg);

                    if (off != GENERA_NIENTE) parola(seg, off, q);
                }
            } else {
                /* Un pallino, non un asterisco: e' il segno che ci si aspetta,
                 * e il carattere c'e' in tutte le facce Liberation. */
                {
                    unsigned int off = genera("-");

                    if (off != GENERA_NIENTE) parola("-", off, 1);
                }
            }
            if (segno_mio != 0) g_pen_x += ex_text_width(font_di(&mio), " ");
        }

        if (uguale(nome, "a")) {
            const char *h = html_attr(&g_doc, v, "href");

            if (h && h[0] && (unsigned int)g_link_n < g_link_max) {
                unsigned int k = 0;

                /* ! SE L'ARENA E' PIENA IL LINK NON SI SCRIVE A META'. Un
                 * indirizzo troncato porta da un'altra parte, e «da un'altra
                 * parte» in un browser vuol dire una pagina sbagliata senza
                 * un errore. Si smette di raccoglierli e basta. */
                while (h[k]) k++;
                if (g_link_usati + k + 1 <= g_link_arena_max) {
                    g_link_off[g_link_n] = g_link_usati;
                    for (k = 0; h[k]; k++)
                        g_link_arena[g_link_usati + k] = h[k];
                    g_link_arena[g_link_usati + k] = '\0';
                    g_link_usati += k + 1;
                    /* ! target: _self, _top e _parent restano qui; _blank e
                     * ogni altro nome aprono una finestra nuova. Un nome gia'
                     * usato dovrebbe riusare la sua finestra: qui ne apre
                     * sempre un'altra, e sta scritto. */
                    {
                        const char *t = html_attr(&g_doc, v, "target");
                        g_link_nuova[g_link_n] = (t && t[0] && !uguale(t, "_self") &&
                                                  !uguale(t, "_top") && !uguale(t, "_parent"));
                    }
                    g_link_ora = g_link_n++;
                }
            }
        }

        if (uguale(nome, "pre") || uguale(nome, "code") ||
            uguale(nome, "tt")  || uguale(nome, "kbd") ||
            uguale(nome, "samp")) g_fisso++;

        /* ! I MARGINI DI UN ELEMENTO IN LINEA SPOSTANO LA PENNA, NON IL
         * BLOCCO. Su un blocco un margine e' un rientro del lato — e quello si
         * fa piu' su, con g_marg_sx e g_marg_dx. Su uno <span> non c'e' nessun
         * lato a cui attaccarsi: il margine e' spazio orizzontale prima e dopo
         * il testo, ed e' proprio cosi' che i siti separano le voci di un
         * menu. Senza, quelle voci si toccano e sembrano una parola sola.
         *
         * ! I MARGINI VERTICALI IN LINEA NON ESISTONO, e non e' una
         * semplificazione nostra: e' la regola del CSS. Un margine sopra e
         * sotto uno <span> non sposta niente. */
        if (!e_blocco && mio.margine[3] != CSS_MISURA_NO && mio.margine[3] > 0)
            g_pen_x += mio.margine[3];

        if (e_blocco && mio.display == CSS_DISPLAY_FLEX && !mio.flex_colonna) {
            flessibile(v, &mio);
            g_stile_ora = mio;
        } else
        for (f = g_doc.nodi[v].primo_figlio; f >= 0; f = g_doc.nodi[f].prossimo) {
            impagina_nodo(f, &mio);
            g_stile_ora = mio;      /* i figli l'hanno cambiato: si rimette */
        }

        if (!e_blocco && mio.margine[1] != CSS_MISURA_NO && mio.margine[1] > 0)
            g_pen_x += mio.margine[1];

        if (uguale(nome, "pre") || uguale(nome, "code") ||
            uguale(nome, "tt")  || uguale(nome, "kbd") ||
            uguale(nome, "samp")) g_fisso--;

        g_link_ora = era_link;

        if (e_blocco) {
            a_capo();
            /* ! `height` E `min-height` (10 ottobre 2026): un riquadro puo'
             * essere piu' alto di quel che contiene, e uno vuoto con
             * un'immagine di sfondo esiste solo cosi'. Si allunga, non si
             * accorcia: tagliare il contenuto vorrebbe `overflow`. */
            {
                int vuole = (mio.altezza != CSS_MISURA_NO && mio.altezza > 0) ? mio.altezza : 0;

                if (mio.altezza_min != CSS_MISURA_NO && mio.altezza_min > vuole) vuole = mio.altezza_min;
                if (vuole > 0 && alt_cima >= 0 && g_pen_y - alt_cima < vuole) g_pen_y = alt_cima + vuole;
            }
            /* Il fondo del riquadro: il contenuto, il padding di sotto, poi il
             * bordo di sotto. Solo se c'e' qualcosa da chiudere la penna scende:
             * un blocco senza bordi e padding resta esattamente com'era. */
            if (pb || bb || bt || bl || br) {
                g_pen_y += ((g_pen_x > rx()) ? g_riga_h : 0) + pb;
                if (bb) rettangolo(box_x, g_pen_y, box_w, bb, col_sotto);
                g_pen_y += bb;
                if (i_sx >= 0) g_sfondi[i_sx].h = g_pen_y - box_y;
                if (i_dx >= 0) g_sfondi[i_dx].h = g_pen_y - box_y;
            }
            if (sfondo_mio >= 0) {
                int fine = g_pen_y + ((g_pen_x > rx()) ? g_riga_h : 0);

                g_sfondi[sfondo_mio].h = fine - g_sfondi[sfondo_mio].y;
                if (g_sfondi[sfondo_mio].h < 1) g_sfondi[sfondo_mio].h = 1;
            }
            g_marg_sx = era_sx;
            g_marg_dx = era_dx;
            g_pen_x = rx();
            g_riga_primo = g_pez_n;
            spazio_blocco(2);
        }
    }
}

void impagina(void)
{
    int pagina_sf = -1;         /* il riquadro dello sfondo della pagina */

    g_pez_n = 0;
    g_link_n = 0;
    g_link_usati = 0;

    /* ! E CHI TIENE DEI PEZZI SUOI LI BUTTA ADESSO, nello stesso istante in cui
     * si buttano i nostri. Il perche' — e la giornata che l'ha insegnato — sta
     * accanto a est_azzera, in browser_estranei.c: e' una prova gia' pagata, e
     * si e' spostata insieme al codice che la riguarda. */
    if (g_cliente && g_cliente->azzera) g_cliente->azzera();
    g_fisso = 0;
    /* ! L'ARENA DEL DOCUMENTO NON SI RIAVVOLGE PIU', e la riga che lo faceva
     * era diventata un difetto il giorno che JavaScript ha cominciato a
     * scrivere li' dentro: vedi il commento esteso accanto a genera(). Quel
     * che l'impaginazione inventa sta in g_gen, che si azzera qui. */
    g_gen_n = 0;
    g_marg_sx = g_marg_dx = 0;
    g_riga_primo = 0;
    g_sfondi_n = 0;
    g_gal_n = 0;
    g_col_radice = -1;
    g_rel_nodo = -1;
    g_pen_x = area_x();
    g_pen_y = area_y();
    g_link_ora = -1;
    css_stile_vuoto(&g_stile_ora);
    g_riga_h = alt_riga_f(g_font_testo);

    /* ! LO SFONDO DI <body> E' LO SFONDO DELLA PAGINA (4 ottobre 2026), e non
     * del solo testo: e' la regola dei browser veri, dove il colore di body
     * riempie tutta la finestra. Qui <body> non e' nemmeno un blocco, e il suo
     * sfondo — `style`, una regola del foglio o il vecchio `bgcolor` — non si
     * dipingeva affatto: una pagina su fondo giallo usciva bianca. E' il
     * PRIMO riquadro, cosi' sta sotto a tutti gli altri; l'altezza si mette
     * in fondo, quando la pagina e' impaginata. Se body non dice niente vale
     * lo sfondo di <html>. */
    {
        static const char *const CHI[2] = { "body", "html" };
        unsigned int i;
        int k;

        for (k = 0; k < 2 && pagina_sf < 0; k++)
            for (i = 0; i < g_doc.nodi_n; i++) {
                CssStile sb;

                if (g_doc.nodi[i].tipo != HTML_ELEMENTO ||
                    !uguale(html_nome(&g_doc, (int)i), CHI[k])) continue;
                css_calcola(&g_css, &g_doc, (int)i, 0, &sb);
                suggerimenti((int)i, &sb);
                if (sb.sfondo != CSS_NIENTE && g_sfondi_n < SFONDI_MAX) {
                    pagina_sf = g_sfondi_n++;
                    g_sfondi[pagina_sf].x = area_x();
                    g_sfondi[pagina_sf].y = area_y();
                    g_sfondi[pagina_sf].w = area_w();
                    g_sfondi[pagina_sf].h = 0;
                    g_sfondi[pagina_sf].colore = sb.sfondo;
                    g_sfondi[pagina_sf].bordo  = 0;
                    g_sfondi[pagina_sf].imm    = -1;
                }
                break;
            }
    }

    {
        CssStile radice;

        css_stile_vuoto(&radice);
        impagina_nodo(g_doc.radice, &radice);
    }
    a_capo();

    g_altezza = g_pen_y - area_y() + g_riga_h;
    /* un float puo' scendere piu' giu' dell'ultima riga di testo */
    if (gal_fondo() - area_y() > g_altezza) g_altezza = gal_fondo() - area_y();
    if (g_altezza < 1) g_altezza = 1;

    /* Alto quanto la pagina, e almeno quanto la finestra. */
    if (pagina_sf >= 0)
        g_sfondi[pagina_sf].h = g_altezza > area_h() ? g_altezza : area_h();
}

/* =============================================================================
 * I SUGGERIMENTI DI PRESENTAZIONE DELL'HTML VECCHIO
 *
 * ! MEZZO WEB SCRIVE ANCORA I COLORI NEGLI ATTRIBUTI, e non nei fogli di
 * stile: `<table bgcolor="#ff6600">` e' la barra arancione di Hacker News, e
 * `<td align="right">` e' come si incolonnavano i numeri prima del CSS. Sono
 * chiamati «suggerimenti di presentazione» e stanno al gradino PIU' BASSO
 * della cascata: valgono solo dove il foglio di stile non ha detto niente.
 *
 * ! ED E' PROPRIO PERCHE' STANNO IN FONDO CHE SI APPLICANO DOPO css_calcola e
 * solo sui campi rimasti vuoti. Applicarli prima — o sopra — vorrebbe dire che
 * un `bgcolor` batte una regola CSS, che e' il contrario di quello che deve
 * succedere: una pagina moderna che ha ereditato un vecchio attributo si
 * vedrebbe con i colori di vent'anni fa.
 * ========================================================================== */
static void suggerimenti(int v, CssStile *st)
{
    const char *a;

    a = html_attr(&g_doc, v, "bgcolor");
    if (a && a[0] && st->sfondo == CSS_NIENTE) {
        unsigned int c;
        unsigned int n = 0;

        while (a[n]) n++;
        if (css_colore(a, n, &c)) st->sfondo = c;
    }

    /* `text` sta su <body> e su <font>, e il colore SI EREDITA: qui non c'e'
     * modo di sapere se il valore che c'e' viene da una regola o dal padre.
     * L'attributo vince — su una pagina moderna non c'e', e su una vecchia e'
     * quello che l'autore intendeva. */
    a = html_attr(&g_doc, v, "text");
    if (!a || !a[0]) a = html_attr(&g_doc, v, "color");
    if (a && a[0]) {
        unsigned int c;
        unsigned int n = 0;

        while (a[n]) n++;
        if (css_colore(a, n, &c)) st->colore = c;
    }

    /* ! E L'ALLINEAMENTO SI APPLICA SEMPRE, per la stessa ragione: anche lui
     * si eredita, quindi «vuoto» non si distingue da «ereditato». Un `align`
     * scritto sull'elemento e' un'intenzione esplicita di chi ha scritto la
     * pagina, e vale piu' di quella del padre. */
    a = html_attr(&g_doc, v, "align");
    if (a && a[0]) {
        if (uguale(a, "center"))     st->allineamento = CSS_ALL_CENTRO;
        else if (uguale(a, "right")) st->allineamento = CSS_ALL_DX;
        else if (uguale(a, "left"))  st->allineamento = CSS_ALL_SX;
    }
}

/* -----------------------------------------------------------------------------
 * Il disegno
 * --------------------------------------------------------------------------- */
void disegna(void)
{
    /* ! FROM AN IFRAME'S VIEW, THE WHOLE WINDOW: a timer or a click inside an
     * iframe ends in disegna() like anywhere else, and what must be redrawn is
     * the page that contains it. disegna_tutto() makes the page current and
     * comes back here. */
    if (g_vi->cornice) { disegna_tutto(); return; }

    /* ! I CONTROLLI SI RIDISEGNANO PRIMA DEL CONTENUTO, e non basta riempire
     * di grigio. Qui c'era un ex_fill_rect su tutta la finestra: dipingeva SOPRA
     * la casella dell'indirizzo e i pulsanti, che sono figli e stanno negli
     * stessi pixel. Il risultato era una barra sparita al primo disegno — e
     * siccome il primo disegno arriva subito, non si vedeva mai.
     *
     * ex_default_proc riempie il fondo E ridisegna i figli: e' la stessa
     * cosa in una chiamata, e resta giusta il giorno che si aggiunge un
     * pulsante. */
    schede_segui_titolo();              /* prima dei controlli: la linguetta */
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    disegna_barra();
    disegna_contenuto();
    ex_update(g_f);
}

/* Where drawing is cut: the document's area, or for an iframe the part of its
 * rectangle that is on screen (see tx..th in VistaImp). */
#define T_X()  (g_vi->cornice ? g_vi->tx : area_x())
#define T_Y()  (g_vi->cornice ? g_vi->ty : area_y())
#define T_W()  (g_vi->cornice ? g_vi->tw : area_w())
#define T_H()  (g_vi->cornice ? g_vi->th : area_h())

void disegna_contenuto(void)
{
    int i;

    if (g_vi->cornice) {
        ex_fill_rect(g_f, T_X(), T_Y(), T_W(), T_H(), EX_WHITE);
    } else {
        ex_fill_rect(g_f, T_X() - 2, T_Y() - 2, T_W() + 4, T_H() + 4,
                  EX_WHITE);
        ex_draw_sunken(g_f, T_X() - 2, T_Y() - 2, T_W() + 4, T_H() + 4);
    }

    /* ! GLI SFONDI PRIMA DI TUTTO IL RESTO, e ritagliati a mano all'area come
     * le immagini: ex_fill_rect ritaglia alla FINESTRA, non al documento. */
    for (i = 0; i < g_sfondi_n; i++) {
        int y = g_sfondi[i].y - g_scorri;
        int h = g_sfondi[i].h;

        if (y + h < T_Y() || y > T_Y() + T_H()) continue;
        if (y < T_Y()) { h -= T_Y() - y; y = T_Y(); }
        if (y + h > T_Y() + T_H()) h = T_Y() + T_H() - y;
        if (h <= 0) continue;

        if (!g_sfondi[i].bordo) {
            /* ! ANCHE DI LATO (28 settembre 2026): quel che sporge dalla
             * pagina non si vede, come in un browser senza scorrimento
             * orizzontale — e non finisce sopra la barra di scorrimento. */
            int x = g_sfondi[i].x, w = g_sfondi[i].w;
            if (x < T_X()) { w -= T_X() - x; x = T_X(); }
            if (x + w > T_X() + T_W()) w = T_X() + T_W() - x;
            if (w > 0 && g_sfondi[i].colore != CSS_NIENTE)
                ex_fill_rect(g_f, x, y, w, h, g_sfondi[i].colore);
            /* L'immagine sopra il colore, dentro lo stesso ritaglio. */
            if (w > 0 && g_sfondi[i].imm >= 0 && g_cliente && g_cliente->sfondo_disegna)
                g_cliente->sfondo_disegna(g_sfondi[i].imm,
                                          g_sfondi[i].x, g_sfondi[i].y - g_scorri,
                                          g_sfondi[i].w, g_sfondi[i].h,
                                          g_sfondi[i].imm_px, g_sfondi[i].imm_py,
                                          g_sfondi[i].imm_rip, x, y, w, h);
            continue;
        }

        /* ! IL CONTORNO SI FA DI QUATTRO RIEMPIMENTI, e i due orizzontali si
         * disegnano solo se il loro lato e' DENTRO l'area: il ritaglio qui
         * sopra ha gia' accorciato il rettangolo, quindi una tabella scorsa a
         * meta' avrebbe altrimenti una riga di bordo dove il bordo non c'e'. */
        {
            int b  = g_sfondi[i].bordo;
            int x  = g_sfondi[i].x, w = g_sfondi[i].w;
            int y0 = g_sfondi[i].y - g_scorri;
            int h0 = g_sfondi[i].h;

            ex_fill_rect(g_f, x, y, b, h, g_sfondi[i].colore);
            ex_fill_rect(g_f, x + w - b, y, b, h, g_sfondi[i].colore);
            if (y0 >= T_Y())
                ex_fill_rect(g_f, x, y0, w, b, g_sfondi[i].colore);
            if (y0 + h0 <= T_Y() + T_H())
                ex_fill_rect(g_f, x, y0 + h0 - b, w, b, g_sfondi[i].colore);
        }
    }

    for (i = 0; i < g_pez_n; i++) {
        int y  = g_pez[i].y - g_scorri;
        int ph = (g_pez[i].rif >= 0)
                 ? g_pez[i].h : ex_font_height(g_pez[i].font);

        /* ! SI DISEGNA SOLO CIO' CHE SI VEDE. Con una pagina di migliaia di
         * righe, dipingere tutto vorrebbe dire pagare l'intero documento a
         * ogni riga di scorrimento — e per il novantanove per cento fuori
         * dalla finestra. */
        if (y + ph < T_Y() || y > T_Y() + T_H()) continue;

        /* ! UN PEZZO ESTRANEO SI DISEGNA DA SE', e il ritaglio e' suo. Qui
         * non si sa se sporgere sia lecito: un'immagine alta trecento pixel
         * scorsa in su deve tagliarsi da sola, una casella di testo a meta'
         * non si deve disegnare affatto. Sono due risposte diverse alla stessa
         * domanda, e la domanda non e' dell'impaginato. */
        /* ! E DI LATO: un pezzo tutto fuori dalla pagina non si disegna.
         * Succede quando la pagina e' piu' larga della finestra — l'intestazione
         * di Wikipedia, pensata per 1280 pixel (le @media), disegnava i suoi
         * link sopra la barra di scorrimento. */
        if (g_pez[i].x >= T_X() + T_W() || g_pez[i].x + g_pez[i].w <= T_X()) continue;

        if (g_pez[i].rif >= 0) {
            if (g_cliente && g_cliente->disegna)
                g_cliente->disegna(g_pez[i].rif, g_pez[i].x, y,
                                   g_pez[i].w, g_pez[i].h);
            continue;
        }

        /* ! E UNA RIGA DI TESTO A META' NON SI DISEGNA AFFATTO, perche' non
         * c'e' un ritaglio. `ex_draw_text` taglia alla FINESTRA, non all'area del
         * documento: una riga che comincia sopra il bordo veniva dipinta SOPRA
         * LA BARRA DELL'INDIRIZZO, e una in fondo sopra la barra di stato. Si
         * vedeva appena il documento diventava piu' lungo della finestra —
         * cioe' proprio quando e' arrivata la barra di scorrimento. */
        if (y < T_Y() || y + ph > T_Y() + T_H()) continue;
        /* Una parola non si taglia a meta': se sporge, non c'e'. ! TRANNE
         * quella che comincia al margine sinistro: e' piu' larga della pagina
         * intera (un indirizzo lungo), non ha un posto dove stare, e si
         * disegna come sempre — sparire sarebbe peggio che sbordare. */
        if (g_pez[i].x < T_X() ||
            (g_pez[i].x + g_pez[i].w > T_X() + T_W() && g_pez[i].x > T_X() + 8)) continue;

        {
            ExFont       f = g_pez[i].font;
            const char  *t = testo_pezzo(g_pez[i].testo);
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
                /* The selected words: white on blue, the width of the word
                 * and the space after it (@NAV-SELEZIONE). Only the page's
                 * own view: an iframe keeps its own. */
                if (!g_vi->cornice && g_sel_da >= 0 && i >= g_sel_da && i <= g_sel_a) {
                    int sp = (i < g_sel_a && i + 1 < g_pez_n && g_pez[i + 1].y == g_pez[i].y)
                           ? g_pez[i + 1].x - g_pez[i].x : g_pez[i].w;
                    ex_fill_rect(g_f, g_pez[i].x, y, sp > 0 ? sp : g_pez[i].w, ph, EX_BLUE);
                    c = EX_WHITE;
                }
                ex_draw_text_font(g_f, f, g_pez[i].x, y, cop, c);

                /* ! UN COLLEGAMENTO SI SOTTOLINEA, e non basta il colore: su
                 * uno schermo a pochi colori il blu e il nero si distinguono
                 * male, e chi non li distingue non trova i collegamenti. */
                if (g_pez[i].link >= 0)
                    ex_fill_rect(g_f, g_pez[i].x, y + ex_font_height(f) - 2,
                              g_pez[i].w, 1, c);
            }
        }
    }

    /* ! THE TAB RING, dotted, as Firefox draws it: around every piece of the
     * stop that has the focus (a link can be several words). What the stop IS
     * the layout does not know — only a link number and an opaque `rif`,
     * compared as numbers. Pieces cut by the area's edge are skipped, as text
     * is above. */
    if (g_tab_link >= 0 || g_tab_rif >= 0) {
        for (i = 0; i < g_pez_n; i++) {
            int x, y, w, h, q;

            if (g_tab_link >= 0 ? g_pez[i].link != g_tab_link
                                : g_pez[i].rif != g_tab_rif) continue;
            h = (g_pez[i].rif >= 0) ? g_pez[i].h : ex_font_height(g_pez[i].font);
            x = g_pez[i].x - 2;
            y = g_pez[i].y - g_scorri - 1;
            w = g_pez[i].w + 4;
            h = h + 2;
            if (y < T_Y() || y + h > T_Y() + T_H()) continue;
            for (q = 0; q < w; q += 2) {
                ex_fill_rect(g_f, x + q, y, 1, 1, EX_BLACK);
                ex_fill_rect(g_f, x + q, y + h - 1, 1, 1, EX_BLACK);
            }
            for (q = 0; q < h; q += 2) {
                ex_fill_rect(g_f, x, y + q, 1, 1, EX_BLACK);
                ex_fill_rect(g_f, x + w - 1, y + q, 1, 1, EX_BLACK);
            }
        }
    }

}
