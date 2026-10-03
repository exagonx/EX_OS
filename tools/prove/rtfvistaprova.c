/* =============================================================================
 * tools/prove/rtfvistaprova.c — la vista del testo ricco sull'host
 * (@RTF tappa 2, 30 settembre 2026)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     cc -I lib/exrtf -I lib/exwin -I drivers/kbd -o /tmp/rtfvistaprova \
 *        tools/prove/rtfvistaprova.c lib/exrtf/vista.c lib/exrtf/rtf.c
 *
 * ! LE FUNZIONI DI exwin SONO FINTE, e apposta: un carattere largo meta' del
 * corpo in pixel (un pixel in piu' se grassetto), alto il corpo piu' quattro.
 * Con misure che si contano a mano, «a capo dopo la seconda parola» e «il
 * cursore a x=40» sono numeri da confrontare, non fotografie da guardare. Il
 * disegno vero lo prova QEMU con exeditor.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include "exrtf_vista.h"
#include "kbd_proto.h"

/* --- exwin finto --------------------------------------------------------- */
ExFont ex_font_find(int fam, int corpo, int grassetto, int corsivo)
{
    return (ExFont)((unsigned)corpo | ((unsigned)grassetto << 8) | ((unsigned)corsivo << 9) |
                    ((unsigned)fam << 10));
}
int ex_font_height(ExFont f) { return (int)(f & 255) + 4; }
int ex_font_baseline(ExFont f)    { return (int)(f & 255); }
int ex_text_width(ExFont f, const char *s)
{
    int n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n * ((int)(f & 255) / 2 + (int)((f >> 8) & 1));
}

typedef struct { int x, y, w, h; unsigned int c; } Riempi;
typedef struct { int x, y; unsigned int c; ExFont f; char t[128]; } Scritto;
static Riempi  g_ri[4000]; static int g_ri_n;
static Scritto g_sc[4000]; static int g_sc_n;
void ex_fill_rect(ExWindow f, int x, int y, int w, int h, unsigned int c)
{
    (void)f;
    if (g_ri_n < 4000) { Riempi r = { x, y, w, h, c }; g_ri[g_ri_n++] = r; }
}
void ex_draw_text_font(ExWindow w, ExFont f, int x, int y, const char *s, unsigned int c)
{
    (void)w;
    if (g_sc_n < 4000) {
        Scritto *q = &g_sc[g_sc_n++];
        q->x = x; q->y = y; q->c = c; q->f = f;
        snprintf(q->t, sizeof(q->t), "%s", s);
    }
}
static char g_app[4096]; static unsigned int g_app_n;
unsigned int ex_clipboard_set(const char *t, unsigned int n)
{
    if (n >= sizeof(g_app)) n = sizeof(g_app) - 1;
    memcpy(g_app, t, n); g_app[n] = 0; g_app_n = n; return n;
}
unsigned int ex_clipboard_get(char *out, unsigned int max)
{
    unsigned int n = g_app_n < max ? g_app_n : max;
    memcpy(out, g_app, n); return n;
}

/* --- la prova ------------------------------------------------------------ */
static char          g_testo[65536];
static ExRtfPezzo    g_pezzi[4096];
static unsigned char g_all[4096];
static ExRtfRiga     g_righe[4096];
static ExRtfDoc      d;
static ExRtfVista    v;
static int g_no = 0, g_n = 0;

static void ok(int c, const char *cosa)
{
    g_n++;
    printf("%s %2d %s\n", c ? "ok" : "NO", g_n, cosa);
    if (!c) g_no++;
}

static void scrivi(const char *s) { while (*s) exrtf_vista_tasto(&v, (unsigned char)*s++); }
static void tasto(unsigned int k, int volte) { while (volte--) exrtf_vista_tasto(&v, k); }

static void nuovo(const char *testo, int w, int h)
{
    ExRtfStile s;
    exrtf_prepara(&d, g_testo, sizeof(g_testo), g_pezzi, 4096, g_all, 4096);
    exrtf_stile_base(&s);                               /* 12 pt = 16 px: 8 px a lettera */
    exrtf_aggiungi(&d, testo, (unsigned int)strlen(testo), &s);
    exrtf_vista_prepara(&v, &d, g_righe, 4096);
    exrtf_vista_posto(&v, 100, 50, w, h);
}

/* the caret drawn: the 2 px black fill */
static int cursore_x(void)
{
    int i;
    g_ri_n = g_sc_n = 0;
    exrtf_vista_disegna(&v, 1);
    for (i = 0; i < g_ri_n; i++) if (g_ri[i].w == 2 && g_ri[i].c == EX_BLACK) return g_ri[i].x;
    return -1;
}

static void mostra(void)
{
    unsigned int i;
    printf("  testo [%s] cur %u anc %u\n", d.testo, v.cur, v.anc);
    for (i = 0; i < v.righe_n; i++)
        printf("  riga %u: %u..%u y %d h %d largo %d al %u\n", i, v.righe[i].inizio,
               v.righe[i].fine, v.righe[i].y, v.righe[i].h, v.righe[i].largo, v.righe[i].allinea);
}

int main(void)
{
    ExRtfStile st;
    int x0 = 100 + 8;                   /* v.x + MARGINE */
    int i;

    /* 80 px di testo: w = 80 + 2*8 di margini + 14 di barra */
    printf("--- a capo ---\n");
    nuovo("uno due tre quattro", 110, 200);
    ok(v.righe_n == 3 && v.righe[0].fine == 8 && v.righe[1].inizio == 8 &&
       v.righe[1].fine == 12 && v.righe[2].inizio == 12 && v.righe[2].fine == 19,
       "80 px: \"uno due \" / \"tre \" / \"quattro\"");
    ok(v.righe[0].largo == 56 && v.righe[0].h == 20 && v.righe[1].y == 20,
       "la larghezza senza lo spazio in coda, l'altezza dal font");
    nuovo("supercalifragilistico", 110, 200);
    ok(v.righe_n == 3 && v.righe[0].fine == 10 && v.righe[1].fine == 20,
       "una parola piu' lunga della riga si spezza per lettere");

    printf("--- cursore e tasti ---\n");
    nuovo("uno due tre quattro", 110, 200);
    ok(cursore_x() == x0 && v.cur == 0, "all'inizio, il cursore a sinistra");
    tasto(KBD_K_END, 1);
    ok(v.cur == 7, "Fine su una riga spezzata: prima dello spazio che la spezza");
    tasto(KBD_K_RIGHT, 2);
    ok(v.cur == 9 && cursore_x() == x0 + 8, "destra: si passa alla riga dopo");
    tasto(KBD_K_DOWN, 1);
    ok(v.cur == 13, "giu': la stessa colonna nella riga sotto");
    tasto(KBD_K_UP, 2);
    ok(v.cur == 1, "su due volte: la colonna resta quella voluta");
    tasto(KBD_K_END | KBD_MOD_CTRL, 1);
    scrivi("!");
    ok(strcmp(d.testo, "uno due tre quattro!") == 0 && v.modificato && v.cur == 20,
       "Ctrl+Fine e una lettera: in fondo");
    tasto('\b', 1);
    tasto(KBD_K_HOME | KBD_MOD_CTRL, 1);
    tasto(KBD_K_DEL, 4);
    ok(strcmp(d.testo, "due tre quattro") == 0, "Indietro e Canc");

    printf("--- scelta e stile ---\n");
    nuovo("uno due tre", 400, 200);
    tasto(KBD_K_RIGHT | KBD_MOD_CTRL, 1);
    tasto(KBD_K_RIGHT | KBD_MOD_SHIFT, 3);
    ok(v.anc == 4 && v.cur == 7, "Maiusc+destra sceglie \"due\"");
    exrtf_vista_tasto(&v, 'b' | KBD_MOD_CTRL);
    ok(d.pezzi_n == 3 && d.pezzi[1].inizio == 4 && d.pezzi[1].lung == 3 && d.pezzi[1].stile.grassetto,
       "Ctrl+B: \"due\" in grassetto");
    g_ri_n = g_sc_n = 0;
    exrtf_vista_disegna(&v, 1);
    for (i = 0; i < g_sc_n && strcmp(g_sc[i].t, "due") != 0; i++) ;
    ok(i < g_sc_n && g_sc[i].c == EX_WHITE && g_sc[i].x == x0 + 32 && (g_sc[i].f & 256),
       "disegnata scelta: bianca, al suo posto, col font grassetto");
    tasto(KBD_K_END, 1);
    scrivi("x");
    ok(exrtf_stile_a(&d, 11, &st) && !st.grassetto, "dopo la fine del grassetto si scrive normale");
    tasto(KBD_K_LEFT, 5);
    scrivi("y");
    ok(exrtf_stile_a(&d, 7, &st) && st.grassetto && strcmp(d.testo, "uno duey trex") == 0,
       "dentro il grassetto si scrive in grassetto");
    exrtf_vista_tasto(&v, 'i' | KBD_MOD_CTRL);
    scrivi("z");
    ok(exrtf_stile_a(&d, 8, &st) && st.corsivo && st.grassetto && d.pezzi_n >= 3,
       "Ctrl+I senza scelta: cambia solo quel che si scrive dopo");

    printf("--- paragrafi e allineamento ---\n");
    nuovo("titolo", 110, 200);
    exrtf_vista_allinea(&v, EXRTF_CENTRO);
    ok(cursore_x() == x0 + (80 - 48) / 2, "centrato: il cursore a meta' del vuoto");
    tasto(KBD_K_END, 1);
    tasto('\n', 1);
    scrivi("ab");
    ok(d.par_n == 2 && d.allinea[1] == EXRTF_CENTRO && v.righe_n == 2 &&
       cursore_x() == x0 + (80 - 16) / 2 + 16, "Invio: il paragrafo nuovo resta centrato");
    exrtf_vista_allinea(&v, EXRTF_DESTRA);
    ok(cursore_x() == x0 + 80 && d.allinea[0] == EXRTF_CENTRO, "a destra solo il secondo");
    tasto(KBD_K_HOME, 1);
    tasto('\b', 1);
    ok(d.par_n == 1 && d.allinea[0] == EXRTF_CENTRO && strcmp(d.testo, "titoloab") == 0,
       "Indietro a inizio paragrafo: i due si uniscono, col primo allineamento");

    nuovo("aa bb cc dd ee ff", 110, 200);          /* 80 px: «aa bb cc » / «dd ee ff» */
    exrtf_vista_allinea(&v, EXRTF_GIUSTO);
    g_ri_n = g_sc_n = 0;
    exrtf_vista_disegna(&v, 1);
    {
        int fine1 = 0, fine2 = 0;
        for (i = 0; i < g_sc_n; i++) {
            int e = g_sc[i].x + ex_text_width(g_sc[i].f, g_sc[i].t);
            if (strcmp(g_sc[i].t, "cc ") == 0) fine1 = g_sc[i].x + 16;
            if (strcmp(g_sc[i].t, "dd ee ff") == 0) fine2 = e;   /* not justified: one piece */
        }
        ok(fine1 == x0 + 80 && fine2 == x0 + 64,
           "giustificato: la prima riga arriva al bordo, l'ultima no");
    }

    printf("--- mouse ---\n");
    nuovo("uno due tre quattro", 110, 200);
    exrtf_vista_clic(&v, x0 + 8 * 5 + 3, 50 + 6 + 5, 0);
    ok(v.cur == 5 && v.anc == 5, "clic dentro una lettera: il confine piu' vicino");
    exrtf_vista_trascina(&v, x0 + 8 * 2, 50 + 6 + 25);
    exrtf_vista_su(&v);
    ok(v.anc == 5 && v.cur == 10, "trascinato alla riga sotto: la scelta va fin li'");
    exrtf_vista_clic(&v, x0 + 79, 50 + 6 + 5, 0);
    ok(v.cur == 7, "clic oltre la fine di una riga spezzata: prima dello spazio");
    exrtf_vista_doppio(&v, x0 + 8 * 3, 50 + 6 + 45);    /* inside «quattro» */
    ok(v.anc == 12 && v.cur == 19, "doppio clic: la parola intera");
    exrtf_vista_copia(&v);
    tasto(KBD_K_END | KBD_MOD_CTRL, 1);
    exrtf_vista_tasto(&v, 'v' | KBD_MOD_CTRL);
    ok(strcmp(d.testo, "uno due tre quattroquattro") == 0, "copia e incolla");

    printf("--- scorrimento ---\n");
    {
        static char lungo[4000];
        lungo[0] = 0;
        for (i = 0; i < 40; i++) strcat(lungo, "riga\n");
        nuovo(lungo, 110, 106);                       /* 100 px di righe da 20: 5 */
        tasto(KBD_K_PGDN, 1);
        ok(v.prima == 5 && v.cur == 5 * 5, "Pag giu': cinque righe, e il cursore con loro");
        tasto(KBD_K_END | KBD_MOD_CTRL, 1);
        ok(v.prima == 36 && cursore_x() == x0, "in fondo: si vedono le ultime cinque (l'ultima vuota)");
        exrtf_vista_rotella(&v, -2);
        ok(v.prima == 30, "la rotella: tre righe a scatto");
    }

    if (g_no) mostra();
    printf("rtfvistaprova: %d NO su %d\n", g_no, g_n);
    return g_no ? 1 : 0;
}
