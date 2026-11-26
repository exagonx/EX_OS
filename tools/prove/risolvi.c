/* =============================================================================
 * tools/prove/risolvi.c - getaddrinfo as Firefox uses it (voce 132)
 *
 *     risolvi [giri] [nome ...]
 *     risolvi -fasi <dns a.b.c.d> [nome ...]
 *
 * The user, 26/11/2026: in Exilla a site often needs several tries before
 * its name resolves. Firefox resolves with getaddrinfo, from several
 * threads at once. This does the same: each name once in sequence, then all
 * of them from 8 threads together, `giri` times (default 3). Each lookup
 * prints its time and its result; at the end the failures by kind.
 *     RISOLVI <nome> <ms> OK <a.b.c.d> | NO <code> <text>
 *     RISOLVI-TOTALE <lookups> <failures> <slowest ms>
 * -fasi: the same UDP steps the libc's resolver takes (socket, sendto,
 * recvfrom, close), one thread per name all together, each step timed:
 *     FASI <nome> socket <ms> sendto <ms> recvfrom <ms> close <ms> <bytes>
 * Built with the EX-OS cross gcc; on Linux it compiles too (the same
 * questions to compare).
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

static const char *g_nomi_base[] = {
    "www.google.com", "accounts.firefox.com", "www.youtube.com", "www.wikipedia.org",
    "www.mozilla.org", "github.com", "www.repubblica.it", "www.amazon.it"
};
static const char **g_nomi;
static int g_n;
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static int g_tot, g_no, g_max;

static unsigned long ms_ora(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long)t.tv_sec * 1000ul + (unsigned long)t.tv_nsec / 1000000ul;
}

static void una(const char *nome)
{
    struct addrinfo h, *r = NULL;
    unsigned long   da = ms_ora(), ms;
    int             e;
    char            ip[32] = "?";

    memset(&h, 0, sizeof(h));
    h.ai_family   = AF_UNSPEC;          /* as Firefox asks */
    h.ai_socktype = SOCK_STREAM;
    e  = getaddrinfo(nome, "443", &h, &r);
    ms = ms_ora() - da;
    if (e == 0 && r) inet_ntop(AF_INET, &((struct sockaddr_in *)r->ai_addr)->sin_addr, ip, sizeof(ip));
    pthread_mutex_lock(&g_m);
    g_tot++;
    if ((int)ms > g_max) g_max = (int)ms;
    if (e == 0) printf("RISOLVI %s %lu OK %s\n", nome, ms, ip);
    else { g_no++; printf("RISOLVI %s %lu NO %d %s\n", nome, ms, e, gai_strerror(e)); }
    fflush(stdout);
    pthread_mutex_unlock(&g_m);
    if (r) freeaddrinfo(r);
}

static const char *g_dns;

static void *fase(void *a)
{
    const char        *nome = (const char *)a, *x;
    unsigned char      q[300], r[1500];
    struct sockaddr_in srv;
    struct timeval     tv = { 5, 0 };
    unsigned long      t0, t1, t2, t3, t4;
    unsigned int       o = 12;
    int                fd, n;

    memset(q, 0, 12);
    q[0] = (unsigned char)(rand() >> 8); q[1] = (unsigned char)rand(); q[2] = 1; q[5] = 1;
    for (x = nome; *x; ) {
        const char *pt = strchr(x, '.');
        unsigned int l = pt ? (unsigned int)(pt - x) : (unsigned int)strlen(x);
        q[o++] = (unsigned char)l; memcpy(q + o, x, l); o += l; x += l; if (*x == '.') x++;
    }
    q[o++] = 0; q[o++] = 0; q[o++] = 1; q[o++] = 0; q[o++] = 1;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET; srv.sin_port = htons(53);
    inet_pton(AF_INET, g_dns, &srv.sin_addr);
    t0 = ms_ora();
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    t1 = ms_ora();
    sendto(fd, q, o, 0, (struct sockaddr *)&srv, sizeof(srv));
    t2 = ms_ora();
    n = (int)recvfrom(fd, r, sizeof(r), 0, NULL, NULL);
    t3 = ms_ora();
    close(fd);
    t4 = ms_ora();
    pthread_mutex_lock(&g_m);
    printf("FASI %s socket %lu sendto %lu recvfrom %lu close %lu %d\n", nome,
           t1 - t0, t2 - t1, t3 - t2, t4 - t3, n);
    fflush(stdout);
    pthread_mutex_unlock(&g_m);
    return NULL;
}

static void *filo(void *a)
{
    una((const char *)a);
    return NULL;
}

int main(int argc, char **argv)
{
    int giri = 3, g, i;

    if (argc > 2 && strcmp(argv[1], "-fasi") == 0) {
        pthread_t t[64];
        int       n = argc - 3 < 64 ? argc - 3 : 64;

        g_dns = argv[2];
        for (i = 0; i < n; i++) una(argv[3 + i]);       /* warm: ARP, the stack */
        printf("FASI-INSIEME %d fili\n", n);
        for (i = 0; i < n; i++) pthread_create(&t[i], NULL, fase, argv[3 + i]);
        for (i = 0; i < n; i++) pthread_join(t[i], NULL);
        printf("FASI-IN-FILA\n");
        for (i = 0; i < n; i++) fase(argv[3 + i]);
        return 0;
    }
    if (argc > 1) giri = atoi(argv[1]);
    if (giri <= 0) giri = 3;
    if (argc > 2) { g_nomi = (const char **)(argv + 2); g_n = argc - 2; }
    else { g_nomi = g_nomi_base; g_n = (int)(sizeof(g_nomi_base) / sizeof(g_nomi_base[0])); }

    printf("RISOLVI-INIZIO %d nomi, %d giri\n", g_n, giri);
    for (g = 0; g < giri; g++) {
        pthread_t t[64];
        int       n = g_n < 64 ? g_n : 64;

        printf("RISOLVI-GIRO %d in fila\n", g + 1);
        for (i = 0; i < g_n; i++) una(g_nomi[i]);
        printf("RISOLVI-GIRO %d insieme (%d fili)\n", g + 1, n);
        for (i = 0; i < n; i++) pthread_create(&t[i], NULL, filo, (void *)g_nomi[i]);
        for (i = 0; i < n; i++) pthread_join(t[i], NULL);
    }
    printf("RISOLVI-TOTALE %d %d %d\n", g_tot, g_no, g_max);
    return g_no ? 1 : 0;
}
