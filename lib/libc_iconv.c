/* =============================================================================
 * lib/libc_iconv.c — iconv: character set conversion (included by lib/libc.c)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * See lib/include/iconv.h for the encodings. Every conversion goes through a
 * Unicode code point: decode one character from the source, encode it into
 * the target. So there is one decoder and one encoder per encoding instead of
 * one routine per pair.
 *
 * Only in libc.a (lib/libc.c includes it under !EXOS_LIBC_SO).
 * ============================================================================= */

/* the type of lib/include/iconv.h, which libc.c does not include (it would
 * bring libc.h in a second time) */
#ifndef EXOS_ICONV_H
typedef void *iconv_t;
iconv_t iconv_open(const char *verso, const char *da);
size_t  iconv(iconv_t cd, char **in, size_t *in_n, char **out, size_t *out_n);
int     iconv_close(iconv_t cd);
#endif

enum { IC_UTF8, IC_UTF16, IC_UTF16LE, IC_UTF16BE, IC_UTF32, IC_UTF32LE, IC_UTF32BE,
       IC_ASCII, IC_LATIN1, IC_LATIN9, IC_CP1252, IC_CP437 };

typedef struct {
    int da, verso;
    int da_be;              /* UTF-16/32 without an order: what the BOM said */
    int da_bom_visto;
    int verso_bom_fatto;
    int traslittera;        /* //TRANSLIT: '?' for what the target lacks */
    int ignora;             /* //IGNORE: drop it */
} IcStato;

/* 0x80-0x9F of Windows-1252; 0 where the code page has nothing */
static const unsigned short ic_cp1252[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178 };

/* 0x80-0xFF of code page 437, the PC console's */
static const unsigned short ic_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0 };

/* ISO-8859-15 differs from Latin-1 in eight places */
static const unsigned short ic_latin9_da[8] = { 0xA4, 0xA6, 0xA8, 0xB4, 0xB8, 0xBC, 0xBD, 0xBE };
static const unsigned short ic_latin9_a[8]  = { 0x20AC, 0x0160, 0x0161, 0x017D, 0x017E, 0x0152, 0x0153, 0x0178 };

/* the name, without case, '-' and '_'; -1 if unknown. Sets the flags of a
 * "//TRANSLIT" / "//IGNORE" suffix. */
static int ic_nome(const char *n, int *tr, int *ig)
{
    static const struct { const char *nome; int cod; } nomi[] = {
        { "", IC_UTF8 }, { "UTF8", IC_UTF8 },
        { "UTF16", IC_UTF16 }, { "UTF16LE", IC_UTF16LE }, { "UTF16BE", IC_UTF16BE },
        { "UCS2", IC_UTF16 }, { "UCS2LE", IC_UTF16LE }, { "UCS2BE", IC_UTF16BE },
        { "UTF32", IC_UTF32 }, { "UTF32LE", IC_UTF32LE }, { "UTF32BE", IC_UTF32BE },
        { "UCS4", IC_UTF32 }, { "UCS4LE", IC_UTF32LE }, { "UCS4BE", IC_UTF32BE },
        { "WCHART", IC_UTF32LE },
        { "ASCII", IC_ASCII }, { "USASCII", IC_ASCII }, { "ANSIX3.41968", IC_ASCII },
        { "ISO88591", IC_LATIN1 }, { "LATIN1", IC_LATIN1 }, { "L1", IC_LATIN1 },
        { "ISO885915", IC_LATIN9 }, { "LATIN9", IC_LATIN9 },
        { "WINDOWS1252", IC_CP1252 }, { "CP1252", IC_CP1252 },
        { "CP437", IC_CP437 }, { "IBM437", IC_CP437 },
    };
    char   c[40];
    size_t k = 0, i;

    for (; *n && k < sizeof(c) - 1; n++) {
        if (n[0] == '/' && n[1] == '/') {
            const char *s = n + 2;
            for (;;) {
                if (!strncasecmp(s, "TRANSLIT", 8)) { *tr = 1; s += 8; }
                else if (!strncasecmp(s, "IGNORE", 6)) { *ig = 1; s += 6; }
                else break;
                if (*s == ',') s++;
            }
            break;
        }
        if (*n == '-' || *n == '_') continue;
        c[k++] = (char)toupper((unsigned char)*n);
    }
    c[k] = 0;
    for (i = 0; i < sizeof(nomi) / sizeof(nomi[0]); i++)
        if (strcmp(c, nomi[i].nome) == 0) return nomi[i].cod;
    return -1;
}

iconv_t iconv_open(const char *verso, const char *da)
{
    IcStato *s;
    int      tr = 0, ig = 0, d, v, x = 0, y = 0;

    if (!verso || !da) { errno = EINVAL; return (iconv_t)-1; }
    d = ic_nome(da, &x, &y);
    v = ic_nome(verso, &tr, &ig);
    if (d < 0 || v < 0) { errno = EINVAL; return (iconv_t)-1; }
    s = (IcStato *)calloc(1, sizeof(IcStato));
    if (!s) { errno = ENOMEM; return (iconv_t)-1; }
    s->da = d; s->verso = v; s->traslittera = tr; s->ignora = ig;
    s->da_be = 1;
    return (iconv_t)s;
}

int iconv_close(iconv_t cd)
{
    if (!cd || cd == (iconv_t)-1) { errno = EBADF; return -1; }
    free(cd);
    return 0;
}

static unsigned int ic_16(const unsigned char *p, int be) { return be ? ((unsigned)p[0] << 8) | p[1] : ((unsigned)p[1] << 8) | p[0]; }
static unsigned int ic_32(const unsigned char *p, int be)
{
    return be ? ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]
              : ((unsigned)p[3] << 24) | ((unsigned)p[2] << 16) | ((unsigned)p[1] << 8) | p[0];
}

/* one character in: bytes used, 0 = a BOM swallowed (bytes in *usati),
 * -1 = invalid (EILSEQ), -2 = cut short (EINVAL) */
static int ic_leggi(IcStato *s, const unsigned char *p, size_t n, unsigned int *cp, size_t *usati)
{
    int be;

    switch (s->da) {
    case IC_UTF8: {
        unsigned int c = p[0], k, i, min;
        if (c < 0x80) { *cp = c; return 1; }
        if (c < 0xC2) return -1;
        if (c < 0xE0)      { k = 2; c &= 0x1F; min = 0x80; }
        else if (c < 0xF0) { k = 3; c &= 0x0F; min = 0x800; }
        else if (c < 0xF5) { k = 4; c &= 0x07; min = 0x10000; }
        else return -1;
        for (i = 1; i < k; i++) {
            if (i >= n) return -2;
            if ((p[i] & 0xC0) != 0x80) return -1;
            c = (c << 6) | (p[i] & 0x3F);
        }
        if (c < min || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) return -1;
        *cp = c;
        return (int)k;
    }
    case IC_UTF16: case IC_UTF16LE: case IC_UTF16BE: {
        unsigned int a, b;
        be = s->da == IC_UTF16BE ? 1 : s->da == IC_UTF16LE ? 0 : s->da_be;
        if (n < 2) return -2;
        if (s->da == IC_UTF16 && !s->da_bom_visto) {
            s->da_bom_visto = 1;
            if (p[0] == 0xFE && p[1] == 0xFF) { s->da_be = 1; *usati = 2; return 0; }
            if (p[0] == 0xFF && p[1] == 0xFE) { s->da_be = 0; *usati = 2; return 0; }
        }
        a = ic_16(p, be);
        if (a >= 0xDC00 && a < 0xE000) return -1;
        if (a < 0xD800 || a >= 0xE000) { *cp = a; return 2; }
        if (n < 4) return -2;
        b = ic_16(p + 2, be);
        if (b < 0xDC00 || b >= 0xE000) return -1;
        *cp = 0x10000 + ((a - 0xD800) << 10) + (b - 0xDC00);
        return 4;
    }
    case IC_UTF32: case IC_UTF32LE: case IC_UTF32BE: {
        unsigned int c;
        be = s->da == IC_UTF32BE ? 1 : s->da == IC_UTF32LE ? 0 : s->da_be;
        if (n < 4) return -2;
        if (s->da == IC_UTF32 && !s->da_bom_visto) {
            s->da_bom_visto = 1;
            if (ic_32(p, 1) == 0xFEFF) { s->da_be = 1; *usati = 4; return 0; }
            if (ic_32(p, 0) == 0xFEFF) { s->da_be = 0; *usati = 4; return 0; }
        }
        c = ic_32(p, be);
        if (c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) return -1;
        *cp = c;
        return 4;
    }
    case IC_ASCII:
        if (p[0] >= 0x80) return -1;
        *cp = p[0]; return 1;
    case IC_LATIN1:
        *cp = p[0]; return 1;
    case IC_LATIN9: {
        int i;
        *cp = p[0];
        for (i = 0; i < 8; i++) if (p[0] == ic_latin9_da[i]) *cp = ic_latin9_a[i];
        return 1;
    }
    case IC_CP1252:
        if (p[0] >= 0x80 && p[0] < 0xA0) {
            if (!ic_cp1252[p[0] - 0x80]) return -1;
            *cp = ic_cp1252[p[0] - 0x80];
        } else *cp = p[0];
        return 1;
    case IC_CP437:
        *cp = p[0] < 0x80 ? p[0] : ic_cp437[p[0] - 0x80];
        return 1;
    }
    return -1;
}

/* one byte from a 256-entry code page: 0 if it is not there */
static int ic_tabella(unsigned int cp, const unsigned short *t, unsigned int primo, unsigned int n,
                      unsigned char *b)
{
    unsigned int i;

    for (i = 0; i < n; i++) if (t[i] == cp && cp) { *b = (unsigned char)(primo + i); return 1; }
    return 0;
}

/* one character out: bytes written, -1 = the target has no such character,
 * -2 = no room (E2BIG) */
static int ic_scrivi(IcStato *s, unsigned int cp, unsigned char *o, size_t n)
{
    unsigned char b = 0;
    int           be, k = 0, i;

    switch (s->verso) {
    case IC_UTF8:
        if (cp < 0x80)         { if (n < 1) return -2; o[0] = (unsigned char)cp; return 1; }
        if (cp < 0x800)        { if (n < 2) return -2; o[0] = (unsigned char)(0xC0 | (cp >> 6)); o[1] = (unsigned char)(0x80 | (cp & 0x3F)); return 2; }
        if (cp < 0x10000)      { if (n < 3) return -2; o[0] = (unsigned char)(0xE0 | (cp >> 12)); o[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (unsigned char)(0x80 | (cp & 0x3F)); return 3; }
        if (n < 4) return -2;
        o[0] = (unsigned char)(0xF0 | (cp >> 18)); o[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        o[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (unsigned char)(0x80 | (cp & 0x3F));
        return 4;
    case IC_UTF16: case IC_UTF16LE: case IC_UTF16BE: {
        unsigned int u[3], m = 0;
        be = s->verso == IC_UTF16BE;
        if (s->verso == IC_UTF16 && !s->verso_bom_fatto) u[m++] = 0xFEFF;
        if (cp < 0x10000) u[m++] = cp;
        else { cp -= 0x10000; u[m++] = 0xD800 + (cp >> 10); u[m++] = 0xDC00 + (cp & 0x3FF); }
        if (n < 2 * m) return -2;
        for (i = 0; i < (int)m; i++) {
            o[k++] = (unsigned char)(be ? u[i] >> 8 : u[i]);
            o[k++] = (unsigned char)(be ? u[i] : u[i] >> 8);
        }
        s->verso_bom_fatto = 1;
        return k;
    }
    case IC_UTF32: case IC_UTF32LE: case IC_UTF32BE: {
        unsigned int u[2], m = 0, j;
        be = s->verso == IC_UTF32BE;
        if (s->verso == IC_UTF32 && !s->verso_bom_fatto) u[m++] = 0xFEFF;
        u[m++] = cp;
        if (n < 4 * m) return -2;
        for (i = 0; i < (int)m; i++)
            for (j = 0; j < 4; j++) o[k++] = (unsigned char)(u[i] >> (be ? 24 - 8 * j : 8 * j));
        s->verso_bom_fatto = 1;
        return k;
    }
    case IC_ASCII:  if (cp >= 0x80) return -1; b = (unsigned char)cp; break;
    case IC_LATIN1: if (cp >= 0x100) return -1; b = (unsigned char)cp; break;
    case IC_LATIN9:
        if (ic_tabella(cp, ic_latin9_a, 0, 8, &b)) { b = (unsigned char)ic_latin9_da[b]; break; }
        if (cp >= 0x100) return -1;
        for (i = 0; i < 8; i++) if (cp == ic_latin9_da[i]) return -1;   /* replaced in Latin-9 */
        b = (unsigned char)cp;
        break;
    case IC_CP1252:
        if (cp < 0x80 || (cp >= 0xA0 && cp < 0x100)) { b = (unsigned char)cp; break; }
        if (!ic_tabella(cp, ic_cp1252, 0x80, 32, &b)) return -1;
        break;
    case IC_CP437:
        if (cp < 0x80) { b = (unsigned char)cp; break; }
        if (!ic_tabella(cp, ic_cp437, 0x80, 128, &b)) return -1;
        break;
    default:
        return -1;
    }
    if (n < 1) return -2;
    o[0] = b;
    return 1;
}

size_t iconv(iconv_t cd, char **in, size_t *in_n, char **out, size_t *out_n)
{
    IcStato *s = (IcStato *)cd;
    size_t   fatti = 0;          /* irreversible conversions, as POSIX counts them */

    if (!s || cd == (iconv_t)-1) { errno = EBADF; return (size_t)-1; }
    if (!in || !*in) {            /* back to the initial state */
        s->da_bom_visto = 0; s->da_be = 1; s->verso_bom_fatto = 0;
        return 0;
    }
    while (*in_n > 0) {
        unsigned int cp = 0;
        size_t       usati = 0;
        int          r = ic_leggi(s, (const unsigned char *)*in, *in_n, &cp, &usati), w;

        if (r == 0) { *in += usati; *in_n -= usati; continue; }   /* a BOM */
        if (r == -2) { errno = EINVAL; return (size_t)-1; }
        if (r == -1) {
            if (!s->ignora) { errno = EILSEQ; return (size_t)-1; }
            *in += 1; *in_n -= 1; fatti++;
            continue;
        }
        w = ic_scrivi(s, cp, (unsigned char *)*out, *out_n);
        if (w == -1) {
            if (s->traslittera) w = ic_scrivi(s, '?', (unsigned char *)*out, *out_n);
            else if (s->ignora) w = 0;
            else { errno = EILSEQ; return (size_t)-1; }
            if (w >= 0) fatti++;
        }
        if (w == -2) { errno = E2BIG; return (size_t)-1; }
        *out += w; *out_n -= (size_t)w;
        *in += r; *in_n -= (size_t)r;
    }
    return fatti;
}
