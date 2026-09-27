/* =============================================================================
 * lib/extar/extar.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * tar and tar.gz, for /bin/tar AND for Archivi (@ARCHIVI-TAR, 27 Sept 2026)
 *
 * ! THE SAME FUNCTIONS FOR THE COMMAND AND FOR THE WINDOW, as lib/exzip is for
 * zip: two programs that each wrote their own tar would one day disagree about
 * what a tar is. The code came out of bin/tar/tar.c, unchanged in substance.
 *
 * ! IT IS COMPILED INTO EACH PROGRAM, NOT A SHARED LIBRARY — the difference
 * from exzip, and on purpose: /bin/tar is a static program that must also
 * work from the floppy, where there is no /exwin/lib to load a .so from. The
 * source is one; the copies are two. Next to it the program needs
 * lib/eximg/inflate.c and lib/exzip/deflate.c.
 *
 * The shape is exzip's, so Archivi can use either without two programs in one:
 *
 *     ExTar *t = ex_tar_apri("/disk/a.tar.gz");      reading
 *     n = ex_tar_quante(t);  ex_tar_voce(t, i, &v);  ex_tar_estrai(t, i, dest);
 *     ex_tar_chiudi(t);
 *
 *     ExTar *w = ex_tar_crea("/disk/b.tgz", 1);      writing (1 = gzip)
 *     ex_tar_aggiungi(w, "/disk/x", "x");            a file or a whole tree
 *     ex_tar_finisci(w);  ex_tar_chiudi(w);
 *
 * ! ADDING TO AN EXISTING ARCHIVE REWRITES IT: a gzip cannot be reopened at
 * the end like a zip catalogue. ex_tar_riapri() copies the old entries into a
 * new file next to it and ex_tar_finisci() puts it in place of the old one:
 * each addition costs as much as the whole archive.
 *
 * ! ONE WRITER AT A TIME: deflate.c keeps one stream. Readers, any number.
 *
 * ! READING IS WHOLE-FILE, up to EXTAR_LEGGI_MAX: inflate works on buffers.
 * ============================================================================= */
#ifndef EXTAR_H
#define EXTAR_H

#define EXTAR_NOME_MAX  512             /* a name inside the archive, '\0' included */
#define EXTAR_LEGGI_MAX (64ul * 1024 * 1024)

typedef struct ExTar ExTar;

typedef struct {
    char          nome[EXTAR_NOME_MAX]; /* without a leading "./"; a directory ends with '/' */
    unsigned long dim;
    char          tipo;                 /* '0' file, '5' directory, others as in ustar */
    int           directory;
    unsigned int  anno, mese, giorno, ore, minuti;
} ExTarVoce;

/* Reading: a .tar, or a .tar.gz / .tgz — gzip is recognised by its bytes. */
ExTar        *ex_tar_apri(const char *percorso);
unsigned int  ex_tar_quante(ExTar *t);
int           ex_tar_voce(ExTar *t, unsigned int i, ExTarVoce *v);

/* Entry i to the file (or directory) `dove`, a whole path. The directories
 * above it are made. 1, or 0 with ex_tar_errore(). */
int           ex_tar_estrai(ExTar *t, unsigned int i, const char *dove);

/* Every entry under the directory `dove`, refusing the names that would climb
 * out of it. Returns how many did NOT come out (0 = all well). */
int           ex_tar_estrai_tutto(ExTar *t, const char *dove);

/* Writing. gz = 1 compresses the whole stream (tar.gz). */
ExTar        *ex_tar_crea(const char *percorso, int gz);
ExTar        *ex_tar_riapri(const char *percorso);
int           ex_tar_aggiungi(ExTar *w, const char *file, const char *nome);
int           ex_tar_aggiungi_albero(ExTar *w, const char *cartella, const char *nome,
                                     int *saltati);
int           ex_tar_finisci(ExTar *w);

void          ex_tar_chiudi(ExTar *t);

/* 1 (fast) .. 3 (best), for the gzip of the archives written from now on. */
void          ex_tar_livello(unsigned int livello);

/* Who wants to see each name as it is added or extracted (/bin/tar prints
 * them). 0 = nobody, the default. */
void          ex_tar_eco(void (*f)(const char *nome));

/* The last error, in words. */
const char   *ex_tar_errore(void);

/* Is this name a tar? .tar, .tar.gz, .tgz; *gz says whether compressed. */
int           ex_tar_nome(const char *percorso, int *gz);

/* gzip and gunzip of a single file, for /bin/gzip and /bin/gunzip. */
int           ex_gz_comprimi(const char *da, const char *a);
int           ex_gz_espandi(const char *da, const char *a);

#endif /* EXTAR_H */
