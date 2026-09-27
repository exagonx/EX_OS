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
 * tar and gzip, our own (@TAR-GZ, road 2). SINCE 27 SEPTEMBER 2026 THE
 * FORMAT LIVES IN lib/extar (@ARCHIVI-TAR), which Archivi uses too: this file
 * is only the command line. extar.c, inflate.c and deflate.c are compiled in:
 * this is a static program and needs no shared library, so it can also live
 * on the floppy.
 *
 * One binary, two names: argv[0] ending in "gzip" or "gunzip" selects gzip.
 *
 *   tar c[z]f ARCHIVE PATH...     create (z = gzip)
 *   tar x[z]f ARCHIVE [-C DIR]    extract (gzip is recognised by itself)
 *   tar t[z]f ARCHIVE             list
 *   gzip FILE                     FILE -> FILE.gz (the original stays)
 *   gzip -d FILE.gz / gunzip      FILE.gz -> FILE
 *
 * What is covered, and what is not: lib/extar/extar.h.
 * ============================================================================= */

#include "libc.h"
#include "extar.h"

/* +0.001 a ogni modifica: `tar -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("tar", "0.003");

#define PERC_MAX 512

static const char *g_io = "tar";

static void eco(const char *nome)
{
    /* The library says "! ..." for what did not come out. */
    if (nome[0] == '!') printf("%s: %s\n", g_io, nome + 2);
    else                printf("%s\n", nome);
}

static int manca(const char *cosa)
{
    printf("%s: %s: %s\n", g_io, cosa, ex_tar_errore());
    return 1;
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
    char   nome[PERC_MAX];
    int    i, ok = 1;
    ExTar *w;

    if (n == 0) { printf("%s: niente da archiviare\n", g_io); return 1; }
    if (!(w = ex_tar_crea(arch, gz))) return manca(arch);
    for (i = 0; i < n; i++) {
        nome_interno(percorsi[i], nome);
        if (!ex_tar_aggiungi(w, percorsi[i], nome)) { manca(percorsi[i]); ok = 0; }
    }
    if (!ex_tar_finisci(w)) { manca(arch); ok = 0; }
    ex_tar_chiudi(w);
    return ok ? 0 : 1;
}

static int leggi_archivio(const char *arch, int estrai, const char *dove)
{
    ExTar       *t = ex_tar_apri(arch);
    ExTarVoce    v;
    unsigned int i;
    int          rc = 0;

    if (!t) return manca(arch);
    /* Opened, but a header went bad halfway: what came before is still here,
     * and it is said once. */
    if (ex_tar_errore()[0]) { manca(arch); rc = 1; }
    if (estrai) {
        if (ex_tar_estrai_tutto(t, dove)) rc = 1;
    } else {
        for (i = 0; i < ex_tar_quante(t); i++) {
            if (!ex_tar_voce(t, i, &v)) continue;
            printf("%c %8lu  %s\n", v.directory ? 'd' : v.tipo == '0' ? '-' : v.tipo,
                   v.dim, v.nome);
        }
    }
    ex_tar_chiudi(t);
    return rc;
}

static int gzip_file(const char *nome)
{
    char dest[PERC_MAX];

    if (snprintf(dest, sizeof(dest), "%s.gz", nome) >= (int)sizeof(dest)) return 1;
    if (!ex_gz_comprimi(nome, dest)) return manca(nome);
    printf("%s -> %s\n", nome, dest);
    return 0;
}

static int gunzip_file(const char *nome)
{
    char   dest[PERC_MAX];
    size_t l = strlen(nome);

    if (l > 3 && strcmp(nome + l - 3, ".gz") == 0) {
        snprintf(dest, sizeof(dest), "%.*s", (int)(l - 3), nome);
    } else if (l > 4 && strcmp(nome + l - 4, ".tgz") == 0) {
        snprintf(dest, sizeof(dest), "%.*s.tar", (int)(l - 4), nome);
    } else {
        printf("%s: %s: suffisso sconosciuto (.gz o .tgz)\n", g_io, nome); return 1;
    }
    if (!ex_gz_espandi(nome, dest)) return manca(nome);
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

    ex_tar_eco(eco);
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
