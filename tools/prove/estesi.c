/* =============================================================================
 * tools/prove/estesi.c — a file with many extents, cut and grown (@EXT4-SCRITTURA)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     estesi FILE N        writes N pieces of 4 KB, each one followed by a
 *                          hole of 4 KB: on ext4 N extents, so the tree
 *                          leaves the inode (more than 4), fills a leaf
 *                          block and splits it; reads it all back (the holes
 *                          as zeros); truncates it in the middle of piece
 *                          N/2 (the tree is cut); writes the pieces from N/2
 *                          on again (it grows back) and checks the whole
 *                          file; then FILE.g, ONE write() of 4 MB (a
 *                          transaction cut in pieces by ext4.mod)
 *
 *     estesi -f DIR K      creates K small files DIR/fNNNN ("file NNNN"):
 *                          enough to fill the groups that have inodes and
 *                          take one from a group never initialised
 *
 * One line per phase, "ESTESI <fase> OK" or "ESTESI ERRORE <fase> ...", for
 * tools/prova_ext4.sh, which checks the file from outside with debugfs:
 * the byte at offset o is atteso(o), or 0 in a hole.
 * ============================================================================= */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PEZZO   4096ul
#define GRANDE  (4ul * 1024ul * 1024ul)

static unsigned char atteso(unsigned long o)
{
    return (unsigned char)((o * 131ul + (o >> 12) * 7ul) & 0xFFul);
}

static unsigned char b[PEZZO];
static int esito = 0;

static void male(const char *fase, unsigned long dove)
{
    printf("ESTESI ERRORE %s a %lu\n", fase, dove);
    esito = 1;
}

/* the pieces da..a-1, at offset i * 2 * PEZZO */
static int scrivi(const char *f, unsigned long da, unsigned long a, int crea)
{
    int fd = open(f, crea ? (O_WRONLY | O_CREAT | O_TRUNC) : O_WRONLY, 0644);
    unsigned long i, k;

    if (fd < 0) return -1;
    for (i = da; i < a; i++) {
        for (k = 0; k < PEZZO; k++) b[k] = atteso(i * 2ul * PEZZO + k);
        if (lseek(fd, (long)(i * 2ul * PEZZO), SEEK_SET) < 0 ||
            write(fd, b, PEZZO) != (long)PEZZO) { close(fd); return -1; }
    }
    close(fd);
    return 0;
}

/* the whole file, `dim` bytes, against the pieces 0..n-1 and holes */
static void controlla(const char *f, unsigned long dim, const char *fase)
{
    int fd = open(f, O_RDONLY);
    unsigned long o = 0, k;
    long r;

    if (fd < 0) { male(fase, 0); return; }
    if (lseek(fd, 0, SEEK_END) != (long)dim) male(fase, dim);
    lseek(fd, 0, SEEK_SET);
    while ((r = read(fd, b, PEZZO)) > 0) {
        for (k = 0; k < (unsigned long)r; k++, o++) {
            unsigned char v = ((o / PEZZO) % 2ul == 0) ? atteso(o) : 0;

            if (b[k] != v) { male(fase, o); close(fd); return; }
        }
    }
    close(fd);
    if (o != dim) male(fase, o);
    else printf("ESTESI %s OK\n", fase);
}

int main(int argc, char **argv)
{
    unsigned long n, taglio, k;
    static char g[256];
    unsigned char *grande;
    int fd;

    if (argc < 3) { printf("uso: estesi FILE N | estesi -f DIR K\n"); return 2; }
    if (strcmp(argv[1], "-f") == 0 && argc >= 4) {
        unsigned long i, quanti = strtoul(argv[3], NULL, 10);
        char nome[256], riga[32];

        for (i = 0; i < quanti; i++) {
            snprintf(nome, sizeof(nome), "%s/f%04lu", argv[2], i);
            snprintf(riga, sizeof(riga), "file %04lu\n", i);
            fd = open(nome, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0 || write(fd, riga, strlen(riga)) != (long)strlen(riga)) male("file", i);
            if (fd >= 0) close(fd);
        }
        printf("ESTESI FILE %lu %s\n", quanti, esito ? "ERRORI" : "PULITO");
        return esito;
    }
    n = strtoul(argv[2], NULL, 10);

    if (scrivi(argv[1], 0, n, 1) != 0) male("scrittura", 0);
    controlla(argv[1], (n * 2ul - 1ul) * PEZZO, "scritto");

    taglio = (n / 2ul) * 2ul * PEZZO + 100ul;
    if (truncate(argv[1], (unsigned int)taglio) != 0) male("truncate", taglio);
    {
        /* the 100 bytes of piece n/2 stay, the rest is gone */
        int f2 = open(argv[1], O_RDONLY);
        long r;

        if (f2 < 0 || lseek(f2, 0, SEEK_END) != (long)taglio) male("tagliato", taglio);
        else {
            lseek(f2, (long)(taglio - 100ul), SEEK_SET);
            r = read(f2, b, PEZZO);
            if (r != 100) male("tagliato", taglio);
            else {
                for (k = 0; k < 100ul; k++)
                    if (b[k] != atteso(taglio - 100ul + k)) { male("tagliato", k); break; }
                if (k == 100ul) printf("ESTESI tagliato OK\n");
            }
        }
        if (f2 >= 0) close(f2);
    }

    if (scrivi(argv[1], n / 2ul, n, 0) != 0) male("ricrescita", 0);
    controlla(argv[1], (n * 2ul - 1ul) * PEZZO, "ricresciuto");

    /* one write() of 4 MB */
    snprintf(g, sizeof(g), "%s.g", argv[1]);
    grande = (unsigned char *)malloc(GRANDE);
    if (grande == NULL) { male("malloc", 0); return 1; }
    for (k = 0; k < GRANDE; k++) grande[k] = atteso(k);
    fd = open(g, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, grande, GRANDE) != (long)GRANDE) male("grande", 0);
    if (fd >= 0) close(fd);
    fd = open(g, O_RDONLY);
    if (fd < 0) male("grande-lettura", 0);
    else {
        unsigned long o = 0;
        long r;
        int  rotto = 0;

        while (!rotto && (r = read(fd, b, PEZZO)) > 0)
            for (k = 0; k < (unsigned long)r; k++, o++)
                if (b[k] != atteso(o)) { male("grande-lettura", o); rotto = 1; break; }
        close(fd);
        if (!rotto && o == GRANDE) printf("ESTESI grande OK\n");
        else if (!rotto) male("grande-lettura", o);
    }
    free(grande);
    printf("ESTESI FINE %s\n", esito ? "ERRORI" : "PULITO");
    return esito;
}
