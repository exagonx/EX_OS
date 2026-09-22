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
    unsigned long  voce_off[EXZIP_VOCI_MAX];
    unsigned int   quante;

    /* Writing: what the central directory will have to say, collected while
     * the entries go down. */
    unsigned long  w_off[EXZIP_VOCI_MAX];     /* local header offset */
    unsigned long  w_crc[EXZIP_VOCI_MAX];
    unsigned long  w_dim[EXZIP_VOCI_MAX];
    unsigned int   w_data[EXZIP_VOCI_MAX];    /* DOS date and time, packed */
    unsigned long  w_nome[EXZIP_VOCI_MAX];    /* offset into the name pool */
    unsigned int   w_quante;
    char          *nomi;                      /* the pool */
    unsigned long  nomi_usati;
    unsigned long  scritto;                   /* bytes written so far */
};

#define NOMI_POOL   (64 * 1024)

/* ! ONE ERROR AT A TIME, AND IT IS A SENTENCE. Returning -errno would push onto
 * every caller the job of turning a number into something a person can read,
 * and half of them would print the number. The text is in Italian because it
 * ends up on a screen: see the ASCII rule of this system. */
static char g_err[160] = "";

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
        errore("l'archivio ha piu' di 1024 file: non ci stanno");
        close(z->fd); free(z); return 0;
    }
    if (cd_n == 0 || cd_off + cd_n > (unsigned long)dim) {
        errore("il catalogo dell'archivio punta fuori dal file");
        close(z->fd); free(z); return 0;
    }

    z->cd = (unsigned char *)malloc(cd_n);
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

ExZip *ex_zip_crea(const char *percorso)
{
    ExZip *z;

    g_err[0] = '\0';

    z = (ExZip *)malloc(sizeof(ExZip));
    if (!z) { errore("non c'e' memoria per creare l'archivio"); return 0; }
    memset(z, 0, sizeof(ExZip));

    z->nomi = (char *)malloc(NOMI_POOL);
    if (!z->nomi) { errore("non c'e' memoria per i nomi"); free(z); return 0; }

    z->fd = open(percorso, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (z->fd < 0) { errore("non riesco a creare l'archivio"); free(z); return 0; }

    z->scrittura = 1;
    return z;
}

/* The CRC of a file, read through. See ex_zip_aggiungi() for why it is read
 * twice. */
static int crc_del_file(const char *file, unsigned long *crc, unsigned long *dim)
{
    unsigned char buf[BUF];
    unsigned long c = 0xFFFFFFFFul, n = 0;
    int fd, letti;

    fd = open(file, O_RDONLY, 0);
    if (fd < 0) return 0;

    while ((letti = (int)read(fd, buf, BUF)) > 0) {
        c = crc32(c, buf, (unsigned long)letti);
        n += (unsigned long)letti;
    }
    close(fd);

    *crc = c ^ 0xFFFFFFFFul;
    *dim = n;
    return letti == 0;          /* a read error is not a zero-length file */
}

int ex_zip_aggiungi(ExZip *z, const char *file, const char *nome)
{
    unsigned char h[LOC_FISSO];
    unsigned char buf[BUF];
    unsigned long crc = 0, dim = 0;
    unsigned int  ln;
    unsigned int  data;
    int           in, letti;

    g_err[0] = '\0';

    if (!z || !z->scrittura) { errore("questo archivio non e' in scrittura"); return 0; }
    if (z->w_quante >= EXZIP_VOCI_MAX) { errore("l'archivio e' pieno: 1024 file"); return 0; }

    ln = (unsigned int)strlen(nome);
    if (ln == 0 || ln >= EXZIP_NOME_MAX) { errore("nome non valido dentro l'archivio"); return 0; }
    if (z->nomi_usati + ln + 1 > NOMI_POOL) { errore("troppi nomi: non ci stanno"); return 0; }

    /* ! THE FILE IS READ TWICE, AND IT IS THE CHEAPEST OF THE THREE WAYS. The
     * local header carries the CRC and the size BEFORE the data, and we only
     * know them after having read the file. The alternatives are a "data
     * descriptor" after the data - which old tools read badly - or writing the
     * header, then seeking back to patch it, which cannot be done when the
     * archive is a pipe. Reading a file twice off a disk costs nothing next to
     * an archive somebody cannot open. */
    if (!crc_del_file(file, &crc, &dim)) { errore("il file da aggiungere non si legge"); return 0; }

    data = data_dos();

    memset(h, 0, LOC_FISSO);
    metti32(h + 0,  SIG_LOCALE);
    metti16(h + 4,  20);                /* version needed: 2.0, store and deflate */
    metti16(h + 6,  0);                 /* no flags: sizes are here and they are true */
    metti16(h + 8,  EXZIP_STORE);
    metti16(h + 10, data & 0xFFFF);     /* time */
    metti16(h + 12, (data >> 16) & 0xFFFF);
    metti32(h + 14, crc);
    metti32(h + 18, dim);               /* compressed = uncompressed: store */
    metti32(h + 22, dim);
    metti16(h + 26, ln);
    metti16(h + 28, 0);

    z->w_off[z->w_quante]  = z->scritto;
    z->w_crc[z->w_quante]  = crc;
    z->w_dim[z->w_quante]  = dim;
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

    while ((letti = (int)read(in, buf, BUF)) > 0) {
        if (write(z->fd, buf, (unsigned int)letti) != letti) {
            close(in);
            errore("non riesco a scrivere: il disco e' pieno?");
            return 0;
        }
        z->scritto += (unsigned long)letti;
    }
    close(in);

    z->w_quante++;
    return 1;
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
        metti16(c + 8,  0);
        metti16(c + 10, EXZIP_STORE);
        metti16(c + 12, z->w_data[i] & 0xFFFF);
        metti16(c + 14, (z->w_data[i] >> 16) & 0xFFFF);
        metti32(c + 16, z->w_crc[i]);
        metti32(c + 20, z->w_dim[i]);
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

void ex_zip_chiudi(ExZip *z)
{
    if (!z) return;
    if (z->fd >= 0) close(z->fd);

    /* ! free() GIVES NOTHING BACK TO THE SYSTEM HERE, and the pointers are
     * dropped anyway: a program that opens one archive after another would
     * grow otherwise, and the day free() does give memory back this is already
     * the right thing to have written. */
    if (z->cd)   free(z->cd);
    if (z->nomi) free(z->nomi);
    free(z);
}
