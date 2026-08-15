/* =============================================================================
 * bin/xcp/xcp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Copia ricorsiva di una directory.
 *
 *   xcp <sorgente> <destinazione>
 *
 * Ricrea sotto <destinazione> l'intero albero di <sorgente>: directory,
 * file e subdirectory a qualsiasi profondita'. Le directory di destinazione
 * vengono create se non esistono; i file gia' presenti NON vengono
 * sovrascritti (stessa politica di cp).
 *
 * Se la destinazione non esiste viene creata. Se esiste e non e' una
 * directory, xcp si ferma prima di toccare nulla.
 *
 * La ricorsione usa lo stack del processo. Su un floppy da 1.44 MB la
 * profondita' reale dell'albero e' al massimo qualche decina di livelli,
 * ben dentro i limiti dello stack utente.
 * ============================================================================= */
#include "libc.h"

#define BLOCCO       4096
#define PERCORSO_MAX 320

static char buf_copia[BLOCCO];

/* Costruisce dst = dir "/" nome, con le stesse garanzie di snprintf:
 * il risultato e' sempre terminato da '\0' e non supera size byte totali.
 * Ritorna 0 se ci stava, -1 se avrebbe troncato. */
static int path_join(char *dst, int size, const char *dir, const char *nome)
{
    int ld = 0, ln = 0;
    const char *p;

    for (p = dir;  *p; p++) ld++;
    for (p = nome; *p; p++) ln++;

    /* Conto conservativo: assume sempre un '/' in mezzo. */
    if (ld + 1 + ln + 1 > size) return -1;

    for (p = dir; *p; p++) *dst++ = *p;
    /* Aggiunge il separatore solo se dir non finisce gia' con '/'. */
    if (ld > 0 && dst[-1] != '/') *dst++ = '/';
    for (p = nome; *p; p++) *dst++ = *p;
    *dst = '\0';
    return 0;
}

/* Copia un singolo file da src a dst.
 * Come cp: non sovrascrive, usa la stessa logica di write parziale.
 * Ritorna 0 ok, -1 errore (messaggio gia' stampato). */
static int copia_file(const char *src, const char *dst)
{
    int fs, fd, n, tot = 0;

    /* La destinazione si controlla PRIMA di aprire la sorgente. */
    fd = open(dst, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        printf("xcp: %s: esiste gia'\n", dst);
        return -1;
    }

    fs = open(src, O_RDONLY);
    if (fs < 0) {
        printf("xcp: %s: %s\n", src, strerror(errno));
        return -1;
    }

    fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        printf("xcp: %s: %s\n", dst, strerror(errno));
        close(fs);
        return -1;
    }

    while ((n = (int)read(fs, buf_copia, BLOCCO)) > 0) {
        int scritti = 0;

        /* write() puo' scrivere meno di quanto chiesto: si insiste. */
        while (scritti < n) {
            int w = (int)write(fd, buf_copia + scritti,
                               (unsigned int)(n - scritti));
            if (w <= 0) {
                printf("xcp: scrittura fallita su %s dopo %d byte",
                       dst, tot + scritti);
                if (w < 0) printf(": %s", strerror(errno));
                printf("\n");
                close(fs); close(fd);
                return -1;
            }
            scritti += w;
        }
        tot += scritti;
    }

    close(fs);
    close(fd);

    if (n < 0) {
        printf("xcp: lettura fallita su %s dopo %d byte: %s\n",
               src, tot, strerror(errno));
        return -1;
    }

    return 0;
}

/* Copia ricorsiva dell'albero src -> dst.
 * dst deve gia' esistere come directory al momento della chiamata.
 * Ritorna il numero di errori incontrati (0 = tutto ok). */
static int copia_dir(const char *src, const char *dst)
{
    DIR           *d;
    struct dirent *e;
    char           p_src[PERCORSO_MAX];
    char           p_dst[PERCORSO_MAX];
    int            errori = 0;

    d = opendir(src);
    if (d == NULL) {
        printf("xcp: %s: %s\n", src, strerror(errno));
        return 1;
    }

    while ((e = readdir(d)) != NULL) {

        if (path_join(p_src, PERCORSO_MAX, src, e->d_name) < 0 ||
            path_join(p_dst, PERCORSO_MAX, dst, e->d_name) < 0) {
            printf("xcp: percorso troppo lungo, saltato: %s/%s\n",
                   src, e->d_name);
            errori++;
            continue;
        }

        if (e->d_type == DT_DIR) {
            /* Crea la subdirectory nella destinazione se manca. */
            struct stat st;
            if (stat(p_dst, &st) != 0) {
                if (mkdir(p_dst, 0755) != 0) {
                    printf("xcp: mkdir %s: %s\n", p_dst, strerror(errno));
                    errori++;
                    continue;        /* la subdir di sorgente si salta */
                }
            }
            /* Discesa ricorsiva: gli errori figli si sommano. */
            errori += copia_dir(p_src, p_dst);
        } else {
            /* File ordinario. */
            if (copia_file(p_src, p_dst) != 0)
                errori++;
        }
    }

    closedir(d);
    return errori;
}

int main(int argc, char **argv)
{
    struct stat st;
    int         errori;

    if (argc != 3) {
        printf("uso: xcp <sorgente> <destinazione>\n");
        printf("\nCopia ricorsiva di una directory intera.\n");
        printf("Non sovrascrive file esistenti nella destinazione.\n");
        printf("Per copiare un file singolo usa cp.\n");
        return 1;
    }

    /* La sorgente deve esistere ed essere una directory. */
    if (stat(argv[1], &st) != 0) {
        printf("xcp: %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        printf("xcp: %s: non e' una directory (usa cp per i file singoli)\n",
               argv[1]);
        return 1;
    }

    /* La destinazione: creala se non esiste, fermati se e' un file. */
    if (stat(argv[2], &st) != 0) {
        if (mkdir(argv[2], 0755) != 0) {
            printf("xcp: mkdir %s: %s\n", argv[2], strerror(errno));
            return 1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        printf("xcp: %s: esiste gia' come file\n", argv[2]);
        return 1;
    }

    errori = copia_dir(argv[1], argv[2]);

    if (errori > 0) {
        printf("xcp: completato con %d error%s\n",
               errori, errori == 1 ? "e" : "i");
        return 1;
    }

    printf("xcp: %s -> %s: completato\n", argv[1], argv[2]);
    return 0;
}
