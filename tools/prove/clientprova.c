/* =============================================================================
 * tools/prove/clientprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il banco di prova del cliente TLS: lo stesso codice che gira dentro EX-OS,
 * qui compilato sull'host e messo a parlare con un server OpenSSL vero.
 *
 * ! IL RIFERIMENTO E' L'ALTRA PARTE, non un vettore di prova. Un handshake si
 * puo' sbagliare in venti punti e diciannove di quelli danno «bad record mac»:
 * l'unico modo di sapere se e' giusto e' che un'implementazione che nessuno di
 * noi ha scritto lo porti a termine e risponda.
 *
 *     clientprova HOST PORTA NOME ca.pem
 *
 * Rende 0 se la stretta e' riuscita e sono arrivati dei byte, altrimenti il
 * codice di errore stampato in chiaro sulla riga «esito».
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <sys/time.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#include "extls.h"

void sha256(const void *dati, size_t len, unsigned char out[32])
{
    SHA256((const unsigned char *)dati, len, out);
}

static void casuale(unsigned char *b, unsigned int n)
{
    if (RAND_bytes(b, (int)n) != 1) {
        fprintf(stderr, "clientprova: RAND_bytes non funziona\n");
        exit(2);
    }
}

/* --- il trasporto: un socket e basta ---------------------------------------- */
static int sock_leggi(void *stato, unsigned char *dst, unsigned int max,
                      unsigned int ms)
{
    (void)ms;
    return (int)read(*(int *)stato, dst, max);
}

static int sock_scrivi(void *stato, const unsigned char *src, unsigned int n)
{
    unsigned int fatti = 0;

    while (fatti < n) {
        ssize_t r = write(*(int *)stato, src + fatti, n - fatti);

        if (r <= 0) return -1;
        fatti += (unsigned int)r;
    }
    return (int)n;
}

static char   pem[1024 * 1024];
static unsigned char der[1024 * 1024];
static ExMagazzino magazzino;

/* Una connessione: stretta, GET, lettura fino alla chiusura. `o` porta il
 * biglietto da offrire e l'impronta fidata; `preso`, se non nullo, riceve un
 * biglietto di questa connessione per la prossima. */
static int giro(char **argv, const ExTlsOpzioni *o, ExTlsSessione *preso)
{
    ExTlsSotto sotto;
    void      *tls;
    int        fd, n, ricevuti = 0, r;
    struct sockaddr_in a;
    char       richiesta[512];

    /* --- il socket -------------------------------------------------------- */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons((unsigned short)atoi(argv[2]));
    a.sin_addr.s_addr = inet_addr(argv[1]);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        perror("connect");
        return 2;
    }

    {
        /* Un tetto al tempo di lettura: senza, un server che non risponde piu'
         * lascia la prova appesa e non si sa a che punto era. */
        struct timeval tv;
        tv.tv_sec = 10; tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    sotto.stato  = &fd;
    sotto.leggi  = sock_leggi;
    sotto.scrivi = sock_scrivi;

    tls = malloc(extls_misura());
    if (!tls) return 2;

    /* Le date dei certificati si guardano contro un'ora vera. */
    {
        char adesso[32];
        time_t ora = time(0);
        struct tm *g = gmtime(&ora);

        sprintf(adesso, "%04d%02d%02d%02d%02d%02dZ",
                g->tm_year + 1900, g->tm_mon + 1, g->tm_mday,
                g->tm_hour, g->tm_min, g->tm_sec);

        r = extls_stretta_con(tls, &sotto, argv[3], &magazzino, adesso, casuale, o);
    }

    {
        unsigned char imp[EXTLS_IMPRONTA];
        int k;

        if (extls_impronta_sito(tls, imp) == 0) {
            printf("impronta: ");
            for (k = 0; k < EXTLS_IMPRONTA; k++) printf("%02x", imp[k]);
            printf("\n");
        }
    }

    if (r != EXTLS_OK) {
        printf("esito: %d (%s)", r, extls_perche(r));
        if (extls_allarme(tls)) printf(" [allarme %u]", extls_allarme(tls));
        if (extls_motivo(tls))  printf(" [catena %d]", extls_motivo(tls));
        printf("\n");
        return 1;
    }
    printf("stretta: riuscita%s\n", extls_ripresa(tls) ? " (ripresa)" : "");

    sprintf(richiesta,
            "GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", argv[3]);
    if (extls_scrivi(tls, (const unsigned char *)richiesta,
                     (unsigned int)strlen(richiesta)) < 0) {
        printf("esito: non sono riuscito a mandare la richiesta\n");
        return 1;
    }

    for (;;) {
        unsigned char buf[4096];

        n = extls_leggi(tls, buf, sizeof(buf), 15000);
        if (n <= 0) break;
        if (ricevuti == 0) {
            int k = n < 40 ? n : 40;
            printf("prima riga: %.*s\n", k, (char *)buf);
        }
        ricevuti += n;
    }

    printf("ricevuti: %d byte", ricevuti);
    if (extls_ultimo(tls))
        printf(" [interrotto: %s]", extls_perche(extls_ultimo(tls)));
    if (extls_allarme(tls)) printf(" [allarme %u]", extls_allarme(tls));
    printf("\n");
    if (preso) printf("biglietto: %s\n", extls_biglietto_prendi(tls, preso) ? "si" : "no");
    extls_chiudi(tls);
    close(fd);
    free(tls);

    return ricevuti > 0 ? 0 : 1;
}

/* Gli argomenti dopo ca.pem:
 *   -fidata HEX   l'impronta SHA-256 del certificato gia' accettato
 *   -riprendi     due connessioni: la seconda riprende la prima
 *   -alpn P       il protocollo ALPN ("" = nessuno; senza, http/1.1) */
int main(int argc, char **argv)
{
    static unsigned char fidata[EXTLS_IMPRONTA];
    static ExTlsSessione sessione;
    ExTlsOpzioni o;
    int          r, k, riprendi = 0;

    if (argc < 5) {
        fprintf(stderr, "uso: clientprova HOST PORTA NOME ca.pem [-fidata HEX] [-riprendi] [-alpn P]\n");
        return 2;
    }
    memset(&o, 0, sizeof(o));
    for (k = 5; k < argc; k++) {
        if (!strcmp(argv[k], "-riprendi")) riprendi = 1;
        else if (!strcmp(argv[k], "-alpn") && k + 1 < argc) o.alpn = argv[++k];
        else if (!strcmp(argv[k], "-fidata") && k + 1 < argc) {
            const char *h = argv[++k];
            int j;
            for (j = 0; j < EXTLS_IMPRONTA && h[2*j] && h[2*j+1]; j++) {
                unsigned int v;
                sscanf(h + 2*j, "%2x", &v);
                fidata[j] = (unsigned char)v;
            }
            o.fidata = fidata;
        }
    }

    /* --- il magazzino ---------------------------------------------------- */
    {
        FILE *f = fopen(argv[4], "rb");
        size_t letti;

        if (!f) { perror(argv[4]); return 2; }
        letti = fread(pem, 1, sizeof(pem), f);
        fclose(f);

        r = extls_magazzino_pem(&magazzino, pem, (unsigned int)letti,
                                der, sizeof(der), 0);
        printf("magazzino: %d certificati\n", r);
        if (r < 0) return 2;
    }

    r = giro(argv, &o, riprendi ? &sessione : 0);
    if (r != 0 || !riprendi) return r;
    if (sessione.biglietto_n == 0) return 1;
    o.riprendi = &sessione;
    o.eta_ms   = 100;
    return giro(argv, &o, 0);
}
