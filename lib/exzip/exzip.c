/* =============================================================================
 * lib/exzip/exzip.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The ZIP format: three structures and one rule
 *
 * A ZIP file is read BACKWARDS, and that is the first thing to know about it.
 * At the end there is the "end of central directory" record; it says where the
 * central directory is; the central directory lists the entries and says where
 * each local header is. Nothing at the front tells you what is in the file -
 * which is exactly what makes a ZIP appendable, and what makes finding the end
 * record the only fragile step (it carries a comment of up to 64 KB after it,
 * so it has to be searched for backwards).
 *
 * ! THE LOCAL HEADER IS NOT TO BE TRUSTED, and this is the rule that costs
 * people days. With flag bit 3 set, the sizes and the CRC in the local header
 * are ZERO and the real ones sit after the data, in a "data descriptor". The
 * central directory always has the truth. So: names, sizes and CRC come from
 * the central directory, and the local header is read for one thing only - the
 * length of its name and extra fields, which is how you find where the data
 * starts.
 * ============================================================================= */

#include "libc.h"
#include "exzip.h"
#include "inflate.h"
#include "deflate.h"

/* The four signatures, little-endian as they sit in the file. */
#define SIG_LOCALE      0x04034B50ul
#define SIG_CENTRALE    0x02014B50ul
#define SIG_FINE        0x06054B50ul

#define LOC_FISSO       30      /* bytes of a local header before the name */
#define CEN_FISSO       46      /* same, for a central directory entry */
#define FINE_FISSO      22      /* the end record without its comment */

#define BUF             4096    /* the fixed buffer everything streams through */

/* ! 64 KB IS NOT A ROUND NUMBER, IT IS THE FORMAT'S: the comment length in the
 * end record is 16 bits, so the record can start at most that far from the end
 * of the file, plus its own 22 bytes. Searching further would be searching
 * where the format says it cannot be. */
#define CODA_MAX        (65535 + FINE_FISSO)

struct ExZip {
    int           fd;
    int           scrittura;      /* 1 = being created, 0 = being read */

    /* Reading: the central directory, kept raw, plus where each entry starts
     * inside it. Parsing on demand costs nothing and keeps one copy of the
     * names instead of two. */
    unsigned char *cd;
    unsigned long  cd_n;
    unsigned long *voce_off;        /* one per entry, allocated on opening */
    unsigned int   quante;

    /* Writing: what the central directory will have to say, collected while
     * the entries go down. */
    /* ! THE TABLES GROW WITH THE ENTRIES (26 September 2026). They were
     * fixed arrays for 65535 entries plus a 4 MB name pool: over 6 MB for
     * an archive of two files, taken at every ex_zip_crea(). In Archivi,
     * where «Nuovo» creates and then reopens, the window of the next dialog
     * could no longer be created — and nothing said why. See posto(). */
    unsigned int   w_cap;                     /* entries the tables can hold */
    unsigned long *w_off;                     /* local header offset */
    unsigned long *w_crc;
    unsigned long *w_dim;
    unsigned long *w_cdim;                    /* size inside the archive */
    unsigned int  *w_met;                     /* EXZIP_STORE or EXZIP_DEFLATE */
    unsigned int  *w_data;                    /* DOS date and time, packed */
    unsigned int  *w_bit;                     /* general purpose flags: 0 for
                                               * what we write, kept for what
                                               * ex_zip_riapri found (UTF-8
                                               * names, encryption) */
    unsigned long *w_nome;                    /* offset into the name pool */
    unsigned int   w_quante;
    char          *nomi;                      /* the pool */
    unsigned long  nomi_usati;
    unsigned long  nomi_cap;
    unsigned long  scritto;                   /* bytes written so far */
};

/* The names of an archive being written: a ceiling, not an allocation. */
#define NOMI_POOL   (4 * 1024 * 1024)

/* ! ONE ERROR AT A TIME, AND IT IS A SENTENCE. Returning -errno would push onto
 * every caller the job of turning a number into something a person can read,
 * and half of them would print the number. The text is in Italian because it
 * ends up on a screen: see the ASCII rule of this system. */
static char g_err[160] = "";

/* How hard to compress what is added from now on: 0 = store everything,
 * 1 fast, 2 normal (the default), 3 best. See ex_zip_livello(). */
static unsigned int g_livello = 2;

void ex_zip_livello(unsigned int livello)
{
    g_livello = livello > 3 ? 2 : livello;
    defl_livello(g_livello ? (int)g_livello : 2);
}

static void errore(const char *s)
{
    unsigned int i = 0;

    while (s[i] && i + 1 < sizeof(g_err)) { g_err[i] = s[i]; i++; }
    g_err[i] = '\0';
}

const char *ex_zip_errore(void)
{
    return g_err[0] ? g_err : "nessun errore";
}

/* -----------------------------------------------------------------------------
 * Little-endian, by hand
 *
 * ! THE FORMAT IS LITTLE-ENDIAN AND THIS MACHINE IS TOO, and that is exactly
 * why these are written by hand instead of casting a pointer. A cast would
 * work here and break the day this code is read on a machine that is not, and
 * it would break silently. Four shifts cost nothing.
 * --------------------------------------------------------------------------- */
static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void metti16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void metti32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* CRC-32, polynomial 0xEDB88320, bit by bit and without a table.
 *
 * ! NO TABLE ON PURPOSE: a kilobyte of .data in every process that opens this
 * library, to save a few tenths of a second on an archive that took longer to
 * read off the disk. It is the same choice netupdate made for gzip, and the
 * day one of the two has to change they should still agree. */
static unsigned long crc32(unsigned long c, const unsigned char *d, unsigned long n)
{
    unsigned long i;
    int k;

    for (i = 0; i < n; i++) {
        c ^= d[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320ul & (unsigned long)(-(long)(c & 1)));
    }
    return c;
}

/* -----------------------------------------------------------------------------
 * The DOS date, which is what a ZIP carries
 *
 * ! THE YEAR STARTS AT 1980 AND THE SECONDS COUNT BY TWO. It is not a quirk to
 * work around: it is five bits for the day, four for the month, seven for the
 * year, and a ZIP written with a year before 1980 is a ZIP other tools show as
 * 1980. We clamp instead of wrapping, so an archive made by a machine whose
 * clock never got set looks old rather than looking like the future.
 * --------------------------------------------------------------------------- */
static unsigned int data_dos(void)
{
    RtcTime t;
    unsigned int g, o;

    memset(&t, 0, sizeof(t));
    if (time_now(&t) != 0 || t.anno < 1980) {
        t.anno = 1980; t.mese = 1; t.giorno = 1;
        t.ora = 0; t.minuto = 0; t.secondo = 0;
    }

    g = ((unsigned int)(t.anno - 1980) << 9) |
        ((unsigned int)t.mese << 5) | (unsigned int)t.giorno;
    o = ((unsigned int)t.ora << 11) | ((unsigned int)t.minuto << 5) |
        ((unsigned int)t.secondo / 2);

    return (g << 16) | o;
}

static void data_leggi(unsigned int ora, unsigned int data, ExZipVoce *v)
{
    v->anno   = 1980 + ((data >> 9) & 0x7F);
    v->mese   = (data >> 5) & 0x0F;
    v->giorno = data & 0x1F;
    v->ore    = (ora >> 11) & 0x1F;
    v->minuti = (ora >> 5) & 0x3F;
}

/* -----------------------------------------------------------------------------
 * Reading
 * --------------------------------------------------------------------------- */

/* Finds the end record by walking backwards. Returns its offset, or -1. */
static long trova_fine(int fd, long dim)
{
    unsigned char coda[CODA_MAX];
    long           quanto = dim < (long)CODA_MAX ? dim : (long)CODA_MAX;
    long           base = dim - quanto;
    long           i;
    int            n;

    if (quanto < FINE_FISSO) return -1;

    if (lseek(fd, base, SEEK_SET) < 0) return -1;
    n = (int)read(fd, coda, (unsigned int)quanto);
    if (n < FINE_FISSO) return -1;

    /* ! FROM THE END BACKWARDS, and the first hit wins: a ZIP whose comment
     * happens to contain the signature would fool a forward search, and the
     * real record is always the last one. */
    for (i = (long)n - FINE_FISSO; i >= 0; i--)
        if (le32(coda + i) == SIG_FINE) return base + i;

    return -1;
}

ExZip *ex_zip_apri(const char *percorso)
{
    ExZip        *z;
    unsigned char fine[FINE_FISSO];
    long          dim, off_fine;
    unsigned long cd_off, cd_n, p;
    unsigned int  n_voci, i;

    g_err[0] = '\0';

    z = (ExZip *)malloc(sizeof(ExZip));
    if (!z) { errore("non c'e' memoria per aprire l'archivio"); return 0; }
    memset(z, 0, sizeof(ExZip));

    z->fd = open(percorso, O_RDONLY, 0);
    if (z->fd < 0) { errore("l'archivio non si apre"); free(z); return 0; }

    dim = lseek(z->fd, 0, SEEK_END);
    off_fine = trova_fine(z->fd, dim);
    if (off_fine < 0) {
        errore("non e' un archivio ZIP: manca il record di fine");
        close(z->fd); free(z); return 0;
    }

    lseek(z->fd, off_fine, SEEK_SET);
    if (read(z->fd, fine, FINE_FISSO) != FINE_FISSO) {
        errore("l'archivio finisce a meta'");
        close(z->fd); free(z); return 0;
    }

    /* ! A ZIP SPLIT OVER SEVERAL FILES IS REFUSED WHOLE. Reading the first
     * volume of one would show a list of names of which only some can be
     * extracted, which is worse than saying no. */
    if (le16(fine + 4) != 0 || le16(fine + 6) != 0) {
        errore("archivio diviso su piu' volumi: non li apro");
        close(z->fd); free(z); return 0;
    }

    n_voci = le16(fine + 10);
    cd_n   = le32(fine + 12);
    cd_off = le32(fine + 16);

    if (n_voci > EXZIP_VOCI_MAX) {
        errore("l'archivio ha piu' di 65535 file: serve ZIP64, che non so leggere");
        close(z->fd); free(z); return 0;
    }
    /* ! AN EMPTY ARCHIVE IS AN ARCHIVE: only the end record, no catalogue.
     * Archivi builds one the moment «Nuovo» is chosen (26 September 2026),
     * and every archiver writes the same 22 bytes for it. */
    if (n_voci == 0 && cd_n == 0 && cd_off <= (unsigned long)dim) return z;

    if (cd_n == 0 || cd_off + cd_n > (unsigned long)dim) {
        errore("il catalogo dell'archivio punta fuori dal file");
        close(z->fd); free(z); return 0;
    }

    z->cd = (unsigned char *)malloc(cd_n);
    z->voce_off = (unsigned long *)malloc(n_voci * sizeof(unsigned long));
    if (z->cd && !z->voce_off) { free(z->cd); z->cd = 0; }
    if (!z->cd) { errore("non c'e' memoria per il catalogo"); close(z->fd); free(z); return 0; }
    z->cd_n = cd_n;

    lseek(z->fd, (long)cd_off, SEEK_SET);
    if ((unsigned long)read(z->fd, z->cd, (unsigned int)cd_n) != cd_n) {
        errore("il catalogo dell'archivio non si legge");
        close(z->fd); free(z); return 0;
    }

    /* One walk to note where each entry begins. Doing it now rather than at
     * every question keeps ex_zip_voce() from being linear in i. */
    p = 0;
    for (i = 0; i < n_voci; i++) {
        unsigned long ln, xn, cn;

        if (p + CEN_FISSO > cd_n || le32(z->cd + p) != SIG_CENTRALE) {
            errore("il catalogo dell'archivio e' rovinato");
            close(z->fd); free(z); return 0;
        }
        ln = le16(z->cd + p + 28);
        xn = le16(z->cd + p + 30);
        cn = le16(z->cd + p + 32);

        z->voce_off[i] = p;
        p += CEN_FISSO + ln + xn + cn;
    }
    z->quante = n_voci;

    return z;
}

unsigned int ex_zip_quante(ExZip *z)
{
    return z ? z->quante : 0;
}

int ex_zip_voce(ExZip *z, unsigned int i, ExZipVoce *v)
{
    const unsigned char *c;
    unsigned long ln;
    unsigned int  k;

    if (!z || !v || i >= z->quante) return 0;

    c  = z->cd + z->voce_off[i];
    ln = le16(c + 28);

    memset(v, 0, sizeof(*v));
    v->metodo = le16(c + 10);
    data_leggi(le16(c + 12), le16(c + 14), v);
    v->crc    = le32(c + 16);
    v->dim_c  = le32(c + 20);
    v->dim    = le32(c + 24);

    /* ! A NAME LONGER THAN OUR BUFFER IS CUT, AND THE CUT IS VISIBLE. It ends
     * up in the listing as a shorter name, and extracting it by that name
     * would write the wrong file - which is why ex_zip_estrai() takes `dove`
     * from the caller and never builds it from here. */
    for (k = 0; k < ln && k + 1 < EXZIP_NOME_MAX; k++) v->nome[k] = (char)c[CEN_FISSO + k];
    v->nome[k] = '\0';

    v->directory = (k > 0 && v->nome[k - 1] == '/') ? 1 : 0;

    return 1;
}

/* Where the data of entry i begins: the local header says how long its own
 * name and extra fields are, and nothing else there is to be trusted. */
static long dati_off(ExZip *z, unsigned int i)
{
    const unsigned char *c = z->cd + z->voce_off[i];
    unsigned long loc = le32(c + 42);
    unsigned char h[LOC_FISSO];

    if (lseek(z->fd, (long)loc, SEEK_SET) < 0) return -1;
    if (read(z->fd, h, LOC_FISSO) != LOC_FISSO) return -1;
    if (le32(h) != SIG_LOCALE) return -1;

    return (long)loc + LOC_FISSO + (long)le16(h + 26) + (long)le16(h + 28);
}

static int estrai_store(ExZip *z, int out, unsigned long n, unsigned long crc_atteso)
{
    unsigned char buf[BUF];
    unsigned long resto = n, c = 0xFFFFFFFFul;

    while (resto > 0) {
        unsigned int chiedo = resto > BUF ? BUF : (unsigned int)resto;
        int          letti  = (int)read(z->fd, buf, chiedo);

        if (letti <= 0) { errore("l'archivio finisce prima del file"); return 0; }
        if (write(out, buf, (unsigned int)letti) != letti) {
            errore("non riesco a scrivere: il disco e' pieno o in sola lettura");
            return 0;
        }
        c = crc32(c, buf, (unsigned long)letti);
        resto -= (unsigned long)letti;
    }

    if ((c ^ 0xFFFFFFFFul) != crc_atteso) {
        errore("il CRC non torna: il file dentro l'archivio e' rovinato");
        return 0;
    }
    return 1;
}

static int estrai_deflate(ExZip *z, int out, unsigned long n_c, unsigned long n,
                          unsigned long crc_atteso)
{
    unsigned char *dentro, *fuori;
    unsigned int   prodotti = 0;
    unsigned long  c;
    int            ok = 0;

    /* ! HERE THE STREAM STOPS, AND THE LIBRARY SAYS SO INSTEAD OF DYING.
     * inflate() wants the whole output buffer from the caller (see
     * lib/eximg/inflate.h - deliberate: on EX-OS free() gives nothing back to
     * the system). So one deflated entry costs its own size in memory, twice. */
    dentro = (unsigned char *)malloc(n_c ? n_c : 1);
    fuori  = (unsigned char *)malloc(n ? n : 1);
    if (!dentro || !fuori) {
        errore("non c'e' memoria per decomprimere questo file");
        return 0;
    }

    if ((unsigned long)read(z->fd, dentro, (unsigned int)n_c) != n_c) {
        errore("l'archivio finisce prima del file");
        return 0;
    }

    if (inflate(dentro, (unsigned int)n_c, fuori, (unsigned int)n, &prodotti) != 0 ||
        (unsigned long)prodotti != n) {
        errore("i dati compressi non si aprono: l'archivio e' rovinato");
        return 0;
    }

    c = crc32(0xFFFFFFFFul, fuori, n) ^ 0xFFFFFFFFul;
    if (c != crc_atteso) {
        errore("il CRC non torna: il file dentro l'archivio e' rovinato");
        return 0;
    }

    ok = ((unsigned long)write(out, fuori, (unsigned int)n) == n);
    if (!ok) errore("non riesco a scrivere: il disco e' pieno o in sola lettura");
    return ok;
}

int ex_zip_estrai(ExZip *z, unsigned int i, const char *dove)
{
    ExZipVoce v;
    long      off;
    int       out, ok;

    g_err[0] = '\0';

    if (!z || z->scrittura || !ex_zip_voce(z, i, &v)) {
        errore("voce inesistente");
        return 0;
    }

    if (v.directory) {
        if (mkdir(dove, 0755) != 0 && errno != EEXIST) {
            errore("non riesco a creare la cartella");
            return 0;
        }
        return 1;
    }

    if (v.metodo != EXZIP_STORE && v.metodo != EXZIP_DEFLATE) {
        errore("metodo di compressione che non conosco: so store e deflate");
        return 0;
    }

    off = dati_off(z, i);
    if (off < 0) { errore("l'intestazione del file dentro l'archivio non torna"); return 0; }
    lseek(z->fd, off, SEEK_SET);

    out = open(dove, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        /* ! THIS IS WHERE A LONG NAME ON FAT LANDS, and it is said out loud.
         * On this system long names are read-only on FAT: the create fails,
         * and a library that answered by shortening the name would hand back a
         * file nobody asked for. */
        errore("non riesco a creare il file: nome non accettato, "
               "o disco in sola lettura");
        return 0;
    }

    if (v.metodo == EXZIP_STORE) ok = estrai_store(z, out, v.dim, v.crc);
    else                         ok = estrai_deflate(z, out, v.dim_c, v.dim, v.crc);

    close(out);
    return ok;
}

/* -----------------------------------------------------------------------------
 * Writing
 * --------------------------------------------------------------------------- */

/* Room for `voci` entries and `byte` more bytes of names; the tables double
 * when they grow. 0 when there is no memory or the ceiling is reached. */
static void *cresci(void *p, unsigned long n, unsigned long dim)
{
    void *q = realloc(p, n * dim);
    return q;
}

static int posto(ExZip *z, unsigned int voci, unsigned long byte)
{
    if (voci > EXZIP_VOCI_MAX || z->nomi_usati + byte > NOMI_POOL) return 0;

    if (voci > z->w_cap) {
        unsigned int c = z->w_cap ? z->w_cap : 64;
        void *t;

        while (c < voci) c *= 2;
        if (c > EXZIP_VOCI_MAX) c = EXZIP_VOCI_MAX;

#define CRESCI(campo) \
        if (!(t = cresci(z->campo, c, sizeof(*z->campo)))) return 0; \
        z->campo = t;
        CRESCI(w_off) CRESCI(w_crc) CRESCI(w_dim) CRESCI(w_cdim)
        CRESCI(w_met) CRESCI(w_data) CRESCI(w_bit) CRESCI(w_nome)
#undef CRESCI
        /* The flags of new entries are 0: realloc leaves the new part as
         * it finds it. */
        memset(z->w_bit + z->w_cap, 0, (c - z->w_cap) * sizeof(*z->w_bit));
        z->w_cap = c;
    }

    if (z->nomi_usati + byte > z->nomi_cap) {
        unsigned long c = z->nomi_cap ? z->nomi_cap : 4096;
        char *t;

        while (c < z->nomi_usati + byte) c *= 2;
        if (c > NOMI_POOL) c = NOMI_POOL;
        if (!(t = (char *)realloc(z->nomi, c))) return 0;
        z->nomi = t;
        z->nomi_cap = c;
    }
    return 1;
}

ExZip *ex_zip_crea(const char *percorso)
{
    ExZip *z;

    g_err[0] = '\0';

    z = (ExZip *)malloc(sizeof(ExZip));
    if (!z) { errore("non c'e' memoria per creare l'archivio"); return 0; }
    memset(z, 0, sizeof(ExZip));

    z->fd = open(percorso, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (z->fd < 0) { errore("non riesco a creare l'archivio"); free(z); return 0; }

    z->scrittura = 1;
    return z;
}

/* The CRC of a file, read through — and, when `cdim` is given, the size it
 * would have compressed: the compressor runs counting only. See
 * ex_zip_aggiungi() for why the file is read twice. `cdim` stays 0 when the
 * compressor has no memory: the file then goes in as it is. */
static int crc_del_file(const char *file, unsigned long *crc, unsigned long *dim,
                        unsigned long *cdim)
{
    unsigned char buf[BUF];
    unsigned long c = 0xFFFFFFFFul, n = 0;
    int fd, letti, comprimo = 0;

    fd = open(file, O_RDONLY, 0);
    if (fd < 0) return 0;

    if (cdim) { *cdim = 0; comprimo = defl_apri(0, 0); }

    while ((letti = (int)read(fd, buf, BUF)) > 0) {
        c = crc32(c, buf, (unsigned long)letti);
        n += (unsigned long)letti;
        if (comprimo) defl_dati(buf, (unsigned int)letti);
    }
    close(fd);

    if (comprimo) { defl_fine(); *cdim = defl_prodotti(); }

    *crc = c ^ 0xFFFFFFFFul;
    *dim = n;
    return letti == 0;          /* a read error is not a zero-length file */
}

/* Where the compressor's bytes go on the second pass: the archive. */
static int scrivi_archivio(void *chi, const unsigned char *p, unsigned int n)
{
    ExZip *z = (ExZip *)chi;

    if ((unsigned int)write(z->fd, p, n) != n) return 0;
    z->scritto += n;
    return 1;
}

int ex_zip_aggiungi(ExZip *z, const char *file, const char *nome)
{
    unsigned char h[LOC_FISSO];
    unsigned char buf[BUF];
    unsigned long crc = 0, dim = 0, cdim = 0;
    unsigned int  ln, metodo;
    unsigned int  data;
    int           in, letti;

    g_err[0] = '\0';

    if (!z || !z->scrittura) { errore("questo archivio non e' in scrittura"); return 0; }
    if (z->w_quante >= EXZIP_VOCI_MAX) { errore("l'archivio e' pieno: 65535 file (il limite del formato senza ZIP64)"); return 0; }

    ln = (unsigned int)strlen(nome);
    if (ln == 0 || ln >= EXZIP_NOME_MAX) { errore("nome non valido dentro l'archivio"); return 0; }
    if (!posto(z, z->w_quante + 1, ln + 1)) { errore("troppi nomi, o non c'e' memoria: non ci stanno"); return 0; }

    /* ! THE FILE IS READ TWICE, AND IT IS THE CHEAPEST OF THE THREE WAYS. The
     * local header carries the CRC and the size BEFORE the data, and we only
     * know them after having read the file. The alternatives are a "data
     * descriptor" after the data - which old tools read badly - or writing the
     * header, then seeking back to patch it, which cannot be done when the
     * archive is a pipe. Reading a file twice off a disk costs nothing next to
     * an archive somebody cannot open. */
    /* Level 0 does not even run the compressor: cdim stays 0, and 0 means
     * store (see below). */
    if (!crc_del_file(file, &crc, &dim, g_livello ? &cdim : 0)) { errore("il file da aggiungere non si legge"); return 0; }

    /* ! DEFLATE ONLY WHEN IT MAKES THE FILE SMALLER (since 23 September
     * 2026; before, everything was stored). A JPEG, a ZIP inside a ZIP, an
     * already compressed font come out of deflate a little BIGGER: those go
     * in as they are, which is what every archiver does. The size was
     * measured on the pass above, without writing anything; the second pass
     * below must produce exactly as many bytes, and it is checked. */
    metodo = (cdim > 0 && cdim < dim) ? EXZIP_DEFLATE : EXZIP_STORE;
    if (metodo == EXZIP_STORE) cdim = dim;

    data = data_dos();

    memset(h, 0, LOC_FISSO);
    metti32(h + 0,  SIG_LOCALE);
    metti16(h + 4,  20);                /* version needed: 2.0, store and deflate */
    metti16(h + 6,  0);                 /* no flags: sizes are here and they are true */
    metti16(h + 8,  metodo);
    metti16(h + 10, data & 0xFFFF);     /* time */
    metti16(h + 12, (data >> 16) & 0xFFFF);
    metti32(h + 14, crc);
    metti32(h + 18, cdim);              /* inside the archive */
    metti32(h + 22, dim);
    metti16(h + 26, ln);
    metti16(h + 28, 0);

    z->w_off[z->w_quante]  = z->scritto;
    z->w_crc[z->w_quante]  = crc;
    z->w_dim[z->w_quante]  = dim;
    z->w_cdim[z->w_quante] = cdim;
    z->w_met[z->w_quante]  = metodo;
    z->w_data[z->w_quante] = data;
    z->w_nome[z->w_quante] = z->nomi_usati;
    memcpy(z->nomi + z->nomi_usati, nome, ln + 1);
    z->nomi_usati += ln + 1;

    if (write(z->fd, h, LOC_FISSO) != LOC_FISSO ||
        (unsigned int)write(z->fd, nome, ln) != ln) {
        errore("non riesco a scrivere nell'archivio");
        return 0;
    }
    z->scritto += LOC_FISSO + ln;

    in = open(file, O_RDONLY, 0);
    if (in < 0) { errore("il file da aggiungere non si apre"); return 0; }

    if (metodo == EXZIP_DEFLATE) {
        unsigned long prima = z->scritto;
        int ok = defl_apri(scrivi_archivio, z);

        while (ok && (letti = (int)read(in, buf, BUF)) > 0)
            ok = defl_dati(buf, (unsigned int)letti);
        close(in);
        if (ok) ok = defl_fine();
        if (!ok) { errore("non riesco a scrivere: il disco e' pieno?"); return 0; }

        /* ! THE HEADER ALREADY SAYS HOW MANY BYTES THERE ARE, so a second
         * pass that came out different — the file changed between the two
         * reads — is an archive that lies. Better to say so than to finish
         * it. */
        if (z->scritto - prima != cdim) {
            errore("il file e' cambiato mentre lo comprimevo: riprova");
            return 0;
        }
    } else {
        while ((letti = (int)read(in, buf, BUF)) > 0) {
            if (write(z->fd, buf, (unsigned int)letti) != letti) {
                close(in);
                errore("non riesco a scrivere: il disco e' pieno?");
                return 0;
            }
            z->scritto += (unsigned long)letti;
        }
        close(in);
    }

    z->w_quante++;
    return 1;
}

/* =============================================================================
 * WHOLE DIRECTORIES — 23 September 2026
 *
 * Asked: in Archivi, being able to choose a directory too, not only files.
 * It sits in the library and not in the window because /bin/zip wants the
 * same thing (`zip a.zip cartella` failed on a directory), and two copies of
 * a walk over a tree would diverge.
 *
 * ! EVERY DIRECTORY GETS ITS OWN ENTRY, "name/", before its contents. Without
 * it an EMPTY directory would not survive the trip, and the extraction here
 * (ex_zip_estrai) creates directories from those entries, not by guessing
 * from the names of the files.
 *
 * ! ONE FILE THAT CANNOT BE READ DOES NOT STOP THE REST: it is counted, the
 * last reason is kept, and the caller says «N not added, because...». A tree
 * of a thousand files lost for one locked file would be the worse deal.
 * ============================================================================= */
#define ALBERO_FONDO  24        /* nested directories: deeper is a loop */

static int voce_cartella(ExZip *z, const char *nome)
{
    unsigned char h[LOC_FISSO];
    unsigned int  ln = (unsigned int)strlen(nome), data = data_dos();

    if (z->w_quante >= EXZIP_VOCI_MAX) { errore("l'archivio e' pieno: 65535 voci (il limite del formato senza ZIP64)"); return 0; }
    if (ln == 0 || ln >= EXZIP_NOME_MAX || !posto(z, z->w_quante + 1, ln + 1)) {
        errore("nome di cartella troppo lungo per l'archivio, o niente memoria");
        return 0;
    }

    memset(h, 0, LOC_FISSO);
    metti32(h + 0,  SIG_LOCALE);
    metti16(h + 4,  20);
    metti16(h + 8,  EXZIP_STORE);
    metti16(h + 10, data & 0xFFFF);
    metti16(h + 12, (data >> 16) & 0xFFFF);
    metti16(h + 26, ln);

    z->w_off[z->w_quante]  = z->scritto;
    z->w_crc[z->w_quante]  = 0;
    z->w_dim[z->w_quante]  = 0;
    z->w_cdim[z->w_quante] = 0;
    z->w_met[z->w_quante]  = EXZIP_STORE;
    z->w_data[z->w_quante] = data;
    z->w_nome[z->w_quante] = z->nomi_usati;
    memcpy(z->nomi + z->nomi_usati, nome, ln + 1);
    z->nomi_usati += ln + 1;

    if (write(z->fd, h, LOC_FISSO) != LOC_FISSO ||
        (unsigned int)write(z->fd, nome, ln) != ln) {
        errore("non riesco a scrivere nell'archivio");
        return 0;
    }
    z->scritto += LOC_FISSO + ln;
    z->w_quante++;
    return 1;
}

static void albero(ExZip *z, const char *cartella, const char *nome, int fondo,
                   int *messi, int *saltati, char *ultimo)
{
    DIR           *d;
    struct dirent *e;
    char           dentro[EXZIP_NOME_MAX];

    if (fondo > ALBERO_FONDO) { (*saltati)++; strcpy(ultimo, "cartelle troppo annidate"); return; }

    snprintf(dentro, sizeof(dentro), "%s/", nome);
    if (!voce_cartella(z, dentro)) { (*saltati)++; strcpy(ultimo, g_err); return; }

    d = opendir(cartella);
    if (!d) { (*saltati)++; strcpy(ultimo, "una cartella non si apre"); return; }

    while ((e = readdir(d)) != 0) {
        char        perc[512], sotto[EXZIP_NOME_MAX];
        struct stat st;
        int         e_cartella;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (snprintf(perc, sizeof(perc), "%s/%s", cartella, e->d_name) >= (int)sizeof(perc) ||
            snprintf(sotto, sizeof(sotto), "%s/%s", nome, e->d_name) >= (int)sizeof(sotto)) {
            (*saltati)++;
            strcpy(ultimo, "un percorso troppo lungo");
            continue;
        }

        e_cartella = e->d_type == DT_DIR;
        if (e->d_type == DT_UNKNOWN && stat(perc, &st) == 0) e_cartella = S_ISDIR(st.st_mode);

        if (e_cartella) {
            albero(z, perc, sotto, fondo + 1, messi, saltati, ultimo);
        } else if (ex_zip_aggiungi(z, perc, sotto)) {
            (*messi)++;
        } else {
            (*saltati)++;
            strcpy(ultimo, g_err);
        }
    }
    closedir(d);
}

int ex_zip_aggiungi_albero(ExZip *z, const char *cartella, const char *nome,
                           int *quanti_saltati)
{
    int  messi = 0, saltati = 0;
    char ultimo[sizeof(g_err)] = "";
    char base[EXZIP_NOME_MAX];
    int  i;

    g_err[0] = '\0';
    if (!z || !z->scrittura) { errore("questo archivio non e' in scrittura"); return -1; }

    /* The name inside the archive: no leading or trailing slash. */
    while (*nome == '/') nome++;
    snprintf(base, sizeof(base), "%s", nome);
    i = (int)strlen(base);
    while (i > 0 && base[i - 1] == '/') base[--i] = '\0';
    if (!base[0]) { errore("la cartella ha bisogno di un nome dentro l'archivio"); return -1; }

    albero(z, cartella, base, 0, &messi, &saltati, ultimo);
    if (quanti_saltati) *quanti_saltati = saltati;

    if (saltati) {
        char t[sizeof(g_err)];

        snprintf(t, sizeof(t), "%d non aggiunt%s: %s", saltati,
                 saltati == 1 ? "o" : "i", ultimo);
        errore(t);
    }
    return messi;
}

int ex_zip_finisci(ExZip *z)
{
    unsigned char c[CEN_FISSO];
    unsigned char f[FINE_FISSO];
    unsigned long cd_off, cd_n = 0;
    unsigned int  i;

    g_err[0] = '\0';

    if (!z || !z->scrittura) { errore("questo archivio non e' in scrittura"); return 0; }

    cd_off = z->scritto;

    for (i = 0; i < z->w_quante; i++) {
        const char  *nome = z->nomi + z->w_nome[i];
        unsigned int ln = (unsigned int)strlen(nome);

        memset(c, 0, CEN_FISSO);
        metti32(c + 0,  SIG_CENTRALE);
        metti16(c + 4,  20);            /* version made by */
        metti16(c + 6,  20);            /* version needed */
        metti16(c + 8,  z->w_bit[i]);
        metti16(c + 10, z->w_met[i]);
        metti16(c + 12, z->w_data[i] & 0xFFFF);
        metti16(c + 14, (z->w_data[i] >> 16) & 0xFFFF);
        metti32(c + 16, z->w_crc[i]);
        metti32(c + 20, z->w_cdim[i]);
        metti32(c + 24, z->w_dim[i]);
        metti16(c + 28, ln);
        metti32(c + 42, z->w_off[i]);

        if (write(z->fd, c, CEN_FISSO) != CEN_FISSO ||
            (unsigned int)write(z->fd, nome, ln) != ln) {
            errore("non riesco a scrivere il catalogo");
            return 0;
        }
        cd_n += CEN_FISSO + ln;
    }

    memset(f, 0, FINE_FISSO);
    metti32(f + 0,  SIG_FINE);
    metti16(f + 8,  z->w_quante);
    metti16(f + 10, z->w_quante);
    metti32(f + 12, cd_n);
    metti32(f + 16, cd_off);

    if (write(z->fd, f, FINE_FISSO) != FINE_FISSO) {
        errore("non riesco a chiudere l'archivio");
        return 0;
    }

    close(z->fd);
    z->fd = -1;
    z->scrittura = 0;
    return 1;
}

/* =============================================================================
 * ADDING TO AN ARCHIVE THAT IS ALREADY FINISHED (26 September 2026)
 *
 * Asked for Archivi — «Costruisci», and the archive built by itself after
 * every file added — and it was the piece @ZIP still lacked.
 *
 * ! THE OLD ENTRIES ARE NOT MOVED OR COPIED. Their data stays where it is;
 * what is rebuilt is only the catalogue at the end. The archive is read as
 * for extracting, every entry becomes one «already written» of an archive
 * being created, and writing starts again WHERE THE OLD CATALOGUE BEGAN:
 * new entries go over it, and ex_zip_finisci() writes a new catalogue with
 * old and new entries.
 *
 * ! NO TRUNCATE IS NEEDED, AND THAT IS WHY IT CAN BE DONE HERE: what is
 * written from the old catalogue on is the new entries plus a catalogue that
 * holds every old entry again, plus the end record — never shorter than what
 * it replaces.
 *
 * ! UNTIL ex_zip_finisci() THE FILE IS NOT AN ARCHIVE, exactly as with
 * ex_zip_crea(): the old catalogue is being overwritten. Whoever reopens
 * must finish, or the old entries are lost with it.
 * ============================================================================= */
ExZip *ex_zip_riapri(const char *percorso)
{
    ExZip        *r, *z;
    unsigned char fine[FINE_FISSO];
    long          dim, off_fine;
    unsigned long cd_off;
    unsigned int  i;

    r = ex_zip_apri(percorso);
    if (!r) return 0;

    /* Where the old catalogue starts: read again from the end record, which
     * ex_zip_apri() has already checked. */
    dim = lseek(r->fd, 0, SEEK_END);
    off_fine = trova_fine(r->fd, dim);
    lseek(r->fd, off_fine, SEEK_SET);
    if (off_fine < 0 || read(r->fd, fine, FINE_FISSO) != FINE_FISSO) {
        errore("l'archivio finisce a meta'");
        ex_zip_chiudi(r);
        return 0;
    }
    cd_off = le32(fine + 16);

    z = (ExZip *)malloc(sizeof(ExZip));
    if (!z) { errore("non c'e' memoria per riaprire l'archivio"); ex_zip_chiudi(r); return 0; }
    memset(z, 0, sizeof(ExZip));
    z->fd = -1;             /* not 0: ex_zip_chiudi() on an error would close stdin */

    for (i = 0; i < r->quante; i++) {
        const unsigned char *c = r->cd + r->voce_off[i];
        unsigned int ln = le16(c + 28);

        if (ln == 0 || ln >= EXZIP_NOME_MAX || !posto(z, i + 1, ln + 1)) {
            errore("un nome nel catalogo non va bene, o non c'e' memoria: non lo riscrivo");
            ex_zip_chiudi(z); ex_zip_chiudi(r);
            return 0;
        }
        z->w_bit[i]  = le16(c + 8);
        z->w_met[i]  = le16(c + 10);
        z->w_data[i] = le16(c + 12) | ((unsigned int)le16(c + 14) << 16);
        z->w_crc[i]  = le32(c + 16);
        z->w_cdim[i] = le32(c + 20);
        z->w_dim[i]  = le32(c + 24);
        z->w_off[i]  = le32(c + 42);
        z->w_nome[i] = z->nomi_usati;
        memcpy(z->nomi + z->nomi_usati, c + CEN_FISSO, ln);
        z->nomi[z->nomi_usati + ln] = '\0';
        z->nomi_usati += ln + 1;
    }
    z->w_quante = r->quante;
    ex_zip_chiudi(r);

    z->fd = open(percorso, O_WRONLY, 0);
    if (z->fd < 0 || lseek(z->fd, (long)cd_off, SEEK_SET) != (long)cd_off) {
        errore("non riesco a riaprire l'archivio in scrittura");
        ex_zip_chiudi(z);
        return 0;
    }
    z->scritto   = cd_off;
    z->scrittura = 1;
    return z;
}

void ex_zip_chiudi(ExZip *z)
{
    if (!z) return;
    if (z->fd >= 0) close(z->fd);

    /* ! free() GIVES NOTHING BACK TO THE SYSTEM HERE, and the pointers are
     * dropped anyway: a program that opens one archive after another would
     * grow otherwise, and the day free() does give memory back this is already
     * the right thing to have written. */
    if (z->cd)       free(z->cd);
    if (z->voce_off) free(z->voce_off);
    if (z->nomi)     free(z->nomi);
    if (z->w_off)  { free(z->w_off); free(z->w_crc); free(z->w_dim); free(z->w_cdim);
                     free(z->w_met); free(z->w_data); free(z->w_bit); free(z->w_nome); }
    free(z);
}
