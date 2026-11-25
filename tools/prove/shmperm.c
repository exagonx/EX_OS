/* =============================================================================
 * tools/prove/shmperm.c -- named zones have an owner (kernel 0.367,
 * debug/bug_SHM001)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Run as root:
 *   a zone root made (0600 by default): uid 1000 cannot attach (EACCES);
 *   one made SHM_MODO(0666), as the audio driver does: uid 1000 attaches;
 *   one uid 1000 made: another process of 1000 attaches, uid 1001 does not;
 *   SHM_CREA|SHM_ESCL on a name that is there: EEXIST, never an attach;
 *   root attaches to everything.
 * Until 0.367 anyone attached to any name: windows, clipboard, wserver.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

/* ShmZona and shm_apri come with stdio.h (libc.h); the flags of 0.367 may be
 * newer than the cross toolchain's copy */
typedef ShmZona Zona;
#define CREA      0x0001u
#define ESCL      0x0002u
#define MODO(m)   (((unsigned int)(m) & 0777u) << 16)

static int no;
static void esito(int c, const char *m) { printf("  [%s] %s\n", c ? "OK" : "NO", m); if (!c) no++; }

static int apri(const char *nome, unsigned int flag, Zona *z)
{
    memset(z, 0, sizeof(*z));
    strncpy(z->nome, nome, sizeof(z->nome) - 1);
    z->byte = 4096;
    z->flag = flag;
    return shm_apri(z);
}

/* a child as uid that attaches to nome: 0 or -errno. `del_padre`: the
 * parent's own attach of that zone, which the fork copied - attaching twice
 * from one process is EEXIST by design (kernel/mm/shm.c), so the child lets
 * it go first and then asks as a stranger would. */
static int come(unsigned uid, const char *nome, const Zona *del_padre)
{
    int st, pid = fork();

    if (pid == 0) {
        Zona z;
        int  r;

        if (del_padre) shm_chiudi((void *)(unsigned long)del_padre->virt);
        if (uid && (setgid(uid) != 0 || setuid(uid) != 0)) _exit(250);
        r = apri(nome, 0, &z);
        if (r == 0) shm_chiudi((void *)(unsigned long)z.virt);
        _exit(r == 0 ? 0 : (-r) & 0xFF);
    }
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return 251;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 252;
}

int main(void)
{
    Zona a, b, d, e;
    int  r, st, pid, p2[2], p3[2];
    char m[128], c;

    if (getuid() != 0) { printf("shmperm: da root\n"); return 2; }

    r = apri("prova.root", CREA, &a);
    esito(r == 0, "root crea prova.root (0600)");
    r = come(1000, "prova.root", &a);
    snprintf(m, sizeof(m), "uid 1000 non si aggancia a prova.root (errno %d)", r);
    esito(r == EACCES, m);
    r = apri("prova.tutti", CREA | MODO(0666), &b);
    esito(r == 0, "root crea prova.tutti con SHM_MODO(0666), come il driver audio");
    r = come(1000, "prova.tutti", &b);
    snprintf(m, sizeof(m), "uid 1000 si aggancia a prova.tutti (errno %d)", r);
    esito(r == 0, m);

    r = apri("prova.root", CREA | ESCL, &d);
    snprintf(m, sizeof(m), "SHM_CREA|SHM_ESCL su un nome che c'e': EEXIST (%d)", r);
    esito(r == -EEXIST, m);
    r = come(0, "prova.tutti", &b);
    esito(r == 0, "un altro processo di root si aggancia");

    /* uid 1000 makes one and keeps it while the others try */
    if (pipe(p2) != 0 || pipe(p3) != 0) return 1;
    pid = fork();
    if (pid == 0) {
        Zona z;

        if (setgid(1000) != 0 || setuid(1000) != 0) _exit(250);
        c = apri("prova.mario", CREA, &z) == 0 ? 'k' : 'n';
        if (write(p2[1], &c, 1) != 1) _exit(1);
        if (read(p3[0], &c, 1) != 1) _exit(1);  /* until the parent is done */
        _exit(0);
    }
    r = read(p2[0], &c, 1);
    esito(r == 1 && c == 'k', "uid 1000 crea prova.mario");
    r = come(1000, "prova.mario", NULL);
    snprintf(m, sizeof(m), "un altro processo di 1000 si aggancia (errno %d)", r);
    esito(r == 0, m);
    r = come(1001, "prova.mario", NULL);
    snprintf(m, sizeof(m), "uid 1001 no (errno %d)", r);
    esito(r == EACCES, m);
    r = apri("prova.mario", 0, &e);
    esito(r == 0, "root si aggancia anche a quella di 1000");
    if (r == 0) shm_chiudi((void *)(unsigned long)e.virt);
    c = 'x';
    if (write(p3[1], &c, 1) != 1) return 1;
    waitpid(pid, &st, 0);

    printf("shmperm: %d no\n", no);
    return no ? 1 : 0;
}
