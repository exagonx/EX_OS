/* =============================================================================
 * exwin/bin/exbrowser/browser_caratteri.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * THE PAGE'S OWN FONTS (@font-face; debug/implementazione_BROWSER002.txt, P6)
 *
 * A page says "this text is Roboto, and Roboto is in this .woff2". Until
 * today every word came out in one of our three Liberation faces. Here:
 *   - the file becomes a TrueType/OpenType font: WOFF2 through Google's
 *     decoder (libwoff2dec, tools/woff2-exos), WOFF1 through zlib, TTF and
 *     OTF as they are;
 *   - HARFBUZZ (libharfbuzz.so.0) shapes each word: which glyphs, where -
 *     kerning, ligatures, the marks of the scripts that have them;
 *   - FREETYPE (libfreetype.so.6) draws each glyph, CFF included, which our
 *     own exfont does not read;
 *   - the word is put on the window as one picture, ex_pixmap_blend.
 * exfont stays the system's font: these handles are only the page's.
 *
 * ! THE LIBRARIES ARE OPENED WITH dlopen, the first time a page has an
 * @font-face. Without one of them (an ISO without /lib/libharfbuzz.so.0)
 * car_aggiungi says no, and the text keeps its Liberation face - the page as
 * it was yesterday.
 *
 * ! THEIR TYPES ARE WRITTEN HERE, NOT INCLUDED, as for zlib and nghttp2 in
 * lib/exhttp: the browser is built with the project's headers. Five
 * offsets of FreeType 2.15 structures (FT_FaceRec.glyph, FT_GlyphSlotRec
 * .bitmap/.bitmap_left/.bitmap_top, FT_Bitmap), measured with offsetof on
 * the real headers at 32 and 64 bit; HarfBuzz's are plain C structs of int.
 *
 * ! A HANDLE OF OURS IS AN ExFont WITH CAR_PAGINA SET: the layout keeps
 * passing ExFont around as before, and car_altezza, car_larghezza and
 * car_disegna send to ExWin whatever is not ours.
 * ============================================================================= */
#include "browser_priv.h"

/* ---------------------------------------------------------------------------
 * The libraries
 * ------------------------------------------------------------------------- */
#if defined(__x86_64__)
#define FT_FACCIA_GLIFO   152   /* FT_FaceRec.glyph            */
#define FT_SLOT_BITMAP    152   /* FT_GlyphSlotRec.bitmap      */
#define FT_SLOT_SX        192   /*                .bitmap_left */
#define FT_SLOT_SU        196   /*                .bitmap_top  */
#define FT_BMP_PASSO        8   /* FT_Bitmap.pitch             */
#define FT_BMP_DATI        16   /*          .buffer            */
#define FT_BMP_MODO        26   /*          .pixel_mode        */
#define WOFF2_CALCOLA "_ZN5woff221ComputeWOFF2FinalSizeEPKhm"
#define WOFF2_CONVERTI "_ZN5woff217ConvertWOFF2ToTTFEPhmPKhm"
#else
#define FT_FACCIA_GLIFO    84
#define FT_SLOT_BITMAP     76
#define FT_SLOT_SX        100
#define FT_SLOT_SU        104
#define FT_BMP_PASSO        8
#define FT_BMP_DATI        12
#define FT_BMP_MODO        18
#define WOFF2_CALCOLA "_ZN5woff221ComputeWOFF2FinalSizeEPKhj"
#define WOFF2_CONVERTI "_ZN5woff217ConvertWOFF2ToTTFEPhjPKhj"
#endif
#define FT_LOAD_RENDER        (1 << 2)
#define FT_LOAD_TARGET_LIGHT  (1 << 16)
#define FT_PIXEL_MODE_GRAY    2

typedef struct { unsigned int codice, maschera, gruppo, v1, v2; } HbInfo;      /* hb_glyph_info_t */
typedef struct { int ax, ay, dx, dy; unsigned int v; } HbPos;                  /* hb_glyph_position_t */
typedef struct { int sopra, sotto, spazio, riserva[9]; } HbMisure;             /* hb_font_extents_t */

static int    (*ft_init)(void **);
static int    (*ft_faccia)(void *, const unsigned char *, long, long, void **);
static int    (*ft_corpo)(void *, unsigned int, unsigned int);
static int    (*ft_glifo)(void *, unsigned int, int);
static int    (*ft_chiudi)(void *);

static void  *(*hb_blob)(const char *, unsigned int, int, void *, void *);
static void   (*hb_blob_via)(void *);
static void  *(*hb_faccia)(void *, unsigned int);
static void   (*hb_faccia_via)(void *);
static void  *(*hb_font)(void *);
static void   (*hb_font_via)(void *);
static void   (*hb_scala)(void *, int, int);
static int    (*hb_misure)(void *, HbMisure *);
static void  *(*hb_buf)(void);
static void   (*hb_buf_svuota)(void *);
static void   (*hb_buf_utf8)(void *, const char *, int, unsigned int, int);
static void   (*hb_buf_indovina)(void *);
static void   (*hb_forma)(void *, void *, const void *, unsigned int);
static HbInfo *(*hb_info)(void *, unsigned int *);
static HbPos  *(*hb_pos)(void *, unsigned int *);

static unsigned long (*w2_misura)(const unsigned char *, unsigned long);
static _Bool         (*w2_converti)(unsigned char *, unsigned long, const unsigned char *, unsigned long);
static int           (*z_srotola)(unsigned char *, unsigned long *, const unsigned char *, unsigned long);

static int   g_cercate = 0, g_pronte = 0, g_w2 = -1, g_z = -1;
static void *g_ft = 0;          /* FT_Library */
static void *g_hbbuf = 0;       /* one shaping buffer for everything */

static void *sim(void *h, const char *nome, int *ok)
{
    void *p = h ? dlsym(h, nome) : 0;

    if (!p) *ok = 0;
    return p;
}

int car_disponibile(void)
{
    void *f, *h;
    int   ok = 1;

    if (g_cercate) return g_pronte;
    g_cercate = 1;
    f = dlopen("libfreetype.so.6", RTLD_NOW);
    h = dlopen("libharfbuzz.so.0", RTLD_NOW);
    if (!f || !h) return 0;
    *(void **)&ft_init   = sim(f, "FT_Init_FreeType", &ok);
    *(void **)&ft_faccia = sim(f, "FT_New_Memory_Face", &ok);
    *(void **)&ft_corpo  = sim(f, "FT_Set_Pixel_Sizes", &ok);
    *(void **)&ft_glifo  = sim(f, "FT_Load_Glyph", &ok);
    *(void **)&ft_chiudi = sim(f, "FT_Done_Face", &ok);
    *(void **)&hb_blob        = sim(h, "hb_blob_create", &ok);
    *(void **)&hb_blob_via    = sim(h, "hb_blob_destroy", &ok);
    *(void **)&hb_faccia      = sim(h, "hb_face_create", &ok);
    *(void **)&hb_faccia_via  = sim(h, "hb_face_destroy", &ok);
    *(void **)&hb_font        = sim(h, "hb_font_create", &ok);
    *(void **)&hb_font_via    = sim(h, "hb_font_destroy", &ok);
    *(void **)&hb_scala       = sim(h, "hb_font_set_scale", &ok);
    *(void **)&hb_misure      = sim(h, "hb_font_get_h_extents", &ok);
    *(void **)&hb_buf         = sim(h, "hb_buffer_create", &ok);
    *(void **)&hb_buf_svuota  = sim(h, "hb_buffer_clear_contents", &ok);
    *(void **)&hb_buf_utf8    = sim(h, "hb_buffer_add_utf8", &ok);
    *(void **)&hb_buf_indovina = sim(h, "hb_buffer_guess_segment_properties", &ok);
    *(void **)&hb_forma       = sim(h, "hb_shape", &ok);
    *(void **)&hb_info        = sim(h, "hb_buffer_get_glyph_infos", &ok);
    *(void **)&hb_pos         = sim(h, "hb_buffer_get_glyph_positions", &ok);
    if (!ok || ft_init(&g_ft) != 0) return 0;
    g_hbbuf = hb_buf();
    g_pronte = g_hbbuf != 0;
    return g_pronte;
}

static int woff2_pronto(void)
{
    void *h;
    int   ok = 1;

    if (g_w2 >= 0) return g_w2;
    h = dlopen("libwoff2dec.so.1.0.2", RTLD_NOW);
    *(void **)&w2_misura   = sim(h, WOFF2_CALCOLA, &ok);
    *(void **)&w2_converti = sim(h, WOFF2_CONVERTI, &ok);
    g_w2 = (h && ok) ? 1 : 0;
    return g_w2;
}

static int zlib_pronta(void)
{
    void *h;
    int   ok = 1;

    if (g_z >= 0) return g_z;
    h = dlopen("libz.so.1", RTLD_NOW);
    *(void **)&z_srotola = sim(h, "uncompress", &ok);
    g_z = (h && ok) ? 1 : 0;
    return g_z;
}

int car_woff2_pronto(void) { return woff2_pronto(); }

/* ---------------------------------------------------------------------------
 * From the file of the page to a font file
 * ------------------------------------------------------------------------- */
static unsigned int be32(const unsigned char *p)
{
    return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3];
}

static unsigned int be16(const unsigned char *p) { return (unsigned int)p[0] << 8 | p[1]; }

static void scrivi32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void scrivi16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
}

/* WOFF 1.0: a table directory and each table perhaps zlib'd. The sfnt is
 * put back together: offset table, directory, the tables aligned to 4. */
static unsigned char *woff1_srotola(const unsigned char *d, unsigned int n, unsigned int *esce)
{
    unsigned int   nt, tot, i, k, pos, cerca = 1, liv = 0;
    unsigned char *o;

    if (n < 44 || !zlib_pronta()) return 0;
    nt  = be16(d + 12);
    tot = be32(d + 16);
    if (nt == 0 || nt > 256 || 44u + nt * 20u > n || tot > 32u * 1024 * 1024 || tot < 12 + nt * 16) return 0;
    o = (unsigned char *)malloc(tot + 4 * nt + 16);
    if (!o) return 0;
    memset(o, 0, tot + 4 * nt + 16);
    while (cerca * 2 <= nt) { cerca *= 2; liv++; }
    scrivi32(o, be32(d + 4));                   /* flavor */
    scrivi16(o + 4, nt);
    scrivi16(o + 6, cerca * 16);
    scrivi16(o + 8, liv);
    scrivi16(o + 10, nt * 16 - cerca * 16);
    pos = 12 + nt * 16;
    for (i = 0; i < nt; i++) {
        const unsigned char *t = d + 44 + i * 20;
        unsigned int off = be32(t + 4), cl = be32(t + 8), ol = be32(t + 12);

        if (off > n || cl > n - off || ol > tot || pos + ol > tot + 4 * nt) { free(o); return 0; }
        memcpy(o + 12 + i * 16, t, 4);          /* tag */
        scrivi32(o + 12 + i * 16 + 4, be32(t + 16));
        scrivi32(o + 12 + i * 16 + 8, pos);
        scrivi32(o + 12 + i * 16 + 12, ol);
        if (cl < ol) {
            unsigned long fuori = ol;

            if (z_srotola(o + pos, &fuori, d + off, cl) != 0 || fuori != ol) { free(o); return 0; }
        } else {
            memcpy(o + pos, d + off, ol);
        }
        pos += ol;
        for (k = pos; k & 3; k++) o[k] = 0;
        pos = k;
    }
    *esce = pos;
    return o;
}

/* WOFF2: Google's decoder. ComputeWOFF2FinalSize is an upper bound; the
 * sfnt says where it really ends, and FreeType reads only that. */
static unsigned char *woff2_srotola(const unsigned char *d, unsigned int n, unsigned int *esce)
{
    unsigned long  tot;
    unsigned char *o;

    if (!woff2_pronto()) return 0;
    tot = w2_misura(d, n);
    if (tot == 0 || tot > 32ul * 1024 * 1024) return 0;
    o = (unsigned char *)malloc(tot);
    if (!o) return 0;
    if (!w2_converti(o, tot, d, n)) { free(o); return 0; }
    *esce = (unsigned int)tot;
    return o;
}

/* ---------------------------------------------------------------------------
 * The faces and their sizes
 * ------------------------------------------------------------------------- */
#define CAR_FACCE   48
#define CAR_ISTANZE 160

typedef struct {
    char           url[NAV_URL_MAX];
    char           famiglia[64];
    unsigned short peso_min, peso_max;
    unsigned char  corsivo, buona;
    unsigned char *dati;            /* the sfnt, which FreeType and HarfBuzz read in place */
    unsigned int   n;
    void          *ft;              /* FT_Face */
    void          *hbf;             /* hb_face_t */
    int            corpo_ft;        /* the size FreeType's face is set to now */
} CarFaccia;

typedef struct {
    int    faccia, corpo;
    void  *hb;                      /* hb_font_t, scaled to corpo * 64 */
    int    alt, base;
    ExFont ripiego;                 /* for a word with a glyph the face lacks */
} CarIstanza;

static CarFaccia  g_facce[CAR_FACCE];
static int        g_facce_n = 0;
static CarIstanza g_ist[CAR_ISTANZE];
static int        g_ist_n = 0;

static void cache_svuota(void);

void car_pagina_nuova(void)
{
    int i;

    for (i = 0; i < g_ist_n; i++) if (g_ist[i].hb) hb_font_via(g_ist[i].hb);
    g_ist_n = 0;
    for (i = 0; i < g_facce_n; i++) {
        if (g_facce[i].ft) ft_chiudi(g_facce[i].ft);
        if (g_facce[i].hbf) hb_faccia_via(g_facce[i].hbf);
        free(g_facce[i].dati);
    }
    g_facce_n = 0;
    cache_svuota();
}

int car_gia(const char *url)
{
    int i;

    for (i = 0; i < g_facce_n; i++) if (!strcmp(g_facce[i].url, url)) return 1;
    return 0;
}

/* The file `d` (n bytes, from `url`) as a face of `ff`. Kept or not, the
 * address is remembered, so a page is not asked for it twice. 1 = a face. */
int car_aggiungi(const char *url, const CssFontFace *ff, const unsigned char *d, unsigned int n,
                 const char **come)
{
    CarFaccia     *F;
    unsigned char *sfnt = 0;
    unsigned int   sn = 0;
    void          *blob;

    *come = "";
    if (g_facce_n >= CAR_FACCE || car_gia(url) || strlen(url) >= NAV_URL_MAX) return 0;
    F = &g_facce[g_facce_n++];
    memset(F, 0, sizeof(*F));
    strcpy(F->url, url);
    strncpy(F->famiglia, ff->famiglia, sizeof(F->famiglia) - 1);
    F->peso_min = ff->peso_min;
    F->peso_max = ff->peso_max;
    F->corsivo  = ff->corsivo;
    if (!car_disponibile()) { *come = "senza freetype/harfbuzz"; return 0; }
    if (n < 12) { *come = "corto"; return 0; }
    if (!memcmp(d, "wOF2", 4)) {
        *come = "woff2";
        sfnt = woff2_srotola(d, n, &sn);
    } else if (!memcmp(d, "wOFF", 4)) {
        *come = "woff";
        sfnt = woff1_srotola(d, n, &sn);
    } else if (be32(d) == 0x00010000u || !memcmp(d, "OTTO", 4) || !memcmp(d, "true", 4)) {
        *come = d[0] == 'O' ? "otf" : "ttf";
        sfnt = (unsigned char *)malloc(n);
        if (sfnt) { memcpy(sfnt, d, n); sn = n; }
    } else {
        *come = "formato ignoto";
        return 0;
    }
    if (!sfnt) return 0;
    if (ft_faccia(g_ft, sfnt, (long)sn, 0, &F->ft) != 0 || !F->ft) { free(sfnt); F->ft = 0; return 0; }
    blob = hb_blob((const char *)sfnt, sn, 1 /* HB_MEMORY_MODE_READONLY */, 0, 0);
    F->hbf = blob ? hb_faccia(blob, 0) : 0;
    if (blob) hb_blob_via(blob);           /* the face keeps its own reference */
    if (!F->hbf) { ft_chiudi(F->ft); F->ft = 0; free(sfnt); return 0; }
    F->dati = sfnt;
    F->n = sn;
    F->buona = 1;
    return 1;
}

/* The name of family k of the current page's sheet */
static const char *nome_di(int k)
{
    static const CssFoglio *ultimo_f = 0;
    static int              ultimo_k = -1;
    static unsigned int     ultimo_dich = 0;
    static char             ultimo_nome[64];
    CssFontFace             ff;
    unsigned int            i;

    if (ultimo_f == &g_css && ultimo_k == k && ultimo_dich == g_css.dich_n) return ultimo_nome;
    for (i = 0; css_font_face(&g_css, i, &ff); i++)
        if (ff.k == k) {
            ultimo_f = &g_css; ultimo_k = k; ultimo_dich = g_css.dich_n;
            strncpy(ultimo_nome, ff.famiglia, sizeof(ultimo_nome) - 1);
            ultimo_nome[sizeof(ultimo_nome) - 1] = 0;
            return ultimo_nome;
        }
    return 0;
}

ExFont car_della_pagina(int famiglia, int neretto, int corsivo, int corpo)
{
    const char *nome;
    int         i, meglio = -1, dist = 100000, voluto = neretto ? 700 : 400, gen, fam;

    if (!g_facce_n || !CSS_FAM_E_PAGINA(famiglia)) return 0;
    nome = nome_di(CSS_FAM_PAGINA_K(famiglia));
    if (!nome) return 0;
    if (corpo < CORPO_MIN) corpo = CORPO_MIN;
    if (corpo > CORPO_MAX) corpo = CORPO_MAX;
    /* the nearest weight, the right style first: the CSS font matching, in short */
    for (i = 0; i < g_facce_n; i++) {
        const CarFaccia *F = &g_facce[i];
        int d;

        if (!F->buona || strcmp(F->famiglia, nome)) continue;
        d = voluto < F->peso_min ? F->peso_min - voluto : voluto > F->peso_max ? voluto - F->peso_max : 0;
        if ((F->corsivo != 0) != (corsivo != 0)) d += 10000;
        if (d < dist) { dist = d; meglio = i; }
    }
    if (meglio < 0) return 0;
    for (i = 0; i < g_ist_n; i++)
        if (g_ist[i].faccia == meglio && g_ist[i].corpo == corpo) return CAR_PAGINA | (unsigned int)i;
    if (g_ist_n >= CAR_ISTANZE) return 0;
    {
        CarIstanza *I = &g_ist[g_ist_n];
        HbMisure    m;

        I->faccia = meglio;
        I->corpo  = corpo;
        I->hb     = hb_font(g_facce[meglio].hbf);
        if (!I->hb) return 0;
        hb_scala(I->hb, corpo * 64, corpo * 64);
        memset(&m, 0, sizeof(m));
        hb_misure(I->hb, &m);
        if (m.sopra <= 0) { m.sopra = corpo * 64 * 4 / 5; m.sotto = -corpo * 64 / 5; }
        I->base = (m.sopra + m.spazio / 2 + 32) / 64;
        I->alt  = (m.sopra - m.sotto + m.spazio + 32) / 64;
        if (I->alt < corpo) I->alt = corpo;
        gen = CSS_FAM_PAGINA_GEN(famiglia);
        fam = gen == CSS_FAM_FISSO ? FAM_MONO : gen == CSS_FAM_SERIF ? FAM_SERIF : FAM_SANS;
        I->ripiego = font_per(neretto, corsivo, fam, corpo);
        return CAR_PAGINA | (unsigned int)g_ist_n++;
    }
}

static CarIstanza *ist(ExFont f)
{
    unsigned int i;

    if (!(f & CAR_PAGINA)) return 0;
    i = f & ~CAR_PAGINA;
    return i < (unsigned int)g_ist_n ? &g_ist[i] : 0;
}

int car_altezza(ExFont f)
{
    CarIstanza *I = ist(f);

    return I ? I->alt : ex_font_height(f);
}

int car_base(ExFont f)
{
    CarIstanza *I = ist(f);

    return I ? I->base : ex_font_baseline(f);
}

/* ---------------------------------------------------------------------------
 * Shaping: the glyphs of one word, and where they go (in 1/64 px)
 * ------------------------------------------------------------------------- */
static unsigned int g_n;
static HbInfo      *g_gi;
static HbPos       *g_gp;

/* 0: a glyph the face does not have (the word goes to the fallback) */
static int forma(CarIstanza *I, const char *s, int *larg64)
{
    unsigned int i, n2 = 0;
    int          tot = 0;

    hb_buf_svuota(g_hbbuf);
    hb_buf_utf8(g_hbbuf, s, -1, 0, -1);
    hb_buf_indovina(g_hbbuf);
    hb_forma(I->hb, g_hbbuf, 0, 0);
    g_gi = hb_info(g_hbbuf, &g_n);
    g_gp = hb_pos(g_hbbuf, &n2);
    if (!g_gi || !g_gp || n2 != g_n) return 0;
    for (i = 0; i < g_n; i++) {
        if (g_gi[i].codice == 0) return 0;
        tot += g_gp[i].ax;
    }
    *larg64 = tot;
    return 1;
}

int car_larghezza(ExFont f, const char *s)
{
    CarIstanza *I = ist(f);
    int         l;

    if (!I) return ex_text_width(f, s);
    if (!s[0]) return 0;
    if (!forma(I, s, &l)) return ex_text_width(I->ripiego, s);
    return (l + 32) / 64;
}

/* ---------------------------------------------------------------------------
 * The glyphs, drawn once and kept
 * ------------------------------------------------------------------------- */
#define CACHE_VOCI  4096                    /* a power of two */
#define CACHE_BYTE  (4u * 1024u * 1024u)

typedef struct {
    int            ist;                     /* -1 = empty */
    unsigned int   gid;
    short          w, h, sx, su;
    unsigned char *a;
} Glifo;

static Glifo        g_glifi[CACHE_VOCI];
static unsigned int g_glifi_byte = 0;
static int          g_glifi_pronta = 0;

static void cache_svuota(void)
{
    int i;

    for (i = 0; i < CACHE_VOCI; i++) {
        if (g_glifi_pronta && g_glifi[i].ist >= 0) free(g_glifi[i].a);
        g_glifi[i].ist = -1;
        g_glifi[i].a = 0;
    }
    g_glifi_byte = 0;
    g_glifi_pronta = 1;
}

static const Glifo *glifo(int ii, unsigned int gid)
{
    CarIstanza    *I = &g_ist[ii];
    CarFaccia     *F = &g_facce[I->faccia];
    unsigned int   h = (gid * 2654435761u ^ (unsigned int)ii * 40503u) & (CACHE_VOCI - 1), giri;
    Glifo         *g;
    unsigned char *slot, *bmp, *src;
    unsigned int   righe, larg;
    int            passo, y;

    if (!g_glifi_pronta) cache_svuota();
    for (giri = 0; giri < 16; giri++, h = (h + 1) & (CACHE_VOCI - 1)) {
        g = &g_glifi[h];
        if (g->ist == ii && g->gid == gid) return g;
        if (g->ist < 0) break;
    }
    if (giri == 16 || g_glifi_byte > CACHE_BYTE) {
        cache_svuota();
        h = (gid * 2654435761u ^ (unsigned int)ii * 40503u) & (CACHE_VOCI - 1);
    }
    g = &g_glifi[h];
    if (F->corpo_ft != I->corpo) {
        if (ft_corpo(F->ft, 0, (unsigned int)I->corpo) != 0) return 0;
        F->corpo_ft = I->corpo;
    }
    if (ft_glifo(F->ft, gid, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) return 0;
    slot  = *(unsigned char **)((unsigned char *)F->ft + FT_FACCIA_GLIFO);
    bmp   = slot + FT_SLOT_BITMAP;
    righe = *(unsigned int *)(bmp + 0);
    larg  = *(unsigned int *)(bmp + 4);
    passo = *(int *)(bmp + FT_BMP_PASSO);
    src   = *(unsigned char **)(bmp + FT_BMP_DATI);
    if (bmp[FT_BMP_MODO] != FT_PIXEL_MODE_GRAY && righe && larg) return 0;
    if (righe > 512 || larg > 512) return 0;
    g->ist = ii;
    g->gid = gid;
    g->w   = (short)larg;
    g->h   = (short)righe;
    g->sx  = (short)*(int *)(slot + FT_SLOT_SX);
    g->su  = (short)*(int *)(slot + FT_SLOT_SU);
    g->a   = 0;
    if (righe && larg) {
        g->a = (unsigned char *)malloc(righe * larg);
        if (!g->a) { g->ist = -1; return 0; }
        for (y = 0; y < (int)righe; y++)
            memcpy(g->a + (unsigned int)y * larg, src + (passo >= 0 ? y * passo : ((int)righe - 1 - y) * -passo), larg);
        g_glifi_byte += righe * larg;
    }
    return g;
}

void car_disegna(ExWindow w, ExFont f, int x, int y, const char *s, unsigned int c)
{
    static unsigned int *px = 0;
    static unsigned int  px_max = 0;
    CarIstanza          *I = ist(f);
    int                  l, W, H, ox, penna = 0;
    unsigned int         i;

    if (!I) { ex_draw_text_font(w, f, x, y, s, c); return; }
    if (!s[0]) return;
    if (!forma(I, s, &l)) { ex_draw_text_font(w, I->ripiego, x, y, s, c); return; }

    /* the picture: the word's advance, and room for what sticks out */
    ox = 2 + I->corpo / 4;
    W  = (l + 32) / 64 + 2 * ox;
    H  = I->alt + I->corpo / 2;
    if (W <= 0 || H <= 0 || W > 4096 || H > 512) return;
    if ((unsigned int)(W * H) > px_max) {
        free(px);
        px_max = (unsigned int)(W * H) + 4096;
        px = (unsigned int *)malloc(px_max * 4);
        if (!px) { px_max = 0; return; }
    }
    memset(px, 0, (size_t)W * (size_t)H * 4);
    c &= 0xFFFFFFu;
    for (i = 0; i < g_n; i++) {
        unsigned int  gid = g_gi[i].codice;
        int           gx  = ox + (penna + g_gp[i].dx + 32) / 64;
        int           gy  = I->base - (g_gp[i].dy + 32) / 64;
        const Glifo  *g   = glifo((int)(I - g_ist), gid);
        int           yy, xx;

        /* the shaping buffer may be touched by nothing in between: glifo()
         * only rasterizes */
        penna += g_gp[i].ax;
        if (!g || !g->a) continue;
        gx += g->sx;
        gy -= g->su;
        for (yy = 0; yy < g->h; yy++) {
            int ry = gy + yy;

            if (ry < 0 || ry >= H) continue;
            for (xx = 0; xx < g->w; xx++) {
                int          rx = gx + xx;
                unsigned int a  = g->a[yy * g->w + xx], *d;

                if (!a || rx < 0 || rx >= W) continue;
                d = &px[ry * W + rx];
                if ((*d >> 24) < a) *d = (a << 24) | c;
            }
        }
    }
    ex_pixmap_blend(w, x - ox, y, W, H, px, (unsigned int)W);
}

/* ---------------------------------------------------------------------------
 * What the browser asks before fetching
 * ------------------------------------------------------------------------- */

/* Does the unicode-range include 'A'? (no range: every character) The
 * subsets of Google Fonts are a dozen files per face, one per script; the
 * Latin one is the one a Western page reads. */
int car_copre_latino(const char *r)
{
    const char *p = r;

    if (!p || !*p) return 1;
    while (*p) {
        unsigned int a = 0, b, cifre = 0;

        while (*p && *p != 'U' && *p != 'u') p++;
        if (!*p) break;
        p++;
        if (*p != '+') continue;
        p++;
        while (*p && (isxdigit((unsigned char)*p) || *p == '?')) {
            a = a * 16 + (*p == '?' ? 0 : (unsigned int)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10)));
            p++; cifre++;
        }
        b = a;
        if (*p == '-') {
            p++;
            b = 0;
            while (*p && isxdigit((unsigned char)*p)) {
                b = b * 16 + (unsigned int)(isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
                p++;
            }
        }
        if (cifre && a <= 0x41 && b >= 0x41) return 1;
    }
    return 0;
}

/* The src list: the best url() this browser can read, by format() or by its
 * ending. local() is skipped: the system has no font catalogue. */
int car_scegli_src(const char *src, char *out, unsigned int max)
{
    const char *p = src;
    int         meglio = 0;

    out[0] = 0;
    while (*p) {
        const char *u0 = 0, *u1 = 0, *f0 = 0;
        char        q = 0;
        int         voto = 0;

        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;
        if (!strncasecmp(p, "url(", 4)) {
            p += 4;
            while (*p == ' ') p++;
            if (*p == '"' || *p == '\'') q = *p++;
            u0 = p;
            while (*p && (q ? *p != q : (*p != ')' && *p != ' '))) p++;
            u1 = p;
            while (*p && *p != ')') p++;
            if (*p) p++;
        }
        /* to the comma that ends this entry: format() and tech() are in there */
        while (*p && *p != ',') {
            if (!strncasecmp(p, "format(", 7)) f0 = p + 7;
            if (*p == '(') { while (*p && *p != ')') p++; if (*p) p++; continue; }
            p++;
        }
        if (!u0 || u1 <= u0 || (unsigned int)(u1 - u0) >= max) continue;
        if (u1 - u0 > 5 && !strncasecmp(u0, "data:", 5)) continue;
        if (f0) {
            while (*f0 == '"' || *f0 == '\'' || *f0 == ' ') f0++;
            if (!strncasecmp(f0, "woff2", 5)) voto = woff2_pronto() ? 4 : 0;
            else if (!strncasecmp(f0, "woff", 4)) voto = 3;
            else if (!strncasecmp(f0, "truetype", 8) || !strncasecmp(f0, "opentype", 8)) voto = 2;
            else voto = 0;                     /* embedded-opentype, svg, collection... */
        } else {
            unsigned int n = (unsigned int)(u1 - u0);
            const char  *e = u1;
            const char  *qm = memchr(u0, '?', n);

            if (qm) e = qm;
            if (e - u0 >= 6 && !strncasecmp(e - 6, ".woff2", 6)) voto = woff2_pronto() ? 4 : 0;
            else if (e - u0 >= 5 && !strncasecmp(e - 5, ".woff", 5)) voto = 3;
            else if (e - u0 >= 4 && (!strncasecmp(e - 4, ".ttf", 4) || !strncasecmp(e - 4, ".otf", 4))) voto = 2;
            else if (e - u0 >= 4 && !strncasecmp(e - 4, ".eot", 4)) voto = 0;
            else voto = 1;                     /* no ending: let the bytes say */
        }
        if (voto > meglio) {
            meglio = voto;
            memcpy(out, u0, (size_t)(u1 - u0));
            out[u1 - u0] = 0;
        }
    }
    return meglio > 0;
}

/* how many faces, of how many asked: for the console */
void car_conta(int *buone, int *tutte)
{
    int i;

    *buone = 0;
    for (i = 0; i < g_facce_n; i++) if (g_facce[i].buona) (*buone)++;
    *tutte = g_facce_n;
}
