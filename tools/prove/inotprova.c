/* =============================================================================
 * tools/prove/inotprova.c <ext2 dir> <fat dir> - inotify (kernel 0.373)
 *
 * Every step prints "INO <step> OK" or "INO <step> NO" with the events
 * that came. On the ext2 directory: create, write, close, chmod, rename
 * (cookie, the watch follows), mkdir/rmdir, unlink (DELETE_SELF and
 * IGNORED), FIONREAD and poll, a read too short (EINVAL), a blocking read
 * woken by another process, rm_watch, the errors; on the FAT one: create
 * and delete.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>

#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

typedef struct { int wd; unsigned mask, cookie; char nome[64]; } Ev;
static Ev  ev[256];
static int nev;
static int fd;

static long ms_ora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

/* everything waiting, without blocking */
static void prendi(void)
{
    char buf[4096];
    int  n, i;

    nev = 0;
    for (;;) {
        n = (int)read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (i = 0; i < n && nev < 256; ) {
            struct inotify_event *e = (struct inotify_event *)(buf + i);
            ev[nev].wd = e->wd; ev[nev].mask = e->mask; ev[nev].cookie = e->cookie;
            snprintf(ev[nev].nome, sizeof(ev[nev].nome), "%s", e->len ? e->name : "");
            nev++;
            i += (int)sizeof(*e) + (int)e->len;
        }
    }
}

static void elenco(void)
{
    int i;
    for (i = 0; i < nev; i++)
        printf("INO     ev wd=%d mask=%#x cookie=%u nome=%s\n", ev[i].wd, ev[i].mask, ev[i].cookie, ev[i].nome);
}

/* is (wd, mask, nome) among the events from position *da on? */
static int c_e(int *da, int wd, unsigned mask, const char *nome)
{
    int i;
    for (i = *da; i < nev; i++)
        if (ev[i].wd == wd && ev[i].mask == mask && strcmp(ev[i].nome, nome) == 0) { *da = i + 1; return i; }
    return -1;
}

static void esito(const char *passo, int ok)
{
    printf("INO %s %s\n", passo, ok ? "OK" : "NO");
    if (!ok) elenco();
}

static void scrivi(const char *p, const char *testo, int fl)
{
    int f = open(p, fl, 0644);
    if (f >= 0) { write(f, testo, strlen(testo)); close(f); }
}

int main(int argc, char **argv)
{
    char d[256], a[256], b[256], sub[256], c[256], fa[256];
    int  wd, wf, wfat, da, i, j, n, r, e;
    struct pollfd pf;
    char  piccolo[8];
    pid_t figlio;
    long  t0;

    if (argc < 3) return 1;
    snprintf(d, sizeof d, "%s/d", argv[1]);
    snprintf(a, sizeof a, "%s/a", d);
    snprintf(b, sizeof b, "%s/b", d);
    snprintf(c, sizeof c, "%s/c", d);
    snprintf(sub, sizeof sub, "%s/sub", d);
    snprintf(fa, sizeof fa, "%s/f.txt", argv[2]);
    mkdir(d, 0755);

    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    esito("init", fd >= 0);
    r = (int)read(fd, piccolo, sizeof piccolo); e = errno;
    esito("vuota-eagain", r < 0 && e == EAGAIN);

    r = inotify_add_watch(fd, "/non/esiste", IN_ALL_EVENTS); e = errno;
    esito("enoent", r < 0 && e == ENOENT);
    wd = inotify_add_watch(fd, d, IN_ALL_EVENTS);
    esito("guarda", wd > 0);
    r = inotify_add_watch(fd, d, IN_ALL_EVENTS);
    esito("stessa-wd", r == wd);

    scrivi(a, "ciao", O_CREAT | O_WRONLY | O_TRUNC);
    n = 0; ioctl(fd, FIONREAD, &n);
    pf.fd = fd; pf.events = POLLIN; pf.revents = 0;
    r = poll(&pf, 1, 0);
    esito("fionread-poll", n > 0 && r == 1 && (pf.revents & POLLIN));
    prendi(); da = 0;
    esito("crea", c_e(&da, wd, IN_CREATE, "a") >= 0 && c_e(&da, wd, IN_OPEN, "a") >= 0 &&
                  c_e(&da, wd, IN_MODIFY, "a") >= 0 && c_e(&da, wd, IN_CLOSE_WRITE, "a") >= 0);

    r = inotify_add_watch(fd, a, IN_ONLYDIR | IN_ALL_EVENTS); e = errno;
    esito("onlydir-enotdir", r < 0 && e == ENOTDIR);
    wf = inotify_add_watch(fd, a, IN_MODIFY | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF);
    esito("guarda-file", wf > 0 && wf != wd);

    chmod(a, 0600);
    prendi(); da = 0; i = c_e(&da, wd, IN_ATTRIB, "a"); da = 0; j = c_e(&da, wf, IN_ATTRIB, "");
    esito("attrib", i >= 0 && j >= 0);

    rename(a, b);
    prendi(); da = 0; i = c_e(&da, wd, IN_MOVED_FROM, "a"); j = c_e(&da, wd, IN_MOVED_TO, "b");
    da = 0;
    esito("rinomina", i >= 0 && j >= 0 && ev[i].cookie != 0 && ev[i].cookie == ev[j].cookie &&
                      c_e(&da, wf, IN_MOVE_SELF, "") >= 0);

    scrivi(b, "altro", O_WRONLY | O_APPEND);
    prendi(); da = 0;
    esito("segue-il-nome", c_e(&da, wf, IN_MODIFY, "") >= 0 && (da = 0, c_e(&da, wd, IN_MODIFY, "b") >= 0));

    mkdir(sub, 0755); rmdir(sub);
    prendi(); da = 0;
    esito("cartella", c_e(&da, wd, IN_CREATE | IN_ISDIR, "sub") >= 0 && c_e(&da, wd, IN_DELETE | IN_ISDIR, "sub") >= 0);

    unlink(b);
    prendi(); da = 0; i = c_e(&da, wd, IN_DELETE, "b"); da = 0;
    esito("cancella", i >= 0 && c_e(&da, wf, IN_DELETE_SELF, "") >= 0 && c_e(&da, wf, IN_IGNORED, "") >= 0);

    scrivi(a, "x", O_CREAT | O_WRONLY);
    r = (int)read(fd, piccolo, sizeof piccolo); e = errno;
    esito("corta-einval", r < 0 && e == EINVAL);
    prendi();

    /* blocking: another process makes c after 300 ms */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    figlio = fork();
    if (figlio == 0) { usleep(300000); scrivi(c, "c", O_CREAT | O_WRONLY); _exit(0); }
    t0 = ms_ora();
    {
        char buf[512];
        struct inotify_event *x = (struct inotify_event *)buf;
        r = (int)read(fd, buf, sizeof buf);
        t0 = ms_ora() - t0;
        printf("INO     bloccante r=%d ms=%ld\n", r, t0);
        esito("bloccante", r > 0 && x->wd == wd && x->mask == IN_CREATE && strcmp(x->name, "c") == 0 &&
                           t0 >= 250 && t0 <= 2000);
    }
    waitpid(figlio, NULL, 0);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    prendi();

    /* poll that waits: another process again */
    figlio = fork();
    if (figlio == 0) { usleep(200000); unlink(c); _exit(0); }
    t0 = ms_ora();
    pf.revents = 0;
    r = poll(&pf, 1, 3000);
    t0 = ms_ora() - t0;
    printf("INO     poll r=%d ms=%ld\n", r, t0);
    esito("poll-attende", r == 1 && t0 >= 150 && t0 <= 2000);
    waitpid(figlio, NULL, 0);
    prendi(); da = 0;
    esito("poll-evento", c_e(&da, wd, IN_DELETE, "c") >= 0);

    r = inotify_rm_watch(fd, wd);
    prendi(); da = 0;
    esito("togli", r == 0 && c_e(&da, wd, IN_IGNORED, "") >= 0);
    r = inotify_rm_watch(fd, wd); e = errno;
    esito("togli-einval", r < 0 && e == EINVAL);

    wfat = inotify_add_watch(fd, argv[2], IN_CREATE | IN_DELETE | IN_MODIFY);
    scrivi(fa, "fat", O_CREAT | O_WRONLY);
    unlink(fa);
    prendi(); da = 0;
    esito("fat", wfat > 0 && c_e(&da, wfat, IN_CREATE, "f.txt") >= 0 &&
                 c_e(&da, wfat, IN_MODIFY, "f.txt") >= 0 && c_e(&da, wfat, IN_DELETE, "f.txt") >= 0);

    close(fd);
    unlink(a);
    printf("INO FINE\n");
    return 0;
}
