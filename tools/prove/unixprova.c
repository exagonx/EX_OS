/* =============================================================================
 * tools/prove/unixprova.c - socketpair(AF_UNIX, SOCK_STREAM) (kernel 0.358)
 *
 *     unixprova
 *
 * What Firefox with more processes asks of a unix pair (voce 115): bytes both
 * ways, send/recv with MSG_DONTWAIT, SOCK_NONBLOCK, poll that wakes, a child
 * from fork talking to its parent (more than the pipes hold, so the writes are
 * partial and the waits real), dup, the end of data and EPIPE when the other
 * end is gone, fstat saying S_IFSOCK (and S_IFIFO for a pipe).
 * And SCM_RIGHTS: descriptors that travel with the bytes, in the same process
 * and from a child, coming out with the byte they were sent with; closed by
 * the kernel when nobody can receive them, or when read() takes the bytes;
 * MSG_CTRUNC when the receiver has room for fewer.
 * And the memory of shm_open(SHM_ANON) and memfd_create, a kernel descriptor
 * since 0.358 (FD_SHM): ftruncate, fstat, mmap MAP_SHARED, the same memory in
 * a child from fork and in one that got it by SCM_RIGHTS, a mapping that
 * outlives close, growing after a mapping.
 * And its frozen copy (0.366): open("/proc/self/fd/N", O_RDONLY) is the same
 * memory read-only, as Gecko makes it on Linux; it never maps or mprotects
 * for writing, never grows, never opens for writing again, and a child that
 * got only it and writes there dies of SIGSEGV.
 *
 * Prints one line per check that fails and the count at the end:
 *     unixprova: N prove, M fallite
 * Exit: 0 if nothing failed.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>

static int g_prove, g_fallite;

static void prova(int ok, const char *cosa)
{
    g_prove++;
    if (!ok) { g_fallite++; printf("  NO: %s (errno %d)\n", cosa, errno); }
}

#define TANTI (100 * 1024)               /* more than the 4 KB of a pipe */

static unsigned char byte_di(unsigned int i) { return (unsigned char)(i * 7u + (i >> 9)); }

static int scrivi_tutto(int fd, const unsigned char *b, size_t n)
{
    size_t o = 0;
    while (o < n) {
        ssize_t r = write(fd, b + o, n - o);
        if (r <= 0) return -1;
        o += (size_t)r;
    }
    return 0;
}

static int leggi_tutto(int fd, unsigned char *b, size_t n)
{
    size_t o = 0;
    while (o < n) {
        ssize_t r = read(fd, b + o, n - o);
        if (r <= 0) return -1;
        o += (size_t)r;
    }
    return 0;
}

/* sendmsg with n descriptors and one byte */
static int manda_fd(int s, char byte, const int *fds, int n)
{
    struct msghdr   m;
    struct iovec    v;
    char            ctl[CMSG_SPACE(4 * sizeof(int))];
    struct cmsghdr *c;

    memset(&m, 0, sizeof(m));
    memset(ctl, 0, sizeof(ctl));
    v.iov_base = &byte; v.iov_len = 1;
    m.msg_iov = &v; m.msg_iovlen = 1;
    m.msg_control = ctl; m.msg_controllen = CMSG_SPACE(n * sizeof(int));
    c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(n * sizeof(int));
    memcpy(CMSG_DATA(c), fds, n * sizeof(int));
    return (int)sendmsg(s, &m, 0);
}

/* recvmsg with room for `posto` descriptors: bytes read, the descriptors in
 * fds, how many in *n, MSG_CTRUNC in *tronc */
static int ricevi_fd(int s, char *buf, int lung, int *fds, int posto, int *n, int *tronc)
{
    struct msghdr   m;
    struct iovec    v;
    char            ctl[CMSG_SPACE(4 * sizeof(int))];
    struct cmsghdr *c;
    int             r;

    memset(&m, 0, sizeof(m));
    v.iov_base = buf; v.iov_len = (size_t)lung;
    m.msg_iov = &v; m.msg_iovlen = 1;
    /* ! CMSG_LEN, NOT CMSG_SPACE: at 64 bit the header is 16 bytes and
     * CMSG_SPACE(4) leaves room for two descriptors - on Linux too */
    m.msg_control = ctl; m.msg_controllen = CMSG_LEN(posto * sizeof(int));
    *n = 0;
    r = (int)recvmsg(s, &m, 0);
    *tronc = (m.msg_flags & MSG_CTRUNC) != 0;
    for (c = CMSG_FIRSTHDR(&m); r >= 0 && c; c = CMSG_NXTHDR(&m, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            *n = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            memcpy(fds, CMSG_DATA(c), (size_t)*n * sizeof(int));
        }
    return r;
}

static int g_sveglia_fd;
static void *scrittore_tardo(void *x)
{
    (void)x;
    usleep(300 * 1000);
    (void)write(g_sveglia_fd, "z", 1);
    return NULL;
}

int main(void)
{
    int           sv[2], pp[2], d, st;
    char          c[16];
    struct stat   s;
    struct pollfd pf;
    pid_t         figlio;
    unsigned char *buf;
    unsigned int  i;

    signal(SIGPIPE, SIG_IGN);
    alarm(120);     /* a check that hangs must not eat the commands after it */
    buf = malloc(TANTI);

    /* 1. two ends, both ways, read/write and send/recv */
    prova(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
    prova(sv[0] > 2 && sv[1] > 2 && sv[0] != sv[1], "due numeri nuovi");
    prova(fstat(sv[0], &s) == 0 && S_ISSOCK(s.st_mode), "fstat: S_IFSOCK");
    prova(write(sv[0], "ciao", 4) == 4, "scrivo in A");
    prova(read(sv[1], c, sizeof(c)) == 4 && memcmp(c, "ciao", 4) == 0, "B legge cio' che A ha scritto");
    prova(send(sv[1], "ehi", 3, MSG_NOSIGNAL) == 3, "send da B");
    prova(recv(sv[0], c, sizeof(c), 0) == 3 && memcmp(c, "ehi", 3) == 0, "recv in A");

    /* 2. nothing to read: MSG_DONTWAIT does not wait on a blocking end */
    errno = 0;
    prova(recv(sv[0], c, 1, MSG_DONTWAIT) == -1 && errno == EAGAIN, "recv MSG_DONTWAIT su vuoto: EAGAIN");

    /* 3. poll: room to write at once; to read only when there is something,
     *    and a poll asleep wakes when another thread writes */
    pf.fd = sv[0]; pf.events = POLLIN | POLLOUT; pf.revents = 0;
    prova(poll(&pf, 1, 0) == 1 && (pf.revents & POLLOUT) && !(pf.revents & POLLIN), "poll: POLLOUT, non POLLIN");
    {
        pthread_t t;

        g_sveglia_fd = sv[1];
        pthread_create(&t, NULL, scrittore_tardo, NULL);
        pf.fd = sv[0]; pf.events = POLLIN; pf.revents = 0;
        prova(poll(&pf, 1, 5000) == 1 && (pf.revents & POLLIN), "poll si sveglia quando l'altro filo scrive");
        pthread_join(t, NULL);
        prova(read(sv[0], c, 1) == 1 && c[0] == 'z', "e il byte e' quello");
    }

    /* 4. fork: the child sends TANTI bytes and gets them back from the parent */
    figlio = fork();
    if (figlio == 0) {
        close(sv[0]);
        for (i = 0; i < TANTI; i++) buf[i] = byte_di(i);
        if (scrivi_tutto(sv[1], buf, TANTI) != 0) _exit(2);
        memset(buf, 0, TANTI);
        if (leggi_tutto(sv[1], buf, TANTI) != 0) _exit(3);
        for (i = 0; i < TANTI; i++) if (buf[i] != byte_di(i)) _exit(4);
        _exit(0);
    }
    prova(figlio > 0, "fork");
    close(sv[1]);
    prova(leggi_tutto(sv[0], buf, TANTI) == 0, "il padre riceve 100 KB dal figlio");
    for (i = 0; i < TANTI && buf[i] == byte_di(i); i++) ;
    prova(i == TANTI, "i 100 KB sono giusti");
    prova(scrivi_tutto(sv[0], buf, TANTI) == 0, "il padre li rimanda");
    prova(waitpid(figlio, &st, 0) == figlio && WIFEXITED(st) && WEXITSTATUS(st) == 0, "il figlio li ha riavuti giusti");

    /* 5. the child is gone: end of data, POLLHUP, EPIPE */
    pf.fd = sv[0]; pf.events = POLLIN; pf.revents = 0;
    prova(poll(&pf, 1, 1000) == 1 && (pf.revents & POLLHUP), "poll: POLLHUP quando l'altro capo non c'e' piu'");
    prova(read(sv[0], c, 1) == 0, "read: fine dei dati");
    errno = 0;
    prova(write(sv[0], "x", 1) == -1 && errno == EPIPE, "write: EPIPE");
    close(sv[0]);

    /* 6. dup: closing one of the two copies does not end the pair */
    prova(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv) == 0, "socketpair NONBLOCK|CLOEXEC");
    prova(fcntl(sv[0], F_GETFD) == FD_CLOEXEC, "FD_CLOEXEC c'e'");
    prova((fcntl(sv[0], F_GETFL) & O_NONBLOCK) != 0, "O_NONBLOCK c'e'");
    errno = 0;
    prova(read(sv[0], c, 1) == -1 && errno == EAGAIN, "read su capo non bloccante vuoto: EAGAIN");
    d = dup(sv[1]);
    close(sv[1]);
    prova(write(d, "k", 1) == 1 && read(sv[0], c, 1) == 1 && c[0] == 'k', "la copia di dup parla ancora");
    close(d);
    prova(read(sv[0], c, 1) == 0, "chiusa anche la copia: fine dei dati");
    close(sv[0]);

    /* 7. what is not AF_UNIX/SOCK_STREAM is refused, not faked */
    errno = 0;
    prova(socketpair(AF_INET, SOCK_STREAM, 0, sv) == -1 && errno == EAFNOSUPPORT, "AF_INET: EAFNOSUPPORT");

    /* 8. a pipe is S_IFIFO for fstat (until 0.357: ENOSYS) */
    prova(pipe(pp) == 0 && fstat(pp[0], &s) == 0 && S_ISFIFO(s.st_mode), "fstat di una pipe: S_IFIFO");
    close(pp[0]); close(pp[1]);

    /* 9. SCM_RIGHTS in one process: the write end of a pipe goes over the
     *    pair, the sender closes its own, the received one still writes; and
     *    when that is closed too the pipe says end of data */
    {
        int f[4], n, tr, x, q[2];

        prova(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0 && pipe(pp) == 0, "socketpair e pipe");
        prova(manda_fd(sv[0], 'F', &pp[1], 1) == 1, "sendmsg con un descrittore");
        close(pp[1]);
        prova(ricevi_fd(sv[1], c, sizeof(c), f, 4, &n, &tr) == 1 && c[0] == 'F' && n == 1 && !tr,
              "recvmsg: il byte e un descrittore");
        x = n == 1 ? f[0] : -1;
        prova(x > 2 && x != sv[0] && x != sv[1], "un numero nuovo");
        prova(write(x, "via", 3) == 3 && read(pp[0], c, sizeof(c)) == 3 && memcmp(c, "via", 3) == 0,
              "il descrittore ricevuto scrive nella pipe");
        close(x);
        prova(read(pp[0], c, 1) == 0, "chiuso anche quello: fine dei dati");
        close(pp[0]);

        /* 10. the bytes before the descriptor come in a read of their own */
        prova(pipe(pp) == 0 && write(sv[0], "AB", 2) == 2 && manda_fd(sv[0], 'C', &pp[1], 1) == 1, "AB, poi C con un descrittore");
        close(pp[1]);
        prova(ricevi_fd(sv[1], c, sizeof(c), f, 4, &n, &tr) == 2 && n == 0, "prima AB, senza descrittori");
        prova(ricevi_fd(sv[1], c, sizeof(c), f, 4, &n, &tr) == 1 && c[0] == 'C' && n == 1, "poi C, con il descrittore");
        if (n == 1) close(f[0]);
        prova(read(pp[0], c, 1) == 0, "e la pipe ha di nuovo la fine dei dati");
        close(pp[0]);

        /* 11. read() instead of recvmsg: the descriptor is closed */
        prova(pipe(pp) == 0 && manda_fd(sv[0], 'R', &pp[1], 1) == 1, "un descrittore letto con read()");
        close(pp[1]);
        prova(read(sv[1], c, 1) == 1 && c[0] == 'R', "read() prende il byte");
        prova(read(pp[0], c, 1) == 0, "e il descrittore e' stato chiuso");
        close(pp[0]);

        /* 12. MSG_CTRUNC: two sent, room for one */
        prova(pipe(pp) == 0 && pipe(q) == 0, "due pipe");
        f[0] = pp[1]; f[1] = q[1];
        prova(manda_fd(sv[0], '2', f, 2) == 1, "sendmsg con due descrittori");
        close(pp[1]); close(q[1]);
        prova(ricevi_fd(sv[1], c, sizeof(c), f, 1, &n, &tr) == 1 && n == 1 && tr, "posto per uno: uno e MSG_CTRUNC");
        if (n == 1) close(f[0]);
        prova(read(pp[0], c, 1) == 0 && read(q[0], c, 1) == 0, "nessuno dei due resta aperto");
        close(pp[0]); close(q[0]);

        /* 13. nobody receives: the receiving end is closed with the
         *     descriptor still on its way, and the kernel closes it */
        prova(pipe(pp) == 0 && manda_fd(sv[0], 'O', &pp[1], 1) == 1, "un descrittore che nessuno ricevera'");
        close(pp[1]);
        close(sv[1]);
        prova(read(pp[0], c, 1) == 0, "chiuso dal kernel con il capo che doveva riceverlo");
        close(pp[0]);
        close(sv[0]);

        /* 14. from a child: it sends the read end of a pipe it writes into */
        prova(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair per il figlio");
        figlio = fork();
        if (figlio == 0) {
            close(sv[0]);
            if (pipe(q) != 0 || manda_fd(sv[1], 'P', &q[0], 1) != 1) _exit(2);
            close(q[0]);
            if (write(q[1], "dal figlio", 10) != 10) _exit(3);
            _exit(0);
        }
        close(sv[1]);
        prova(ricevi_fd(sv[0], c, sizeof(c), f, 4, &n, &tr) == 1 && c[0] == 'P' && n == 1, "il padre riceve un descrittore dal figlio");
        prova(waitpid(figlio, &st, 0) == figlio && WIFEXITED(st) && WEXITSTATUS(st) == 0, "il figlio e' uscito bene");
        x = n == 1 ? f[0] : -1;
        prova(read(x, c, sizeof(c)) == 10 && memcmp(c, "dal figlio", 10) == 0, "e legge cio' che il figlio ha scritto");
        prova(read(x, c, 1) == 0, "poi la fine dei dati: il figlio non c'e' piu'");
        close(x);
        close(sv[0]);
    }

    /* 15. memory that is a descriptor: size, mapping, fork */
    {
        int            m, f[4], n, tr;
        unsigned char *a, *b;
        volatile unsigned char *v;

        m = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
        prova(m > 2, "shm_open(SHM_ANON)");
        prova(fstat(m, &s) == 0 && s.st_size == 0, "fstat: 0 byte appena fatta");
        prova(ftruncate(m, 3 * 4096 + 100) == 0, "ftruncate a 3 pagine e 100 byte");
        prova(fstat(m, &s) == 0 && s.st_size == 3 * 4096 + 100, "fstat: la misura data");
        a = mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, m, 0);
        prova(a != MAP_FAILED, "mmap MAP_SHARED");
        if (a == MAP_FAILED) a = NULL;
        errno = 0;
        prova(mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, m, 0) == MAP_FAILED && errno == EINVAL,
              "MAP_PRIVATE: EINVAL");
        if (a) {
            v = a;
            v[0] = 1; v[3 * 4096 + 99] = 2;
            figlio = fork();
            if (figlio == 0) {
                unsigned char *c2 = mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, m, 0);

                if (c2 == MAP_FAILED || c2[0] != 1 || c2[3 * 4096 + 99] != 2) _exit(2);
                c2[1] = 11;             /* through its own mapping */
                v[2] = 22;              /* through the one fork kept */
                _exit(0);
            }
            prova(waitpid(figlio, &st, 0) == figlio && WIFEXITED(st) && WEXITSTATUS(st) == 0,
                  "il figlio di fork mappa la stessa memoria");
            prova(v[1] == 11 && v[2] == 22, "e il padre vede cio' che il figlio ha scritto");

            /* 16. by SCM_RIGHTS to a child that has no copy of it */
            prova(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair per la memoria");
            figlio = fork();
            if (figlio == 0) {
                unsigned char *c2;

                close(sv[0]);
                close(m);
                if (ricevi_fd(sv[1], (char *)c, sizeof(c), f, 4, &n, &tr) != 1 || n != 1) _exit(2);
                if (fstat(f[0], &s) != 0 || s.st_size != 3 * 4096 + 100) _exit(3);
                c2 = mmap(NULL, 4 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, f[0], 0);
                if (c2 == MAP_FAILED || c2[1] != 11) _exit(4);
                close(f[0]);            /* the mapping stays */
                c2[3 * 4096] = 33;
                _exit(0);
            }
            close(sv[1]);
            prova(manda_fd(sv[0], 'M', &m, 1) == 1, "la memoria va al figlio con sendmsg");
            prova(waitpid(figlio, &st, 0) == figlio && WIFEXITED(st) && WEXITSTATUS(st) == 0,
                  "il figlio la riceve, la misura e la mappa");
            prova(v[3 * 4096] == 33, "e scrive nella stessa memoria");
            close(sv[0]);

            /* 17. bigger after a mapping: the first pages are the same */
            prova(ftruncate(m, 8 * 4096) == 0, "ftruncate piu' grande dopo la mappatura");
            b = mmap(NULL, 8 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED, m, 0);
            prova(b != MAP_FAILED && b[1] == 11 && b[7 * 4096] == 0, "le pagine di prima ci sono, le nuove a zero");
            if (b != MAP_FAILED) { b[0] = 44; prova(v[0] == 44, "due mappature, la stessa memoria"); munmap(b, 8 * 4096); }

            /* 18. close the descriptor: the mapping lives on */
            close(m);
            v[5] = 55;
            prova(v[5] == 55 && v[1] == 11, "chiuso il descrittore la mappatura resta");
            prova(munmap(a, 4 * 4096) == 0, "munmap");
        }

        /* 19. memfd_create */
        m = memfd_create("prova", MFD_CLOEXEC);
        prova(m > 2 && fcntl(m, F_GETFD) == FD_CLOEXEC, "memfd_create con MFD_CLOEXEC");
        prova(ftruncate(m, 4096) == 0 && (a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, m, 0)) != MAP_FAILED,
              "memfd: ftruncate e mmap");
        if (m > 2) close(m);
    }

    /* 20. the frozen copy: /proc/self/fd/N read-only (0.366) */
    {
        int            w, r, f[4], n, tr;
        unsigned char *aw = NULL, *ar = NULL;
        char           nome[40];

        w = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
        prova(w > 2 && ftruncate(w, 4096) == 0, "memoria da congelare");
        if (w > 2) aw = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, w, 0);
        if (aw == MAP_FAILED) aw = NULL;
        if (aw) aw[0] = 7;
        snprintf(nome, sizeof(nome), "/proc/self/fd/%d", w);
        r = open(nome, O_RDONLY | O_CLOEXEC);
        prova(r > 2, "open di /proc/self/fd/N in sola lettura");
        prova(r > 2 && fcntl(r, F_GETFD) == FD_CLOEXEC, "con O_CLOEXEC");
        if (r > 2) ar = mmap(NULL, 4096, PROT_READ, MAP_SHARED, r, 0);
        if (ar == MAP_FAILED) ar = NULL;
        prova(ar && aw && ar[0] == 7, "la copia si mappa in lettura: la stessa memoria");
        if (ar && aw) { aw[1] = 8; prova(ar[1] == 8, "e vede cio' che scrive l'originale"); }
        errno = 0;
        prova(r > 2 && mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, r, 0) == MAP_FAILED &&
              errno == EACCES, "la copia mappata in scrittura: EACCES");
        errno = 0;
        prova(ar && mprotect(ar, 4096, PROT_READ | PROT_WRITE) == -1 && errno == EACCES,
              "mprotect in scrittura della copia: EACCES");
        prova(r > 2 && ftruncate(r, 8192) == -1, "la copia non cambia misura");
        snprintf(nome, sizeof(nome), "/proc/self/fd/%d", r);
        errno = 0;
        prova(open(nome, O_RDWR) == -1 && errno == EACCES, "dalla copia la scrittura non torna");

        /* a child that has only the copy, by SCM_RIGHTS, and writes */
        if (r > 2 && aw && socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            figlio = fork();
            if (figlio == 0) {
                volatile unsigned char *c2;

                close(sv[0]);
                close(w);
                close(r);
                munmap(aw, 4096);
                if (ricevi_fd(sv[1], (char *)c, sizeof(c), f, 4, &n, &tr) != 1 || n != 1) _exit(2);
                c2 = mmap(NULL, 4096, PROT_READ, MAP_SHARED, f[0], 0);
                if (c2 == MAP_FAILED || c2[0] != 7) _exit(3);
                if (mprotect((void *)c2, 4096, PROT_READ | PROT_WRITE) == 0) _exit(4);
                c2[0] = 99;             /* must not get through */
                _exit(5);
            }
            close(sv[1]);
            prova(manda_fd(sv[0], 'R', &r, 1) == 1, "la copia va a un figlio");
            prova(waitpid(figlio, &st, 0) == figlio && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV,
                  "il figlio che scrive nella copia muore di SIGSEGV");
            prova(aw[0] == 7, "e la memoria non e' cambiata");
            close(sv[0]);
        }
        if (aw) munmap(aw, 4096);
        if (r > 2) close(r);
        if (w > 2) close(w);
    }

    printf("unixprova: %d prove, %d fallite\n", g_prove, g_fallite);
    return g_fallite ? 1 : 0;
}
