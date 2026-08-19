/* =============================================================================
 * lib/exhttp/exhttp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La meta' dell'HTTP che parla con la rete. Il perche' del trasporto astratto
 * sta in exhttp.h; qui c'e' il trasporto TCP e il giro della richiesta.
 * ============================================================================= */

#include "libc.h"
#include "rete.h"
#include "dns.h"
#include "ip_proto.h"
#include "exhttp.h"

/* -----------------------------------------------------------------------------
 * Il trasporto TCP
 *
 * ! LO STACK SI CERCA UNA VOLTA SOLA E SI RICORDA. rete_richiedi() stampa da se'
 * le istruzioni quando manca un pezzo della catena, e stamparle a ogni immagine
 * di una pagina vorrebbe dire riempire lo schermo di consigli identici.
 * --------------------------------------------------------------------------- */
static int g_pid_ip = 0;

typedef struct {
    int id;                 /* la connessione, lato stack */
} TcpStato;

/* ! UNO SOLO PER VOLTA, E VA DETTO. Il browser di oggi scarica una pagina alla
 * volta; il giorno che ne vorra' quattro insieme questo diventera' un vettore,
 * e sara' una modifica di tre righe perche' il trasporto e' gia' una struttura
 * a parte. Meglio un limite dichiarato di un'allocazione che non serve. */
static TcpStato g_tcp;

static int ip_pronto(void)
{
    if (g_pid_ip > 0) return 1;
    g_pid_ip = rete_richiedi(IP_SERVIZIO);
    return g_pid_ip > 0;
}

static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;
    int        i;

    /* ! SI SCARTANO I MESSAGGI DI ALTRI, NON SI RIFIUTANO: la mailbox e' una
     * sola per processo, e un'applicazione grafica ci riceve anche gli eventi
     * del server a finestre. Prendere il primo che arriva vorrebbe dire
     * leggere un clic del mouse come se fosse una risposta dello stack. */
    for (i = 0; i < 64; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != g_pid_ip) continue;
        if (meta.tipo != tipo) continue;
        if (len) *len = meta.len;
        return 0;
    }
    return -1;
}

static int esito(unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpEsito       e;

    if (attendi(IP_MSG_ESITO, buf, &len, ms) != 0 || len < sizeof(e)) return -1;
    memcpy(&e, buf, sizeof(e));
    return e.codice;
}

/* =============================================================================
 * ! CIO' CHE LO STACK HA GIA' MANDATO E NESSUNO HA LETTO VA BUTTATO PRIMA DI
 * CHIEDERE ALTRO, e questo difetto e' costato una redirezione che non
 * funzionava.
 *
 * Il sintomo: la prima pagina arrivava, la seconda connessione rendeva -104,
 * cioe' ECONNRESET — che nello stack viene messo quando arriva un RST. Ma la
 * connessione NUOVA non aveva ricevuto nessun RST: quell'esito era della
 * VECCHIA, rimasto in mailbox dopo che il server aveva chiuso, e il TCP_APRI
 * successivo si prendeva la risposta sbagliata.
 *
 * ! GLI ESITI NON HANNO UN NUMERO DI RICHIESTA, ed e' la ragione per cui puo'
 * succedere: IP_MSG_ESITO dice «e' andata cosi'» e non a QUALE domanda. Finche'
 * e' cosi', l'unica difesa e' non lasciarne mai indietro. dns.c lo dice gia'
 * per conto suo: «la conferma della chiusura non interessa, ma va CONSUMATA».
 *
 * ! E QUESTO NON BASTERA' PER IL BROWSER. attendi() butta i messaggi che non
 * sono dello stack, e in un'applicazione GRAFICA quei messaggi sono gli eventi
 * del server a finestre: un clic mangiato mentre si scarica una pagina. Il
 * giorno che l'HTTP gira dentro una finestra servira' un numero di richiesta
 * nel protocollo, o una mailbox per servizio. Sta scritto qui perche' e' il
 * posto dove si scoprira'.
 * ============================================================================= */
static void svuota_stack(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    IpcMessage    meta;
    int           i;

    /* ! UN MILLISECONDO, NON ZERO: in questa libc `timeout_ms == 0` vuol dire
     * ATTESA SENZA SCADENZA, cioe' esattamente ipc_recv (vedi libc.h). Scritto
     * zero credendo di dire «non aspettare», questa funzione si e' piantata
     * per sempre alla prima mailbox vuota — e il sintomo era «scarica non
     * risponde piu'», che non somiglia a «ho svuotato la posta». */
    for (i = 0; i < 32; i++)
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 1) < 0) return;
}

static int tcp_leggi(void *st, unsigned char *dst, unsigned int max,
                     unsigned int ms)
{
    TcpStato     *s = (TcpStato *)st;
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     d;

    r.id = (unsigned int)s->id;
    if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0)
        return -1;
    if (attendi(IP_MSG_TCP_DATI, buf, &len, ms) != 0) return -1;
    if (len < sizeof(d)) return -1;

    memcpy(&d, buf, sizeof(d));
    if (d.len == 0) return 0;
    if (d.len > max) d.len = max;

    memcpy(dst, buf + sizeof(d), d.len);
    return (int)d.len;
}

static int tcp_scrivi(void *st, const unsigned char *src, unsigned int n)
{
    TcpStato     *s = (TcpStato *)st;
    unsigned char msg[sizeof(IpTcpDati) + IP_TCP_DATI_MAX];
    unsigned int  fatti = 0;

    while (fatti < n) {
        unsigned int q = n - fatti;
        IpTcpDati    d;
        int          rc;

        if (q > IP_TCP_DATI_MAX) q = IP_TCP_DATI_MAX;

        d.id  = (unsigned int)s->id;
        d.len = q;
        memcpy(msg, &d, sizeof(d));
        memcpy(msg + sizeof(d), src + fatti, q);

        if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_INVIA,
                     msg, sizeof(d) + q) < 0) return -1;

        rc = esito(5000);
        if (rc < 0) return -1;

        /* ! LO STACK PUO' PRENDERNE MENO DI QUANTI GLIENE SI DANNO, e il campo
         * lo dice: il suo buffer di trasmissione e' pieno. Dare per scontato
         * che li abbia presi tutti vuol dire una richiesta troncata a meta',
         * che il server non capisce e a cui non risponde. */
        if (rc == 0) usleep(20 * 1000);
        fatti += (unsigned int)rc;
    }
    return (int)fatti;
}

static void tcp_chiudi(void *st)
{
    TcpStato *s = (TcpStato *)st;
    IpTcpRif  r;

    if (s->id <= 0) return;
    r.id = (unsigned int)s->id;
    ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    (void)esito(2000);
    s->id = 0;
}

/* Un indirizzo scritto a cifre, o 0 se non lo e'. */
static int a_cifre(const char *host, unsigned char *ip)
{
    unsigned int v = 0, n = 0, cifre = 0;
    const char  *p = host;

    for (;;) {
        if (*p >= '0' && *p <= '9') {
            v = v * 10 + (unsigned int)(*p - '0');
            if (v > 255) return 0;
            cifre++;
        } else if (*p == '.' || *p == '\0') {
            if (cifre == 0 || n >= 4) return 0;
            ip[n++] = (unsigned char)v;
            v = 0; cifre = 0;
            if (*p == '\0') break;
        } else {
            return 0;
        }
        p++;
    }
    return n == 4;
}

/* ! L'ULTIMO ERRORE DELLO STACK SI TIENE, e non e' una comodita' per chi
 * scrive il codice: «non riesco a connettermi» non distingue un nome che non
 * si risolve da una porta chiusa da un ARP che non risponde, e sono tre cose
 * da riparare in tre posti diversi. */
static int g_ultimo_errore = 0;

int exhttp_tcp(ExHttpTrasporto *t, const char *host, unsigned int porta)
{
    unsigned char ip[4];
    IpTcpApri     a;
    int           id = -1, giri;

    g_ultimo_errore = 0;

    if (!t || !host) return 0;
    if (!ip_pronto()) { g_ultimo_errore = -1; return 0; }

    if (!a_cifre(host, ip)) {
        if (dns_risolvi(host, ip) != 0) { g_ultimo_errore = -2; return 0; }
    }

    /* Prima di chiedere, si butta cio' che e' rimasto: vedi svuota_stack. */
    svuota_stack();

    memcpy(a.ip, ip, 4);
    a.porta      = porta;
    a.timeout_ms = 8000;

    /* ! SI RITENTA SU -EAGAIN, e non e' un errore vero: vuol dire che l'ARP non
     * ha ancora la risposta per quell'indirizzo. E' la stessa attesa che fa
     * ftp, e senza di lei la prima connessione dopo l'avvio fallisce sempre. */
    for (giri = 0; giri < 10; giri++) {
        if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0)
            return 0;
        id = esito(10000);
        if (id != -EAGAIN) break;
        usleep(200 * 1000);
    }
    if (id <= 0) { g_ultimo_errore = id; return 0; }

    g_tcp.id  = id;
    t->stato  = &g_tcp;
    t->leggi  = tcp_leggi;
    t->scrivi = tcp_scrivi;
    t->chiudi = tcp_chiudi;
    return 1;
}

/* -----------------------------------------------------------------------------
 * Lo scambio
 * --------------------------------------------------------------------------- */
int exhttp_scambio(ExHttpTrasporto *t, const HttpUrl *u,
                   unsigned char *buf, unsigned int max, ExHttpEsito *e,
                   HttpRisposta *r)
{
    static unsigned char acc[16 * 1024];    /* le intestazioni, mentre arrivano */
    unsigned int  acc_n = 0;
    char          req[1024];
    int           n, fine = 0;
    HttpPezzi     pezzi;
    unsigned int  corpo_gia = 0;

    if (!t || !u || !buf || !e || !r) return 0;

    n = http_richiesta(req, sizeof(req), u, "EX-OS");
    if (n <= 0) { strcpy(e->errore, "richiesta troppo lunga"); return 0; }
    if (t->scrivi(t->stato, (const unsigned char *)req, (unsigned int)n) != n) {
        strcpy(e->errore, "non riesco a mandare la richiesta");
        return 0;
    }

    /* --- le intestazioni ---------------------------------------------- */
    for (;;) {
        if (acc_n >= sizeof(acc)) {
            strcpy(e->errore, "intestazioni troppo lunghe");
            return 0;
        }

        fine = http_intestazioni(acc, acc_n, r);
        if (fine < 0) { strcpy(e->errore, "risposta malformata"); return 0; }
        if (fine > 0) break;

        n = t->leggi(t->stato, acc + acc_n, sizeof(acc) - acc_n, 10000);
        if (n < 0) { strcpy(e->errore, "connessione interrotta"); return 0; }
        if (n == 0) { strcpy(e->errore, "chiusa senza rispondere"); return 0; }
        acc_n += (unsigned int)n;
    }

    e->codice = r->codice;
    strncpy(e->tipo, r->tipo, sizeof(e->tipo) - 1);
    e->tipo[sizeof(e->tipo) - 1] = '\0';

    /* ! CIO' CHE E' GIA' ARRIVATO DOPO LE INTESTAZIONI E' GIA' CORPO, e va
     * usato invece di rileggerlo: la prima lettura dal socket porta quasi
     * sempre le intestazioni E l'inizio del corpo nello stesso blocco.
     * Buttarlo vorrebbe dire una pagina a cui manca il primo pezzo. */
    corpo_gia = acc_n - (unsigned int)fine;

    http_pezzi_avvia(&pezzi);
    e->byte = 0;
    e->troncata = 0;

    /* --- il corpo ------------------------------------------------------ */
    {
        static unsigned char blocco[4096];
        const unsigned char *resto   = acc + fine;
        unsigned int         resto_n = corpo_gia;

        for (;;) {
            /* Prima si consuma cio' che si ha gia' in mano. */
            while (resto_n > 0) {
                if (r->a_pezzi) {
                    unsigned int usati = 0;
                    int prodotti = http_pezzi(&pezzi, resto, resto_n, &usati,
                                              buf + e->byte, max - e->byte);

                    if (prodotti < 0) {
                        strcpy(e->errore, "corpo a pezzi malformato");
                        return 0;
                    }
                    e->byte += (unsigned int)prodotti;
                    resto   += usati;
                    resto_n -= usati;

                    /* Uscita piena e niente consumato: si tronca e si smette. */
                    if (usati == 0 && prodotti == 0) { e->troncata = 1; return 1; }
                } else {
                    unsigned int q = resto_n;

                    if (q > max - e->byte) { q = max - e->byte; e->troncata = 1; }
                    memcpy(buf + e->byte, resto, q);
                    e->byte += q;
                    resto   += q;
                    resto_n -= q;

                    if (e->troncata) return 1;
                }
            }

            /* =============================================================
             * ! «HO FINITO?» SI CHIEDE QUI, FUORI DAL CICLO CHE CONSUMA, e
             * questo difetto e' costato una redirezione. La domanda stava
             * DENTRO quel ciclo: con un corpo VUOTO — «Content-Length: 0»,
             * che e' esattamente cio' che manda un 301 — il ciclo non gira
             * nemmeno una volta e la domanda non si faceva mai. Il risultato
             * era aspettare dieci secondi che il server chiudesse, e poi
             * aprire la connessione successiva mentre la prima si stava
             * ancora spegnendo.
             * ============================================================= */
            if (r->a_pezzi) {
                if (pezzi.stato == HTTP_P_FATTO) return 1;
            } else if (r->ha_lunghezza) {
                if (e->byte >= r->lunghezza) return 1;
            }

            /* ! SENZA LUNGHEZZA NE' PEZZI, LA FINE E' LA CHIUSURA, ed e'
             * legittimo: HTTP/1.0 fa cosi', e con «Connection: close» anche
             * l'1.1 puo'. Percio' una lettura che rende 0 non e' un errore. */
            n = t->leggi(t->stato, blocco, sizeof(blocco), 10000);
            if (n <= 0) break;

            resto   = blocco;
            resto_n = (unsigned int)n;
        }
    }

    return 1;
}

/* -----------------------------------------------------------------------------
 * Il giro completo, redirezioni comprese
 * --------------------------------------------------------------------------- */

/* Risolve una posizione relativa contro l'URL da cui si viene. */
static void unisci_url(const HttpUrl *da, const char *pos, char *out,
                       unsigned int max)
{
    if (pos[0] == 'h' || pos[0] == 'H') {       /* http:// o https:// */
        strncpy(out, pos, max - 1);
        out[max - 1] = '\0';
        return;
    }

    /* ! UNA POSIZIONE PUO' ESSERE RELATIVA, e i server la mandano cosi' piu'
     * spesso di quanto si creda. Trattarla come assoluta darebbe un host che
     * comincia per «/», cioe' un URL che non si risolve e una redirezione che
     * sembra rotta. */
    strncpy(out, da->cifrato ? "https://" : "http://", max - 1);
    out[max - 1] = '\0';
    strncat(out, da->host, max - strlen(out) - 1);

    /* ! E LA PORTA VA RIMESSA, ALTRIMENTI SI RIPARTE DALLA 80. Ricostruendo
     * l'indirizzo assoluto dall'host e basta, una redirezione relativa su una
     * porta non prevista finisce da un'altra parte: il pacchetto se ne andava
     * a 10.0.2.2:80 invece che a :8099, e li' non ascoltava nessuno. Il
     * sintomo era «non riesco a connettermi (-104)» — cioe' un RST, che era la
     * verita': nessuno ascoltava. L'ha trovato il pcap, non la lettura. */
    if ((da->cifrato && da->porta != 443) || (!da->cifrato && da->porta != 80)) {
        char cifre[8], rov[8];
        unsigned int p = da->porta;
        int k = 0, r = 0;

        while (p) { rov[r++] = (char)('0' + (p % 10)); p /= 10; }
        while (r) cifre[k++] = rov[--r];
        cifre[k] = '\0';

        strncat(out, ":", max - strlen(out) - 1);
        strncat(out, cifre, max - strlen(out) - 1);
    }

    if (pos[0] == '/') {
        strncat(out, pos, max - strlen(out) - 1);
    } else {
        /* Relativa alla directory dell'URL di partenza. */
        char base[HTTP_PERCORSO_MAX];
        int  i;

        strncpy(base, da->percorso, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        i = (int)strlen(base);
        while (i > 0 && base[i - 1] != '/') i--;
        base[i] = '\0';

        strncat(out, base, max - strlen(out) - 1);
        strncat(out, pos,  max - strlen(out) - 1);
    }
}

int exhttp_prendi(const char *url, unsigned char *buf, unsigned int max,
                  ExHttpEsito *e)
{
    char adesso[EXHTTP_URL_MAX];
    int  salto;

    if (!url || !buf || !e || max == 0) return 0;

    memset(e, 0, sizeof(*e));
    strncpy(adesso, url, sizeof(adesso) - 1);
    adesso[sizeof(adesso) - 1] = '\0';

    for (salto = 0; salto <= EXHTTP_SALTI_MAX; salto++) {
        HttpUrl         u;
        HttpRisposta    r;
        ExHttpTrasporto t;
        int             ok;

        e->salti = salto;
        strncpy(e->finale, adesso, sizeof(e->finale) - 1);
        e->finale[sizeof(e->finale) - 1] = '\0';

        if (!http_url(adesso, &u)) { strcpy(e->errore, "indirizzo illeggibile"); return 0; }

        /* ! https:// SI RIFIUTA IN CHIARO, E LO DICE. Aprire una connessione
         * TCP alla porta 443 e parlarci HTTP darebbe una risposta
         * incomprensibile e un messaggio che non c'entra niente: meglio dire
         * che il TLS non c'e' ancora. */
        if (u.cifrato) {
            strcpy(e->errore, "https non ancora: manca il TLS");
            return 0;
        }

        if (!exhttp_tcp(&t, u.host, u.porta)) {
            if (g_ultimo_errore == -1)
                strcpy(e->errore, "la rete non e' pronta");
            else if (g_ultimo_errore == -2)
                strcpy(e->errore, "il nome non si risolve");
            else
                sprintf(e->errore, "non riesco a connettermi (%d)",
                        g_ultimo_errore);
            return 0;
        }

        ok = exhttp_scambio(&t, &u, buf, max, e, &r);
        t.chiudi(t.stato);
        if (!ok) return 0;

        /* Una redirezione: si rifa' il giro con la posizione nuova. */
        if ((r.codice == 301 || r.codice == 302 || r.codice == 303 ||
             r.codice == 307 || r.codice == 308) && r.posizione[0]) {
            char nuovo[EXHTTP_URL_MAX];

            unisci_url(&u, r.posizione, nuovo, sizeof(nuovo));
            strncpy(adesso, nuovo, sizeof(adesso) - 1);
            adesso[sizeof(adesso) - 1] = '\0';
            continue;
        }

        return 1;
    }

    strcpy(e->errore, "troppe redirezioni");
    return 0;
}
