/* =============================================================================
 * lib/exfont/raster.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il rasterizzatore. Cosa fa e perche' cosi' sta in raster.h.
 *
 * Il giro e' sempre lo stesso, tre passi:
 *
 *   1. i comandi del glifo diventano SEGMENTI, in 26.6 e gia' in scala: le
 *      curve quadratiche si spezzano in spezzate;
 *   2. dai segmenti si ricava il riquadro;
 *   3. si riempie riga per riga, sedici campioni per riga.
 * ============================================================================= */

#include "raster.h"

/* ! I BUFFER SONO STATICI E CON UN TETTO, e il tetto non e' un'ipotesi sui
 * font: e' cio' che rende il consumo di memoria di questa libreria NOTO. Un
 * glifo che sfora viene disegnato incompleto invece di far crescere qualcosa
 * senza limite — e un glifo cosi' non esiste in nessun font vero: la 'B' di
 * Liberation ne usa 29. */
#define COMANDI_MAX     1024
#define SEGMENTI_MAX    4096
#define INCROCI_MAX     128

/* Quanti campioni in verticale dentro un pixel. Vedi raster.h. */
#define SOTTO           16

/* Quanto puo' essere fine una curva prima di smettere di spezzarla: un quarto
 * di pixel in 26.6. Piu' fine non si vedrebbe, piu' grosso si vede. */
#define PIATTO          16

/* Il tetto alla suddivisione di una curva. Serve solo a chiudere il caso di
 * una curva degenere in cui la distanza non scende mai. */
#define GIU_MAX         8

typedef struct { int x0, y0, x1, y1; } Segmento;

/* -----------------------------------------------------------------------------
 * Passare fra pixel e 26.6, su numeri CON SEGNO
 *
 * ! `x << 6` SU UN NEGATIVO E' COMPORTAMENTO INDEFINITO, non un modo brutto di
 * scrivere una moltiplicazione. Su x86 con GCC fa quello che ci si aspetta, ed
 * e' esattamente il genere di cosa che funziona per anni e poi cambia con
 * un'ottimizzazione. E i negativi qui ci sono davvero: una 'j' comincia a
 * SINISTRA della penna, e un glifo tutto sotto la linea di base ha la cima
 * negativa. Trovato da UndefinedBehaviorSanitizer, non leggendo.
 *
 * ! E LA DIVISIONE DEVE ARROTONDARE VERSO IL BASSO ANCHE SUI NEGATIVI. In C la
 * divisione fra interi tronca verso lo ZERO: -1/64 fa 0, non -1. Per un bordo
 * a -0,5 pixel vorrebbe dire un riquadro che comincia a 0 e taglia via mezza
 * lettera. Lo spostamento a destra su un negativo fa la cosa giusta ma e'
 * definito dall'implementazione: qui si scrive il caso a mano, e si vede.
 * --------------------------------------------------------------------------- */
static int a_266(int pixel)     { return pixel * 64; }

static int giu_266(int v)       /* verso il basso, sempre */
{
    return (v >= 0) ? (v / 64) : -(((-v) + 63) / 64);
}

static int su_266(int v)        /* verso l'alto, sempre */
{
    return -giu_266(-v);
}

static TtfComando g_cmd[COMANDI_MAX];
static Segmento   g_seg[SEGMENTI_MAX];
static int        g_n_seg;

/* Gli incroci di una sotto-riga con i segmenti: la x e il verso. */
static int g_ix[INCROCI_MAX];
static int g_iv[INCROCI_MAX];

/* -----------------------------------------------------------------------------
 * La scala: da unita' del font a 26.6
 *
 * ! NON ESISTE LA DIVISIONE A 64 BIT, e non e' una preferenza: una libreria di
 * EX-OS si collega senza libgcc, quindi `__divdi3` non c'e' e il collegamento
 * FALLISCE. E' lo stesso muro contro cui era andato tsc.c. Le moltiplicazioni
 * lunghe invece vanno bene — sono due istruzioni, non una chiamata — e infatti
 * in_scala() ne usa una.
 *
 * ! QUESTO CONTO STA IN 32 BIT PERCHE' IL CORPO HA UN TETTO. Con
 * RASTER_CORPO_MAX a 256 il numeratore vale al massimo 256*64*65536, cioe'
 * esattamente 2^30: dentro un intero con segno, con un bit di margine. E' il
 * primo dei due posti in cui il tetto sul corpo non e' una comodita' ma
 * l'ipotesi che rende il conto sicuro.
 * --------------------------------------------------------------------------- */
static int scala_di(const TtfFont *f, int corpo)
{
    /* pixel per unita', in 16.16, moltiplicato per 64 perche' l'uscita e' 26.6 */
    return (int)(((unsigned int)corpo * 64u * 65536u) / (unsigned int)f->unita_em);
}

/* ! LE COORDINATE IN SCALA SI LIMITANO, ed e' il secondo posto. L'incrocio di
 * una sotto-riga con un segmento moltiplica due differenze fra coordinate:
 * senza un tetto NOTO su quelle, il prodotto non si puo' dire che stia in 32
 * bit — e un traboccamento li' non da' un errore, da' una lettera con un
 * pezzo che sbuca dall'altra parte dello schermo.
 *
 * Trentamila sessantaquattresimi sono 468 pixel: con il corpo massimo di 256
 * vuol dire un glifo largo o alto quasi due volte il suo quadratone. Nessun
 * font vero ci arriva; un font costruito apposta si vede tagliare il glifo,
 * che e' il modo giusto di reagire a un file assurdo. */
#define COORD_MAX   30000

static int limita(int v)
{
    if (v >  COORD_MAX) return  COORD_MAX;
    if (v < -COORD_MAX) return -COORD_MAX;
    return v;
}

/* ! SI ARROTONDA, NON SI TRONCA, E LA DIFFERENZA E' UN PIXEL INTERO. Il
 * risultato e' in sessantaquattresimi: troncare butta via meno di un
 * sessantaquattresimo di pixel, che sembra nulla — ma quando il bordo alto di
 * un glifo cade a 704,5 sessantaquattresimi, troncare da' esattamente 11,000
 * pixel e l'arrotondamento all'insu' del riquadro NON aggiunge la riga che
 * servirebbe. Il sintomo era che a 16 pixel quasi ogni MAIUSCOLA veniva alta
 * un pixel meno che in FreeType: sistematico, sempre lo stesso, e invisibile
 * finche' non si e' confrontato con qualcun altro. Mezzo in piu' prima dello
 * spostamento e' tutto il rimedio. */
static int in_scala(int unita, int scala)
{
    return limita((int)((((long long)unita * (long long)scala) + 32768) >> 16));
}

/* -----------------------------------------------------------------------------
 * Appiattire
 * --------------------------------------------------------------------------- */
static void aggiungi_seg(int x0, int y0, int x1, int y1)
{
    /* ! I SEGMENTI ORIZZONTALI NON SERVONO E SI BUTTANO. Non incrociano mai
     * una sotto-riga (che e' orizzontale anche lei), e tenerli vorrebbe dire
     * un caso da trattare nel ciclo degli incroci: una divisione per y1-y0
     * uguale a zero. Buttarli qui e' cio' che rende quel ciclo privo di casi
     * particolari. */
    if (y0 == y1) return;
    if (g_n_seg >= SEGMENTI_MAX) return;

    g_seg[g_n_seg].x0 = x0; g_seg[g_n_seg].y0 = y0;
    g_seg[g_n_seg].x1 = x1; g_seg[g_n_seg].y1 = y1;
    g_n_seg++;
}

/* Una curva quadratica, spezzata finche' non e' abbastanza dritta.
 *
 * ! LA PIATTEZZA SI MISURA SUL PUNTO DI CONTROLLO, non sul numero di passi. Un
 * numero fisso di passi spreca lavoro sulle curve piccole e fa gli angoli su
 * quelle grandi; la distanza fra il controllo e la corda dice esattamente
 * quanto quella curva si scosta da una retta, che e' la cosa che si vuole
 * sapere. */
static void curva(int x0, int y0, int cx, int cy, int x1, int y1, int giu)
{
    int dx = ((x0 + x1) >> 1) - cx;
    int dy = ((y0 + y1) >> 1) - cy;
    int d  = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);

    if (d <= PIATTO || giu >= GIU_MAX) {
        aggiungi_seg(x0, y0, x1, y1);
        return;
    }

    /* Suddivisione di de Casteljau: due meta', tutte con spostamenti di bit. */
    {
        int ax = (x0 + cx) >> 1, ay = (y0 + cy) >> 1;
        int bx = (cx + x1) >> 1, by = (cy + y1) >> 1;
        int mx = (ax + bx) >> 1, my = (ay + by) >> 1;

        curva(x0, y0, ax, ay, mx, my, giu + 1);
        curva(mx, my, bx, by, x1, y1, giu + 1);
    }
}

/* Da comandi a segmenti. Rende 0 se il glifo non ha contorni. */
static int appiattisci(const TtfFont *f, unsigned int glifo, int corpo)
{
    int n_cmd, i, scala;
    int px = 0, py = 0;         /* la penna */
    int sx = 0, sy = 0;         /* l'inizio del contorno aperto */

    g_n_seg = 0;

    n_cmd = ttf_contorno(f, glifo, g_cmd, COMANDI_MAX);
    if (n_cmd <= 0) return 0;

    scala = scala_di(f, corpo);

    for (i = 0; i < n_cmd; i++) {
        int x = in_scala(g_cmd[i].x, scala);
        int y = in_scala(g_cmd[i].y, scala);

        switch (g_cmd[i].tipo) {
        case TTF_MOSSA:
            px = sx = x; py = sy = y;
            break;

        case TTF_LINEA:
            aggiungi_seg(px, py, x, y);
            px = x; py = y;
            break;

        case TTF_CURVA: {
            int cx = in_scala(g_cmd[i].cx, scala);
            int cy = in_scala(g_cmd[i].cy, scala);

            curva(px, py, cx, cy, x, y, 0);
            px = x; py = y;
            break;
        }

        case TTF_CHIUDI:
        default:
            /* ! IL CONTORNO SI CHIUDE SEMPRE, anche quando il file non lo
             * dice. Un contorno aperto lascerebbe un bordo senza il suo
             * compagno, e il conteggio non nullo colerebbe fuori dal glifo:
             * il sintomo sarebbe mezza riga di schermo dipinta. */
            aggiungi_seg(px, py, sx, sy);
            px = sx; py = sy;
            break;
        }
    }

    return g_n_seg;
}

/* -----------------------------------------------------------------------------
 * Il riquadro
 * --------------------------------------------------------------------------- */
static void riquadro(int *x0, int *y0, int *x1, int *y1)
{
    int i;

    *x0 = *y0 = 0x7FFFFFF;
    *x1 = *y1 = -0x7FFFFFF;

    for (i = 0; i < g_n_seg; i++) {
        int a;

        a = g_seg[i].x0 < g_seg[i].x1 ? g_seg[i].x0 : g_seg[i].x1;
        if (a < *x0) *x0 = a;
        a = g_seg[i].x0 > g_seg[i].x1 ? g_seg[i].x0 : g_seg[i].x1;
        if (a > *x1) *x1 = a;

        a = g_seg[i].y0 < g_seg[i].y1 ? g_seg[i].y0 : g_seg[i].y1;
        if (a < *y0) *y0 = a;
        a = g_seg[i].y0 > g_seg[i].y1 ? g_seg[i].y0 : g_seg[i].y1;
        if (a > *y1) *y1 = a;
    }
}

int raster_misura(const TtfFont *f, unsigned int glifo, int corpo,
                  RasterMisure *m)
{
    int x0, y0, x1, y1;

    if (!f || !m) return 0;
    if (corpo < RASTER_CORPO_MIN || corpo > RASTER_CORPO_MAX) return 0;
    if (glifo >= f->n_glifi) return 0;

    m->larghezza = m->altezza = m->sinistra = m->cima = 0;
    m->avanzamento = in_scala((int)ttf_avanzamento(f, glifo), scala_di(f, corpo));

    if (!appiattisci(f, glifo, corpo)) return 1;    /* vuoto: lo spazio */

    riquadro(&x0, &y0, &x1, &y1);

    /* Dal 26.6 ai pixel, allargando ai bordi interi: un bordo che cade a meta'
     * di un pixel deve avere quel pixel dentro il riquadro, o la sua copertura
     * finirebbe fuori e il glifo apparirebbe tagliato di un pixel. */
    x0 = giu_266(x0);
    y0 = giu_266(y0);
    x1 = su_266(x1);
    y1 = su_266(y1);

    m->larghezza = x1 - x0;
    m->altezza   = y1 - y0;
    m->sinistra  = x0;

    /* ! LA y DEL FONT CRESCE VERSO L'ALTO, QUELLA DELLO SCHERMO VERSO IL
     * BASSO, e il giro si fa QUI, una volta sola. `cima` e' quanto il glifo
     * sale sopra la linea di base: chi disegna fa `y_base - cima` e non deve
     * sapere niente di questa faccenda. */
    m->cima = y1;

    return 1;
}

/* -----------------------------------------------------------------------------
 * Il riempimento
 * --------------------------------------------------------------------------- */

/* Aggiunge la copertura di un tratto orizzontale [xa,xb) — in 26.6, relativi
 * al bordo sinistro del riquadro — alla riga dell'accumulatore.
 *
 * ! I PIXEL AI DUE ESTREMI PRENDONO LA LORO FRAZIONE, quelli in mezzo tutto.
 * E' qui che sta la precisione orizzontale: campionare anche in x vorrebbe
 * dire sedici volte il lavoro per un risultato peggiore, quando la frazione
 * si sa calcolare esattamente. */
static void tratto(int *acc, int larghezza, int xa, int xb)
{
    int pa, pb;

    if (xb <= xa) return;
    if (xa < 0) xa = 0;
    if (xb > a_266(larghezza)) xb = a_266(larghezza);
    if (xb <= xa) return;

    pa = xa >> 6;
    pb = (xb - 1) >> 6;

    if (pa == pb) {                         /* tutto dentro un pixel solo */
        acc[pa] += xb - xa;
        return;
    }

    acc[pa] += 64 - (xa & 63);              /* la coda del primo */
    {
        int p;
        for (p = pa + 1; p < pb; p++) acc[p] += 64;
    }
    acc[pb] += xb - a_266(pb);              /* la testa dell'ultimo */
}

int raster_glifo(const TtfFont *f, unsigned int glifo, int corpo,
                 const RasterMisure *m, unsigned char *cop)
{
    static int acc[2048];
    int  riga, i, s;
    int  ox, oy;                            /* l'origine del riquadro, in 26.6 */
    long tot;

    if (!f || !m || !cop) return 0;
    if (m->larghezza <= 0 || m->altezza <= 0) return 1;   /* vuoto: gia' fatto */
    if (m->larghezza > 2048) return 0;

    /* Il riquadro si azzera comunque: vedi raster.h. */
    tot = (long)m->larghezza * (long)m->altezza;
    for (i = 0; i < (int)tot; i++) cop[i] = 0;

    if (!appiattisci(f, glifo, corpo)) return 1;

    ox = a_266(m->sinistra);
    oy = a_266(m->cima);

    for (riga = 0; riga < m->altezza; riga++) {
        for (i = 0; i < m->larghezza; i++) acc[i] = 0;

        for (s = 0; s < SOTTO; s++) {
            /* La y del campione, in coordinate del font (in su), 26.6.
             * La riga 0 del riquadro e' la piu' ALTA: da li' si scende. */
            int y = oy - a_266(riga) - (s * 64 + 32) / SOTTO;
            int n_inc = 0;

            for (i = 0; i < g_n_seg; i++) {
                const Segmento *g = &g_seg[i];
                int ya = g->y0, yb = g->y1, verso = 1;
                int xa = g->x0, xb = g->x1;

                if (ya > yb) {
                    int t;
                    t = ya; ya = yb; yb = t;
                    t = xa; xa = xb; xb = t;
                    verso = -1;
                }

                /* ! IL BORDO BASSO E' DENTRO E QUELLO ALTO E' FUORI, e non e'
                 * arbitrario: due segmenti che si toccano in un vertice
                 * darebbero altrimenti DUE incroci alla stessa y, e il
                 * conteggio non tornerebbe. Con la regola «>=  e  <» ogni
                 * vertice conta una volta sola. */
                if (y < ya || y >= yb) continue;
                if (n_inc >= INCROCI_MAX) break;

                /* ! IL PRODOTTO SI FA SENZA SEGNO, E IL SEGNO SI RIMETTE
                 * DOPO. Con le coordinate limitate a COORD_MAX le due
                 * differenze stanno in 60000, e il loro prodotto in 3,6
                 * miliardi: sta in un intero SENZA segno (4,29 miliardi) e
                 * NON in uno con segno (2,15). Il segno tolto prima e rimesso
                 * dopo e' cio' che fa entrare il conto in 32 bit, e in 32 bit
                 * la divisione esiste. */
                {
                    int          dx = xb - xa;
                    unsigned int mag = (unsigned int)(dx < 0 ? -dx : dx);
                    unsigned int q;

                    q = (mag * (unsigned int)(y - ya)) / (unsigned int)(yb - ya);
                    g_ix[n_inc] = xa + (dx < 0 ? -(int)q : (int)q);
                }
                g_iv[n_inc] = verso;
                n_inc++;
            }

            /* Ordinamento per inserzione: gli incroci sono pochi — una lettera
             * ne ha due o quattro — e su pochi elementi l'inserzione batte
             * qualunque cosa piu' furba. */
            for (i = 1; i < n_inc; i++) {
                int vx = g_ix[i], vv = g_iv[i], j = i - 1;

                while (j >= 0 && g_ix[j] > vx) {
                    g_ix[j + 1] = g_ix[j];
                    g_iv[j + 1] = g_iv[j];
                    j--;
                }
                g_ix[j + 1] = vx;
                g_iv[j + 1] = vv;
            }

            /* Conteggio non nullo: si riempie dove il conto non e' zero. */
            {
                int conto = 0, inizio = 0;

                for (i = 0; i < n_inc; i++) {
                    int prima = conto;

                    conto += g_iv[i];
                    if (prima == 0 && conto != 0) inizio = g_ix[i];
                    else if (prima != 0 && conto == 0)
                        tratto(acc, m->larghezza, inizio - ox, g_ix[i] - ox);
                }
            }
        }

        /* Da accumulo a copertura. Il massimo e' SOTTO * 64. */
        for (i = 0; i < m->larghezza; i++) {
            int v = acc[i] * 255 / (SOTTO * 64);

            if (v < 0) v = 0;
            if (v > 255) v = 255;
            cop[riga * m->larghezza + i] = (unsigned char)v;
        }
    }

    return 1;
}
