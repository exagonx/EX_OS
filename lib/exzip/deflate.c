/* =============================================================================
 * lib/exzip/deflate.c — the DEFLATE compressor (RFC 1951)
 *
 * The why and the contract are in deflate.h. Here is the how, which is the
 * classic one — the same shape as zlib's deflate_slow(), written again:
 *
 *   - LZ77 over a 32 KB window, kept in a 64 KB buffer that slides by half;
 *   - hash chains on three bytes to find earlier copies, at most CATENA
 *     candidates per position;
 *   - LAZY matching: before taking a match, look one byte further; if a
 *     longer match starts there, the current byte goes out as a literal;
 *   - the symbols of a block (up to SIMBOLI) are kept, and then the block is
 *     written with the cheaper of two trees: the fixed one of the RFC, or a
 *     dynamic one built from what the block actually contains.
 *
 * ! THE CHECK IS OUTSIDE, ON THE HOST: tools/prova_deflate.sh compresses a
 * set of awkward files and reads every stream back twice, with
 * lib/eximg/inflate.c and with Python's zlib — a decoder that is not ours.
 * Inside EX-OS exzip.c checks the one promise it relies on: the second pass
 * produces exactly as many bytes as the first one counted.
 * ============================================================================= */
#include "libc.h"
#include "deflate.h"

#define WSIZE       32768
#define WMASK       (WSIZE - 1)
#define HBITS       15
#define HSIZE       (1 << HBITS)
#define HMASK       (HSIZE - 1)
#define MIN_MATCH   3
#define MAX_MATCH   258
#define MIN_LOOK    (MAX_MATCH + MIN_MATCH + 1)
#define MAX_DIST    (WSIZE - MIN_LOOK)
#define NIL         (-1)

/* How hard to look. These are zlib's numbers for its default level (6):
 * they are the ones everybody has measured. */
#define CATENA      128         /* candidates per position */
#define BUONA       8           /* a match this long: look a quarter as hard */
#define PIGRO       16          /* a match this long: do not try the next byte */
#define LONTANO     4096        /* a 3-byte match farther than this is not worth it */

#define SIMBOLI     16384       /* symbols per block */

#define L_CODICI    286         /* literal/length codes actually used */
#define L_TUTTI     288         /* the fixed tree names 288 */
#define D_CODICI    30
#define C_CODICI    19

typedef struct {
    unsigned char  win[2 * WSIZE];
    int            head[HSIZE];
    int            prev[WSIZE];
    int            strstart, lookahead;
    int            match_start, prev_match;
    int            match_length, prev_length;
    int            match_available;

    /* The block being collected: a literal (s_dist == 0) or a length-3 and
     * a distance. */
    unsigned char  s_lit[SIMBOLI];
    unsigned short s_dist[SIMBOLI];
    unsigned int   n_sim;
    unsigned int   f_lit[L_TUTTI];
    unsigned int   f_dist[D_CODICI];

    /* Bits out, least significant first as the RFC wants. */
    unsigned long  bitbuf;
    unsigned int   bitcnt;
    unsigned char  out[4096];
    unsigned int   n_out;
    unsigned long  prodotti;
    DeflScrivi     scrivi;
    void          *chi;
    int            guasto;
} Stato;

static Stato *S = 0;

static const unsigned short len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const unsigned short dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577 };
static const unsigned char dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static const unsigned char ordine_cl[C_CODICI] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* Length 3..258 -> length code 0..28, filled once. */
static unsigned char g_lcode[256];

static void tavole(void)
{
    int c, k;

    for (c = 0; c < 28; c++)
        for (k = 0; k < (1 << len_extra[c]); k++)
            if (len_base[c] + k < 258) g_lcode[len_base[c] + k - 3] = (unsigned char)c;
    /* ! 258 HAS A CODE OF ITS OWN, 285, even though code 284 with five extra
     * bits could reach it too. The RFC says 285, and inflaters check it. */
    g_lcode[258 - 3] = 28;
}

static int dcode(int d)
{
    int c = 29;

    while (dist_base[c] > d) c--;
    return c;
}

/* -----------------------------------------------------------------------------
 * Bits out
 * --------------------------------------------------------------------------- */
static void svuota(void)
{
    if (S->n_out && S->scrivi && !S->guasto &&
        !S->scrivi(S->chi, S->out, S->n_out))
        S->guasto = 1;
    S->prodotti += S->n_out;
    S->n_out = 0;
}

static void byte_out(unsigned int b)
{
    if (S->n_out == sizeof(S->out)) svuota();
    S->out[S->n_out++] = (unsigned char)b;
}

/* At most 16 bits at a time, and the buffer never holds more than 7 before:
 * 23 bits fit in an unsigned long of 32. */
static void bits(unsigned int v, unsigned int n)
{
    S->bitbuf |= (unsigned long)v << S->bitcnt;
    S->bitcnt += n;
    while (S->bitcnt >= 8) {
        byte_out((unsigned int)(S->bitbuf & 0xFF));
        S->bitbuf >>= 8;
        S->bitcnt -= 8;
    }
}

/* -----------------------------------------------------------------------------
 * Huffman: lengths from frequencies, codes from lengths
 * --------------------------------------------------------------------------- */

/* ! THE LENGTH LIMIT IS KEPT BY HALVING THE FREQUENCIES, not by the optimal
 * algorithm (package-merge). A tree deeper than the limit only happens with
 * very skewed counts; halving flattens them, and a few halvings always end
 * with a tree of equal weights, whose depth is log2 of the symbols — nine
 * for 286. It costs a fraction of a percent on the rare block that needs
 * it, and is twenty lines instead of a hundred. */
static void costruisci(const unsigned int *freq, unsigned int n,
                       unsigned int maxb, unsigned char *len)
{
    static unsigned int  f[L_TUTTI], sym[L_TUTTI];
    static unsigned long w[2 * L_TUTTI];
    static int           par[2 * L_TUTTI];
    static unsigned char vivo[2 * L_TUTTI];
    unsigned int i, m, nodi, k, massimo;

    for (i = 0; i < n; i++) { f[i] = freq[i]; len[i] = 0; }

    for (;;) {
        m = 0;
        for (i = 0; i < n; i++)
            if (f[i]) { sym[m] = i; w[m] = f[i]; par[m] = -1; vivo[m] = 1; m++; }

        if (m == 0) return;
        if (m == 1) { len[sym[0]] = 1; return; }

        nodi = m;
        for (k = 0; k + 1 < m; k++) {
            int a = -1, b = -1;

            for (i = 0; i < nodi; i++) {
                if (!vivo[i]) continue;
                if (a < 0 || w[i] < w[a]) { b = a; a = (int)i; }
                else if (b < 0 || w[i] < w[b]) b = (int)i;
            }
            w[nodi] = w[a] + w[b];
            par[nodi] = -1;
            vivo[nodi] = 1;
            par[a] = par[b] = (int)nodi;
            vivo[a] = vivo[b] = 0;
            nodi++;
        }

        massimo = 0;
        for (i = 0; i < m; i++) {
            unsigned int d = 0;
            int j = (int)i;

            while (par[j] >= 0) { j = par[j]; d++; }
            len[sym[i]] = (unsigned char)d;
            if (d > massimo) massimo = d;
        }
        if (massimo <= maxb) return;

        for (i = 0; i < n; i++) { if (f[i]) f[i] = (f[i] + 1) >> 1; len[i] = 0; }
    }
}

/* Canonical codes (RFC 1951, 3.2.2), bit-reversed: the codes go out most
 * significant bit first, and bits() writes least significant first. */
static void codici(const unsigned char *len, unsigned int n, unsigned short *cod)
{
    unsigned int conta[16], prossimo[16], i, b, c = 0;

    for (b = 0; b < 16; b++) conta[b] = 0;
    for (i = 0; i < n; i++) conta[len[i]]++;
    conta[0] = 0;
    for (b = 1; b < 16; b++) { c = (c + conta[b - 1]) << 1; prossimo[b] = c; }

    for (i = 0; i < n; i++) {
        unsigned int v, r = 0, k;

        cod[i] = 0;
        if (!len[i]) continue;
        v = prossimo[len[i]]++;
        for (k = 0; k < len[i]; k++) { r = (r << 1) | (v & 1); v >>= 1; }
        cod[i] = (unsigned short)r;
    }
}

/* A tree must have at least two codes, or it is not a complete prefix code
 * and some inflaters refuse it. One used code gets a partner of length 1. */
static void almeno_due(unsigned char *len, unsigned int n)
{
    unsigned int i, usati = 0, uno = 0;

    for (i = 0; i < n; i++) if (len[i]) { usati++; uno = i; }
    if (usati >= 2) return;
    if (usati == 0) { len[0] = 1; len[1] = 1; return; }
    len[uno] = 1;
    len[uno == 0 ? 1 : 0] = 1;
}

/* -----------------------------------------------------------------------------
 * A block
 * --------------------------------------------------------------------------- */
static unsigned long costo_dati(const unsigned char *ll, const unsigned char *dl)
{
    unsigned long c = 0;
    unsigned int  i;

    for (i = 0; i < L_CODICI; i++) {
        if (!S->f_lit[i]) continue;
        c += (unsigned long)S->f_lit[i] *
             (ll[i] + (i >= 257 ? len_extra[i - 257] : 0));
    }
    for (i = 0; i < D_CODICI; i++)
        c += (unsigned long)S->f_dist[i] * (dl[i] + dist_extra[i]);
    return c;
}

static void scrivi_simboli(const unsigned char *ll, const unsigned short *lc,
                           const unsigned char *dl, const unsigned short *dc)
{
    unsigned int i;

    for (i = 0; i < S->n_sim; i++) {
        if (S->s_dist[i] == 0) {
            bits(lc[S->s_lit[i]], ll[S->s_lit[i]]);
        } else {
            int l = S->s_lit[i] + 3, d = S->s_dist[i];
            int c = g_lcode[l - 3];
            int e = dcode(d);

            bits(lc[257 + c], ll[257 + c]);
            if (len_extra[c]) bits((unsigned int)(l - len_base[c]), len_extra[c]);
            bits(dc[e], dl[e]);
            if (dist_extra[e]) bits((unsigned int)(d - dist_base[e]), dist_extra[e]);
        }
    }
    bits(lc[256], ll[256]);
}

static void blocco(int ultimo)
{
    static unsigned char  ll[L_TUTTI], dl[D_CODICI + 2], cl[C_CODICI];
    static unsigned short lc[L_TUTTI], dc[D_CODICI + 2], cc[C_CODICI];
    static unsigned char  seq[L_CODICI + D_CODICI];
    static unsigned char  rle_s[L_CODICI + D_CODICI], rle_x[L_CODICI + D_CODICI];
    unsigned int  f_cl[C_CODICI];
    unsigned int  hlit, hdist, hclen, tot, n_rle = 0, i;
    unsigned long c_din, c_fis;

    S->f_lit[256] = 1;

    /* --- the dynamic trees, and what they would cost ------------------- */
    costruisci(S->f_lit, L_CODICI, 15, ll);
    ll[286] = ll[287] = 0;
    almeno_due(ll, L_CODICI);
    costruisci(S->f_dist, D_CODICI, 15, dl);
    almeno_due(dl, D_CODICI);

    hlit = L_CODICI;
    while (hlit > 257 && ll[hlit - 1] == 0) hlit--;
    hdist = D_CODICI;
    while (hdist > 1 && dl[hdist - 1] == 0) hdist--;

    for (i = 0; i < hlit; i++)  seq[i] = ll[i];
    for (i = 0; i < hdist; i++) seq[hlit + i] = dl[i];
    tot = hlit + hdist;

    /* The code lengths, run-length coded: 16 repeats the last length 3..6
     * times, 17 and 18 are runs of zeros (3..10, 11..138). */
    for (i = 0; i < C_CODICI; i++) f_cl[i] = 0;
    i = 0;
    while (i < tot) {
        unsigned int cur = seq[i], j = i;

        while (j < tot && seq[j] == cur) j++;

        if (cur == 0) {
            unsigned int r = j - i;

            while (r >= 3) {
                unsigned int n = r > 138 ? 138 : r;

                if (n >= 11) { rle_s[n_rle] = 18; rle_x[n_rle++] = (unsigned char)(n - 11); }
                else         { rle_s[n_rle] = 17; rle_x[n_rle++] = (unsigned char)(n - 3); }
                f_cl[rle_s[n_rle - 1]]++;
                r -= n; i += n;
            }
            while (r--) { rle_s[n_rle] = 0; rle_x[n_rle++] = 0; f_cl[0]++; i++; }
        } else {
            unsigned int r;

            rle_s[n_rle] = (unsigned char)cur; rle_x[n_rle++] = 0; f_cl[cur]++;
            i++;
            r = j - i;
            while (r >= 3) {
                unsigned int n = r > 6 ? 6 : r;

                rle_s[n_rle] = 16; rle_x[n_rle++] = (unsigned char)(n - 3); f_cl[16]++;
                r -= n; i += n;
            }
            while (r--) { rle_s[n_rle] = (unsigned char)cur; rle_x[n_rle++] = 0; f_cl[cur]++; i++; }
        }
    }

    costruisci(f_cl, C_CODICI, 7, cl);
    almeno_due(cl, C_CODICI);
    hclen = C_CODICI;
    while (hclen > 4 && cl[ordine_cl[hclen - 1]] == 0) hclen--;

    c_din = 3 + 5 + 5 + 4 + 3ul * hclen;
    for (i = 0; i < n_rle; i++)
        c_din += cl[rle_s[i]] + (rle_s[i] == 16 ? 2 : rle_s[i] == 17 ? 3 :
                                 rle_s[i] == 18 ? 7 : 0);
    c_din += costo_dati(ll, dl);

    /* --- the fixed tree ------------------------------------------------ */
    {
        static unsigned char  fl[L_TUTTI], fd[D_CODICI + 2];
        static unsigned short flc[L_TUTTI], fdc[D_CODICI + 2];

        for (i = 0; i < 144; i++)     fl[i] = 8;
        for (; i < 256; i++)          fl[i] = 9;
        for (; i < 280; i++)          fl[i] = 7;
        for (; i < L_TUTTI; i++)      fl[i] = 8;
        for (i = 0; i < D_CODICI + 2; i++) fd[i] = 5;

        c_fis = 3 + costo_dati(fl, fd);

        if (c_fis <= c_din) {
            codici(fl, L_TUTTI, flc);
            codici(fd, D_CODICI + 2, fdc);
            bits((unsigned int)ultimo, 1);
            bits(1, 2);
            scrivi_simboli(fl, flc, fd, fdc);
            goto fatto;
        }
    }

    /* --- the dynamic tree, written ------------------------------------- */
    codici(ll, L_CODICI, lc);
    codici(dl, D_CODICI, dc);
    codici(cl, C_CODICI, cc);

    bits((unsigned int)ultimo, 1);
    bits(2, 2);
    bits(hlit - 257, 5);
    bits(hdist - 1, 5);
    bits(hclen - 4, 4);
    for (i = 0; i < hclen; i++) bits(cl[ordine_cl[i]], 3);
    for (i = 0; i < n_rle; i++) {
        bits(cc[rle_s[i]], cl[rle_s[i]]);
        if (rle_s[i] == 16) bits(rle_x[i], 2);
        else if (rle_s[i] == 17) bits(rle_x[i], 3);
        else if (rle_s[i] == 18) bits(rle_x[i], 7);
    }
    scrivi_simboli(ll, lc, dl, dc);

fatto:
    S->n_sim = 0;
    for (i = 0; i < L_TUTTI; i++)  S->f_lit[i] = 0;
    for (i = 0; i < D_CODICI; i++) S->f_dist[i] = 0;
}

static void sim_lit(unsigned int c)
{
    S->s_lit[S->n_sim] = (unsigned char)c;
    S->s_dist[S->n_sim] = 0;
    S->f_lit[c]++;
    if (++S->n_sim == SIMBOLI) blocco(0);
}

static void sim_copia(int len, int dist)
{
    S->s_lit[S->n_sim] = (unsigned char)(len - 3);
    S->s_dist[S->n_sim] = (unsigned short)dist;
    S->f_lit[257 + g_lcode[len - 3]]++;
    S->f_dist[dcode(dist)]++;
    if (++S->n_sim == SIMBOLI) blocco(0);
}

/* -----------------------------------------------------------------------------
 * LZ77
 * --------------------------------------------------------------------------- */
static int inserisci(int pos)
{
    int h = ((S->win[pos] << 10) ^ (S->win[pos + 1] << 5) ^ S->win[pos + 2]) & HMASK;
    int hh = S->head[h];

    S->prev[pos & WMASK] = hh;
    S->head[h] = pos;
    return hh;
}

static int piu_lunga(int cur)
{
    unsigned int   catena = CATENA;
    unsigned char *scan = S->win + S->strstart;
    int            best = S->prev_length;
    int            limite = S->strstart > MAX_DIST ? S->strstart - MAX_DIST : NIL;
    int            maxlen = S->lookahead < MAX_MATCH ? S->lookahead : MAX_MATCH;

    if (best >= maxlen) return maxlen;
    if (S->prev_length >= BUONA) catena >>= 2;

    do {
        const unsigned char *m = S->win + cur;
        int len;

        if (m[best] != scan[best] || m[0] != scan[0] || m[1] != scan[1]) continue;

        len = 2;
        while (len < maxlen && m[len] == scan[len]) len++;
        if (len > best) {
            S->match_start = cur;
            best = len;
            if (len >= maxlen) break;
        }
    } while ((cur = S->prev[cur & WMASK]) > limite && --catena != 0);

    return best;
}

/* Half the window goes away: everything moves back WSIZE, and a position
 * that falls off the front becomes NIL. */
static void scorri(void)
{
    int i;

    memcpy(S->win, S->win + WSIZE, WSIZE);
    S->strstart    -= WSIZE;
    S->match_start -= WSIZE;
    S->prev_match  -= WSIZE;
    for (i = 0; i < HSIZE; i++) S->head[i] = S->head[i] >= WSIZE ? S->head[i] - WSIZE : NIL;
    for (i = 0; i < WSIZE; i++) S->prev[i] = S->prev[i] >= WSIZE ? S->prev[i] - WSIZE : NIL;
}

/* zlib's deflate_slow(). Without `fine` it stops while it still has a full
 * match's worth of look-ahead, and waits for more data. */
static void lz(int fine)
{
    for (;;) {
        int hh = NIL;

        if (S->lookahead < MIN_LOOK && !fine) return;
        if (S->lookahead == 0) break;

        if (S->lookahead >= MIN_MATCH) hh = inserisci(S->strstart);

        S->prev_length = S->match_length;
        S->prev_match  = S->match_start;
        S->match_length = MIN_MATCH - 1;

        if (hh != NIL && S->prev_length < PIGRO && S->strstart - hh <= MAX_DIST) {
            S->match_length = piu_lunga(hh);
            if (S->match_length == MIN_MATCH && S->strstart - S->match_start > LONTANO)
                S->match_length = MIN_MATCH - 1;
        }

        if (S->prev_length >= MIN_MATCH && S->match_length <= S->prev_length) {
            int max_ins = S->strstart + S->lookahead - MIN_MATCH;

            sim_copia(S->prev_length, S->strstart - 1 - S->prev_match);
            S->lookahead -= S->prev_length - 1;
            S->prev_length -= 2;
            do {
                if (++S->strstart <= max_ins) inserisci(S->strstart);
            } while (--S->prev_length != 0);
            S->match_available = 0;
            S->match_length = MIN_MATCH - 1;
            S->strstart++;
        } else if (S->match_available) {
            sim_lit(S->win[S->strstart - 1]);
            S->strstart++;
            S->lookahead--;
        } else {
            S->match_available = 1;
            S->strstart++;
            S->lookahead--;
        }
    }
    if (S->match_available) {
        sim_lit(S->win[S->strstart - 1]);
        S->match_available = 0;
    }
}

/* -----------------------------------------------------------------------------
 * The interface
 * --------------------------------------------------------------------------- */
int defl_apri(DeflScrivi scrivi, void *chi)
{
    if (!S) {
        S = (Stato *)malloc(sizeof(Stato));
        if (!S) return 0;
        tavole();
    }
    memset(S->head, 0xFF, sizeof(S->head));     /* all NIL */
    memset(S->prev, 0xFF, sizeof(S->prev));
    S->strstart = S->lookahead = 0;
    S->match_start = S->prev_match = 0;
    S->match_length = S->prev_length = MIN_MATCH - 1;
    S->match_available = 0;
    S->n_sim = 0;
    memset(S->f_lit, 0, sizeof(S->f_lit));
    memset(S->f_dist, 0, sizeof(S->f_dist));
    S->bitbuf = 0;
    S->bitcnt = 0;
    S->n_out = 0;
    S->prodotti = 0;
    S->scrivi = scrivi;
    S->chi = chi;
    S->guasto = 0;
    return 1;
}

int defl_dati(const unsigned char *p, unsigned int n)
{
    while (n) {
        int spazio;
        unsigned int k;

        if (S->strstart >= WSIZE + MAX_DIST) scorri();

        spazio = 2 * WSIZE - (S->strstart + S->lookahead);
        k = n < (unsigned int)spazio ? n : (unsigned int)spazio;
        memcpy(S->win + S->strstart + S->lookahead, p, k);
        S->lookahead += (int)k;
        p += k;
        n -= k;
        lz(0);
    }
    return !S->guasto;
}

int defl_fine(void)
{
    lz(1);
    blocco(1);
    if (S->bitcnt) { byte_out((unsigned int)(S->bitbuf & 0xFF)); S->bitbuf = 0; S->bitcnt = 0; }
    svuota();
    return !S->guasto;
}

unsigned long defl_prodotti(void)
{
    return S ? S->prodotti + S->n_out : 0;
}
