/* =============================================================================
 * tools/prove/suprova.c -- the brake of SYS_SU (kernel 0.367, debug/bug_SU001)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Run as root on a disk from tools/mkhd.sh (root/root). It goes down to
 * other uids by itself:
 *   uid 1000  three wrong passwords: EPERM, each one costing ~1 s;
 *             the 4th at once: EAGAIN, fast, without looking at it;
 *             a forked child of the same uid: EAGAIN too (per uid, not pid);
 *             a name that does not exist: EAGAIN as well (no oracle);
 *             after the hold-off (1 s) the right password: root.
 *   uid 1001  while 1000 is held off: the right password works at once.
 *   uid 1002  four processes at the same moment with the right password:
 *             all four root (the static buffer refused some, before 0.367).
 * Until 0.367 the wrong passwords answered in a few ms, forever.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>

int diventa_root(const char *nome, const char *password);

static int ok, no;
static void si_(const char *m) { printf("  [OK] %s\n", m); ok++; }
static void no_(const char *m) { printf("  [NO] %s\n", m); no++; }
#define PROVA(c, m) ((c) ? si_(m) : no_(m))

static long ms(void)
{
    struct timeval t;

    gettimeofday(&t, NULL);
    return t.tv_sec * 1000L + t.tv_usec / 1000L;
}

/* one call, its answer (0 or -errno, as the kernel gives it) and its cost */
static int prova(const char *nome, const char *pw, long *quanto)
{
    long t0 = ms();
    /* the kernel's answer as it is: 0 or -errno (-1 IS -EPERM, errno untouched) */
    int  r  = diventa_root(nome, pw);

    *quanto = ms() - t0;
    return r;
}

/* a child that becomes `uid` and returns what f says (0 = all good) */
static int come(unsigned uid, int (*f)(void))
{
    int st, pid = fork();

    if (pid == 0) {
        if (setgid(uid) != 0 || setuid(uid) != 0) _exit(99);
        _exit(f());
    }
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return 98;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 97;
}

static int figlio_eagain(void)
{
    long q;

    return prova("root", "nonquesta", &q) == -EAGAIN ? 0 : 1;
}

static int uid1000(void)
{
    long q, t;
    int  i, r, st, pid;
    char m[128];

    for (i = 1; i <= 3; i++) {
        r = prova("root", "sbagliata", &q);
        snprintf(m, sizeof(m), "uid 1000, errore %d: EPERM e costa ~1 s (%d, %ld ms)", i, r, q);
        PROVA(r == -EPERM && q >= 800, m);
    }
    r = prova("root", "sbagliata", &q);
    snprintf(m, sizeof(m), "uid 1000, il 4o subito: EAGAIN senza guardare (%d, %ld ms)", r, q);
    PROVA(r == -EAGAIN && q < 300, m);
    r = prova("nessuno", "x", &t);
    snprintf(m, sizeof(m), "uid 1000, un nome che non c'e': EAGAIN anche lui (%d)", r);
    PROVA(r == -EAGAIN, m);
    pid = fork();
    if (pid == 0) _exit(figlio_eagain());
    waitpid(pid, &st, 0);
    PROVA(WIFEXITED(st) && WEXITSTATUS(st) == 0, "uid 1000, un figlio con fork: EAGAIN anche lui (per uid)");
    fflush(stdout);

    /* uid 1001 meanwhile: someone else's brake is not mine */
    usleep(1300000);   /* the hold-off after the 3rd failure is 1 s */
    r = prova("root", "root", &q);
    snprintf(m, sizeof(m), "uid 1000, dopo l'attesa la password giusta: root (%d, uid %d)", r, (int)getuid());
    PROVA(r == 0 && getuid() == 0, m);
    return no ? 1 : 0;
}

static int uid1001(void)
{
    long q;
    int  r = prova("root", "root", &q);

    return (r == 0 && getuid() == 0 && q < 800) ? 0 : 1;
}

static int uid1002(void)
{
    int i, st, pid[4], tutti = 0;

    for (i = 0; i < 4; i++) {
        pid[i] = fork();
        if (pid[i] == 0) {
            long q;
            _exit(prova("root", "root", &q) == 0 && getuid() == 0 ? 0 : 1);
        }
    }
    for (i = 0; i < 4; i++)
        if (pid[i] > 0 && waitpid(pid[i], &st, 0) == pid[i] && WIFEXITED(st) && WEXITSTATUS(st) == 0)
            tutti++;
    return 4 - tutti;
}

int main(void)
{
    int st, pid, r;
    char m[96];

    if (getuid() != 0) { printf("suprova: da root\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);

    pid = fork();
    if (pid == 0) {
        if (setgid(1000) != 0 || setuid(1000) != 0) _exit(99);
        _exit(uid1000());
    }
    /* while 1000 sits in its hold-off, 1001 is not touched */
    sleep(4);
    r = come(1001, uid1001);
    PROVA(r == 0, "uid 1001 mentre 1000 e' frenato: la password giusta va subito");
    waitpid(pid, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) no++;
    r = come(1002, uid1002);
    snprintf(m, sizeof(m), "uid 1002, quattro processi insieme: tutti root (%d no)", r);
    PROVA(r == 0, m);

    printf("suprova: %d no\n", no);
    return no ? 1 : 0;
}
