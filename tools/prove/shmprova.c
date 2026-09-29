/* =============================================================================
 * tools/prove/shmprova.c — POSIX shared memory inside EX-OS (@SHM-OPEN)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built with i386-exos-gcc against the static libc.a and run in QEMU by
 * tools/prova_shm.sh. The parent creates a zone with a name as long as
 * Mozilla's, closes the descriptor (the mapping must survive), and starts
 * itself again: the child opens the same name, reads what the parent wrote
 * and answers in the second page. Last line: "shmprova: <n> NO".
 * ============================================================================= */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define NOME "/org.mozilla.ipc.exos.prova.1"

static int g_no = 0, g_n = 0;

static void verifica(int cond, const char *cosa)
{
    g_n++;
    printf("%s %d %s\n", cond ? "ok" : "NO", g_n, cosa);
    if (!cond) g_no++;
}

static int figlio(void)
{
    struct stat st;
    char       *p;
    int         fd = shm_open(NOME, O_RDWR, 0);

    if (fd < 0) { printf("figlio: shm_open errno %d\n", errno); return 2; }
    if (fstat(fd, &st) != 0 || st.st_size != 8192) { printf("figlio: fstat\n"); return 3; }
    p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { printf("figlio: mmap errno %d\n", errno); return 4; }
    if (strcmp(p, "dal padre") != 0) { printf("figlio: letto '%s'\n", p); return 5; }
    strcpy(p + 4096, "dal figlio");
    munmap(p, 8192);
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    struct stat st;
    char       *p;
    int         fd, pid, stato = -1, r;

    if (argc > 1 && strcmp(argv[1], "figlio") == 0) return figlio();

    fd = shm_open(NOME, O_CREAT | O_RDWR, 0600);
    verifica(fd >= 0, "shm_open con un nome piu' lungo di 15 caratteri");
    verifica(fstat(fd, &st) == 0 && st.st_size == 0, "fstat prima di ftruncate: 0 byte");
    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    verifica(p == MAP_FAILED && errno == ENXIO, "mmap prima di ftruncate: ENXIO");
    verifica(ftruncate(fd, 8192) == 0, "ftruncate crea la zona");
    verifica(fstat(fd, &st) == 0 && st.st_size == 8192, "fstat: 8192 byte");
    verifica(ftruncate(fd, 65536) != 0 && errno == EINVAL, "una zona non cresce: EINVAL");
    p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    verifica(p != MAP_FAILED, "mmap sul descrittore");
    if (p == MAP_FAILED) goto fine;
    strcpy(p, "dal padre");
    verifica(close(fd) == 0, "close del descrittore");
    verifica(strcmp(p, "dal padre") == 0, "la mappatura sopravvive a close");
    verifica(shm_open(NOME, O_RDWR, 0) < 0 && errno == EEXIST,
             "lo stesso nome due volte nello stesso processo: EEXIST (dichiarato)");

    {
        char *av[3];
        av[0] = argv[0];
        av[1] = "figlio";
        av[2] = NULL;
        pid = spawn(argv[0], av);
    }
    verifica(pid > 0, "il figlio parte");
    if (pid > 0) waitpid(pid, &stato, 0);
    verifica(stato == 0, "il figlio ha aperto il nome e letto «dal padre»");
    verifica(strcmp(p + 4096, "dal figlio") == 0, "e la sua risposta si legge qui");

    verifica(munmap(p, 8192) == 0, "munmap della zona");
    r = shm_open(NOME, O_RDWR, 0);
    verifica(r < 0 && errno == ENOENT, "l'ultimo utente l'ha chiusa: il nome non c'e' piu'");
    verifica(ftruncate(1, 10) != 0, "ftruncate su un descrittore che non e' una zona: errore");
fine:
    printf("shmprova: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
