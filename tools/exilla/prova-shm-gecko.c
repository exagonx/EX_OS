/* tools/exilla/prova-shm-gecko.c — la memoria condivisa come la usa Gecko
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *   tools/exilla/esegui-in-exos.sh <binario>
 *
 * Il percorso di ipc/glue/SharedMemoryPlatform_posix.cpp (CreateImpl, Map,
 * CloneHandle, Freeze): shm_open in scrittura, lo STESSO nome riaperto in
 * sola lettura, shm_unlink, ftruncate, dup, fcntl(F_SETFD), mmap MAP_SHARED
 * da due descrittori che devono vedere la stessa memoria. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

static int g_no = 0;
static void ok(const char *cosa, int va)
{
    printf("  %s  %s\n", va ? "[OK]" : "[NO]", cosa);
    if (!va) g_no++;
}

int main(void)
{
    const char *nome = "/org.mozilla.ipc.21.0";
    int   fd, ro, cl, giro;
    char *a, *b;

    printf("prova-shm-gecko: la memoria condivisa di Gecko\n");
    for (giro = 0; giro < 3; giro++) {
        fd = shm_open(nome, O_RDWR | O_CREAT | O_EXCL, 0600);
        ok("shm_open in scrittura", fd >= 0);
        ro = shm_open(nome, O_RDONLY, 0400);
        ok("lo stesso nome riaperto (il descrittore da congelare)", ro >= 0 && ro != fd);
        ok("shm_unlink", shm_unlink(nome) == 0);
        ok("ftruncate a 12000 byte", ftruncate(fd, 12000) == 0);
        cl = dup(ro);
        ok("dup del descrittore", cl >= 0 && cl != ro && cl != fd);
        ok("fcntl F_SETFD sul duplicato", fcntl(cl, F_SETFD, 1) == 0);
        a = mmap(NULL, 12000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        b = mmap(NULL, 12000, PROT_READ, MAP_SHARED, cl, 0);
        ok("mmap dai due descrittori", a != MAP_FAILED && b != MAP_FAILED);
        if (a != MAP_FAILED && b != MAP_FAILED) {
            strcpy(a + 9000, "vista dall'altro");
            ok("la scrittura da uno si legge dall'altro", strcmp(b + 9000, "vista dall'altro") == 0);
        }
        ok("close dei tre descrittori", close(fd) == 0 && close(ro) == 0 && close(cl) == 0);
        ok("la mappa sopravvive ai descrittori", a != MAP_FAILED && a[9000] == 'v');
        ok("munmap", munmap(a, 12000) == 0 && munmap(b, 12000) == 0);
    }
    /* Il percorso di Gecko su EX-OS: SHM_ANON, e il descrittore da
     * congelare e' un dup (patch a SharedMemoryPlatform_posix.cpp). */
    for (giro = 0; giro < 3; giro++) {
        fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
        ro = dup(fd);
        ok("SHM_ANON e il suo dup", fd >= 0 && ro >= 0 && ro != fd);
        ok("ftruncate a 256 KB", ftruncate(fd, 256 * 1024) == 0);
        a = mmap(NULL, 256 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        b = mmap(NULL, 256 * 1024, PROT_READ, MAP_SHARED, ro, 0);
        ok("mmap da tutt'e due", a != MAP_FAILED && b != MAP_FAILED);
        if (a != MAP_FAILED && b != MAP_FAILED) {
            strcpy(a + 200000, "privata e condivisa");
            ok("stessa memoria", strcmp(b + 200000, "privata e condivisa") == 0);
        }
        close(fd); close(ro);
        ok("munmap libera la zona", munmap(a, 256 * 1024) == 0 && munmap(b, 256 * 1024) == 0);
    }

    /* Gecko tiene aperte molte zone insieme (mappe condivise, preferenze,
     * blocchi da 256 KB della lista dei caratteri...): quante ne regge? */
    {
        int  tenuti[64], n, e = 0;
        char nm[40];

        for (n = 0; n < 40; n++) {
            (void)nm;
            tenuti[n] = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
            if (tenuti[n] < 0) { e = errno; break; }
            if (ftruncate(tenuti[n], 256 * 1024) != 0) { e = errno; close(tenuti[n]); break; }
        }
        printf("  zone SHM_ANON da 256 KB aperte insieme: %d (errno %d)\n", n, e);
        ok("40 zone private da 256 KB insieme", n == 40);
        while (n-- > 0) close(tenuti[n]);
    }

    printf("prova-shm-gecko: %d NO\n", g_no);
    return g_no;
}
