/* =============================================================================
 * lib/extar/extar.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * tar and gzip: ustar headers of 512 bytes, gzip = RFC 1952 around the
 * DEFLATE of lib/exzip/deflate.c, read back with lib/eximg/inflate.c. What
 * this is for, and why it is compiled in rather than shared: extar.h.
 *
 * Covered: ustar, GNU long names ('L'), pax headers (their "path" is
 * honoured, the rest skipped). Not covered: links, devices, sparse files —
 * they are listed, and extracting them says so.
 * ============================================================================= */

#include "libc.h"
#include "inflate.h"
#include "deflate.h"
#include "extar.h"

#define BLOCCO   512
#define PERC_MAX 512

typedef struct {
    char          nome[EXTAR_NOME_MAX];
    unsigned long dim;
    long          dati;         /* where the data starts in the buffer */
    unsigned long mtime;
    char          tipo;
} Voce;

struct ExTar {
    int            scrittura;

    /* reading */
    unsigned char *d;
    long           n;
    long           fine;        /* where the end-of-archive blocks start */
    int            era_gz;
    Voce          *v;
    unsigned int   nv, cap;

    /* writing */
    int            fd;
    int            gz;
    unsigned long  crc, isize;
    int            ok;
    char           tmp[PERC_MAX];      /* ex_tar_riapri: the new file ... */
    char           finale[PERC_MAX];   /* ... and the one it replaces */
};

static char    g_err[200];
static void  (*g_eco)(const char *) = 0;
static int     g_livello = 2;

static void errore(const char *s)
{
    strncpy(g_err, s, sizeof(g_err) - 1);
    g_err[sizeof(g_err) - 1] = '\0';
}

const char *ex_tar_errore(void)          { return g_err; }
void ex_tar_eco(void (*f)(const char *)) { g_eco = f; }
void ex_tar_livello(unsigned int l)      { g_livello = (l >= 1 && l <= 3) ? (int)l : 2; }

/* ---------------------------------------------------------------------------
 * CRC32 (the gzip trailer)
 * ------------------------------------------------------------------------- */
static unsigned long g_crc_tab[256];
static int           g_crc_pronta = 0;

static unsigned long crc_agg(unsigned long c, const unsigned char *p, unsigned long n)
{
    if (!g_crc_pronta) {
        unsigned long k;
        int i, j;

        for (i = 0; i < 256; i++) {
            k = (unsigned long)i;
            for (j = 0; j < 8; j++) k = (k & 1) ? 0xEDB88320ul ^ (k >> 1) : k >> 1;
            g_crc_tab[i] = k;
        }
        g_crc_pronta = 1;
    }
    while (n--) c = g_crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c;
}

/* ---------------------------------------------------------------------------
 * The output: a plain file, or DEFLATE into a gzip file
 * ------------------------------------------------------------------------- */
static int scrivi_tutto(int fd, const unsigned char *p, unsigned int n)
{
    while (n > 0) {
        int w = write(fd, p, n);
        if (w <= 0) return 0;
        p += w; n -= (unsigned int)w;
    }
    return 1;
}

static int defl_al_file(void *chi, const unsigned char *p, unsigned int n)
{
    return scrivi_tutto(((ExTar *)chi)->fd, p, n);
}

static int uscita(ExTar *w, const unsigned char *p, unsigned int n)
{
    if (!w->ok) return 0;
    if (!w->gz) w->ok = scrivi_tutto(w->fd, p, n);
    else {
        w->crc = crc_agg(w->crc, p, n);
        w->isize += n;
        w->ok = defl_dati(p, n);
    }
    if (!w->ok) errore("errore di scrittura (disco pieno?)");
    return w->ok;
}

static ExTar *scrittore(const char *perc, int gz)
{
    static const unsigned char testa[10] = { 0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3 };
    ExTar *w = (ExTar *)malloc(sizeof(ExTar));

    if (!w) { errore("non c'e' memoria"); return 0; }
    memset(w, 0, sizeof(*w));
    w->scrittura = 1;
    w->ok = 1;
    w->gz = gz;
    w->fd = open(perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (w->fd < 0) { errore("l'archivio non si crea"); free(w); return 0; }
    if (gz) {
        w->crc = 0xFFFFFFFFul;
        defl_livello(g_livello);
        if (!scrivi_tutto(w->fd, testa, 10) || !defl_apri(defl_al_file, w)) {
            errore("non c'e' memoria per comprimere");
            close(w->fd); free(w); return 0;
        }
    }
    return w;
}

/* ---------------------------------------------------------------------------
 * Reading a whole file, and opening a gzip in memory
 * ------------------------------------------------------------------------- */
static unsigned char *leggi_file(const char *nome, long *n)
{
    struct stat st;
    unsigned char *d;
    long letti = 0;
    int fd;

    if (stat(nome, &st) != 0) { errore("il file non c'e'"); return 0; }
    if ((unsigned long)st.st_size > EXTAR_LEGGI_MAX) { errore("il file e' troppo grande (oltre 64 MB)"); return 0; }
    fd = open(nome, O_RDONLY, 0);
    if (fd < 0) { errore("il file non si apre"); return 0; }
    d = (unsigned char *)malloc(st.st_size ? (unsigned long)st.st_size : 1);
    if (!d) { close(fd); errore("non c'e' memoria per leggerlo"); return 0; }
    while (letti < (long)st.st_size) {
        int r = read(fd, d + letti, (unsigned int)(st.st_size - letti));
        if (r <= 0) break;
        letti += r;
    }
    close(fd);
    *n = letti;
    return d;
}

static unsigned long le32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static int e_gzip(const unsigned char *d, long n)
{
    return n >= 18 && d[0] == 0x1f && d[1] == 0x8b;
}

/* The opened data, or 0 with the reason. */
static unsigned char *gz_apri(const unsigned char *d, long n, long *quanti)
{
    long i = 10;
    unsigned long isize;
    unsigned int prodotti = 0;
    unsigned char flg, *out;

    if (!e_gzip(d, n) || d[2] != 8) { errore("non e' un gzip (deflate)"); return 0; }
    flg = d[3];
    if (flg & 0x04) i += 2 + d[i] + (d[i + 1] << 8);
    if (flg & 0x08) while (i < n && d[i++] != 0) { }
    if (flg & 0x10) while (i < n && d[i++] != 0) { }
    if (flg & 0x02) i += 2;
    if (i >= n - 8) { errore("il gzip finisce a meta'"); return 0; }

    isize = le32(d + n - 4);
    if (isize > EXTAR_LEGGI_MAX) { errore("il gzip dice di contenere piu' di 64 MB: rifiutato"); return 0; }
    out = (unsigned char *)malloc(isize ? isize : 1);
    if (!out) { errore("non c'e' memoria per aprire il gzip"); return 0; }
    if (inflate(d + i, (unsigned int)(n - i - 8), out, (unsigned int)isize, &prodotti) != 0 ||
        prodotti != isize) {
        errore("i dati compressi sono rovinati"); free(out); return 0;
    }
    if ((crc_agg(0xFFFFFFFFul, out, isize) ^ 0xFFFFFFFFul) != le32(d + n - 8)) {
        errore("il CRC32 non torna: il file e' rovinato"); free(out); return 0;
    }
    *quanti = (long)isize;
    return out;
}

/* ---------------------------------------------------------------------------
 * tar headers
 * ------------------------------------------------------------------------- */
static unsigned long ottale(const unsigned char *p, int max)
{
    unsigned long v = 0;
    int i = 0;

    while (i < max && (p[i] == ' ' || p[i] == 0)) i++;
    for (; i < max && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (p[i] - '0');
    return v;
}

static void metti_ottale(unsigned char *p, int larg, unsigned long v)
{
    int i;

    p[larg - 1] = 0;
    for (i = larg - 2; i >= 0; i--) { p[i] = (unsigned char)('0' + (v & 7)); v >>= 3; }
}

static unsigned long somma(const unsigned char *h)
{
    unsigned long s = 0;
    int i;

    for (i = 0; i < BLOCCO; i++) s += (i >= 148 && i < 156) ? ' ' : h[i];
    return s;
}

static int blocco_vuoto(const unsigned char *h)
{
    int i;

    for (i = 0; i < BLOCCO; i++) if (h[i]) return 0;
    return 1;
}

static long arrotonda(unsigned long dim)
{
    return (long)((dim + BLOCCO - 1) / BLOCCO * BLOCCO);
}

static int testa_scrivi(ExTar *w, const char *nome, char tipo, unsigned long dim,
                        unsigned int modo, unsigned long mtime)
{
    unsigned char h[BLOCCO];
    unsigned int  l = (unsigned int)strlen(nome);

    memset(h, 0, BLOCCO);
    if (l > 100) {
        /* GNU long name: an 'L' entry carrying the name as data */
        unsigned char d[BLOCCO];
        unsigned int  fatti;
        if (!testa_scrivi(w, "././@LongLink", 'L', l + 1, 0644, 0)) return 0;
        for (fatti = 0; fatti <= l; fatti += BLOCCO) {
            memset(d, 0, BLOCCO);
            memcpy(d, nome + fatti, (l + 1 - fatti) < BLOCCO ? (l + 1 - fatti) : BLOCCO);
            if (!uscita(w, d, BLOCCO)) return 0;
        }
        l = 100;
    }
    memcpy(h, nome, l);
    metti_ottale(h + 100, 8, modo);
    metti_ottale(h + 108, 8, 0);
    metti_ottale(h + 116, 8, 0);
    metti_ottale(h + 124, 12, dim);
    metti_ottale(h + 136, 12, mtime);
    h[156] = (unsigned char)tipo;
    memcpy(h + 257, "ustar", 6);
    h[263] = '0'; h[264] = '0';
    metti_ottale(h + 148, 7, somma(h));
    h[155] = ' ';
    return uscita(w, h, BLOCCO);
}

/* Finds "path=" in a pax extended header. */
static int pax_path(const unsigned char *d, unsigned long n, char *fuori)
{
    unsigned long i = 0;

    while (i < n) {
        unsigned long len = 0, j = i;
        while (j < n && d[j] >= '0' && d[j] <= '9') len = len * 10 + (d[j++] - '0');
        if (len == 0 || i + len > n) return 0;
        if (j + 6 < i + len && memcmp(d + j, " path=", 6) == 0) {
            unsigned long l = i + len - (j + 6) - 1;
            if (l >= EXTAR_NOME_MAX) return 0;
            memcpy(fuori, d + j + 6, l); fuori[l] = 0;
            return 1;
        }
        i += len;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Reading: the whole archive in memory, and an index of its entries
 * ------------------------------------------------------------------------- */
static int indicizza(ExTar *t)
{
    char lungo[EXTAR_NOME_MAX];
    long i = 0;

    lungo[0] = 0;
    t->fine = 0;
    while (i + BLOCCO <= t->n) {
        unsigned char *h = t->d + i;
        unsigned long dim;
        char tipo, *s;
        Voce *v;

        if (blocco_vuoto(h)) break;
        if (somma(h) != ottale(h + 148, 8)) {
            errore("un'intestazione e' rovinata (la somma non torna)");
            return i == 0 ? 0 : 1;      /* what came before is still good */
        }
        dim  = ottale(h + 124, 12);
        tipo = (char)h[156];
        i += BLOCCO;
        if ((unsigned long)(t->n - i) < dim) { errore("l'archivio finisce a meta'"); return 0; }

        if (tipo == 'L' || tipo == 'x') {
            if (tipo == 'L') {
                unsigned long l = dim < EXTAR_NOME_MAX ? dim : EXTAR_NOME_MAX - 1;
                memcpy(lungo, t->d + i, l); lungo[l] = 0;
            } else if (!pax_path(t->d + i, dim, lungo)) {
                lungo[0] = 0;
            }
            i += arrotonda(dim);
            t->fine = i;
            continue;
        }
        if (tipo == 'g') { i += arrotonda(dim); t->fine = i; continue; }

        if (t->nv == t->cap) {
            unsigned int nc = t->cap ? t->cap * 2 : 64;
            Voce *nuove = (Voce *)malloc(nc * sizeof(Voce));
            if (!nuove) { errore("non c'e' memoria per l'elenco"); return 0; }
            if (t->v) { memcpy(nuove, t->v, t->nv * sizeof(Voce)); free(t->v); }
            t->v = nuove;
            t->cap = nc;
        }
        v = &t->v[t->nv];
        if (lungo[0]) {
            snprintf(v->nome, sizeof(v->nome), "%s", lungo);
            lungo[0] = 0;
        } else if (memcmp(h + 257, "ustar", 5) == 0 && h[345]) {
            snprintf(v->nome, sizeof(v->nome), "%.155s/%.100s", (char *)h + 345, (char *)h);
        } else {
            snprintf(v->nome, sizeof(v->nome), "%.100s", (char *)h);
        }

        /* "./sub/x" is what GNU tar writes for `tar cf a.tar .`: the "./"
         * says nothing, and the "./" entry itself is no entry at all. */
        s = v->nome;
        while (s[0] == '.' && s[1] == '/') { s += 2; while (*s == '/') s++; }
        if (s != v->nome) memmove(v->nome, s, strlen(s) + 1);

        v->dim   = dim;
        v->dati  = i;
        v->tipo  = tipo;
        v->mtime = ottale(h + 136, 12);
        i += arrotonda(dim);
        t->fine = i;
        if (v->nome[0] && strcmp(v->nome, ".") != 0) t->nv++;
    }
    return 1;
}

ExTar *ex_tar_apri(const char *percorso)
{
    ExTar *t;
    long   n;
    unsigned char *d;

    g_err[0] = '\0';           /* a warning after a good opening means something */
    if (!(d = leggi_file(percorso, &n))) return 0;
    t = (ExTar *)malloc(sizeof(ExTar));
    if (!t) { free(d); errore("non c'e' memoria"); return 0; }
    memset(t, 0, sizeof(*t));
    t->fd = -1;

    if (e_gzip(d, n)) {
        long m;
        unsigned char *a = gz_apri(d, n, &m);
        free(d);
        if (!a) { free(t); return 0; }
        d = a; n = m;
        t->era_gz = 1;
    } else if (n >= BLOCCO && !blocco_vuoto(d) && memcmp(d + 257, "ustar", 5) != 0 &&
               somma(d) != ottale(d + 148, 8)) {
        free(d); free(t);
        errore("non e' un archivio tar");
        return 0;
    }
    t->d = d;
    t->n = n;
    if (!indicizza(t)) { ex_tar_chiudi(t); return 0; }
    return t;
}

unsigned int ex_tar_quante(ExTar *t) { return (t && !t->scrittura) ? t->nv : 0; }

int ex_tar_voce(ExTar *t, unsigned int i, ExTarVoce *v)
{
    const Voce *s;
    time_t      tt;
    struct tm  *tm;
    size_t      l;

    if (!t || t->scrittura || i >= t->nv) return 0;
    s = &t->v[i];
    memset(v, 0, sizeof(*v));
    strcpy(v->nome, s->nome);
    v->dim  = s->dim;
    v->tipo = s->tipo ? s->tipo : '0';
    l = strlen(v->nome);
    v->directory = s->tipo == '5' || (l > 0 && v->nome[l - 1] == '/');
    if (s->tipo == '5' && l > 0 && v->nome[l - 1] != '/' && l + 1 < EXTAR_NOME_MAX) {
        v->nome[l] = '/'; v->nome[l + 1] = '\0';
    }
    tt = (time_t)s->mtime;
    if (s->mtime && (tm = gmtime(&tt)) != 0) {
        v->anno = (unsigned int)tm->tm_year + 1900; v->mese = (unsigned int)tm->tm_mon + 1;
        v->giorno = (unsigned int)tm->tm_mday;
        v->ore = (unsigned int)tm->tm_hour; v->minuti = (unsigned int)tm->tm_min;
    }
    return 1;
}

/* ! A FAILED mkdir IS ASKED AGAIN WITH stat, NOT WITH errno. On EX-OS a
 * directory that already exists does not always answer EEXIST (a mount
 * point like /disk, or "." does not), and trusting errno made every
 * extraction under -C /disk/x fail on its first component. */
static int cartella_pronta(const char *perc)
{
    struct stat st;

    if (mkdir(perc, 0755) == 0) return 1;
    return stat(perc, &st) == 0 && S_ISDIR(st.st_mode);
}

static int crea_cartelle(char *perc, int anche_ultimo)
{
    char *p;

    for (p = perc + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (!cartella_pronta(perc)) { *p = '/'; return 0; }
        *p = '/';
    }
    if (anche_ultimo && !cartella_pronta(perc)) return 0;
    return 1;
}

/* A name from an archive may not climb out of the destination. */
static int nome_sicuro(const char *n)
{
    const char *p = n;

    if (*n == '/') return 0;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == 0)) return 0;
        while (*p && *p != '/') p++;
        while (*p == '/') p++;
    }
    return 1;
}

int ex_tar_estrai(ExTar *t, unsigned int i, const char *dove)
{
    const Voce *v;
    char        dest[PERC_MAX];
    size_t      l;

    if (!t || t->scrittura || i >= t->nv) { errore("non c'e' quella voce"); return 0; }
    v = &t->v[i];
    if (strlen(dove) >= sizeof(dest)) { errore("percorso troppo lungo"); return 0; }
    strcpy(dest, dove);

    if (v->tipo == '5') {
        l = strlen(dest);
        while (l > 1 && dest[l - 1] == '/') dest[--l] = 0;
        if (!crea_cartelle(dest, 1)) { errore("la cartella non si crea"); return 0; }
        return 1;
    }
    if (v->tipo == '0' || v->tipo == 0 || v->tipo == '7') {
        int fd, ok;

        crea_cartelle(dest, 0);
        fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ok = fd >= 0 && scrivi_tutto(fd, t->d + v->dati, (unsigned int)v->dim);
        if (fd >= 0) close(fd);
        if (!ok) { errore("il file non si scrive"); return 0; }
        return 1;
    }
    errore("tipo di voce non supportato (collegamento o dispositivo)");
    return 0;
}

int ex_tar_estrai_tutto(ExTar *t, const char *dove)
{
    char         dest[PERC_MAX], riga[PERC_MAX + 80];
    unsigned int i;
    int          falliti = 0;

    for (i = 0; i < ex_tar_quante(t); i++) {
        const char *nome = t->v[i].nome;

        if (!nome_sicuro(nome)) {
            snprintf(riga, sizeof(riga), "! nome pericoloso saltato: %s", nome);
            if (g_eco) g_eco(riga);
            falliti++;
            continue;
        }
        if (snprintf(dest, sizeof(dest), "%s/%s", dove, nome) >= (int)sizeof(dest)) {
            snprintf(riga, sizeof(riga), "! percorso troppo lungo: %s", nome);
            if (g_eco) g_eco(riga);
            falliti++;
            continue;
        }
        if (!ex_tar_estrai(t, i, dest)) {
            snprintf(riga, sizeof(riga), "! %s: %s", nome, g_err);
            if (g_eco) g_eco(riga);
            falliti++;
            continue;
        }
        if (g_eco) g_eco(nome);
    }
    return falliti;
}

/* ---------------------------------------------------------------------------
 * Writing
 * ------------------------------------------------------------------------- */
ExTar *ex_tar_crea(const char *percorso, int gz)
{
    return scrittore(percorso, gz);
}

/* ! THE NEW FILE IS BORN NEXT TO THE OLD ONE, with an 8.3 name: on FAT long
 * names are read-only here (the same reason as filemgr's FMTMP.TMP). The old
 * archive is removed only in ex_tar_finisci, when the new one is whole. */
ExTar *ex_tar_riapri(const char *percorso)
{
    ExTar *r = ex_tar_apri(percorso), *w;
    char   tmp[PERC_MAX];
    const char *u;

    if (!r) return 0;
    u = strrchr(percorso, '/');
    if (!u) snprintf(tmp, sizeof(tmp), "EXTARTMP.TMP");
    else    snprintf(tmp, sizeof(tmp), "%.*s/EXTARTMP.TMP", (int)(u - percorso), percorso);

    w = scrittore(tmp, r->era_gz);
    if (!w) { ex_tar_chiudi(r); return 0; }
    strcpy(w->tmp, tmp);
    strncpy(w->finale, percorso, sizeof(w->finale) - 1);
    if (r->fine > 0) uscita(w, r->d, (unsigned int)r->fine);
    ex_tar_chiudi(r);
    if (!w->ok) { ex_tar_chiudi(w); unlink(tmp); return 0; }
    return w;
}

int ex_tar_aggiungi(ExTar *w, const char *perc, const char *nome)
{
    struct stat st;

    if (!w || !w->scrittura) { errore("questo archivio non e' in scrittura"); return 0; }
    if (stat(perc, &st) != 0) { errore("il file non c'e'"); return 0; }

    if (S_ISDIR(st.st_mode)) {
        char sotto_p[PERC_MAX], sotto_n[PERC_MAX];
        struct dirent *e;
        DIR *d;
        int ok = 1;

        snprintf(sotto_n, sizeof(sotto_n), "%s/", nome);
        if (!testa_scrivi(w, sotto_n, '5', 0, 0755, (unsigned long)st.st_mtime)) return 0;
        if (g_eco) g_eco(sotto_n);
        d = opendir(perc);
        if (!d) { errore("una cartella non si legge"); return 0; }
        while ((e = readdir(d)) != 0) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (snprintf(sotto_p, sizeof(sotto_p), "%s/%s", perc, e->d_name) >= (int)sizeof(sotto_p) ||
                snprintf(sotto_n, sizeof(sotto_n), "%s/%s", nome, e->d_name) >= (int)sizeof(sotto_n)) {
                errore("un percorso e' troppo lungo"); ok = 0; continue;
            }
            if (!ex_tar_aggiungi(w, sotto_p, sotto_n)) ok = 0;
        }
        closedir(d);
        return ok;
    } else {
        static unsigned char buf[16 * BLOCCO];
        unsigned long resto = (unsigned long)st.st_size;
        int fd = open(perc, O_RDONLY, 0);

        if (fd < 0) { errore("il file non si apre"); return 0; }
        if (!testa_scrivi(w, nome, '0', (unsigned long)st.st_size, 0644,
                          (unsigned long)st.st_mtime)) { close(fd); return 0; }
        if (g_eco) g_eco(nome);
        while (resto > 0) {
            unsigned int voglio = resto < sizeof(buf) ? (unsigned int)resto : sizeof(buf);
            unsigned int tondo  = (voglio + BLOCCO - 1) / BLOCCO * BLOCCO;
            int r = read(fd, buf, voglio);
            if (r != (int)voglio) { close(fd); errore("errore di lettura"); w->ok = 0; return 0; }
            memset(buf + voglio, 0, tondo - voglio);
            if (!uscita(w, buf, tondo)) { close(fd); return 0; }
            resto -= voglio;
        }
        close(fd);
        return 1;
    }
}

/* Counts the files (not the directories) of a tree, for the answer. */
static int conta_file(const char *perc)
{
    struct stat st;
    struct dirent *e;
    DIR *d;
    char sotto[PERC_MAX];
    int n = 0;

    if (stat(perc, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return 1;
    if (!(d = opendir(perc))) return 0;
    while ((e = readdir(d)) != 0) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (snprintf(sotto, sizeof(sotto), "%s/%s", perc, e->d_name) < (int)sizeof(sotto))
            n += conta_file(sotto);
    }
    closedir(d);
    return n;
}

int ex_tar_aggiungi_albero(ExTar *w, const char *cartella, const char *nome, int *saltati)
{
    int ok = ex_tar_aggiungi(w, cartella, nome);

    if (saltati) *saltati = ok ? 0 : 1;
    if (!ok && (!w || !w->ok)) return -1;
    return conta_file(cartella);
}

int ex_tar_finisci(ExTar *w)
{
    unsigned char zero[BLOCCO];
    int ok;

    if (!w || !w->scrittura) { errore("questo archivio non e' in scrittura"); return 0; }
    memset(zero, 0, BLOCCO);
    uscita(w, zero, BLOCCO);
    uscita(w, zero, BLOCCO);
    ok = w->ok;
    if (w->gz) {
        unsigned char coda[8];
        unsigned long c = w->crc ^ 0xFFFFFFFFul;
        int i;

        ok = defl_fine() && ok;
        for (i = 0; i < 4; i++) {
            coda[i]     = (unsigned char)(c >> (8 * i));
            coda[4 + i] = (unsigned char)(w->isize >> (8 * i));
        }
        ok = ok && scrivi_tutto(w->fd, coda, 8);
        w->gz = 0;                  /* the stream is closed: not twice */
    }
    close(w->fd);
    w->fd = -1;
    w->ok = 0;                      /* nothing more goes in */
    if (!ok) { errore("errore di scrittura (disco pieno?)"); return 0; }

    if (w->finale[0]) {
        unlink(w->finale);
        if (rename(w->tmp, w->finale) != 0) {
            snprintf(g_err, sizeof(g_err), "il nuovo archivio e' rimasto in %s", w->tmp);
            return 0;
        }
        w->finale[0] = '\0';
    }
    return 1;
}

void ex_tar_chiudi(ExTar *t)
{
    if (!t) return;
    if (t->scrittura && t->fd >= 0) {
        /* Closed without finishing: the half file is thrown away. */
        if (t->gz) defl_fine();
        close(t->fd);
        if (t->tmp[0]) unlink(t->tmp);
    }
    if (t->d) free(t->d);
    if (t->v) free(t->v);
    free(t);
}

int ex_tar_nome(const char *p, int *gz)
{
    size_t l = strlen(p);
    int    z = 0, si = 0;

    if (l > 4 && strcmp(p + l - 4, ".tar") == 0) si = 1;
    else if (l > 7 && strcmp(p + l - 7, ".tar.gz") == 0) si = z = 1;
    else if (l > 4 && strcmp(p + l - 4, ".tgz") == 0) si = z = 1;
    if (gz) *gz = z;
    return si;
}

/* ---------------------------------------------------------------------------
 * gzip / gunzip of one file
 * ------------------------------------------------------------------------- */
int ex_gz_comprimi(const char *da, const char *a)
{
    static unsigned char buf[8192];
    ExTar *w;
    int fd, r, ok;

    fd = open(da, O_RDONLY, 0);
    if (fd < 0) { errore("il file non si apre"); return 0; }
    if (!(w = scrittore(a, 1))) { close(fd); return 0; }
    while ((r = read(fd, buf, sizeof(buf))) > 0)
        if (!uscita(w, buf, (unsigned int)r)) break;
    close(fd);
    ok = w->ok;
    {
        unsigned char coda[8];
        unsigned long c = w->crc ^ 0xFFFFFFFFul;
        int i;

        ok = defl_fine() && ok;
        for (i = 0; i < 4; i++) {
            coda[i]     = (unsigned char)(c >> (8 * i));
            coda[4 + i] = (unsigned char)(w->isize >> (8 * i));
        }
        ok = ok && scrivi_tutto(w->fd, coda, 8);
    }
    close(w->fd);
    free(w);
    if (!ok) errore("errore di scrittura (disco pieno?)");
    return ok;
}

int ex_gz_espandi(const char *da, const char *a)
{
    long n, m;
    unsigned char *d, *x;
    int fd, ok;

    if (!(d = leggi_file(da, &n))) return 0;
    x = gz_apri(d, n, &m);
    free(d);
    if (!x) return 0;
    fd = open(a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ok = fd >= 0 && scrivi_tutto(fd, x, (unsigned int)m);
    if (fd >= 0) close(fd);
    free(x);
    if (!ok) errore("il file non si scrive");
    return ok;
}
