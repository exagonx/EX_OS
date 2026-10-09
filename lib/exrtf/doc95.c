/* =============================================================================
 * lib/exrtf/doc95.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * READING THE OLD .DOC: WORD 6 AND WORD 95 (9 October 2026)
 *
 * Asked by the user for ExEditor: «il supporto per il vecchio doc di Office
 * 95, ma solo se non e' dispendioso». It is READ ONLY, into the same model
 * RTF is read into (exrtf.h); what was opened is saved as RTF.
 *
 * The file is two formats one inside the other:
 *
 *   - outside, an OLE2 "compound file": a small filesystem of 512-byte
 *     sectors with an allocation table, holding named streams. The text is in
 *     the stream «WordDocument»;
 *   - inside, the Word file proper. It begins with the FIB, a table of
 *     offsets; the text is 8-bit (Windows-1252), in one run or in the pieces
 *     of a "fast save"; character and paragraph properties sit in 512-byte
 *     pages (FKP), each a table of file offsets and of lists of SPRMs - one
 *     byte of opcode, then its operand.
 *
 * ! WHAT IS READ: the main text; bold, italic, underline, size, colour,
 * typeface (by family); alignment, indents, space before and after, line
 * spacing as a multiple, tab stops; the paper and its margins; the styles,
 * as far as they give those same properties (a heading is big and bold
 * because its STYLE says so, not the paragraph).
 *
 * ! WHAT IS NOT, declared: Word 97 and later (another layout: said to the
 * caller, -1), encrypted files (-2), footnotes, headers, pictures, tables
 * (their text comes, a tab between cells), fields (the result is kept, the
 * code is not). An opcode this file does not know ENDS the list it is in:
 * the operand's length is not known, and guessing it would turn the rest
 * into noise.
 *
 * ! EVERY OFFSET COMES FROM THE FILE, AND IS CHECKED BEFORE IT IS USED. A
 * file is the most hostile input a program reads: all the reads go through
 * b8/b16/b32, which answer 0 outside the buffer.
 * ============================================================================= */
#include "exrtf.h"

#define SETT        512u
#define FINE_CATENA 0xFFFFFFFEu

typedef struct {
    const unsigned char *b;         /* the Word stream, contiguous */
    unsigned int         n;
    unsigned int         fc_chp, lcb_chp, fc_pap, lcb_pap;
    unsigned int         fc_stsh, lcb_stsh, fc_ffn, lcb_ffn;
} W;

static unsigned int b8(const W *w, unsigned int o)  { return o < w->n ? w->b[o] : 0; }
static unsigned int b16(const W *w, unsigned int o) { return b8(w, o) | (b8(w, o + 1) << 8); }
static unsigned int b32(const W *w, unsigned int o) { return b16(w, o) | (b16(w, o + 2) << 16); }
static int s16(const W *w, unsigned int o)
{
    unsigned int v = b16(w, o);
    return v & 0x8000u ? (int)v - 65536 : (int)v;
}

static unsigned int r16(const unsigned char *b, unsigned int n, unsigned int o)
{
    return o + 1 < n ? (unsigned int)b[o] | ((unsigned int)b[o + 1] << 8) : 0;
}
static unsigned int r32(const unsigned char *b, unsigned int n, unsigned int o)
{
    return r16(b, n, o) | (r16(b, n, o + 2) << 16);
}

/* -----------------------------------------------------------------------------
 * The compound file: find «WordDocument» and copy it out, sector by sector
 * --------------------------------------------------------------------------- */
static const unsigned char FIRMA[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };

/* The sector after `s` in the chain. The allocation table is itself spread
 * over sectors, listed in the header (109 of them) and then in DIFAT sectors:
 * a document from the nineties never needs those, and they are not followed. */
static unsigned int dopo_sett(const unsigned char *b, unsigned int n, unsigned int s)
{
    unsigned int quale = s / (SETT / 4), fat;

    if (quale >= 109) return FINE_CATENA;
    fat = r32(b, n, 0x4C + quale * 4);
    if (fat >= 0xFFFFFFF0u) return FINE_CATENA;
    return r32(b, n, (fat + 1) * SETT + (s % (SETT / 4)) * 4);
}

/* Copies a stream that starts at sector `s` and is `lungo` bytes long. */
static int copia_catena(const unsigned char *b, unsigned int n, unsigned int s,
                        unsigned int lungo, unsigned char *out)
{
    unsigned int fatti = 0, giri = 0;

    while (fatti < lungo) {
        unsigned int off = (s + 1) * SETT, k = lungo - fatti, i;

        if (s >= 0xFFFFFFF0u || off + SETT > n + SETT - 1 || off >= n) return 0;
        if (k > SETT) k = SETT;
        if (off + k > n) k = n - off;
        for (i = 0; i < k; i++) out[fatti + i] = b[off + i];
        fatti += k;
        if (k < SETT && fatti < lungo) return 0;
        s = dopo_sett(b, n, s);
        if (++giri > n / SETT + 2) return 0;        /* a chain that loops */
    }
    return 1;
}

static int nome_e(const unsigned char *voce, const char *nome)
{
    unsigned int i;

    for (i = 0; nome[i]; i++)
        if (voce[i * 2] != (unsigned char)nome[i] || voce[i * 2 + 1] != 0) return 0;
    return voce[i * 2] == 0 && voce[i * 2 + 1] == 0;
}

/* Finds «WordDocument» and copies it into lavoro. Returns its length, 0 if
 * the file is not a compound file with that stream (or it does not fit). */
static unsigned int flusso_word(const unsigned char *b, unsigned int n,
                                unsigned char *lavoro, unsigned int lavoro_max)
{
    unsigned int dir, giri = 0, i;

    if (n < 3 * SETT) return 0;
    for (i = 0; i < 8; i++) if (b[i] != FIRMA[i]) return 0;
    if (r16(b, n, 0x1E) != 9) return 0;             /* 512-byte sectors only */
    dir = r32(b, n, 0x30);

    while (dir < 0xFFFFFFF0u && giri++ < 4096) {
        unsigned int off = (dir + 1) * SETT;

        if (off + SETT > n) return 0;
        for (i = 0; i < SETT / 128; i++) {
            const unsigned char *v = b + off + i * 128;
            unsigned int inizio = r32(v, 128, 0x74), lungo = r32(v, 128, 0x78);

            if (v[0x42] != 2 || !nome_e(v, "WordDocument")) continue;
            /* ! A STREAM UNDER 4096 BYTES LIVES IN THE "MINI STREAM", another
             * chain inside the root's. A Word file is never that small - the
             * FIB alone asks for more - so it is refused instead of read. */
            if (lungo < 4096 || lungo > lavoro_max) return 0;
            return copia_catena(b, n, inizio, lungo, lavoro) ? lungo : 0;
        }
        dir = dopo_sett(b, n, dir);
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * SPRMs: the opcodes of Word 6 and 95, one byte each
 *
 * The operand's length, by opcode: 1, 2, 3 or 4 bytes; V = a length byte
 * follows; 0 here means «not known to this reader», and ends the list.
 * --------------------------------------------------------------------------- */
#define V 0xFF
static unsigned char lung_sprm(unsigned int op)
{
    /* paragraph, 2..52 */
    static const unsigned char P[] = {
        /* 2 */ 2, V, 1, 1, 1, 1, 1, 1, 1, 1,       /* istd, permute, inclvl, jc, sidebyside, keep, keepfollow, pagebreak, brcl, brcp */
        /* 12 */ V, 1, 1, V, 2, 2, 2, 2, 4, 2, 2,   /* anld, nlvlanm, nolinenumb, chgtabspapx, dxaright, dxaleft, nest, dxaleft1, dyaline, before, after */
        /* 23 */ V, 1, 1, 2, 2, 2, 1,               /* chgtabs, intable, ttp, dxaabs, dyaabs, dxawidth, pc */
        /* 30 */ 2, 2, 2, 2, 2, 2, 2, 1,            /* brc x6 (old), dxafromtext10, wr */
        /* 38 */ 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 1, 1, V   /* brc x6, noautohyph, wheightabs, dcs, shd, dyafromtext, dxafromtext, locked, widow, ruler */
    };
    /* character, 65..110 */
    static const unsigned char C[] = {
        /* 65 */ 1, 1, 1, V, 2, 4, 1, 2, 3, V, 1,   /* strikerm, rmark, fldvanish, piclocation, ibstrmark, dttmrmark, fdata, rmreason, chse, symbol, ole2 */
        /* 76 */ 0, 0, 0, 0,
        /* 80 */ 2, V, 0, 0, 0,                     /* istd, istdpermute, default (?), plain (handled apart), 84 */
        /* 85 */ 1, 1, 1, 1, 1, 1, 1, 1,            /* bold, italic, strike, outline, shadow, smallcaps, caps, vanish */
        /* 93 */ 2, 1, 3, 2, 2, 1, 2, 1, 1, 1,      /* ftc, kul, sizepos, dxaspace, lid, ico, hps, hpsinc, hpspos, hpsposadj */
        /* 103 */ V, 1, V, V, 2, V, 2, 2            /* majority, iss, hpsnew50, hpsinc1, hpskern, majority50, hpsmul, condhyhen */
    };
    /* section, 131..170. 134 and 135 are not known here: Word 97 dropped
     * them, and it is from its list that the others' sizes are taken. */
    static const unsigned char S[] = {
        /* 131 */ 1, 1, V, 0, 0, 3, 3, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1,
        /* 160 */ 2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2
    };

    if (op >= 2 && op < 2 + sizeof(P))   return P[op - 2];
    if (op >= 65 && op < 65 + sizeof(C)) return C[op - 65];
    if (op >= 117 && op <= 119)          return 1;      /* fspec, fobj, picbrcl */
    if (op >= 131 && op < 131 + sizeof(S)) return S[op - 131];
    return 0;
}

/* What a list of SPRMs changes. Character switches keep Word's four values:
 * 0 off, 1 on, 128 as the style, 129 the opposite of the style. */
typedef struct {
    ExRtfStile s;
    ExRtfPar   p;
    int        nascosto;
    int        ttp;             /* the paragraph is a table row's end */
    unsigned int istd;
    int        ftc;             /* the font index asked, -1 none yet */
    /* the page, from a section's list */
    unsigned int carta_w, carta_h, m_sin, m_des, m_su, m_giu;
} Forma;

static const unsigned int ICO[17] = {
    0x000000, 0x000000, 0x0000FF, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0xFFFF00, 0xFFFFFF,
    0x000080, 0x008080, 0x008000, 0x800080, 0x800000, 0x808000, 0x808080, 0xC0C0C0
};

static unsigned char interruttore(unsigned int v, unsigned char dello_stile)
{
    if (v == 128) return dello_stile;
    if (v == 129) return (unsigned char)!dello_stile;
    return (unsigned char)(v != 0);
}

static void tab_cambia(const W *w, unsigned int o, unsigned int fine, ExRtfPar *P, int con_chiusi)
{
    unsigned int nd, na, i, k;

    nd = b8(w, o++);
    for (i = 0; i < nd && o + 1 < fine; i++, o += 2) {
        unsigned int via = b16(w, o);

        for (k = 0; k < P->tab_n; k++)
            if (P->tab[k] == via) {
                unsigned int j;
                for (j = k; j + 1 < P->tab_n; j++) P->tab[j] = P->tab[j + 1];
                P->tab_n--;
                break;
            }
    }
    if (con_chiusi) o += nd * 2;                    /* rgdxaClose */
    if (o >= fine) return;
    na = b8(w, o++);
    for (i = 0; i < na && o + 1 < fine; i++, o += 2) {
        int t = s16(w, o);
        unsigned int j;

        if (t <= 0 || t > 30000 || P->tab_n >= EXRTF_TAB_MAX) continue;
        for (k = 0; k < P->tab_n && P->tab[k] < (unsigned int)t; k++) ;
        if (k < P->tab_n && P->tab[k] == (unsigned int)t) continue;
        for (j = P->tab_n; j > k; j--) P->tab[j] = P->tab[j - 1];
        P->tab[k] = (unsigned short)t;
        P->tab_n++;
    }
}

static int fra(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

/* Applies the list at [o, o + lungo) to F. `stile` is what the style gave
 * (for the 128/129 switches). */
static void sprm_applica(const W *w, unsigned int o, unsigned int lungo, Forma *F, const ExRtfStile *stile)
{
    unsigned int fine = o + lungo;

    if (fine > w->n) fine = w->n;
    while (o < fine) {
        unsigned int op = b8(w, o++), l = lung_sprm(op), a = o;

        if (op == 0) continue;                      /* padding */
        if (op == 83) {                             /* plain: back to the style */
            F->s.grassetto = stile->grassetto; F->s.corsivo = stile->corsivo;
            F->s.sottolineato = stile->sottolineato; F->s.corpo = stile->corpo;
            F->s.colore = stile->colore;
            continue;
        }
        if (l == 0) return;                         /* unknown: see the head of the file */
        if (l == V) {
            l = b8(w, o++);
            a = o;
            if (op == 23 && l == 255) return;       /* the long form of the tabs: not read */
        }
        if (a + l > fine) return;

        switch (op) {
        case 2:  F->istd = b16(w, a); break;
        case 5:  { unsigned int j = b8(w, a); F->p.allinea = (unsigned char)(j < 4 ? j : 0); break; }
        case 15: tab_cambia(w, a, a + l, &F->p, 0); break;
        case 23: tab_cambia(w, a, a + l, &F->p, 1); break;
        case 16: F->p.des   = (short)fra(s16(w, a), 0, 20000); break;
        case 17: F->p.sin   = (short)fra(s16(w, a), 0, 20000); break;
        case 19: F->p.prima = (short)fra(s16(w, a), -20000, 20000); break;
        case 20: {
            int dya = s16(w, a), multiplo = s16(w, a + 2);
            if (multiplo && dya > 0) {
                int pc = dya * 100 / 240;
                F->p.interlinea = (unsigned char)(pc <= 110 ? 0 : pc > 250 ? 250 : pc);
            }
            break;
        }
        case 21: F->p.sp_prima = (unsigned short)fra(s16(w, a), 0, 4000); break;
        case 22: F->p.sp_dopo  = (unsigned short)fra(s16(w, a), 0, 4000); break;
        case 25: F->ttp = b8(w, a) != 0; break;
        case 85: F->s.grassetto = interruttore(b8(w, a), stile->grassetto); break;
        case 86: F->s.corsivo   = interruttore(b8(w, a), stile->corsivo); break;
        case 92: F->nascosto    = interruttore(b8(w, a), 0); break;
        case 93: F->ftc = (int)b16(w, a); break;
        case 94: F->s.sottolineato = (unsigned char)(b8(w, a) != 0); break;
        case 98: { unsigned int i = b8(w, a); F->s.colore = i < 17 ? ICO[i] : 0; break; }
        case 99: {
            unsigned int hps = b16(w, a);           /* half points */
            if (hps >= 8 && hps <= 800) F->s.corpo = (unsigned short)((hps + 1) / 2);
            break;
        }
        case 164: F->carta_w = b16(w, a); break;
        case 165: F->carta_h = b16(w, a); break;
        case 166: F->m_sin = b16(w, a); break;
        case 167: F->m_des = b16(w, a); break;
        case 168: F->m_su  = b16(w, a); break;
        case 169: F->m_giu = b16(w, a); break;
        default: break;
        }
        o = a + l;
    }
}

/* -----------------------------------------------------------------------------
 * The styles: each one is its base's properties plus its own
 * --------------------------------------------------------------------------- */
#define STILI_MAX 256

typedef struct {
    unsigned int inizio[STILI_MAX];     /* offset of each STD's body, 0 = empty */
    unsigned int lungo[STILI_MAX];
    unsigned int n, base;               /* how many, and the fixed part's size */
} Stili;

static void stili_trova(const W *w, Stili *T)
{
    unsigned int o = w->fc_stsh, fine = w->fc_stsh + w->lcb_stsh, cb, i;

    T->n = 0;
    if (w->lcb_stsh < 4 || fine > w->n) return;
    cb = b16(w, o);
    T->n = b16(w, o + 2);
    T->base = b16(w, o + 4);
    if (T->n > STILI_MAX) T->n = STILI_MAX;
    o += 2 + cb;
    for (i = 0; i < T->n; i++) {
        unsigned int l;

        if (o + 2 > fine) { T->n = i; break; }
        l = b16(w, o);
        T->inizio[i] = l ? o + 2 : 0;
        T->lungo[i] = l;
        o += 2 + l;
    }
}

/* The properties of style `istd`: its base first (not deeper than ten), then
 * its own paragraph list and character list. */
static void stile_applica(const W *w, const Stili *T, unsigned int istd, Forma *F, int prof)
{
    unsigned int o, fine, base, tipo, quanti, nome, k;
    ExRtfStile   prima;

    if (istd >= T->n || !T->inizio[istd] || prof > 10) return;
    o = T->inizio[istd];
    fine = o + T->lungo[istd];
    tipo = b16(w, o + 2) & 15;                      /* 1 paragraph, 2 character */
    base = b16(w, o + 2) >> 4;
    quanti = b16(w, o + 4) & 15;
    if (base != 0xFFF && base != istd) stile_applica(w, T, base, F, prof + 1);

    /* after the fixed part, the name: a length byte, the letters, a zero */
    nome = o + T->base;
    k = nome + 1 + b8(w, nome) + 1;
    if ((k - o) & 1) k++;                           /* each part starts on an even offset */
    prima = F->s;
    if (tipo == 1 && quanti >= 1 && k + 2 <= fine) {
        unsigned int cb = b16(w, k);

        if (cb >= 2 && k + 2 + cb <= fine) sprm_applica(w, k + 4, cb - 2, F, &prima);   /* after its istd */
        k += 2 + cb;
        if ((k - o) & 1) k++;
        quanti--;
    }
    if (quanti >= 1 && k + 2 <= fine) {
        unsigned int cb = b16(w, k);

        if (k + 2 + cb <= fine) sprm_applica(w, k + 2, cb, F, &prima);
    }
}

/* -----------------------------------------------------------------------------
 * The pages of properties (FKP)
 * --------------------------------------------------------------------------- */

/* The page holding file offset fc, from a table of (n+1) offsets and n page
 * numbers of two bytes. 0 if there is none. */
static unsigned int pagina_di(const W *w, unsigned int tav, unsigned int lcb, unsigned int fc)
{
    unsigned int n, i;

    if (lcb < 10 || tav + lcb > w->n) return 0;
    n = (lcb - 4) / 6;
    for (i = 0; i < n; i++)
        if (fc < b32(w, tav + (i + 1) * 4) || i + 1 == n)
            return b16(w, tav + (n + 1) * 4 + i * 2) * SETT;
    return 0;
}

/* The run of page `pg` holding fc: its bounds, and where its data byte is.
 * `passo` is the size of an entry after the offsets (1 for characters, 7 for
 * paragraphs). Returns the offset of the properties in the file, 0 = none. */
static unsigned int corsa_di(const W *w, unsigned int pg, unsigned int fc, unsigned int passo,
                             unsigned int *da, unsigned int *a)
{
    unsigned int n, i, b;

    *da = fc; *a = fc + 1;
    if (pg == 0 || pg + SETT > w->n) return 0;
    n = b8(w, pg + SETT - 1);
    if (n == 0 || (n + 1) * 4 + n * passo > SETT - 1) return 0;
    for (i = 0; i < n; i++) {
        unsigned int f0 = b32(w, pg + i * 4), f1 = b32(w, pg + (i + 1) * 4);

        if (fc >= f0 && fc < f1) {
            *da = f0; *a = f1;
            b = b8(w, pg + (n + 1) * 4 + i * passo);
            return b ? pg + b * 2 : 0;
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * The fonts: an index becomes one of the three families
 * --------------------------------------------------------------------------- */
static void font_leggi(const W *w, ExRtfDoc *d)
{
    unsigned int o = w->fc_ffn + 2, fine = w->fc_ffn + w->lcb_ffn;

    d->font_n = 0;
    if (w->lcb_ffn < 4 || fine > w->n) return;
    while (o + 6 < fine && d->font_n < 16) {
        unsigned int cb = b8(w, o) + 1, ff = (b8(w, o + 1) >> 4) & 7, i, k = 0;
        char *nome = d->font_nome[d->font_n];

        if (cb < 7 || o + cb > fine) break;
        for (i = o + 6; i < o + cb && b8(w, i) && k + 1 < EXRTF_NOME_MAX; i++) {
            unsigned int c = b8(w, i);
            nome[k++] = (char)(c < 127 && c >= 32 ? c : '?');
        }
        nome[k] = '\0';
        d->font_fam[d->font_n] = (unsigned char)(ff == 2 ? EXRTF_SANS : ff == 3 ? EXRTF_MONO : EXRTF_SERIF);
        /* a file that says nothing about the family: the names everyone had */
        if (ff == 0) {
            if (nome[0] == 'A' && nome[1] == 'r') d->font_fam[d->font_n] = EXRTF_SANS;
            if (nome[0] == 'C' && nome[1] == 'o') d->font_fam[d->font_n] = EXRTF_MONO;
        }
        d->font_n++;
        o += cb;
    }
}

/* -----------------------------------------------------------------------------
 * The text
 * --------------------------------------------------------------------------- */
static const unsigned short CP1252[32] = {
    0x20AC, 0x003F, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039,
    0x0152, 0x003F, 0x017D, 0x003F, 0x003F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x003F, 0x017E, 0x0178
};

static void emetti(ExRtfDoc *d, unsigned int cp, const ExRtfStile *s)
{
    char u[4];
    unsigned int n;

    if (cp < 0x80)       { u[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else                 { u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 63));
                           u[2] = (char)(0x80 | (cp & 63)); n = 3; }
    exrtf_aggiungi(d, u, n, s);
}

typedef struct {
    const W     *w;
    const Stili *T;
    ExRtfDoc    *d;
    /* the paragraph and the run the last character was in */
    unsigned int p_da, p_a, c_da, c_a;
    Forma        par;           /* style + paragraph's own list */
    Forma        car;           /* that, + the run's list */
    int          campo[8], prof;    /* fields: 1 = inside the code, not shown */
} Lettura;

static void carattere(Lettura *L, unsigned int fc)
{
    const W *w = L->w;
    unsigned int c = b8(w, fc), o;

    if (fc < L->p_da || fc >= L->p_a) {
        unsigned int pg = pagina_di(w, w->fc_pap, w->lcb_pap, fc);

        exrtf_stile_base(&L->par.s);
        L->par.s.corpo = 10;                        /* Word's own default */
        exrtf_par_base(&L->par.p);
        L->par.nascosto = L->par.ttp = 0;
        L->par.istd = 0;
        L->par.ftc = -1;
        o = corsa_di(w, pg, fc, 7, &L->p_da, &L->p_a);
        if (o) {
            unsigned int cw = b8(w, o);

            /* ! A COUNT OF WORDS, THEN THE STYLE'S NUMBER IN TWO BYTES, then
             * the list. (One byte for the style was Word 2: written so here
             * at first, and LibreOffice read the test file differently -
             * see tools/prove/doc95-genera.py.) The style first, then the
             * paragraph's own list on top of what the style gave. */
            L->par.istd = cw ? b16(w, o + 1) : 0;
            stile_applica(w, L->T, L->par.istd, &L->par, 0);
            if (cw > 1) sprm_applica(w, o + 3, cw * 2 - 2, &L->par, &L->par.s);
        } else {
            stile_applica(w, L->T, 0, &L->par, 0);
        }
        L->c_da = L->c_a = 0;                       /* the run is to be redone */
    }
    if (fc < L->c_da || fc >= L->c_a) {
        unsigned int pg = pagina_di(w, w->fc_chp, w->lcb_chp, fc);

        L->car = L->par;
        o = corsa_di(w, pg, fc, 1, &L->c_da, &L->c_a);
        if (o) sprm_applica(w, o + 1, b8(w, o), &L->car, &L->par.s);
        if (L->car.ftc >= 0 && (unsigned int)L->car.ftc < L->d->font_n) {
            L->car.s.font = (unsigned char)L->car.ftc;
            L->car.s.famiglia = L->d->font_fam[L->car.ftc];
        }
    }

    /* fields: 19 opens (the code follows), 20 separates (the result follows),
     * 21 closes */
    if (c == 19) { if (L->prof < 8) L->campo[L->prof++] = 1; return; }
    if (c == 20) { if (L->prof > 0) L->campo[L->prof - 1] = 0; return; }
    if (c == 21) { if (L->prof > 0) L->prof--; return; }
    for (o = 0; o < (unsigned int)L->prof; o++) if (L->campo[o]) return;
    if (L->car.nascosto && c != 13) return;

    if (L->d->par_n) L->d->par[L->d->par_n - 1] = L->par.p;
    if (c == 13 || c == 11 || c == 12) { emetti(L->d, '\n', &L->car.s); return; }
    if (c == 7)  { emetti(L->d, L->par.ttp ? '\n' : '\t', &L->car.s); return; }
    if (c == 9)  { emetti(L->d, '\t', &L->car.s); return; }
    if (c == 30) { emetti(L->d, '-', &L->car.s); return; }
    if (c < 32) return;                             /* pictures, notes, optional hyphens */
    if (c >= 0x80 && c < 0xA0) c = CP1252[c - 0x80];
    emetti(L->d, c, &L->car.s);
}

int exrtf_leggi_doc(ExRtfDoc *d, const unsigned char *file, unsigned int n,
                    unsigned char *lavoro, unsigned int lavoro_max)
{
    static Stili   T;
    static Lettura L;
    W            w;
    unsigned int ident, bandiere, fc_min, ccp, fc_clx, lcb_clx, i;

    w.n = flusso_word(file, n, lavoro, lavoro_max);
    if (w.n == 0) return 0;
    w.b = lavoro;

    ident = b16(&w, 0);
    if (ident == 0xA5EC) return -1;                 /* Word 97 and later */
    if (ident != 0xA5DC) return 0;
    bandiere = b16(&w, 0x0A);
    if (bandiere & 0x0100) return -2;               /* encrypted */

    fc_min = b32(&w, 0x18);
    ccp = b32(&w, 0x34);
    w.fc_stsh = b32(&w, 0x60);  w.lcb_stsh = b32(&w, 0x64);
    w.fc_chp  = b32(&w, 0xB8);  w.lcb_chp  = b32(&w, 0xBC);
    w.fc_pap  = b32(&w, 0xC0);  w.lcb_pap  = b32(&w, 0xC4);
    w.fc_ffn  = b32(&w, 0xD0);  w.lcb_ffn  = b32(&w, 0xD4);
    fc_clx = b32(&w, 0x160);    lcb_clx = b32(&w, 0x164);
    if (fc_min >= w.n || ccp > w.n) return -3;

    font_leggi(&w, d);
    stili_trova(&w, &T);

    /* the paper: the first section's list */
    {
        unsigned int sed = b32(&w, 0x88), lcb = b32(&w, 0x8C);

        if (lcb >= 4 + 12 + 4 && sed + lcb <= w.n) {
            unsigned int ns = (lcb - 4) / 16, sepx = b32(&w, sed + (ns + 1) * 4 + 2);

            if (sepx != 0xFFFFFFFFu && sepx + 2 < w.n) {
                Forma F;
                ExRtfStile s0;

                exrtf_stile_base(&s0);
                F.s = s0; exrtf_par_base(&F.p);
                F.carta_w = F.carta_h = F.m_sin = F.m_des = F.m_su = F.m_giu = 0;
                F.nascosto = F.ttp = 0; F.istd = 0; F.ftc = -1;
                sprm_applica(&w, sepx + 2, b16(&w, sepx), &F, &s0);
                if (F.carta_w > 2880 && F.carta_w < 32000) d->carta_w = F.carta_w;
                if (F.carta_h > 2880 && F.carta_h < 32000) d->carta_h = F.carta_h;
                if (F.m_sin > 0 && F.m_sin < 8000) d->marg_sin = F.m_sin;
                if (F.m_des > 0 && F.m_des < 8000) d->marg_des = F.m_des;
                if (F.m_su  > 0 && F.m_su  < 8000) d->marg_su  = F.m_su;
                if (F.m_giu > 0 && F.m_giu < 8000) d->marg_giu = F.m_giu;
            } else {
                /* no list: Word's own page, Letter with 1,25 and 1 inch */
                d->carta_w = 12240; d->carta_h = 15840;
                d->marg_sin = d->marg_des = 1800; d->marg_su = d->marg_giu = 1440;
            }
        }
    }

    L.w = &w; L.T = &T; L.d = d;
    L.p_da = L.p_a = L.c_da = L.c_a = 0;
    L.prof = 0;

    if ((bandiere & 0x0004) && lcb_clx >= 5 && fc_clx + lcb_clx <= w.n) {
        /* a fast save: the text is in pieces, listed after the changes */
        unsigned int o = fc_clx, fine = fc_clx + lcb_clx, np, cp_tot = 0;

        while (o < fine && b8(&w, o) == 1) o += 3 + b16(&w, o + 1);
        if (o + 5 > fine || b8(&w, o) != 2) return -3;
        np = (b32(&w, o + 1) - 4) / 12;
        o += 5;
        for (i = 0; i < np && cp_tot < ccp; i++) {
            unsigned int c0 = b32(&w, o + i * 4), c1 = b32(&w, o + (i + 1) * 4);
            unsigned int fc = b32(&w, o + (np + 1) * 4 + i * 8 + 2), k;

            if (c1 < c0 || fc >= w.n) break;
            for (k = 0; k < c1 - c0 && cp_tot < ccp && fc + k < w.n; k++, cp_tot++)
                carattere(&L, fc + k);
        }
    } else {
        for (i = 0; i < ccp && fc_min + i < w.n; i++) carattere(&L, fc_min + i);
    }

    /* ! THE LAST PARAGRAPH MARK IS WORD'S, NOT THE TEXT'S: every document
     * ends with one, and here it would be an empty line after the last. */
    if (d->testo_n && d->testo[d->testo_n - 1] == '\n') exrtf_cancella(d, d->testo_n - 1, d->testo_n);
    return 1;
}
