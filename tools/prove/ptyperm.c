/* =============================================================================
 * tools/prove/ptyperm.c -- /dev/pts/N has an owner (kernel 0.367,
 * debug/bug_PTY001)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Run as root. root makes a pair (posix_openpt), then:
 *   stat says root's, crw--w---- (0620), as Linux devpts;
 *   uid 1000 cannot open it, neither O_RDWR nor O_RDONLY (EACCES);
 *   root gives it to 1000 with fchown/fchmod: now 1000 opens it, and
 *   stat says 1000; 1001 still cannot; 1000 may fchmod its own terminal
 *   but not fchown it (EPERM);
 *   a slave opened O_WRONLY does not read (EBADF);
 *   uid 1000 makes a pair of its own and opens its slave by name.
 * Until 0.367 every user opened every /dev/pts/N, root's telnet included.
 * ============================================================================= */
#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int no;
static void esito(int c, const char *m) { printf("  [%s] %s\n", c ? "OK" : "NO", m); if (!c) no++; }

/* child as uid: open(nome, modo), 0 if it opened, else errno */
static int apre_come(unsigned uid, const char *nome, int modo)
{
    int st, pid = fork();

    if (pid == 0) {
        int fd;

        if (setgid(uid) != 0 || setuid(uid) != 0) _exit(250);
        fd = open(nome, modo | O_NOCTTY);
        if (fd >= 0) { close(fd); _exit(0); }
        _exit(errno & 0xFF);
    }
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return 251;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 252;
}

static int figlio_proprio(void)
{
    int m = posix_openpt(O_RDWR | O_NOCTTY), s, r = 0;
    char *n;
    struct stat st;

    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0 || (n = ptsname(m)) == NULL) return 1;
    s = open(n, O_RDWR | O_NOCTTY);
    if (s < 0) r |= 2;
    if (stat(n, &st) != 0 || st.st_uid != getuid()) r |= 4;
    if (s >= 0 && fchmod(s, 0600) != 0) r |= 8;
    if (s >= 0 && fchown(s, 0, 0) == 0) r |= 16;      /* chown is root's */
    return r;
}

int main(void)
{
    int m, s, w, st, r, pid;
    char nome[64], b[8], msg[160];
    struct stat sb;

    if (getuid() != 0) { printf("ptyperm: da root\n"); return 2; }
    m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0 || ptsname(m) == NULL) {
        printf("ptyperm: posix_openpt non va (%s)\n", strerror(errno));
        return 1;
    }
    snprintf(nome, sizeof(nome), "%s", ptsname(m));
    s = open(nome, O_RDWR | O_NOCTTY);
    esito(s >= 0, "root apre il suo slave");

    r = stat(nome, &sb);
    snprintf(msg, sizeof(msg), "stat %s: root, crw--w---- (uid %u, modo %o)", nome,
             (unsigned)sb.st_uid, (unsigned)(sb.st_mode & 07777));
    esito(r == 0 && sb.st_uid == 0 && (sb.st_mode & 0777) == 0620 && S_ISCHR(sb.st_mode), msg);

    r = apre_come(1000, nome, O_RDWR);
    snprintf(msg, sizeof(msg), "uid 1000 non apre il terminale di root, O_RDWR (errno %d)", r);
    esito(r == EACCES, msg);
    r = apre_come(1000, nome, O_RDONLY);
    snprintf(msg, sizeof(msg), "uid 1000 nemmeno O_RDONLY: i tasti di root restano suoi (errno %d)", r);
    esito(r == EACCES, msg);

    esito(fchown(s, 1000, 1000) == 0 && fchmod(s, 0620) == 0, "root lo da' a 1000 (fchown, fchmod)");
    r = stat(nome, &sb);
    esito(r == 0 && sb.st_uid == 1000 && sb.st_gid == 1000, "stat dice 1000:1000");
    r = apre_come(1000, nome, O_RDWR);
    snprintf(msg, sizeof(msg), "adesso uid 1000 lo apre (errno %d)", r);
    esito(r == 0, msg);
    r = apre_come(1001, nome, O_RDWR);
    snprintf(msg, sizeof(msg), "uid 1001 no (errno %d)", r);
    esito(r == EACCES, msg);

    w = open(nome, O_WRONLY | O_NOCTTY);
    errno = 0;
    r = w >= 0 ? (int)read(w, b, 1) : 0;
    snprintf(msg, sizeof(msg), "uno slave aperto O_WRONLY non legge (%d, errno %d)", r, errno);
    esito(w >= 0 && r < 0 && errno == EBADF, msg);
    if (w >= 0) close(w);

    pid = fork();
    if (pid == 0) {
        if (setgid(1000) != 0 || setuid(1000) != 0) _exit(250);
        _exit(figlio_proprio());
    }
    waitpid(pid, &st, 0);
    r = WIFEXITED(st) ? WEXITSTATUS(st) : 99;
    snprintf(msg, sizeof(msg), "uid 1000 fa una coppia sua: la apre per nome, e' sua, fchmod si', fchown no (%d)", r);
    esito(r == 0, msg);

    printf("ptyperm: %d no\n", no);
    return no ? 1 : 0;
}
