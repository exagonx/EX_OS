/* =============================================================================
 * tools/prove/eintrprova.c - a caught signal cuts a wait on a socket short
 * (voce 37: kernel 0.346 SYS_IPC_RECV_SEG, ip.drv 0.026 IP_MSG_RITIRA)
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Run by tools/prova_eintr.sh. Prints [OK]/[NO] lines and "eintrprova: N NO".
 *   a  poll on a listener, alarm(1) without SA_RESTART: -1 EINTR, ~1 s
 *   b  the same with SA_RESTART: poll is never restarted, EINTR again
 *   c  epoll_wait on the listener, alarm(1): -1 EINTR
 *   d  nothing stale after a/b/c: a client connects after 300 ms, poll
 *      with 5 s returns 1 at once, accept takes it
 *   e  recv with SO_RCVTIMEO 5 s, alarm(1) without SA_RESTART: -1 EINTR
 *   f  recv with SO_RCVTIMEO 5 s and SA_RESTART: the alarm at 0.5 s, the
 *      data at 1.5 s: recv returns the data
 *   g  pthread_kill on a thread in poll (Apache's listener): EINTR there
 *   h  a poll with no signal still waits its timeout (500 ms)
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <netinet/in.h>

#define PORTA 5137

static int g_no = 0;
static volatile int g_presi = 0;

static void esito(int ok, const char *t)
{
    printf("  [%s] %s\n", ok ? "OK" : "NO", t);
    if (!ok) g_no++;
}

static unsigned int ms_ora(void)
{
    struct timeval t;

    gettimeofday(&t, NULL);
    return (unsigned int)(t.tv_sec * 1000u + t.tv_usec / 1000u);
}

static void gestore(int s) { (void)s; g_presi++; }

static void azione(int sig, int riavvia)
{
    struct sigaction a;

    memset(&a, 0, sizeof a);
    a.sa_handler = gestore;
    a.sa_flags   = riavvia ? SA_RESTART : 0;
    sigemptyset(&a.sa_mask);
    sigaction(sig, &a, NULL);
}

static void allarme_ms(unsigned int ms)
{
    struct itimerval it;

    memset(&it, 0, sizeof it);
    it.it_value.tv_sec  = ms / 1000u;
    it.it_value.tv_usec = (ms % 1000u) * 1000u;
    setitimer(ITIMER_REAL, &it, NULL);
}

static struct sockaddr_in indirizzo(void)
{
    struct sockaddr_in a;

    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_port        = htons(PORTA);
    a.sin_addr.s_addr = htonl(0x7F000001);
    return a;
}

static void *cliente_dopo(void *arg)
{
    struct sockaddr_in a = indirizzo();
    int s = socket(AF_INET, SOCK_STREAM, 0);

    usleep((unsigned int)(long)arg * 1000u);
    if (s >= 0 && connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
        usleep(1500000);
        write(s, "X", 1);
        usleep(500000);
    }
    if (s >= 0) close(s);
    return NULL;
}

static int g_asc;
static volatile int g_g_r = 0, g_g_e = 0;

static void *in_poll(void *arg)
{
    struct pollfd p;

    (void)arg;
    p.fd = g_asc; p.events = POLLIN; p.revents = 0;
    g_g_r = poll(&p, 1, 8000);
    g_g_e = errno;
    return NULL;
}

int main(void)
{
    struct sockaddr_in a = indirizzo();
    struct pollfd p;
    struct epoll_event ev, fuori[4];
    pthread_t t;
    char  m[200], c = 0;
    int   r, e, ep, s, uno = 1;
    unsigned int t0, dt;
    struct timeval rt;

    g_asc = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(g_asc, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
    if (g_asc < 0 || bind(g_asc, (struct sockaddr *)&a, sizeof a) != 0 || listen(g_asc, 4) != 0) {
        printf("eintrprova: socket/bind/listen: errno %d (ip.drv?)\neintrprova: 1 NO\n", errno);
        return 1;
    }

    /* a */
    azione(SIGALRM, 0); g_presi = 0;
    p.fd = g_asc; p.events = POLLIN; p.revents = 0;
    t0 = ms_ora(); alarm(1);
    r = poll(&p, 1, 8000); e = errno; dt = ms_ora() - t0;
    snprintf(m, sizeof m, "a poll interrotto da alarm(1): %d errno %d, %u ms, gestore %d", r, e, dt, g_presi);
    esito(r == -1 && e == EINTR && g_presi == 1 && dt < 3000, m);

    /* b */
    azione(SIGALRM, 1); g_presi = 0;
    t0 = ms_ora(); allarme_ms(500);
    r = poll(&p, 1, 8000); e = errno; dt = ms_ora() - t0;
    snprintf(m, sizeof m, "b con SA_RESTART poll non riparte: %d errno %d, %u ms, gestore %d", r, e, dt, g_presi);
    esito(r == -1 && e == EINTR && g_presi == 1 && dt < 3000, m);

    /* c */
    azione(SIGALRM, 0); g_presi = 0;
    ep = epoll_create1(0);
    memset(&ev, 0, sizeof ev); ev.events = EPOLLIN; ev.data.fd = g_asc;
    epoll_ctl(ep, EPOLL_CTL_ADD, g_asc, &ev);
    t0 = ms_ora(); allarme_ms(500);
    r = epoll_wait(ep, fuori, 4, 8000); e = errno; dt = ms_ora() - t0;
    snprintf(m, sizeof m, "c epoll_wait interrotto: %d errno %d, %u ms, gestore %d", r, e, dt, g_presi);
    esito(r == -1 && e == EINTR && g_presi == 1 && dt < 3000, m);
    close(ep);

    /* d */
    pthread_create(&t, NULL, cliente_dopo, (void *)300L);
    t0 = ms_ora();
    r = poll(&p, 1, 5000); dt = ms_ora() - t0;
    s = r == 1 ? accept(g_asc, NULL, NULL) : -1;
    snprintf(m, sizeof m, "d niente di vecchio nella casella: poll %d (revents 0x%x) in %u ms, accept %d",
             r, p.revents, dt, s);
    esito(r == 1 && (p.revents & POLLIN) && dt < 2000 && s >= 0, m);

    /* e: the client writes 1.8 s after connecting */
    azione(SIGALRM, 0); g_presi = 0;
    rt.tv_sec = 5; rt.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
    t0 = ms_ora(); allarme_ms(400);
    r = (int)recv(s, &c, 1, 0); e = errno; dt = ms_ora() - t0;
    snprintf(m, sizeof m, "e recv con SO_RCVTIMEO interrotta: %d errno %d, %u ms, gestore %d", r, e, dt, g_presi);
    esito(r == -1 && e == EINTR && g_presi == 1 && dt < 1200, m);

    /* f */
    azione(SIGALRM, 1); g_presi = 0;
    allarme_ms(300);
    c = 0; t0 = ms_ora();
    r = (int)recv(s, &c, 1, 0); dt = ms_ora() - t0;
    snprintf(m, sizeof m, "f con SA_RESTART recv riparte e prende '%c' (%d) dopo %u ms, gestore %d",
             c ? c : '-', r, dt, g_presi);
    esito(r == 1 && c == 'X' && g_presi == 1, m);
    pthread_join(t, NULL);
    close(s);

    /* g */
    azione(SIGUSR2, 1);
    g_presi = 0;
    pthread_create(&t, NULL, in_poll, NULL);
    usleep(400000);
    t0 = ms_ora();
    pthread_kill(t, SIGUSR2);
    pthread_join(t, NULL); dt = ms_ora() - t0;
    snprintf(m, sizeof m, "g pthread_kill su un filo in poll: %d errno %d, %u ms, gestore %d",
             g_g_r, g_g_e, dt, g_presi);
    esito(g_g_r == -1 && g_g_e == EINTR && g_presi == 1 && dt < 2000, m);

    /* h */
    p.revents = 0; t0 = ms_ora();
    r = poll(&p, 1, 500); dt = ms_ora() - t0;
    snprintf(m, sizeof m, "h senza segnali poll aspetta la scadenza: %d in %u ms", r, dt);
    esito(r == 0 && dt >= 450 && dt < 1500, m);

    close(g_asc);
    printf("eintrprova: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
