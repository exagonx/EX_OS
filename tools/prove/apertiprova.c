/* =============================================================================
 * tools/prove/apertiprova.c — an open file follows the file, not its name
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     apertiprova DIR        runs the five cases in DIR (an ext2 or FAT mount)
 *
 * The defect (kernel 0.267, 22 October 2026): a VFS handle was a path, so a
 * program whose file was renamed to .old and replaced — what netupdate does —
 * went on reading the NEW file. The cases:
 *   rinomina   read half, rename to .old, write a new file with the old name,
 *              read the rest: it must be the old content
 *   cancella   unlink an open file: the name goes, the reads still work, and
 *              after the close nothing is left in the directory
 *   sopra      rename another file ON TOP of an open one
 *   cartella   rename the directory that holds an open file
 *   chiuso     unlink a closed file: gone at once, no hidden name
 *
 * One line per case, "APERTI <caso> OK" or "APERTI <caso> NO <why>", and
 * "APERTI FINE <failures>" at the end, for tools/prova_aperti.sh.
 * ============================================================================= */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define N 65536

static unsigned char g_buf[N];
static int g_errori;

static unsigned char byte_di(int seme, int i) { return (unsigned char)(seme * 31 + i * 7 + (i >> 8)); }

static int scrivi(const char *nome, int seme)
{
    int fd = open(nome, O_WRONLY | O_CREAT | O_TRUNC, 0644), i;

    if (fd < 0) return -1;
    for (i = 0; i < N; i++) g_buf[i] = byte_di(seme, i);
    if (write(fd, g_buf, N) != N) { close(fd); return -1; }
    return close(fd);
}

/* Reads [da, da+quanti) from fd and checks it against `seme`. */
static int controlla(int fd, int seme, int da, int quanti)
{
    int i;

    if (lseek(fd, da, SEEK_SET) != da) return -1;
    if (read(fd, g_buf, quanti) != quanti) return -1;
    for (i = 0; i < quanti; i++) if (g_buf[i] != byte_di(seme, da + i)) return -1;
    return 0;
}

static int nascosti(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;

    if (d == NULL) return -1;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, "~EXV", 4) == 0 || strncmp(e->d_name, "~exv", 4) == 0) n++;
    closedir(d);
    return n;
}

static void esito(const char *caso, const char *perche)
{
    if (perche == NULL) printf("APERTI %s OK\n", caso);
    else { printf("APERTI %s NO %s\n", caso, perche); g_errori++; }
}

int main(int argc, char **argv)
{
    char a[256], b[256], c[256];
    struct stat st;
    int fd, fd2;

    if (argc < 2) { printf("uso: apertiprova DIR\n"); return 2; }

    /* --- rinomina ------------------------------------------------------- */
    snprintf(a, sizeof(a), "%s/prog", argv[1]);
    snprintf(b, sizeof(b), "%s/prog.old", argv[1]);
    remove(b);
    if (scrivi(a, 1) != 0) { esito("rinomina", "non scrivo"); goto cancella; }
    fd = open(a, O_RDONLY);
    if (fd < 0 || controlla(fd, 1, 0, N / 2) != 0) { esito("rinomina", "prima meta'"); goto cancella; }
    if (rename(a, b) != 0) { esito("rinomina", "rename"); close(fd); goto cancella; }
    if (scrivi(a, 2) != 0) { esito("rinomina", "il nuovo"); close(fd); goto cancella; }
    if (controlla(fd, 1, N / 2, N / 2) != 0) esito("rinomina", "la seconda meta' viene dal file nuovo");
    else {
        fd2 = open(a, O_RDONLY);
        if (fd2 < 0 || controlla(fd2, 2, 0, N) != 0) esito("rinomina", "il nuovo non si legge");
        else esito("rinomina", NULL);
        if (fd2 >= 0) close(fd2);
    }
    close(fd);

cancella:
    /* --- cancella ------------------------------------------------------- */
    snprintf(a, sizeof(a), "%s/via", argv[1]);
    if (scrivi(a, 3) != 0) { esito("cancella", "non scrivo"); goto sopra; }
    fd = open(a, O_RDONLY);
    if (fd < 0) { esito("cancella", "open"); goto sopra; }
    if (unlink(a) != 0) { esito("cancella", "unlink"); close(fd); goto sopra; }
    if (stat(a, &st) == 0) esito("cancella", "il nome c'e' ancora");
    else if (controlla(fd, 3, 0, N) != 0) esito("cancella", "letture dopo unlink");
    else if (nascosti(argv[1]) != 1) esito("cancella", "nessun nome nascosto mentre e' aperto");
    else {
        close(fd); fd = -1;
        if (nascosti(argv[1]) != 0) esito("cancella", "il nascosto resta dopo la chiusura");
        else esito("cancella", NULL);
    }
    if (fd >= 0) close(fd);

sopra:
    /* --- sopra ---------------------------------------------------------- */
    snprintf(a, sizeof(a), "%s/sotto", argv[1]);
    snprintf(b, sizeof(b), "%s/sotto.new", argv[1]);
    if (scrivi(a, 4) != 0 || scrivi(b, 5) != 0) { esito("sopra", "non scrivo"); goto cartella; }
    fd = open(a, O_RDONLY);
    if (fd < 0) { esito("sopra", "open"); goto cartella; }
    if (rename(b, a) != 0) { esito("sopra", "rename"); close(fd); goto cartella; }
    if (controlla(fd, 4, 0, N) != 0) esito("sopra", "l'aperto legge il file arrivato sopra");
    else {
        fd2 = open(a, O_RDONLY);
        if (fd2 < 0 || controlla(fd2, 5, 0, N) != 0) esito("sopra", "il nome non porta al nuovo");
        else {
            close(fd); fd = -1;
            if (nascosti(argv[1]) != 0) esito("sopra", "il nascosto resta dopo la chiusura");
            else esito("sopra", NULL);
        }
        if (fd2 >= 0) close(fd2);
    }
    if (fd >= 0) close(fd);

cartella:
    /* --- cartella ------------------------------------------------------- */
    snprintf(a, sizeof(a), "%s/dir1", argv[1]);
    snprintf(b, sizeof(b), "%s/dir2", argv[1]);
    snprintf(c, sizeof(c), "%s/dir1/f", argv[1]);
    mkdir(a, 0755);
    if (scrivi(c, 6) != 0) { esito("cartella", "non scrivo"); goto chiuso; }
    fd = open(c, O_RDONLY);
    if (fd < 0 || controlla(fd, 6, 0, N / 2) != 0) { esito("cartella", "prima meta'"); goto chiuso; }
    if (rename(a, b) != 0) { esito("cartella", "rename della directory"); close(fd); goto chiuso; }
    if (controlla(fd, 6, N / 2, N / 2) != 0) esito("cartella", "letture dopo la rinomina");
    else esito("cartella", NULL);
    close(fd);

chiuso:
    /* --- chiuso --------------------------------------------------------- */
    snprintf(a, sizeof(a), "%s/chiuso", argv[1]);
    if (scrivi(a, 7) != 0) { esito("chiuso", "non scrivo"); goto fine; }
    if (unlink(a) != 0) esito("chiuso", "unlink");
    else if (stat(a, &st) == 0) esito("chiuso", "il nome c'e' ancora");
    else if (nascosti(argv[1]) != 0) esito("chiuso", "un nome nascosto per un file chiuso");
    else esito("chiuso", NULL);

fine:
    printf("APERTI FINE %d\n", g_errori);
    return g_errori ? 1 : 0;
}
