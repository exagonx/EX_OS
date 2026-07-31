/* =============================================================================
 * bin/cp/cp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Copia un file.
 *
 *   cp <sorgente> <destinazione>
 *
 * Attraversa i montaggi: `cp /bin/sh /disk/SH` prende dal floppy e scrive
 * sul disco, perche' i percorsi passano dal VFS e non da un filesystem
 * particolare. E' il mattone di cui ha bisogno un installatore.
 *
 * NON SOVRASCRIVE una destinazione esistente. Un `cp` che sovrascrive in
 * silenzio e' comodo finche' non si sbaglia a scrivere il nome, e a quel
 * punto il file di prima non c'e' piu' e non c'e' modo di riaverlo. Chi
 * vuole sostituire cancella prima: e' un'operazione in piu', e la fa
 * apposta.
 * ============================================================================= */
#include "libc.h"

#define BLOCCO  4096

static char buf[BLOCCO];

static const char *spiega(int err)
{
    switch (-err) {
        case 2:  return "non trovato";
        case 5:  return "errore di I/O";
        case 17: return "esiste gia'";
        case 21: return "e' una directory";
        case 22: return "percorso o nome non valido";
        case 24: return "troppi file aperti";
        case 28: return "spazio esaurito";
        case 30: return "filesystem in sola lettura";
        default: return "errore";
    }
}

int main(int argc, char **argv)
{
    int fs, fd, n, tot = 0;

    if (argc != 3) {
        printf("uso: cp <sorgente> <destinazione>\n");
        printf("\nNon sovrascrive: se la destinazione esiste, cancellala prima.\n");
        return 1;
    }

    /* La destinazione si controlla PRIMA di aprire la sorgente: aprirla e
     * poi scoprire che non si puo' scrivere sarebbe lavoro buttato, e su
     * un floppy anche qualche secondo di motore. */
    fd = open(argv[2], O_RDONLY);
    if (fd >= 0) {
        close(fd);
        printf("cp: %s: esiste gia'\n", argv[2]);
        return 1;
    }

    fs = open(argv[1], O_RDONLY);
    if (fs < 0) {
        printf("cp: %s: %s (errore %d)\n", argv[1], spiega(fs), fs);
        return 1;
    }

    fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        close(fs);
        printf("cp: %s: %s (errore %d)\n", argv[2], spiega(fd), fd);
        return 1;
    }

    while ((n = (int)read(fs, buf, BLOCCO)) > 0) {
        int scritti = 0;

        /* write() puo' scrivere meno di quanto chiesto — per esempio se il
         * volume si riempie a meta' blocco. Trattare il ritorno come "tutto
         * o niente" produrrebbe un file corto senza accorgersene. */
        while (scritti < n) {
            int w = (int)write(fd, buf + scritti, (unsigned int)(n - scritti));
            if (w <= 0) {
                printf("cp: scrittura fallita dopo %d byte", tot + scritti);
                if (w < 0) printf(": %s (errore %d)", spiega(w), w);
                printf("\n");
                close(fs); close(fd);
                return 1;
            }
            scritti += w;
        }
        tot += scritti;
    }

    close(fs);
    close(fd);

    if (n < 0) {
        printf("cp: lettura fallita dopo %d byte (errore %d)\n", tot, n);
        return 1;
    }

    printf("copiati %d byte in %s\n", tot, argv[2]);
    return 0;
}
