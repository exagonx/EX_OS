/* =============================================================================
 * tools/exilla/prova-socket.c — i socket BSD dentro EX-OS (@SOCKET-BSD)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tappa 3 di Exilla (29 settembre 2026). Si esegue con
 * tools/exilla/prova-socket.sh, che accende la rete di QEMU e tiene
 * dall'altra parte tools/exilla/prova-socket-host.py: un eco TCP sulla porta
 * 7801 e un eco UDP sulla 7802 dell'host (10.0.0.2 visto da qui), e un cliente
 * che si collega al nostro servitore sulla 7000 (la 7700 dell'host).
 *
 * Ogni controllo stampa [OK] o [NO]; l'ultima riga dice quanti NO.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>

static int g_no = 0;

static void ok(const char *cosa, int va)
{
    printf("  %s  %s\n", va ? "[OK]" : "[NO]", cosa);
    if (!va) g_no++;
}

static int collega(unsigned short porta)
{
    struct sockaddr_in a;
    int                fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) return -1;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(porta);
    inet_pton(AF_INET, "10.0.0.2", &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
    return fd;
}

/* Legge esattamente n byte (o meno se finisce). */
static int leggi_tutto(int fd, unsigned char *b, int n)
{
    int fatti = 0;

    while (fatti < n) {
        int r = (int)recv(fd, b + fatti, (size_t)(n - fatti), 0);

        if (r <= 0) break;
        fatti += r;
    }
    return fatti;
}

/* --- fili --------------------------------------------------------------------------- */
static int g_fd_filo;
static char g_letto[16];
static int  g_letti;

static void *lettore(void *arg)
{
    (void)arg;
    g_letti = (int)recv(g_fd_filo, g_letto, sizeof(g_letto) - 1, 0);
    return NULL;
}

int main(void)
{
    unsigned char  ip[4];
    char           testo[32];
    struct addrinfo consigli, *ai = NULL;
    int            fd, r;

    printf("prova-socket: i socket BSD dentro EX-OS\n");

    /* 1. indirizzi */
    ok("inet_pton e inet_ntop, andata e ritorno",
       inet_pton(AF_INET, "192.168.1.200", ip) == 1 &&
       strcmp(inet_ntop(AF_INET, ip, testo, sizeof(testo)), "192.168.1.200") == 0);
    ok("inet_pton rifiuta 1.2.3.256 e 1.2.3", inet_pton(AF_INET, "1.2.3.256", ip) == 0 &&
                                               inet_pton(AF_INET, "1.2.3", ip) == 0);
    ok("htons e ntohl", htons(0x1234) == 0x3412 && ntohl(0x01020304) == 0x04030201);
    memset(&consigli, 0, sizeof(consigli));
    consigli.ai_socktype = SOCK_STREAM;
    r = getaddrinfo("10.0.0.2", "http", &consigli, &ai);
    ok("getaddrinfo numerico, servizio per nome (http = 80)",
       r == 0 && ai && ai->ai_family == AF_INET &&
       ntohs(((struct sockaddr_in *)ai->ai_addr)->sin_port) == 80 && ai->ai_next == NULL);
    if (ai) freeaddrinfo(ai);
    ai = NULL;
    r = getaddrinfo("localhost", NULL, NULL, &ai);
    ok("getaddrinfo localhost: 127.0.0.1, uno per tipo",
       r == 0 && ai && ai->ai_next &&
       ((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr == htonl(0x7F000001));
    if (ai) freeaddrinfo(ai);
    ok("socket AF_INET6 rifiutato con EAFNOSUPPORT",
       socket(AF_INET6, SOCK_STREAM, 0) == -1 && errno == EAFNOSUPPORT);

    /* 2. un cliente TCP: centomila byte d'andata e di ritorno */
    fd = collega(7801);
    ok("connect a 10.0.0.2:7801", fd >= PRESA_BASE);
    if (fd >= 0) {
        static unsigned char via[100000], torna[100000];
        struct sockaddr_in   io, lui;
        socklen_t            l1 = sizeof(io), l2 = sizeof(lui);
        struct stat          st;
        int                  i, mandati;

        for (i = 0; i < (int)sizeof(via); i++) via[i] = (unsigned char)(i * 7 + i / 251);
        mandati = (int)send(fd, via, sizeof(via), 0);
        ok("send di 100000 byte, tutti presi", mandati == (int)sizeof(via));
        ok("e tornano uguali", leggi_tutto(fd, torna, sizeof(torna)) == (int)sizeof(torna) &&
                               memcmp(via, torna, sizeof(via)) == 0);
        ok("getpeername: 10.0.0.2:7801",
           getpeername(fd, (struct sockaddr *)&lui, &l2) == 0 &&
           lui.sin_addr.s_addr == inet_addr("10.0.0.2") && ntohs(lui.sin_port) == 7801);
        ok("getsockname: la nostra porta non e' zero",
           getsockname(fd, (struct sockaddr *)&io, &l1) == 0 && ntohs(io.sin_port) != 0);
        ok("fstat dice socket, isatty no, lseek ESPIPE",
           fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode) && !isatty(fd) &&
           lseek(fd, 0, SEEK_SET) == -1 && errno == ESPIPE);

        /* 3. senza bloccare */
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        r = (int)recv(fd, testo, sizeof(testo), 0);
        ok("O_NONBLOCK: recv senza dati rende EAGAIN", r == -1 && errno == EAGAIN);
        {
            struct pollfd p;
            int           n;

            send(fd, "ping\n", 5, 0);
            p.fd = fd; p.events = POLLIN; p.revents = 0;
            n = poll(&p, 1, 3000);
            r = (int)recv(fd, testo, sizeof(testo), 0);
            ok("poll aspetta i dati, poi recv li da'", n == 1 && (p.revents & POLLIN) &&
                                                      r == 5 && memcmp(testo, "ping\n", 5) == 0);
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

        /* 4. la scadenza */
        {
            struct timeval tv = { 0, 300000 };
            unsigned int   t0;

            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            t0 = uptime_ms();
            r = (int)recv(fd, testo, sizeof(testo), 0);
            t0 = uptime_ms() - t0;
            printf("        (scaduta dopo %u ms)\n", t0);
            ok("SO_RCVTIMEO: EAGAIN dopo ~300 ms", r == -1 && errno == EAGAIN &&
                                                   t0 >= 280 && t0 < 1500);
            tv.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }

        /* 5. un filo legge da un socket aperto da un altro */
        {
            pthread_t f;

            g_fd_filo = fd; g_letti = -2;
            pthread_create(&f, NULL, lettore, NULL);
            {
                struct timespec pausa = { 0, 200000000 };
                nanosleep(&pausa, NULL);
            }
            send(fd, "dal filo", 8, 0);
            pthread_join(f, NULL);
            ok("un filo riceve su un socket aperto dal principale",
               g_letti == 8 && memcmp(g_letto, "dal filo", 8) == 0);

            g_letti = -2;
            pthread_create(&f, NULL, lettore, NULL);
            {
                struct timespec pausa = { 0, 200000000 };
                nanosleep(&pausa, NULL);
            }
            close(fd);
            pthread_join(f, NULL);
            ok("close da un altro filo sveglia chi e' fermo in recv (0 = fine)", g_letti == 0);
        }
    }

    /* 6. UDP */
    {
        struct sockaddr_in a, da;
        socklen_t          l = sizeof(da);
        struct timeval     tv = { 3, 0 };
        int                u = socket(AF_INET, SOCK_DGRAM, 0);

        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port   = htons(7802);
        a.sin_addr.s_addr = inet_addr("10.0.0.2");
        setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        r = (int)sendto(u, "datagramma", 10, 0, (struct sockaddr *)&a, sizeof(a));
        ok("UDP sendto", r == 10);
        memset(testo, 0, sizeof(testo));
        r = (int)recvfrom(u, testo, sizeof(testo), 0, (struct sockaddr *)&da, &l);
        ok("UDP recvfrom: l'eco torna, dalla porta 7802",
           r == 10 && memcmp(testo, "DATAGRAMMA", 10) == 0 && ntohs(da.sin_port) == 7802);
        close(u);
    }

    /* 7. un servitore: l'host si collega alla nostra 7000 */
    {
        struct sockaddr_in a, lui;
        socklen_t          l = sizeof(lui);
        struct timeval     tv = { 40, 0 };
        int                s = socket(AF_INET, SOCK_STREAM, 0), c;
        int                uno = 1;

        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port   = htons(7000);
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));
        r = bind(s, (struct sockaddr *)&a, sizeof(a));
        ok("bind e listen sulla 7000", r == 0 && listen(s, 4) == 0);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        printf("prova-socket: IN ASCOLTO 7000\n");
        fflush(stdout);
        c = accept(s, (struct sockaddr *)&lui, &l);
        ok("accept: arriva l'host", c >= PRESA_BASE && lui.sin_addr.s_addr != 0);
        if (c >= 0) {
            char buf[16];
            int  i;

            memset(buf, 0, sizeof(buf));
            r = leggi_tutto(c, (unsigned char *)buf, 4);
            for (i = 0; i < r; i++) buf[i] = (char)(buf[i] - 'a' + 'A');
            ok("il servitore legge 'ciao' e risponde 'CIAO'",
               r == 4 && send(c, buf, 4, 0) == 4);
            close(c);
        }
        close(s);
    }

    /* 8. il DNS vero: dipende dalla rete dell'host, quindi si dice e non si conta */
    ai = NULL;
    r = getaddrinfo("example.com", "443", NULL, &ai);
    if (r == 0 && ai) {
        char t[16];

        inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, t, sizeof(t));
        printf("        (example.com = %s)\n", t);
        freeaddrinfo(ai);
    } else printf("        (example.com: %s - senza internet dall'host e' normale)\n",
                  gai_strerror(r));

    printf("prova-socket: %d NO\n", g_no);
    return g_no;
}
