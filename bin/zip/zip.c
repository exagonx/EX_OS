/* =============================================================================
 * bin/zip/zip.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * zip — archives from the command line
 *
 *     zip archivio.zip file...     makes the archive with those files
 *     zip -l archivio.zip          lists what is inside
 *     zip -x archivio.zip [dove]   extracts everything into `dove` (or here)
 *
 * ! ONE PROGRAM AND THREE FLAGS, IN THE SHAPE OF PKZIP, which is what was
 * asked for. The Unix habit would be two programs, zip and unzip; the request
 * named PKZIP, and on a system whose floppy has 30 KB left one binary that
 * does three things beats two that do one and a half each.
 *
 * ! THE FORMAT IS NOT IN HERE. Everything about ZIP lives in lib/exzip, and
 * this file only knows about arguments, names and messages - the same split as
 * eximg and the programs that draw. The ExWin archiver will call exactly the
 * same functions, which is the only way the two cannot drift apart.
 * ============================================================================= */

#include "libc.h"
#include "exzip.h"

/* +0.001 a ogni modifica: `zip -version` la stampa. Vedi EX_VERSIONE in libc.h. */
#define VERSIONE_APP "0.003"
EX_VERSIONE("zip", VERSIONE_APP);

#define PERC_MAX  320

static void istruzioni(void)
{
    printf("zip %s - archivi ZIP\n\n", VERSIONE_APP);
    printf("  zip archivio.zip file...     crea l'archivio con quei file\n");
    printf("                               (una cartella entra con tutto il suo albero)\n");
    printf("  zip -l archivio.zip          elenca quello che c'e' dentro\n");
    printf("  zip -x archivio.zip [dove]   estrae tutto (qui, o in 'dove')\n\n");
    printf("  Comprime in 'deflate', il metodo di quasi tutti gli archivi; un\n");
    printf("  file che compresso verrebbe piu' grande (un JPEG, uno ZIP) entra\n");
    printf("  com'e', in 'store'. In lettura capisce tutt'e due.\n");
}

/* The last piece of a path: "/disk/prova/alfa.txt" -> "alfa.txt".
 *
 * ! THE PATH DOES NOT GO INTO THE ARCHIVE, and it is a decision, not an
 * oversight. `zip a.zip /disk/prova/alfa.txt` puts in "alfa.txt": an archive
 * that carried the absolute path would, on being extracted, want to write
 * exactly where it came from - which is somebody else's disk. */
static const char *nome_corto(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int crea(const char *archivio, char **file, int quanti)
{
    ExZip *z = ex_zip_crea(archivio);
    int    i, messi = 0;

    if (!z) { printf("zip: %s\n", ex_zip_errore()); return 1; }

    for (i = 0; i < quanti; i++) {
        struct stat st;

        /* A directory goes in whole, under its own name. */
        if (stat(file[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            char base[256];
            int  k, n, saltati = 0;

            snprintf(base, sizeof(base), "%s", file[i]);
            k = (int)strlen(base);
            while (k > 1 && base[k - 1] == '/') base[--k] = '\0';

            n = ex_zip_aggiungi_albero(z, base, nome_corto(base), &saltati);
            if (n == -2) {
                printf("  ! %s: la libreria degli archivi e' vecchia, non sa le cartelle\n",
                       file[i]);
                continue;
            }
            if (n < 0) { printf("  ! %s: %s\n", file[i], ex_zip_errore()); continue; }
            printf("  + %s/ (%d file)\n", nome_corto(base), n);
            if (saltati) printf("    ! %s\n", ex_zip_errore());
            messi += n;
            continue;
        }

        if (!ex_zip_aggiungi(z, file[i], nome_corto(file[i]))) {
            printf("  ! %s: %s\n", file[i], ex_zip_errore());
            continue;
        }
        printf("  + %s\n", nome_corto(file[i]));
        messi++;
    }

    /* ! THE CATALOGUE GETS WRITTEN EVEN IF SOMETHING WAS LEFT OUT. Without it
     * the file is not an archive at all: the entries are there and nothing
     * points at them, and every unzip in the world calls it broken. Half an
     * archive that opens beats a whole one that does not. */
    if (!ex_zip_finisci(z)) {
        printf("zip: %s\n", ex_zip_errore());
        ex_zip_chiudi(z);
        return 1;
    }
    ex_zip_chiudi(z);

    printf("%s: %d file\n", archivio, messi);
    return messi == quanti ? 0 : 1;
}

static int elenca(const char *archivio)
{
    ExZip       *z = ex_zip_apri(archivio);
    ExZipVoce    v;
    unsigned int i, n;
    unsigned long tot = 0, tot_c = 0;

    if (!z) { printf("zip: %s\n", ex_zip_errore()); return 1; }

    n = ex_zip_quante(z);
    printf("dimensione  compresso  data        ora    nome\n");

    for (i = 0; i < n; i++) {
        if (!ex_zip_voce(z, i, &v)) continue;
        printf("%10lu  %9lu  %04u-%02u-%02u  %02u:%02u  %s%s\n",
               v.dim, v.dim_c, v.anno, v.mese, v.giorno, v.ore, v.minuti,
               v.nome, v.metodo == EXZIP_DEFLATE ? "  (deflate)" : "");
        tot   += v.dim;
        tot_c += v.dim_c;
    }

    printf("\n%u file, %lu byte, %lu nell'archivio\n", n, tot, tot_c);
    ex_zip_chiudi(z);
    return 0;
}

/* Creates the directories a name needs, one level at a time.
 *
 * ! mkdir DOES NOT MAKE A PATH, IT MAKES ONE DIRECTORY, so "a/b/c.txt" wants
 * two calls before the file can be created. Failing with EEXIST is the normal
 * case here, not an error: several entries share their parents. */
static void cartelle_per(const char *perc)
{
    char p[PERC_MAX];
    unsigned int i;

    strncpy(p, perc, PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    for (i = 1; p[i]; i++) {
        if (p[i] != '/') continue;
        p[i] = '\0';
        mkdir(p, 0755);
        p[i] = '/';
    }
}

/* ! A NAME THAT CLIMBS OUT OF THE DESTINATION IS REFUSED, and this is the one
 * check in this program that is about safety rather than tidiness. An archive
 * is written by somebody else: a name like "../../boot/kernel.cfg", or one
 * starting with a slash, would have this program overwrite files nobody asked
 * about. Every archiver in the world has had this bug at least once. */
static int nome_pericoloso(const char *n)
{
    unsigned int i;

    if (n[0] == '/' || n[0] == '\\') return 1;

    for (i = 0; n[i]; i++)
        if (n[i] == '.' && n[i + 1] == '.' &&
            (n[i + 2] == '/' || n[i + 2] == '\0') &&
            (i == 0 || n[i - 1] == '/'))
            return 1;

    return 0;
}

static int estrai(const char *archivio, const char *dove)
{
    ExZip       *z = ex_zip_apri(archivio);
    ExZipVoce    v;
    char         perc[PERC_MAX];
    unsigned int i, n, fatti = 0, saltati = 0;

    if (!z) { printf("zip: %s\n", ex_zip_errore()); return 1; }

    n = ex_zip_quante(z);
    for (i = 0; i < n; i++) {
        if (!ex_zip_voce(z, i, &v)) continue;

        if (nome_pericoloso(v.nome)) {
            printf("  ! %s: nome che esce dalla destinazione, non lo estraggo\n",
                   v.nome);
            saltati++;
            continue;
        }

        if (snprintf(perc, sizeof(perc), "%s/%s", dove, v.nome) >= (int)sizeof(perc)) {
            printf("  ! %s: percorso troppo lungo\n", v.nome);
            saltati++;
            continue;
        }

        cartelle_per(perc);

        if (!ex_zip_estrai(z, i, perc)) {
            printf("  ! %s: %s\n", v.nome, ex_zip_errore());
            saltati++;
            continue;
        }
        printf("  %s\n", v.nome);
        fatti++;
    }

    printf("\n%u estratti", fatti);
    if (saltati) printf(", %u saltati", saltati);
    printf("\n");

    ex_zip_chiudi(z);
    return saltati ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { istruzioni(); return 1; }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-help") == 0) {
        istruzioni();
        return 0;
    }

    if (strcmp(argv[1], "-l") == 0) {
        if (argc < 3) { printf("zip: -l vuole il nome dell'archivio\n"); return 1; }
        return elenca(argv[2]);
    }

    if (strcmp(argv[1], "-x") == 0) {
        if (argc < 3) { printf("zip: -x vuole il nome dell'archivio\n"); return 1; }
        return estrai(argv[2], argc > 3 ? argv[3] : ".");
    }

    if (argv[1][0] == '-') {
        printf("zip: non conosco l'opzione %s\n\n", argv[1]);
        istruzioni();
        return 1;
    }

    if (argc < 3) {
        printf("zip: serve almeno un file da mettere dentro\n");
        return 1;
    }

    return crea(argv[1], argv + 2, argc - 2);
}
