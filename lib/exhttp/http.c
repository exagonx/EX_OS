/* =============================================================================
 * lib/exhttp/http.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * HTTP/1.1, la parte che non tocca la rete. Cosa fa e perche' sta in http.h.
 *
 * ! QUESTO FILE NON INCLUDE LA libc, come ttf.c e raster.c e per la stessa
 * ragione: si compila con un `cc` sull'host e si prova contro risposte in
 * scatola e contro un server vero, invece di volere un giro di costruzione e
 * novanta secondi di avvio per ogni riga cambiata. Le quattro funzioni di
 * stringa che servivano sono scritte qui sotto e sono dieci righe.
 * ============================================================================= */

#include "http.h"

/* -----------------------------------------------------------------------------
 * Le quattro cose che servono, invece della libc
 * --------------------------------------------------------------------------- */
static unsigned int lung(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static void copia(char *d, const char *s, unsigned int max)
{
    unsigned int i = 0;

    if (max == 0) return;
    while (s[i] && i + 1 < max) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int minuscola(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* Confronto senza distinzione di maiuscole, su `n` byte.
 *
 * ! I NOMI DELLE INTESTAZIONI NON DISTINGUONO MAIUSCOLE E MINUSCOLE, e non e'
 * un dettaglio da ignorare: i server veri scrivono «Content-Length»,
 * «content-length» e «CONTENT-LENGTH», tutti e tre legittimi. Un confronto
 * esatto funzionerebbe con il primo server provato e fallirebbe col secondo. */
static int uguale_min(const unsigned char *a, const char *b, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        if (minuscola(a[i]) != minuscola((unsigned char)b[i])) return 0;
    return 1;
}

/* -----------------------------------------------------------------------------
 * L'URL
 * --------------------------------------------------------------------------- */
int http_url(const char *url, HttpUrl *u)
{
    unsigned int i = 0, j;
    const char  *host;

    if (!url || !u) return 0;

    for (j = 0; j < sizeof(*u); j++) ((char *)u)[j] = 0;

    /* Lo schema, se c'e'. */
    {
        unsigned int k = 0;

        while (url[k] && url[k] != ':' && url[k] != '/' && k < 8) k++;

        if (url[k] == ':' && url[k + 1] == '/' && url[k + 2] == '/') {
            if (k >= sizeof(u->schema)) return 0;
            copia(u->schema, url, k + 1);
            i = k + 3;
        } else {
            /* ! SENZA SCHEMA SI ASSUME http, perche' e' quello che la gente
             * scrive in una barra degli indirizzi. Rifiutare «www.esempio.it»
             * sarebbe corretto secondo la specifica e inutile per chi lo usa. */
            copia(u->schema, "http", sizeof(u->schema));
        }
    }

    if (uguale_min((const unsigned char *)u->schema, "https", 5) &&
        u->schema[5] == '\0') {
        u->cifrato = 1;
        u->porta   = 443;
    } else if (uguale_min((const unsigned char *)u->schema, "http", 4) &&
               u->schema[4] == '\0') {
        u->cifrato = 0;
        u->porta   = 80;
    } else {
        return 0;               /* ftp://, file://, ... non sono roba nostra */
    }

    /* L'host, fino a ':' oppure '/'. */
    host = url + i;
    {
        unsigned int k = 0;

        while (host[k] && host[k] != ':' && host[k] != '/') k++;
        if (k == 0 || k >= sizeof(u->host)) return 0;
        copia(u->host, host, k + 1);
        i += k;
    }

    /* La porta, se c'e'. */
    if (url[i] == ':') {
        unsigned int p = 0;

        i++;
        if (url[i] < '0' || url[i] > '9') return 0;
        while (url[i] >= '0' && url[i] <= '9') {
            p = p * 10 + (unsigned int)(url[i] - '0');
            if (p > 65535) return 0;
            i++;
        }
        if (p == 0) return 0;
        u->porta = p;
    }

    /* Il percorso: tutto il resto, e se manca e' «/». */
    if (url[i] == '\0') copia(u->percorso, "/", sizeof(u->percorso));
    else                copia(u->percorso, url + i, sizeof(u->percorso));

    return 1;
}

/* -----------------------------------------------------------------------------
 * La richiesta
 * --------------------------------------------------------------------------- */
static int metti(char *out, unsigned int max, unsigned int *n, const char *s)
{
    unsigned int i = 0;

    while (s[i]) {
        if (*n + 1 >= max) return 0;
        out[*n] = s[i];
        (*n)++;
        i++;
    }
    return 1;
}

/* =============================================================================
 * ! DUE VERBI, UNA FUNZIONE SOLA, ed e' la ragione per cui POST e' arrivato in
 * venti righe: fra un GET e un POST cambiano tre cose — la parola in cima, due
 * intestazioni in fondo e un corpo attaccato dietro. Tutto il resto — l'host,
 * la porta che va nell'host solo se non e' quella prevista, l'agente, il
 * «Connection: close» — e' identico, e duplicarlo avrebbe voluto dire due
 * funzioni che divergono alla prima intestazione aggiunta.
 *
 * ! IL CORPO NON SI TOCCA E NON SI CODIFICA QUI. Chi manda un modulo ha gia'
 * costruito «a=1&b=2» con la codifica percento, ed e' l'unico a sapere come
 * andava fatta. Questa funzione ci mette intorno le due intestazioni che
 * dicono al server quanto e' lungo e di che tipo e', e basta.
 * ========================================================================== */
int http_richiesta_corpo(char *out, unsigned int max, const HttpUrl *u,
                         const char *agente, const char *corpo, int vivo)
{
    unsigned int n = 0;

    if (!out || !u || max == 0) return 0;

    if (!metti(out, max, &n, corpo ? "POST " : "GET ")) return 0;
    if (!metti(out, max, &n, u->percorso)) return 0;
    if (!metti(out, max, &n, " HTTP/1.1\r\nHost: ")) return 0;
    if (!metti(out, max, &n, u->host)) return 0;

    /* ! LA PORTA VA NELL'HOST SE NON E' QUELLA PREVISTA, e omesso il caso
     * normale: un «Host: esempio.it:80» e' legittimo ma alcuni server lo
     * trattano come un host diverso da «esempio.it», e le pagine che
     * confrontano l'host se ne accorgono. */
    if ((u->cifrato && u->porta != 443) || (!u->cifrato && u->porta != 80)) {
        char cifre[8];
        int  k = 0;
        unsigned int p = u->porta;
        char rov[8];
        int  r = 0;

        while (p) { rov[r++] = (char)('0' + (p % 10)); p /= 10; }
        while (r) cifre[k++] = rov[--r];
        cifre[k] = '\0';

        if (!metti(out, max, &n, ":")) return 0;
        if (!metti(out, max, &n, cifre)) return 0;
    }

    if (!metti(out, max, &n, "\r\nUser-Agent: ")) return 0;
    if (!metti(out, max, &n, agente && agente[0] ? agente : "EX-OS")) return 0;

    /* ! SI DICHIARA DI ACCETTARE QUALUNQUE COSA, e non si chiede la
     * compressione. Un «Accept-Encoding: gzip» farebbe arrivare pagine
     * compresse — che sapremmo pure srotolare, inflate c'e' — ma e' un pezzo
     * in piu' da sbagliare prima ancora di aver visto una pagina intera. Si
     * aggiunge quando il resto funziona. */
    if (!metti(out, max, &n, "\r\nAccept: */*\r\nConnection: ")) return 0;
    if (!metti(out, max, &n, vivo ? "keep-alive" : "close")) return 0;

    if (corpo) {
        char cifre[12], rov[12];
        unsigned int len = 0;
        int k = 0, r = 0;

        while (corpo[len]) len++;

        /* ! Content-Length E' OBBLIGATORIO, e sbagliarlo di un byte fa
         * aspettare il server per sempre (troppo corto) o gli fa leggere
         * l'inizio della richiesta dopo (troppo lungo). Si conta il corpo,
         * non si stima. */
        if (len == 0) { rov[r++] = '0'; }
        else { unsigned int q = len; while (q) { rov[r++] = (char)('0' + q % 10); q /= 10; } }
        while (r) cifre[k++] = rov[--r];
        cifre[k] = '\0';

        if (!metti(out, max, &n,
                   "\r\nContent-Type: application/x-www-form-urlencoded"
                   "\r\nContent-Length: ")) return 0;
        if (!metti(out, max, &n, cifre)) return 0;
        if (!metti(out, max, &n, "\r\n\r\n")) return 0;
        if (!metti(out, max, &n, corpo)) return 0;
    } else {
        if (!metti(out, max, &n, "\r\n\r\n")) return 0;
    }

    out[n] = '\0';
    return (int)n;
}

int http_richiesta(char *out, unsigned int max, const HttpUrl *u,
                   const char *agente)
{
    return http_richiesta_corpo(out, max, u, agente, 0, 0);
}

/* -----------------------------------------------------------------------------
 * Le intestazioni della risposta
 * --------------------------------------------------------------------------- */

/* Copia il valore di un'intestazione, saltando gli spazi iniziali. */
static void valore(const unsigned char *d, unsigned int a, unsigned int b,
                   char *out, unsigned int max)
{
    unsigned int i = 0;

    while (a < b && (d[a] == ' ' || d[a] == '\t')) a++;
    while (a < b && i + 1 < max) out[i++] = (char)d[a++];
    out[i] = '\0';
}

static unsigned int numero(const unsigned char *d, unsigned int a, unsigned int b)
{
    unsigned int v = 0;

    while (a < b && (d[a] == ' ' || d[a] == '\t')) a++;
    while (a < b && d[a] >= '0' && d[a] <= '9') {
        /* ! IL TRABOCCAMENTO SI FERMA, non si lascia girare: la lunghezza la
         * scrive il server, e un numero di venti cifre e' un modo di far
         * allocare a chi legge un buffer che non c'entra niente con la
         * pagina. */
        if (v > 400000000u) return 0xFFFFFFFFu;
        v = v * 10 + (unsigned int)(d[a] - '0');
        a++;
    }
    return v;
}

int http_intestazioni(const unsigned char *d, unsigned int n, HttpRisposta *r)
{
    unsigned int i, riga_a, fine = 0, quante = 0;

    if (!d || !r) return -1;

    for (i = 0; i < sizeof(*r); i++) ((char *)r)[i] = 0;

    /* Trova la riga vuota che chiude le intestazioni. */
    for (i = 0; i + 3 < n; i++)
        if (d[i] == '\r' && d[i+1] == '\n' && d[i+2] == '\r' && d[i+3] == '\n') {
            fine = i + 4;
            break;
        }
    if (fine == 0) {
        /* ! UN TETTO ANCHE QUI, e non e' teoria: un server che non manda mai
         * la riga vuota farebbe crescere il buffer di chi legge finche' c'e'
         * memoria. Sessantaquattro kilobyte di intestazioni sono gia' assurdi. */
        if (n > 64u * 1024u) return -1;
        return 0;
    }

    /* La riga di stato: HTTP/1.x <codice> <motivo> */
    if (n < 12 || !uguale_min(d, "http/", 5)) return -1;
    {
        unsigned int k = 5;

        while (k < fine && d[k] != ' ') k++;
        if (k >= fine) return -1;
        r->codice = (int)numero(d, k, fine);
        if (r->codice < 100 || r->codice > 599) return -1;
    }

    /* Le intestazioni, una riga per volta. */
    riga_a = 0;
    while (riga_a < fine && d[riga_a] != '\n') riga_a++;
    riga_a++;

    while (riga_a < fine) {
        unsigned int riga_b = riga_a, due;

        while (riga_b < fine && d[riga_b] != '\r' && d[riga_b] != '\n') riga_b++;
        if (riga_b == riga_a) break;                /* la riga vuota: finito */

        if (++quante > 200) return -1;              /* un tetto anche al numero */

        for (due = riga_a; due < riga_b && d[due] != ':'; due++) { }

        if (due < riga_b) {
            unsigned int nl = due - riga_a;

            if (nl == 14 && uguale_min(d + riga_a, "content-length", 14)) {
                unsigned int v = numero(d, due + 1, riga_b);

                if (v != 0xFFFFFFFFu) { r->lunghezza = v; r->ha_lunghezza = 1; }
            } else if (nl == 12 && uguale_min(d + riga_a, "content-type", 12)) {
                valore(d, due + 1, riga_b, r->tipo, sizeof(r->tipo));
            } else if (nl == 8 && uguale_min(d + riga_a, "location", 8)) {
                valore(d, due + 1, riga_b, r->posizione, sizeof(r->posizione));
            } else if (nl == 10 && uguale_min(d + riga_a, "connection", 10)) {
                char t[32];

                /* ! «close» SI CERCA DENTRO L'ELENCO, non come uguaglianza: la
                 * specifica permette «keep-alive, close» e altre combinazioni,
                 * e chi confronta tutta la riga si perde proprio il caso in
                 * cui il server sta dicendo che chiude. Riusare una
                 * connessione che l'altro ha appena chiuso vuol dire una
                 * richiesta persa. */
                valore(d, due + 1, riga_b, t, sizeof(t));
                {
                    unsigned int k, L = lung(t);

                    for (k = 0; k + 5 <= L; k++)
                        if (uguale_min((const unsigned char *)t + k, "close", 5))
                            r->chiude = 1;
                }
            } else if (nl == 17 &&
                       uguale_min(d + riga_a, "transfer-encoding", 17)) {
                char t[32];

                valore(d, due + 1, riga_b, t, sizeof(t));
                /* ! BASTA CHE CI SIA «chunked», anche in mezzo ad altro: la
                 * specifica permette un elenco, e «gzip, chunked» e' valido.
                 * Cercare l'uguaglianza esatta fallirebbe proprio li'. */
                {
                    unsigned int k, L = lung(t);

                    for (k = 0; k + 7 <= L; k++)
                        if (uguale_min((const unsigned char *)t + k, "chunked", 7))
                            r->a_pezzi = 1;
                }
            }
        }

        riga_a = riga_b;
        while (riga_a < fine && (d[riga_a] == '\r' || d[riga_a] == '\n')) {
            if (d[riga_a] == '\n') { riga_a++; break; }
            riga_a++;
        }
    }

    /* ! I PEZZI VINCONO SULLA LUNGHEZZA, e la specifica lo dice: se ci sono
     * tutt'e due, Content-Length si ignora. Un server che le manda entrambe e'
     * sospetto, ma la regola e' scritta e seguirla costa una riga. */
    if (r->a_pezzi) r->ha_lunghezza = 0;

    return (int)fine;
}

/* -----------------------------------------------------------------------------
 * Il corpo a pezzi
 * --------------------------------------------------------------------------- */
void http_pezzi_avvia(HttpPezzi *p)
{
    if (!p) return;
    p->stato   = HTTP_P_DIM;
    p->restano = 0;
    p->cifre   = 0;
    p->vuote   = 0;
}

int http_pezzi(HttpPezzi *p, const unsigned char *in, unsigned int n,
               unsigned int *consumati, unsigned char *out, unsigned int max)
{
    unsigned int i = 0, prodotti = 0;

    if (consumati) *consumati = 0;
    if (!p || !in || !out) return -1;

    while (i < n) {
        unsigned char c = in[i];

        switch (p->stato) {
        /* ! FINITA L'ESTENSIONE SI TORNA IN DIM SENZA CONSUMARE L'«\n», e non
         * si cade dentro il caso qui sotto: la decisione su cosa fare a fine
         * riga sta in un posto solo, e il giro successivo del ciclo ce la
         * porta con lo stesso byte in mano. Un «cade dentro» funzionerebbe
         * uguale e sarebbe una riga che il compilatore ha ragione a segnalare. */
        case HTTP_P_EXT:
            if (c == '\n') p->stato = HTTP_P_DIM;
            else            i++;
            continue;

        case HTTP_P_DIM:
            if (c == '\n') {
                /* ! UNA RIGA DI LUNGHEZZA SENZA CIFRE E' UN ERRORE, non un
                 * pezzo vuoto: senza questo controllo un flusso di soli «\n»
                 * verrebbe letto come infiniti pezzi da zero byte, cioe' un
                 * ciclo che non finisce mai. */
                if (p->cifre == 0) { p->stato = HTTP_P_ROTTO; return -1; }
                p->cifre = 0;
                p->stato = (p->restano == 0) ? HTTP_P_CODA : HTTP_P_DATI;
                if (p->stato == HTTP_P_CODA) p->vuote = 0;
                i++;
                continue;
            }
            if (c == '\r') { i++; continue; }

            /* Dopo la lunghezza puo' esserci un ';' con delle estensioni: si
             * saltano fino a fine riga, e SI RICORDA di star saltando. */
            if (c == ';') { p->stato = HTTP_P_EXT; i++; continue; }

            {
                unsigned int v;

                if (c >= '0' && c <= '9')      v = (unsigned int)(c - '0');
                else if (c >= 'a' && c <= 'f') v = (unsigned int)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v = (unsigned int)(c - 'A' + 10);
                else if (c == ' ' || c == '\t') { i++; continue; }
                else { p->stato = HTTP_P_ROTTO; return -1; }

                if (p->restano > HTTP_PEZZO_MAX / 16u) {
                    p->stato = HTTP_P_ROTTO;
                    return -1;
                }
                p->restano = p->restano * 16u + v;
                p->cifre++;
                if (p->cifre > 8) { p->stato = HTTP_P_ROTTO; return -1; }
                i++;
            }
            continue;

        case HTTP_P_DATI: {
            unsigned int q = n - i;

            if (q > p->restano)    q = p->restano;
            if (q > max - prodotti) q = max - prodotti;
            if (q == 0) goto fuori;         /* out e' pieno: si torna dopo */

            {
                unsigned int k;
                for (k = 0; k < q; k++) out[prodotti + k] = in[i + k];
            }
            prodotti   += q;
            i          += q;
            p->restano -= q;

            if (p->restano == 0) p->stato = HTTP_P_FINE_R;
            continue;
        }

        case HTTP_P_FINE_R:
            if (c == '\r') { p->stato = HTTP_P_FINE_N; i++; continue; }
            if (c == '\n') { p->stato = HTTP_P_DIM; i++; continue; }
            p->stato = HTTP_P_ROTTO;
            return -1;

        case HTTP_P_FINE_N:
            if (c != '\n') { p->stato = HTTP_P_ROTTO; return -1; }
            p->stato = HTTP_P_DIM;
            i++;
            continue;

        case HTTP_P_CODA:
            /* ! LE INTESTAZIONI FINALI SI BUTTANO, e vanno comunque LETTE
             * fino alla riga vuota: il corpo non e' finito quando arriva lo
             * zero, e' finito quando finisce la coda. Fermarsi allo zero
             * lascerebbe dei byte nel socket, che con una connessione tenuta
             * viva diventerebbero l'inizio della risposta successiva. */
            if (c == '\n') {
                p->vuote++;
                if (p->vuote >= 1 && p->cifre == 0) { p->stato = HTTP_P_FATTO; i++; goto fuori; }
                p->cifre = 0;
                i++;
                continue;
            }
            if (c != '\r') p->cifre = 1;        /* la riga ha del contenuto */
            i++;
            continue;

        case HTTP_P_FATTO:
            goto fuori;

        default:
            return -1;
        }
    }

fuori:
    if (consumati) *consumati = i;
    return (int)prodotti;
}
