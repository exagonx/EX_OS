/* =============================================================================
 * tools/prova-invio/prova-invio.c - how fast EX-OS SENDS over TCP, and whether
 * every byte arrives (ip.drv 0.024, voce 1a of @CODA-20-10)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     prova-invio -s PORTA BYTE          receives BYTE bytes, checks them
 *     prova-invio -c IP PORTA BYTE       sends BYTE bytes
 *
 * Built with the cross gcc (i386-exos / x86_64-exos) and run by
 * prova-invio.sh in two EX-OS machines joined by tools/prova-invio/rele.py,
 * a wire that delays every frame and can lose some. The bytes follow a
 * pattern that depends on their position, so a byte lost, doubled or out of
 * place is seen by the receiver. Both sides print one line:
 *
 *     prova-invio: ricevuti N byte in T ms (K KB/s), sbagliati S
 *     prova-invio: mandati N byte in T ms (K KB/s)
 *
 * and the receiver ends with "prova-invio: OK" or "prova-invio: NO".
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static unsigned char atteso(unsigned long i)
{
    return (unsigned char)(i * 7u + (i >> 9) + (i >> 17));
}

static long ms_ora(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

static void stampa(const char *cosa, unsigned long n, long t)
{
    unsigned long kbs = t > 0 ? (unsigned long)((unsigned long long)n * 1000ull / 1024ull / (unsigned long long)t) : 0ul;

    printf("prova-invio: %s %lu byte in %ld ms (%lu KB/s)", cosa, n, t, kbs);
}

static int ricevi(int porta, unsigned long totale)
{
    struct sockaddr_in a;
    static unsigned char buf[65536];
    unsigned long n = 0, sbagliati = 0;
    long t0, t;
    int s, c, uno = 1;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("prova-invio: socket"); return 1; }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)porta);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 1) < 0) {
        perror("prova-invio: bind/listen");
        return 1;
    }
    printf("prova-invio: ascolto su %d\n", porta);
    fflush(stdout);
    c = accept(s, NULL, NULL);
    if (c < 0) { perror("prova-invio: accept"); return 1; }
    t0 = ms_ora();
    for (;;) {
        ssize_t r = read(c, buf, sizeof(buf));
        ssize_t i;

        if (r < 0) { perror("prova-invio: read"); break; }
        if (r == 0) break;
        for (i = 0; i < r; i++)
            if (buf[i] != atteso(n + (unsigned long)i)) sbagliati++;
        n += (unsigned long)r;
    }
    t = ms_ora() - t0;
    close(c);
    close(s);
    stampa("ricevuti", n, t);
    printf(", sbagliati %lu\n", sbagliati);
    if (n == totale && sbagliati == 0) { printf("prova-invio: OK\n"); return 0; }
    printf("prova-invio: NO (attesi %lu byte)\n", totale);
    return 1;
}

static int manda(const char *ip, int porta, unsigned long totale)
{
    struct sockaddr_in a;
    static unsigned char buf[16384];
    unsigned long n = 0;
    long t0;
    int s, tentativi;

    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)porta);
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) { printf("prova-invio: IP?\n"); return 1; }
    /* the receiver may still be starting: a few tries, one second apart */
    for (tentativi = 0; ; tentativi++) {
        s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) { perror("prova-invio: socket"); return 1; }
        if (connect(s, (struct sockaddr *)&a, sizeof(a)) == 0) break;
        close(s);
        if (tentativi == 20) { perror("prova-invio: connect"); return 1; }
        sleep(1);
    }
    t0 = ms_ora();
    while (n < totale) {
        size_t q = totale - n < sizeof(buf) ? (size_t)(totale - n) : sizeof(buf);
        size_t i, fatti = 0;

        for (i = 0; i < q; i++) buf[i] = atteso(n + i);
        while (fatti < q) {
            ssize_t w = write(s, buf + fatti, q - fatti);

            if (w <= 0) { perror("prova-invio: write"); close(s); return 1; }
            fatti += (size_t)w;
        }
        n += q;
    }
    close(s);
    stampa("mandati", n, ms_ora() - t0);
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "-s") == 0)
        return ricevi(atoi(argv[2]), strtoul(argv[3], NULL, 10));
    if (argc == 5 && strcmp(argv[1], "-c") == 0)
        return manda(argv[2], atoi(argv[3]), strtoul(argv[4], NULL, 10));
    fprintf(stderr, "uso: prova-invio -s PORTA BYTE | -c IP PORTA BYTE\n");
    return 2;
}
