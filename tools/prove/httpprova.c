/* =============================================================================
 * tools/prove/httpprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il banco di prova dell'HTTP, che gira SULL'HOST.
 *
 * ! LA PARTE CHE SI PUO' SBAGLIARE NON TOCCA LA RETE, ed e' per questo che si
 * prova qui: smontare un URL, leggere le intestazioni, srotolare un corpo a
 * pezzi sono conti su byte. Un difetto li' si vede con una risposta scritta a
 * mano, in un decimo di secondo, invece che con un server, una macchina
 * virtuale e novanta secondi di avvio.
 *
 * ! E I CASI CHE CONTANO SONO QUELLI STORTI. Un corpo a pezzi che arriva tutto
 * insieme lo srotolerebbe anche un ciclo sbagliato: quello che distingue una
 * macchina a stati che funziona e' il pezzo tagliato a meta' della sua riga di
 * lunghezza, perche' e' esattamente cio' che fa la rete vera.
 *
 *     cc -o /tmp/httpprova tools/prove/httpprova.c lib/exhttp/http.c \
 *        -I lib/exhttp
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include "http.h"

static int errori = 0;

static void ok(int cond, const char *cosa)
{
    printf("  [%s] %s\n", cond ? " ok " : "NO  ", cosa);
    if (!cond) errori++;
}

/* -----------------------------------------------------------------------------
 * Gli URL
 * --------------------------------------------------------------------------- */
static void prova_url(void)
{
    HttpUrl u;

    printf("URL\n");

    ok(http_url("http://www.google.com", &u) &&
       strcmp(u.host, "www.google.com") == 0 && u.porta == 80 &&
       strcmp(u.percorso, "/") == 0 && !u.cifrato,
       "http://www.google.com  -> host, porta 80, percorso /");

    ok(http_url("https://www.w3c.org/TR/", &u) &&
       strcmp(u.host, "www.w3c.org") == 0 && u.porta == 443 &&
       strcmp(u.percorso, "/TR/") == 0 && u.cifrato,
       "https://www.w3c.org/TR/  -> cifrato, porta 443, percorso");

    ok(http_url("www.esempio.it/pagina", &u) &&
       strcmp(u.host, "www.esempio.it") == 0 && u.porta == 80,
       "senza schema si assume http");

    ok(http_url("http://10.0.2.2:8080/a?b=c", &u) &&
       strcmp(u.host, "10.0.2.2") == 0 && u.porta == 8080 &&
       strcmp(u.percorso, "/a?b=c") == 0,
       "porta esplicita, e la domanda resta nel percorso");

    ok(!http_url("ftp://esempio.it/", &u),  "ftp:// si rifiuta");
    ok(!http_url("http://", &u),            "senza host si rifiuta");
    ok(!http_url("http://a:0/", &u),        "porta zero si rifiuta");
    ok(!http_url("http://a:99999/", &u),    "porta fuori scala si rifiuta");

    {
        /* Un host lunghissimo non deve traboccare da nessuna parte. */
        char lungo[600];
        int  i;

        strcpy(lungo, "http://");
        for (i = 0; i < 400; i++) strcat(lungo, "a");
        strcat(lungo, "/x");
        ok(!http_url(lungo, &u), "host piu' lungo del campo si rifiuta");
    }
}

/* -----------------------------------------------------------------------------
 * La richiesta
 * --------------------------------------------------------------------------- */
static void prova_richiesta(void)
{
    HttpUrl u;
    char    r[1024];
    int     n;

    printf("RICHIESTA\n");

    http_url("http://www.google.com/cerca?q=exos", &u);
    n = http_richiesta(r, sizeof(r), &u, "EX-OS/1.0");

    ok(n > 0, "si costruisce");
    ok(strstr(r, "GET /cerca?q=exos HTTP/1.1\r\n") == r, "riga di richiesta");
    ok(strstr(r, "\r\nHost: www.google.com\r\n") != 0, "Host senza porta 80");
    ok(strstr(r, "\r\nConnection: close\r\n") != 0,    "Connection: close");
    ok(strstr(r, "\r\nUser-Agent: EX-OS/1.0\r\n") != 0, "User-Agent");
    ok(n >= 4 && strcmp(r + n - 4, "\r\n\r\n") == 0,   "chiude con la riga vuota");

    http_url("http://esempio.it:8080/", &u);
    http_richiesta(r, sizeof(r), &u, 0);
    ok(strstr(r, "\r\nHost: esempio.it:8080\r\n") != 0,
       "la porta non prevista entra nell'Host");

    ok(http_richiesta(r, 20, &u, 0) == 0, "un buffer piccolo rende 0, non scrive oltre");
}

/* -----------------------------------------------------------------------------
 * Le intestazioni
 * --------------------------------------------------------------------------- */
static void prova_intestazioni(void)
{
    HttpRisposta r;
    int          k;

    printf("INTESTAZIONI\n");

    {
        const char *s =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: 1234\r\n"
            "\r\nCIAO";

        k = http_intestazioni((const unsigned char *)s, strlen(s), &r);
        ok(k > 0 && (unsigned)k == strlen(s) - 4, "la fine e' dove comincia il corpo");
        ok(r.codice == 200, "codice 200");
        ok(r.ha_lunghezza && r.lunghezza == 1234, "Content-Length");
        ok(strcmp(r.tipo, "text/html; charset=utf-8") == 0, "Content-Type intero");
    }

    {
        /* Maiuscole a caso: i server veri fanno cosi'. */
        const char *s = "HTTP/1.0 404 Not Found\r\n"
                        "CONTENT-LENGTH: 7\r\n"
                        "content-type: text/plain\r\n\r\n";

        http_intestazioni((const unsigned char *)s, strlen(s), &r);
        ok(r.codice == 404 && r.ha_lunghezza && r.lunghezza == 7 &&
           strcmp(r.tipo, "text/plain") == 0,
           "i nomi non distinguono maiuscole e minuscole");
    }

    {
        const char *s = "HTTP/1.1 301 Moved Permanently\r\n"
                        "Location: https://www.google.com/\r\n"
                        "Content-Length: 0\r\n\r\n";

        http_intestazioni((const unsigned char *)s, strlen(s), &r);
        ok(r.codice == 301 && strcmp(r.posizione, "https://www.google.com/") == 0,
           "301 con Location: e' cio' che risponde google in chiaro");
    }

    {
        const char *s = "HTTP/1.1 200 OK\r\n"
                        "Transfer-Encoding: chunked\r\n"
                        "Content-Length: 99\r\n\r\n";

        http_intestazioni((const unsigned char *)s, strlen(s), &r);
        ok(r.a_pezzi && !r.ha_lunghezza,
           "con i pezzi, Content-Length si ignora come dice la specifica");
    }

    {
        const char *s = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n";

        http_intestazioni((const unsigned char *)s, strlen(s), &r);
        ok(r.a_pezzi, "chunked riconosciuto anche dentro un elenco");
    }

    {
        const char *s = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n";

        ok(http_intestazioni((const unsigned char *)s, strlen(s), &r) == 0,
           "intestazioni incomplete: rende 0, non indovina");
    }

    {
        const char *s = "CIAO NON SONO HTTP\r\n\r\n";

        ok(http_intestazioni((const unsigned char *)s, strlen(s), &r) < 0,
           "non e' HTTP: si rifiuta");
    }

    {
        const char *s = "HTTP/1.1 999999 Boh\r\n\r\n";

        ok(http_intestazioni((const unsigned char *)s, strlen(s), &r) < 0,
           "codice fuori scala: si rifiuta");
    }
}

/* -----------------------------------------------------------------------------
 * Il corpo a pezzi
 *
 * ! SI PROVA ANCHE UN BYTE PER VOLTA, ed e' la prova che conta. Dando tutto in
 * un colpo, una macchina a stati sbagliata puo' sembrare giusta; dando un byte
 * per volta ogni transizione viene esercitata da sola, ed e' esattamente cio'
 * che fa una rete lenta.
 * --------------------------------------------------------------------------- */
static int srotola(const char *flusso, unsigned int passo,
                   char *out, unsigned int max, int *finito)
{
    HttpPezzi    p;
    unsigned int letti = 0, prodotti = 0;
    unsigned int n = (unsigned int)strlen(flusso);

    http_pezzi_avvia(&p);
    *finito = 0;

    while (letti < n) {
        unsigned int q = n - letti;
        unsigned int c = 0;
        int          k;

        if (q > passo) q = passo;

        k = http_pezzi(&p, (const unsigned char *)flusso + letti, q, &c,
                       (unsigned char *)out + prodotti, max - prodotti);
        if (k < 0) return -1;
        prodotti += (unsigned int)k;
        letti    += c;

        if (c == 0 && k == 0) break;        /* non avanza piu' */
        if (p.stato == HTTP_P_FATTO) { *finito = 1; break; }
    }

    out[prodotti] = '\0';
    return (int)prodotti;
}

static void prova_pezzi(void)
{
    char out[4096];
    int  fin, n;
    unsigned int passo;

    printf("CORPO A PEZZI\n");

    {
        const char *f = "4\r\nCiao\r\n6\r\n mondo\r\n0\r\n\r\n";

        for (passo = 1; passo <= 64; passo *= 2) {
            char cosa[80];

            n = srotola(f, passo, out, sizeof(out), &fin);
            sprintf(cosa, "«Ciao mondo» a blocchi da %u byte", passo);
            ok(n == 10 && strcmp(out, "Ciao mondo") == 0 && fin, cosa);
        }
    }

    {
        /* Le estensioni dopo la lunghezza sono legittime e si saltano. */
        const char *f = "4;a=b\r\nCiao\r\n0\r\n\r\n";

        n = srotola(f, 1, out, sizeof(out), &fin);
        ok(n == 4 && strcmp(out, "Ciao") == 0 && fin,
           "le estensioni dopo la lunghezza si saltano");
    }

    {
        /* Le intestazioni finali vanno lette, non solo lo zero. */
        const char *f = "3\r\nabc\r\n0\r\nX-Cosa: valore\r\n\r\n";

        n = srotola(f, 3, out, sizeof(out), &fin);
        ok(n == 3 && strcmp(out, "abc") == 0 && fin,
           "la coda dopo lo zero si consuma fino alla riga vuota");
    }

    {
        const char *f = "FFFFFFFFF\r\n";     /* nove cifre: assurdo */

        ok(srotola(f, 1, out, sizeof(out), &fin) < 0,
           "una lunghezza di nove cifre si rifiuta");
    }

    {
        const char *f = "\r\n\r\n\r\n";      /* nessuna cifra */

        ok(srotola(f, 1, out, sizeof(out), &fin) < 0,
           "riga di lunghezza senza cifre: si rifiuta invece di girare a vuoto");
    }

    {
        const char *f = "3\r\nabcXX";        /* il terminatore e' sbagliato */

        ok(srotola(f, 1, out, sizeof(out), &fin) < 0,
           "dopo i dati ci vuole CRLF, e se non c'e' si rifiuta");
    }

    {
        /* L'uscita piccola: si deve poter riprendere. */
        const char *f = "A\r\n0123456789\r\n0\r\n\r\n";
        HttpPezzi   p;
        char        pezzo[4];
        unsigned int letti = 0, tot = 0, c;
        int          k;

        http_pezzi_avvia(&p);
        out[0] = '\0';

        while (letti < strlen(f)) {
            k = http_pezzi(&p, (const unsigned char *)f + letti,
                           (unsigned int)strlen(f) - letti, &c,
                           (unsigned char *)pezzo, sizeof(pezzo));
            if (k < 0) break;
            memcpy(out + tot, pezzo, (unsigned int)k);
            tot += (unsigned int)k;
            letti += c;
            if (c == 0 && k == 0) break;
            if (p.stato == HTTP_P_FATTO) break;
        }
        out[tot] = '\0';
        ok(tot == 10 && strcmp(out, "0123456789") == 0 &&
           p.stato == HTTP_P_FATTO,
           "un'uscita da 4 byte per un pezzo da 10: si riprende");
    }
}

int main(void)
{
    prova_url();
    prova_richiesta();
    prova_intestazioni();
    prova_pezzi();

    printf("\n%s\n", errori ? "CI SONO ERRORI" : "tutto a posto");
    return errori ? 1 : 0;
}
