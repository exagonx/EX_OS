/* =============================================================================
 * bin/tar/tar.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * tar and gzip, our own (@TAR-GZ, road 2): ustar headers of 512 bytes, and
 * gzip = RFC 1952 around the DEFLATE of lib/exzip/deflate.c. Reading uses
 * lib/eximg/inflate.c. Both are compiled in: this is a static program and
 * needs no shared library, so it can also live on the floppy.
 *
 * One binary, two names: argv[0] ending in "gzip" or "gunzip" selects gzip.
 *
 *   tar c[z]f ARCHIVE PATH...     create (z = gzip)
 *   tar x[z]f ARCHIVE [-C DIR]    extract (gzip is recognised by itself)
 *   tar t[z]f ARCHIVE             list
 *   gzip FILE                     FILE -> FILE.gz (the original stays)
 *   gzip -d FILE.gz / gunzip      FILE.gz -> FILE
 *
 * ! READING IS WHOLE-FILE: inflate() works on a buffer, not a stream, like
 * netupdate. Writing streams. Covered: ustar, GNU long names ('L'), pax
 * headers are skipped (their "path" is honoured). Not covered: links,
 * devices, sparse files - they are listed and skipped.
 * ============================================================================= */

#include "libc.h"
#include "inflate.h"
#include "deflate.h"

#define BLOCCO   512
#define PERC_MAX 512
#define LEGGI_MAX (64ul * 1024 * 1024)

static const char *g_io = "tar";

/* ---------------------------------------------------------------------------
 * CRC32 (gzip trailer)
 * ------------------------------------------------------------------------- */
static unsigned long g_crc_tab[256];

static void crc_prepara(void)
{
    unsigned long c;
    int n, k;

    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320ul ^ (c >> 1) : c >> 1;
        g_crc_tab[n] = c;
    }
}

static unsigned long crc_agg(unsigned long c, const unsigned char *p, unsigned long n)
{
    while (n--) c = g_crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c;
}

/* ---------------------------------------------------------------------------
 * The output sink: plain file, or DEFLATE into a gzip file
 * ------------------------------------------------------------------------- */
static int           g_fd_out = -1;
static int           g_gz     = 0;
static unsigned long g_crc, g_isize;

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
    (void)chi;
    return scrivi_tutto(g_fd_out, p, n);
}

static int uscita_apri(const char *nome, int gz)
{
    static const unsigned char testa[10] = { 0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3 };

    g_fd_out = open(nome, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_fd_out < 0) { printf("%s: cannot create %s\n", g_io, nome); return 0; }
    g_gz = gz;
    if (!gz) return 1;
    g_crc = 0xFFFFFFFFul; g_isize = 0;
    if (!scrivi_tutto(g_fd_out, testa, 10)) return 0;
    if (!defl_apri(defl_al_file, 0)) { printf("%s: out of memory\n", g_io); return 0; }
    return 1;
}

static int uscita(const unsigned char *p, unsigned int n)
{
    if (!g_gz) return scrivi_tutto(g_fd_out, p, n);
    g_crc = crc_agg(g_crc, p, n);
    g_isize += n;
    return defl_dati(p, n);
}

static int uscita_chiudi(void)
{
    int ok = 1;

    if (g_gz) {
        unsigned char coda[8];
        unsigned long c = g_crc ^ 0xFFFFFFFFul;
        int i;
        ok = defl_fine();
        for (i = 0; i < 4; i++) {
            coda[i]     = (unsigned char)(c >> (8 * i));
            coda[4 + i] = (unsigned char)(g_isize >> (8 * i));
        }
        ok = ok && scrivi_tutto(g_fd_out, coda, 8);
    }
    close(g_fd_out);
    g_fd_out = -1;
    return ok;
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

    if (stat(nome, &st) != 0) { printf("%s: %s not found\n", g_io, nome); return 0; }
    if ((unsigned long)st.st_size > LEGGI_MAX) { printf("%s: %s too big\n", g_io, nome); return 0; }
    fd = open(nome, O_RDONLY, 0);
    if (fd < 0) { printf("%s: cannot open %s\n", g_io, nome); return 0; }
    d = (unsigned char *)malloc(st.st_size ? st.st_size : 1);
    if (!d) { close(fd); printf("%s: out of memory\n", g_io); return 0; }
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

/* Returns the opened data, or 0. */
static unsigned char *gz_apri(const unsigned char *d, long n, long *quanti)
{
    long i = 10;
    unsigned long isize;
    unsigned int prodotti = 0;
    unsigned char flg, *out;

    if (!e_gzip(d, n) || d[2] != 8) { printf("%s: not a gzip (deflate) file\n", g_io); return 0; }
    flg = d[3];
    if (flg & 0x04) i += 2 + d[i] + (d[i + 1] << 8);
    if (flg & 0x08) while (i < n && d[i++] != 0) { }
    if (flg & 0x10) while (i < n && d[i++] != 0) { }
    if (flg & 0x02) i += 2;
    if (i >= n - 8) { printf("%s: truncated gzip\n", g_io); return 0; }

    isize = le32(d + n - 4);
    if (isize > LEGGI_MAX) { printf("%s: gzip says %lu bytes: refused\n", g_io, isize); return 0; }
    out = (unsigned char *)malloc(isize ? isize : 1);
    if (!out) { printf("%s: out of memory\n", g_io); return 0; }
    if (inflate(d + i, (unsigned int)(n - i - 8), out, (unsigned int)isize, &prodotti) != 0 ||
        prodotti != isize) {
        printf("%s: corrupt deflate data\n", g_io); free(out); return 0;
    }
    if ((crc_agg(0xFFFFFFFFul, out, isize) ^ 0xFFFFFFFFul) != le32(d + n - 8)) {
        printf("%s: CRC32 mismatch\n", g_io); free(out); return 0;
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

static int testa_scrivi(const char *nome, char tipo, unsigned long dim, unsigned int modo)
{
    unsigned char h[BLOCCO];
    unsigned int  l = (unsigned int)strlen(nome);

    memset(h, 0, BLOCCO);
    if (l > 100) {
        /* GNU long name: a 'L' entry carrying the name as data */
        unsigned char d[BLOCCO];
        unsigned int  fatti;
        if (!testa_scrivi("././@LongLink", 'L', l + 1, 0644)) return 0;
        for (fatti = 0; fatti <= l; fatti += BLOCCO) {
            memset(d, 0, BLOCCO);
            memcpy(d, nome + fatti, (l + 1 - fatti) < BLOCCO ? (l + 1 - fatti) : BLOCCO);
            if (!uscita(d, BLOCCO)) return 0;
        }
        l = 100;
    }
    memcpy(h, nome, l);
    metti_ottale(h + 100, 8, modo);
    metti_ottale(h + 108, 8, 0);
    metti_ottale(h + 116, 8, 0);
    metti_ottale(h + 124, 12, dim);
    metti_ottale(h + 136, 12, 0);
    h[156] = (unsigned char)tipo;
    memcpy(h + 257, "ustar", 6);
    h[263] = '0'; h[264] = '0';
    metti_ottale(h + 148, 7, somma(h));
    h[155] = ' ';
    return uscita(h, BLOCCO);
}

/* ---------------------------------------------------------------------------
 * Create
 * ------------------------------------------------------------------------- */
static int aggiungi(const char *perc, const char *nome)
{
    struct stat st;

    if (stat(perc, &st) != 0) { printf("%s: %s not found\n", g_io, perc); return 0; }

    if (S_ISDIR(st.st_mode)) {
        char sotto_p[PERC_MAX], sotto_n[PERC_MAX];
        struct dirent *e;
        DIR *d;
        int ok = 1;

        snprintf(sotto_n, sizeof(sotto_n), "%s/", nome);
        if (!testa_scrivi(sotto_n, '5', 0, 0755)) return 0;
        printf("%s\n", sotto_n);
        d = opendir(perc);
        if (!d) { printf("%s: cannot read %s\n", g_io, perc); return 0; }
        while ((e = readdir(d)) != 0) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (snprintf(sotto_p, sizeof(sotto_p), "%s/%s", perc, e->d_name) >= (int)sizeof(sotto_p) ||
                snprintf(sotto_n, sizeof(sotto_n), "%s/%s", nome, e->d_name) >= (int)sizeof(sotto_n)) {
                printf("%s: path too long in %s\n", g_io, perc); ok = 0; continue;
            }
            if (!aggiungi(sotto_p, sotto_n)) ok = 0;
        }
        closedir(d);
        return ok;
    } else {
        static unsigned char buf[16 * BLOCCO];
        unsigned long resto = st.st_size;
        int fd = open(perc, O_RDONLY, 0);

        if (fd < 0) { printf("%s: cannot open %s\n", g_io, perc); return 0; }
        if (!testa_scrivi(nome, '0', st.st_size, 0644)) { close(fd); return 0; }
        printf("%s\n", nome);
        while (resto > 0) {
            unsigned int voglio = resto < sizeof(buf) ? (unsigned int)resto : sizeof(buf);
            unsigned int tondo  = (voglio + BLOCCO - 1) / BLOCCO * BLOCCO;
            int r = read(fd, buf, voglio);
            if (r != (int)voglio) { close(fd); printf("%s: read error on %s\n", g_io, perc); return 0; }
            memset(buf + voglio, 0, tondo - voglio);
            if (!uscita(buf, tondo)) { close(fd); return 0; }
            resto -= voglio;
        }
        close(fd);
        return 1;
    }
}

/* The name inside the archive: no leading '/', no "./", no trailing '/'. */
static void nome_interno(const char *perc, char *nome)
{
    size_t l;

    while (*perc == '/') perc++;
    while (perc[0] == '.' && perc[1] == '/') perc += 2;
    snprintf(nome, PERC_MAX, "%s", *perc ? perc : ".");
    l = strlen(nome);
    while (l > 1 && nome[l - 1] == '/') nome[--l] = 0;
}

static int crea(const char *arch, int gz, int n, char **percorsi)
{
    unsigned char zero[BLOCCO];
    char nome[PERC_MAX];
    int i, ok = 1;

    if (n == 0) { printf("%s: nothing to archive\n", g_io); return 1; }
    if (!uscita_apri(arch, gz)) return 1;
    for (i = 0; i < n; i++) {
        nome_interno(percorsi[i], nome);
        if (!aggiungi(percorsi[i], nome)) ok = 0;
    }
    memset(zero, 0, BLOCCO);
    if (!uscita(zero, BLOCCO) || !uscita(zero, BLOCCO)) ok = 0;
    if (!uscita_chiudi()) { printf("%s: write error on %s\n", g_io, arch); ok = 0; }
    return ok ? 0 : 1;
}

/* ---------------------------------------------------------------------------
 * Extract / list
 * ------------------------------------------------------------------------- */
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
            if (l >= PERC_MAX) return 0;
            memcpy(fuori, d + j + 6, l); fuori[l] = 0;
            return 1;
        }
        i += len;
    }
    return 0;
}

static int scorri(unsigned char *d, long n, int estrai, const char *dove)
{
    char lungo[PERC_MAX], nome[PERC_MAX], dest[PERC_MAX];
    long i = 0;
    int  errori = 0;

    lungo[0] = 0;
    while (i + BLOCCO <= n) {
        unsigned char *h = d + i;
        unsigned long dim;
        char tipo;

        if (blocco_vuoto(h)) break;
        if (somma(h) != ottale(h + 148, 8)) { printf("%s: bad header checksum\n", g_io); return 1; }
        dim  = ottale(h + 124, 12);
        tipo = (char)h[156];
        i += BLOCCO;
        if ((unsigned long)(n - i) < dim) { printf("%s: truncated archive\n", g_io); return 1; }

        if (tipo == 'L' || tipo == 'x') {
            if (tipo == 'L') {
                unsigned long l = dim < PERC_MAX ? dim : PERC_MAX - 1;
                memcpy(lungo, d + i, l); lungo[l] = 0;
            } else if (!pax_path(d + i, dim, lungo)) {
                lungo[0] = 0;
            }
            i += (long)((dim + BLOCCO - 1) / BLOCCO * BLOCCO);
            continue;
        }
        if (tipo == 'g') { i += (long)((dim + BLOCCO - 1) / BLOCCO * BLOCCO); continue; }

        if (lungo[0]) {
            snprintf(nome, sizeof(nome), "%s", lungo);
            lungo[0] = 0;
        } else if (memcmp(h + 257, "ustar", 5) == 0 && h[345]) {
            snprintf(nome, sizeof(nome), "%.155s/%.100s", (char *)h + 345, (char *)h);
        } else {
            snprintf(nome, sizeof(nome), "%.100s", (char *)h);
        }

        /* "./sub/x" is what GNU tar writes for `tar cf a.tar .`: the "./"
         * says nothing, and the "./" entry itself is the destination. */
        if (estrai) {
            char *s = nome;
            while (s[0] == '.' && s[1] == '/') { s += 2; while (*s == '/') s++; }
            if (*s == 0 || strcmp(s, ".") == 0) { i += (long)((dim + BLOCCO - 1) / BLOCCO * BLOCCO); continue; }
            if (s != nome) memmove(nome, s, strlen(s) + 1);
        }

        if (!estrai) {
            printf("%c %8lu  %s\n", tipo == '5' ? 'd' : (tipo == '0' || tipo == 0) ? '-' : tipo,
                   dim, nome);
        } else if (!nome_sicuro(nome)) {
            printf("%s: skipped unsafe name %s\n", g_io, nome); errori++;
        } else if (snprintf(dest, sizeof(dest), "%s/%s", dove, nome) >= (int)sizeof(dest)) {
            printf("%s: path too long: %s\n", g_io, nome); errori++;
        } else if (tipo == '5') {
            size_t l = strlen(dest);
            while (l > 1 && dest[l - 1] == '/') dest[--l] = 0;
            if (!crea_cartelle(dest, 1)) { printf("%s: cannot create %s\n", g_io, dest); errori++; }
        } else if (tipo == '0' || tipo == 0 || tipo == '7') {
            int fd;
            crea_cartelle(dest, 0);
            fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0 || !scrivi_tutto(fd, d + i, (unsigned int)dim)) {
                printf("%s: cannot write %s\n", g_io, dest); errori++;
            } else {
                printf("%s\n", nome);
            }
            if (fd >= 0) close(fd);
        } else {
            printf("%s: skipped %s (type '%c' not supported)\n", g_io, nome, tipo);
        }
        i += (long)((dim + BLOCCO - 1) / BLOCCO * BLOCCO);
    }
    return errori ? 1 : 0;
}

static int leggi_archivio(const char *arch, int estrai, const char *dove)
{
    long n;
    unsigned char *d = leggi_file(arch, &n);

    if (!d) return 1;
    if (e_gzip(d, n)) {
        long m;
        unsigned char *a = gz_apri(d, n, &m);
        free(d);
        if (!a) return 1;
        d = a; n = m;
    }
    return scorri(d, n, estrai, dove);
}

/* ---------------------------------------------------------------------------
 * gzip / gunzip
 * ------------------------------------------------------------------------- */
static int gzip_file(const char *nome)
{
    static unsigned char buf[8192];
    char dest[PERC_MAX];
    int fd, r, ok = 1;

    if (snprintf(dest, sizeof(dest), "%s.gz", nome) >= (int)sizeof(dest)) return 1;
    fd = open(nome, O_RDONLY, 0);
    if (fd < 0) { printf("%s: cannot open %s\n", g_io, nome); return 1; }
    if (!uscita_apri(dest, 1)) { close(fd); return 1; }
    while ((r = read(fd, buf, sizeof(buf))) > 0)
        if (!uscita(buf, (unsigned int)r)) { ok = 0; break; }
    close(fd);
    if (!uscita_chiudi() || !ok) { printf("%s: write error on %s\n", g_io, dest); return 1; }
    printf("%s -> %s\n", nome, dest);
    return 0;
}

static int gunzip_file(const char *nome)
{
    char dest[PERC_MAX];
    size_t l = strlen(nome);
    long n, m;
    unsigned char *d, *a;
    int fd, ok;

    if (l > 3 && strcmp(nome + l - 3, ".gz") == 0) {
        snprintf(dest, sizeof(dest), "%.*s", (int)(l - 3), nome);
    } else if (l > 4 && strcmp(nome + l - 4, ".tgz") == 0) {
        snprintf(dest, sizeof(dest), "%.*s.tar", (int)(l - 4), nome);
    } else {
        printf("%s: %s: unknown suffix\n", g_io, nome); return 1;
    }
    d = leggi_file(nome, &n);
    if (!d) return 1;
    a = gz_apri(d, n, &m);
    free(d);
    if (!a) return 1;
    fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ok = fd >= 0 && scrivi_tutto(fd, a, (unsigned int)m);
    if (fd >= 0) close(fd);
    free(a);
    if (!ok) { printf("%s: cannot write %s\n", g_io, dest); return 1; }
    printf("%s -> %s\n", nome, dest);
    return 0;
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
static int uso(void)
{
    if (strcmp(g_io, "tar") == 0)
        printf("usage: tar c[z]f ARCHIVE PATH...\n"
               "       tar x[z]f ARCHIVE [-C DIR]\n"
               "       tar t[z]f ARCHIVE\n");
    else
        printf("usage: gzip [-d] FILE...\n"
               "       gunzip FILE.gz...\n");
    return 1;
}

int main(int argc, char **argv)
{
    const char *base = argv[0], *p, *modo, *arch, *dove = ".";
    int i, rc = 0, gz = 0, decomprimi = 0;

    crc_prepara();
    for (p = argv[0]; *p; p++) if (*p == '/') base = p + 1;

    if (strcmp(base, "gzip") == 0 || strcmp(base, "gunzip") == 0) {
        g_io = base;
        decomprimi = strcmp(base, "gunzip") == 0;
        for (i = 1; i < argc && argv[i][0] == '-'; i++) {
            if (strcmp(argv[i], "-d") == 0) decomprimi = 1;
            else return uso();
        }
        if (i >= argc) return uso();
        for (; i < argc; i++)
            if ((decomprimi ? gunzip_file(argv[i]) : gzip_file(argv[i])) != 0) rc = 1;
        return rc;
    }

    if (argc < 3) return uso();
    modo = argv[1];
    if (*modo == '-') modo++;
    if (!strchr(modo, 'f')) return uso();
    if (strchr(modo, 'z')) gz = 1;
    arch = argv[2];

    if (strchr(modo, 'c')) return crea(arch, gz, argc - 3, argv + 3);

    for (i = 3; i < argc; i++)
        if (strcmp(argv[i], "-C") == 0 && i + 1 < argc) dove = argv[++i];
    if (strchr(modo, 'x')) return leggi_archivio(arch, 1, dove);
    if (strchr(modo, 't')) return leggi_archivio(arch, 0, dove);
    return uso();
}
