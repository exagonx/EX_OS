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
#include "extls.h"
#include "excert.h"    /* excert_perche: QUALE dei nove casi, non solo «non va» */

/* -----------------------------------------------------------------------------
 * Il trasporto TCP
 *
 * ! LO STACK SI CERCA UNA VOLTA SOLA E SI RICORDA. rete_richiedi() stampa da se'
 * le istruzioni quando manca un pezzo della catena, e stamparle a ogni immagine
 * di una pagina vorrebbe dire riempire lo schermo di consigli identici.
 * --------------------------------------------------------------------------- */
static int g_pid_ip = 0;

/* ! IL RESTO DI UN PEZZO SI TIENE, E FINO AL 25 AGOSTO 2026 SI BUTTAVA. Lo
 * stack consegna i dati a PEZZI — quello che e' arrivato in un segmento TCP —
 * e chi legge chiede quanti byte gli servono. Finche' sopra c'era solo l'HTTP
 * la cosa non si vedeva: l'HTTP chiede sempre un buffer grande, quindi il
 * pezzo ci stava sempre tutto.
 *
 * Il record di TLS no. Legge PRIMA cinque byte — l'intestazione, che dice
 * quanto e' lungo il resto — e poi il resto. Con un `if (len > max) len = max`
 * quei cinque byte arrivavano e tutto il resto del segmento spariva: la
 * lunghezza letta dopo era un numero preso da byte a caso, e usciva
 * «messaggio troppo grande» su un server che aveva risposto benissimo.
 *
 * Adesso l'avanzo resta qui e si consegna alla lettura dopo. E' cio' che
 * rende questo trasporto un FLUSSO invece di una fila di pacchetti — che e'
 * quello che TCP e' sempre stato, e che l'interfaccia a messaggi nascondeva. */
typedef struct {
    int           id;              /* la connessione, lato stack */
    unsigned char avanzo[IPC_MSG_MAX_DATA];
    unsigned int  a_pos, a_fine;
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

/* =============================================================================
 * ! I MESSAGGI DEGLI ALTRI NON SI TOCCANO, e non e' piu' questo file a
 * doversene ricordare. La mailbox e' una sola per processo: un'applicazione
 * grafica ci riceve gli eventi del server a finestre insieme alle risposte
 * dello stack. Fino al 3 settembre 2026 qui dentro si SCORREVA la cassetta, si
 * metteva da parte in un vettore locale cio' che non era nostro, e alla fine lo
 * si rimetteva sullo scaffale della libc — quattro posti, e se lo scaffale non
 * era vuoto i messaggi in piu' sparivano senza che nessuno guardasse il -1.
 *
 * Adesso il filtro lo fa `ipc_scegli`: cio' che non e' nostro RESTA DOV'E' e
 * non passa mai per le nostre mani, quindi non lo possiamo perdere. Il perche'
 * per esteso sta in libc.h, sopra ipc_scegli.
 *
 * ! LE TRE RISPOSTE DEL FILTRO, E PERCHE' PROPRIO QUESTE:
 *
 *   IPC_MIO      la risposta che si aspettava.
 *
 *   IPC_ALTRUI   quel che non viene dallo stack — gli eventi della finestra —
 *                e i IP_MSG_TCP_DATI. I dati sono ALTRUI e non BUTTA perche'
 *                lo stack quei byte li ha gia' tolti dalla sua coda per
 *                consegnarceli: nessuno li rimandera' piu', e chi aspettava
 *                quella lettura aspetterebbe all'infinito senza un errore da
 *                nessuna parte. Aspettano `tcp_leggi`, che e' il loro padrone.
 *
 *   IPC_BUTTA    il resto di cio' che viene dallo stack: uno STATO, un ESITO,
 *                un'INFO in ritardo. Sono risposte che si possono RICHIEDERE,
 *                e tenerle vorrebbe dire far combaciare la risposta di ieri
 *                con la domanda di oggi — il difetto della redirezione che
 *                rendeva -104, raccontato sopra svuota_stack.
 *
 * ! LA DISTINZIONE LA FA IL TIPO, ed e' l'unica che si puo' fare: il protocollo
 * non numera le domande. Il giorno che le numerera', questo filtro diventa un
 * confronto fra due interi e tutta la prudenza qui sopra se ne va con lui.
 * ============================================================================= */
static int filtro_stack(const IpcMessage *m, void *dato)
{
    unsigned int voluto = *(const unsigned int *)dato;

    if ((int)m->sender_pid != g_pid_ip)  return IPC_ALTRUI;
    if (m->tipo == voluto)               return IPC_MIO;
    if (m->tipo == IP_MSG_TCP_DATI)      return IPC_ALTRUI;
    return IPC_BUTTA;
}

static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;

    if (ipc_scegli(filtro_stack, &tipo, &meta, buf, IPC_MSG_MAX_DATA, ms) < 0)
        return -1;

    if (len) *len = meta.len;
    return 0;
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
 * ! E QUI STAVA SCRITTO «QUESTO NON BASTERA' PER IL BROWSER», ed era vero: si
 * buttava tutto quel che non era dello stack, e in un'applicazione GRAFICA
 * quello e' un clic mangiato a ogni immagine. Adesso si butta solo cio' che
 * viene dallo stack — il resto resta sullo scaffale per chi lo aspetta — e la
 * frase che diceva «servira' un numero di richiesta nel protocollo, o una
 * mailbox per servizio» resta valida per l'altra meta' del problema: senza un
 * numero di richiesta, la sola difesa contro la risposta di ieri e' non
 * lasciarne mai indietro, cioe' questa funzione.
 * ============================================================================= */
/* Qui non c'e' niente di «mio»: si chiama per BUTTARE, e cio' che non e' dello
 * stack resta sullo scaffale per chi lo aspetta. */
static int filtro_pulisci(const IpcMessage *m, void *dato)
{
    (void)dato;
    return (int)m->sender_pid == g_pid_ip ? IPC_BUTTA : IPC_ALTRUI;
}

static void svuota_stack(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    IpcMessage    meta;

    /* ! UN MILLISECONDO, NON ZERO: in questa libc `timeout_ms == 0` vuol dire
     * ATTESA SENZA SCADENZA, cioe' esattamente ipc_recv (vedi libc.h). Scritto
     * zero credendo di dire «non aspettare», questa funzione si e' piantata
     * per sempre alla prima mailbox vuota — e il sintomo era «scarica non
     * risponde piu'», che non somiglia a «ho svuotato la posta».
     *
     * ! E RENDE SEMPRE SCADUTO, perche' il filtro non dice MIO a nessuno: e'
     * il modo di dire «scorri tutto quel che c'e' e poi torna». */
    ipc_scegli(filtro_pulisci, 0, &meta, buf, sizeof(buf), 1);
}

static int tcp_leggi(void *st, unsigned char *dst, unsigned int max,
                     unsigned int ms)
{
    TcpStato     *s = (TcpStato *)st;
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     d;

    /* Prima si finisce quello che era gia' arrivato. */
    if (s->a_pos < s->a_fine) {
        unsigned int q = s->a_fine - s->a_pos;

        if (q > max) q = max;
        memcpy(dst, s->avanzo + s->a_pos, q);
        s->a_pos += q;
        return (int)q;
    }

    r.id = (unsigned int)s->id;
    if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0)
        return -1;
    if (attendi(IP_MSG_TCP_DATI, buf, &len, ms) != 0) return -1;
    if (len < sizeof(d)) return -1;

    memcpy(&d, buf, sizeof(d));
    if (d.len == 0) return 0;
    if (d.len > IPC_MSG_MAX_DATA - sizeof(d)) return -1;

    if (d.len <= max) {
        memcpy(dst, buf + sizeof(d), d.len);
        return (int)d.len;
    }

    /* Ne e' arrivato piu' di quanto ne siano stati chiesti: il resto aspetta. */
    memcpy(dst, buf + sizeof(d), max);
    s->a_fine = d.len - max;
    s->a_pos  = 0;
    memcpy(s->avanzo, buf + sizeof(d) + max, s->a_fine);
    return (int)max;
}

/* =============================================================================
 * ! QUANTI BYTE CI SONO ADESSO, SENZA ASPETTARE — e senza confondere «non e'
 * ancora arrivato niente» con «l'altro ha chiuso».
 *
 * La lettura, di suo, quella differenza non la sa dire: `tcp_leggi` rende -1
 * per un timeout e -1 per un errore, e 0 solo quando la connessione e' finita.
 * Finche' si aspettava e basta non importava; da quando chi legge vuole
 * ANDARSENE A FARE ALTRO mentre non c'e' niente, e' la domanda centrale.
 *
 * ! LA RISPOSTA VIENE DALLO STACK, non da un tentativo di lettura. IP_MSG_TCP_STATO
 * rende `in_coda_rx` — i byte gia' arrivati e non ancora letti — e lo stato
 * della connessione. Provare a leggere con un timeout corto sarebbe sembrato
 * piu' semplice e sarebbe stato sbagliato due volte: il timeout non si
 * distingue dall'errore, e ogni tentativo lascia una prenotazione pendente
 * dentro lo stack (vedi ip_proto.h: «niente viene spinto»).
 * ========================================================================== */
static int tcp_quanti(void *st)
{
    TcpStato     *s = (TcpStato *)st;
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpInfo     info;

    /* Quel che e' avanzato da una lettura di prima e' gia' in mano nostra. */
    if (s->a_pos < s->a_fine) return (int)(s->a_fine - s->a_pos);

    r.id = (unsigned int)s->id;
    if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_STATO, &r, sizeof(r)) < 0)
        return -1;
    if (attendi(IP_MSG_TCP_INFO, buf, &len, 2000) != 0) return -1;
    if (len < sizeof(info)) return -1;
    memcpy(&info, buf, sizeof(info));

    if (info.in_coda_rx > 0) return (int)info.in_coda_rx;

    /* ! UNA CONNESSIONE CHIUSA CON DEI BYTE ANCORA IN CODA NON E' FINITA: si
     * legge prima quel che c'e' — ed e' il motivo per cui questo controllo sta
     * DOPO. Con l'ordine rovesciato l'ultimo pezzo di una pagina sparirebbe
     * proprio sulle risposte che finiscono con la chiusura, cioe' quelle senza
     * Content-Length. */
    if (info.stato != IP_TCP_APERTA) return -1;
    return 0;
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

    g_tcp.id    = id;
    g_tcp.a_pos = g_tcp.a_fine = 0;
    t->stato    = &g_tcp;
    t->leggi  = tcp_leggi;
    t->quanti = tcp_quanti;
    t->scrivi = tcp_scrivi;
    t->chiudi = tcp_chiudi;
    return 1;
}

/* =============================================================================
 * https: lo stesso HTTP, dentro un tubo cifrato
 *
 * ! IL TLS STA SOTTO L'HTTP E NON SI VEDE DA SOPRA, ed e' il motivo per cui
 * questo file poteva accoglierlo senza riscritture: `exhttp_scambio` parla con
 * un ExHttpTrasporto e non ha mai saputo cosa ci sia sotto. Qui si costruisce
 * un trasporto le cui `leggi` e `scrivi` passano per lib/extls, e tutto il
 * resto — richieste, intestazioni, pezzi, redirezioni — resta com'era.
 *
 * ! IL MAGAZZINO DELLE CA SI CARICA UNA VOLTA SOLA E RESTA. Sono duecento
 * certificati e un paio di centinaia di kilobyte: rileggerli a ogni immagine
 * di una pagina vorrebbe dire rileggere il CD venti volte per aprire un sito.
 *
 * ! E SENZA MAGAZZINO NON SI APRE NIENTE. Non c'e' una modalita' «cifra e
 * fidati»: cifrare con chiunque risponda vuol dire cifrare con chi sta in
 * mezzo, e in quel caso il lucchetto dice una bugia a chi lo guarda.
 * ============================================================================= */

/* Dove cercarlo: prima il sistema installato, poi il CD. */
static const char *CA_DOVE[] = {
    "/exos/ssl/certi.pem",
    "/cdrom/exos/ssl/certi.pem",
    0
};

static ExMagazzino  *g_magazzino = 0;
static unsigned char *g_der      = 0;
static char           g_tls_errore[96];

/* La stessa struttura serve da stato del trasporto: dentro c'e' il TLS e
 * sotto di lui il TCP. */
/* ! IL TRASPORTO DI SOTTO DEVE VIVERE QUANTO LA CONNESSIONE, E STA QUI.
 * `extls_stretta` non copia la struttura: si TIENE IL PUNTATORE, e lo usa a
 * ogni record per tutta la vita della connessione. Passandogli una variabile
 * locale di exhttp_tls, quel puntatore restava appeso allo stack di una
 * funzione gia' uscita.
 *
 * ! E PER MESI HA FUNZIONATO LO STESSO, che e' la parte peggiore: la
 * connessione si usava subito dopo la stretta, dalla stessa profondita' di
 * stack, e quei byte erano ancora quelli giusti. Il difetto e' comparso il
 * giorno del RIUSO — la seconda immagine di una pagina, presa piu' tardi da
 * un'altra parte del programma, leggeva funzioni da spazzatura e saltava
 * dentro il nulla. Un puntatore appeso non si vede finche' non cambia chi
 * cammina sopra quella memoria. */
typedef struct {
    void            *tls;
    ExHttpTrasporto  tcp;
    ExTlsSotto       sotto;     /* quello che extls tiene per riferimento */
} StatoTls;

static StatoTls g_stato_tls;

static void casuale(unsigned char *b, unsigned int n)
{
    /* ! SE getrandom NON RIEMPIE, NON SI INVENTA NIENTE. Byte prevedibili in
     * uno scambio di chiavi non danno un errore: danno una connessione che
     * sembra cifrata e non lo e'. Si azzera tutto, la chiave pubblica esce
     * degenere e la stretta fallisce — che e' il comportamento giusto. */
    if (getrandom(b, n, 0) != (ssize_t)n) memset(b, 0, n);
}

static int magazzino_carica(void)
{
    int   fd = -1, i;
    long  dim = 0;
    char *pem;
    int   quanti;

    if (g_magazzino) return 1;

    for (i = 0; CA_DOVE[i]; i++) {
        fd = open(CA_DOVE[i], O_RDONLY);
        if (fd >= 0) break;
    }
    if (fd < 0) {
        strcpy(g_tls_errore, "manca il magazzino delle CA (exos/ssl/certi.pem)");
        return 0;
    }

    dim = fsize(fd);
    if (dim <= 0 || dim > 4 * 1024 * 1024) {
        close(fd);
        strcpy(g_tls_errore, "il magazzino delle CA non si legge");
        return 0;
    }

    pem = (char *)malloc((unsigned int)dim);
    g_der = (unsigned char *)malloc((unsigned int)dim);
    g_magazzino = (ExMagazzino *)malloc(sizeof(ExMagazzino));
    if (!pem || !g_der || !g_magazzino) {
        close(fd);
        free(pem);
        strcpy(g_tls_errore, "non c'e' memoria per il magazzino delle CA");
        g_magazzino = 0;
        return 0;
    }

    /* ! SI LEGGE IN UN CICLO. Una read sola puo' rendere meno di quanto si e'
     * chiesto — e su un file di duecento kilobyte lo fa — e il pezzo mancante
     * diventerebbe un certificato tagliato a meta' invece di un errore. */
    {
        long fatti = 0;

        while (fatti < dim) {
            int r = (int)read(fd, pem + fatti, (unsigned int)(dim - fatti));

            if (r <= 0) break;
            fatti += r;
        }
        close(fd);
        dim = fatti;
    }

    quanti = extls_magazzino_pem(g_magazzino, pem, (unsigned int)dim,
                                 g_der, (unsigned int)dim, 0);
    free(pem);

    if (quanti <= 0) {
        free(g_der); free(g_magazzino);
        g_der = 0; g_magazzino = 0;
        strcpy(g_tls_errore, "il magazzino delle CA e' vuoto");
        return 0;
    }
    return 1;
}

static int tls_leggi(void *stato, unsigned char *dst, unsigned int max,
                     unsigned int ms)
{
    return extls_leggi(((StatoTls *)stato)->tls, dst, max, ms);
}

static int tls_scrivi(void *stato, const unsigned char *src, unsigned int n)
{
    return extls_scrivi(((StatoTls *)stato)->tls, src, n);
}

/* ! PRIMA IL CHIARO GIA' DECIFRATO, POI IL TUBO SOTTO. Un record intero puo'
 * essere gia' arrivato e decifrato e stare li' in attesa che qualcuno lo
 * legga: guardando solo il TCP non ci si troverebbe niente, e chi aspetta se
 * ne andrebbe a fare altro per sempre — con la risposta gia' in casa. */
static int tls_quanti(void *stato)
{
    StatoTls *s = (StatoTls *)stato;
    int       q = extls_pronto(s->tls);

    if (q != 0) return q;
    /* ! E QUEL CHE C'E' NEL TCP PUO' NON BASTARE A UN RECORD INTERO. Allora la
     * lettura aspetta il resto — un record e' al piu' sedici kilobyte e il
     * seguito arriva subito dopo — ed e' un'attesa corta e dichiarata. */
    return s->tcp.quanti ? s->tcp.quanti(s->tcp.stato) : 0;
}

static void tls_chiudi(void *stato)
{
    StatoTls *s = (StatoTls *)stato;

    extls_chiudi(s->tls);
    s->tcp.chiudi(s->tcp.stato);
}

static int leggi_a_pezzi(ExHttpTrasporto *t, unsigned char *dst,
                         unsigned int max, unsigned int ms, int *annullata);

/* ! LA LETTURA DELLA STRETTA: la stessa del resto, col gancio dell'attesa in
 * mezzo. Non si passa direttamente `leggi_a_pezzi` perche' la stretta legge da
 * ExTlsSotto, che ha la sua firma — ed e' giusto che l'abbia: extls non deve
 * sapere che esiste exhttp. */
static int stretta_leggi(void *stato, unsigned char *dst, unsigned int max,
                         unsigned int ms)
{
    (void)stato;
    return leggi_a_pezzi(&g_stato_tls.tcp, dst, max, ms, 0);
}

/* =============================================================================
 * A CHE PUNTO E' LA STRETTA — il ponte fra extls e chi ospita
 *
 * ! IL NOME DEL PASSO ARRIVA DA extls, LA DECISIONE DA CHI OSPITA. Qui in mezzo
 * non si decide niente: si traduce un numero in una frase e si passa la
 * risposta indietro. Senza questo ponte il browser dovrebbe collegarsi a extls
 * per una costante, e non ha nessun altro motivo di conoscerla.
 * ========================================================================== */
static ExHttpPasso  g_passo      = 0;
static void        *g_passo_dato = 0;

static int passo_ponte(void *dato, int p)
{
    (void)dato;
    if (!g_passo) return 1;
    return g_passo(g_passo_dato, extls_passo_nome(p)) ? 1 : 0;
}

void exhttp_passo(ExHttpPasso f, void *dato)
{
    g_passo      = f;
    g_passo_dato = dato;
    extls_passo_metti(f ? passo_ponte : 0, 0);
}

/* L'ora, nella forma che le date dei certificati vogliono. Se l'orologio non
 * risponde si prende una data che fa passare tutto tranne i certificati
 * scaduti da anni: meglio di rifiutare ogni sito su una macchina senza
 * batteria. */
static void adesso_in(char *out, unsigned int max)
{
    time_t     ora = time(0);
    struct tm *g;

    if (ora == (time_t)-1 || (g = gmtime(&ora)) == 0) {
        strncpy(out, "20260101000000Z", max - 1);
        out[max - 1] = 0;
        return;
    }
    snprintf(out, max, "%04d%02d%02d%02d%02d%02dZ",
             g->tm_year + 1900, g->tm_mon + 1, g->tm_mday,
             g->tm_hour, g->tm_min, g->tm_sec);
}

static int exhttp_tls(ExHttpTrasporto *t, const char *host, unsigned int porta)
{
    char       adesso[24];
    int        r;

    g_tls_errore[0] = 0;

    if (!magazzino_carica()) return 0;

    if (!exhttp_tcp(&g_stato_tls.tcp, host, porta)) return 0;

    if (!g_stato_tls.tls) {
        g_stato_tls.tls = malloc(extls_misura());
        if (!g_stato_tls.tls) {
            g_stato_tls.tcp.chiudi(g_stato_tls.tcp.stato);
            strcpy(g_tls_errore, "non c'e' memoria per la connessione cifrata");
            return 0;
        }
    }

    g_stato_tls.sotto.stato  = g_stato_tls.tcp.stato;
    /* =====================================================================
     * ! ANCHE LA STRETTA LEGGE A PEZZI, dal 4 settembre 2026 — e per tre volte
     * prima di quel giorno era stata scritta, provata e TOLTA, perche' con
     * questa riga dentro le pagine https smettevano di aprirsi: ci si fermava
     * dentro la stretta, senza un errore da nessuna parte.
     *
     * ! LA CAUSA NON ERA QUI, ED ERANO DUE. Nessuna delle due sta in questo
     * file, ed e' il motivo per cui cercarla qui non ha mai reso niente:
     *
     *   1. LA CASSETTA POSTALE. Mentre la stretta legge ci sono DUE
     *      consumatori sulla stessa mailbox — exhttp che aspetta lo stack e il
     *      ciclo dei messaggi che aspetta il server a finestre — e ognuno dei
     *      due, cercando il proprio, scorreva la cassetta e teneva in mano
     *      quel che era dell'altro. Chi tiene in mano puo' lasciar cadere: lo
     *      scaffale dove si rimetteva aveva quattro posti e nessuno guardava
     *      il -1 di quando era pieno. Adesso la scelta la fa `ipc_scegli`
     *      (vedi libc.h): quel che non e' di chi legge non passa MAI per le
     *      sue mani.
     *
     *   2. LA FINESTRA DI TCP CHE NON SI RIAPRIVA. Chi legge chiedendo «quanti
     *      byte ci sono?» invece di prenotare legge a SCATTI, e fra uno
     *      scatto e l'altro un mittente veloce riempie i quattro kilobyte del
     *      buffer di ricezione. Lo stack annunciava la finestra solo dentro un
     *      ACK — cioe' solo all'arrivo di un segmento — quindi quando poi si
     *      svuotava non aveva piu' modo di dirlo, e il server restava fermo ad
     *      aspettare noi. Sintomo: «quanti byte ci sono?» rispondeva ZERO per
     *      sempre. La cura sta in drivers/ip/ip.c, dentro tcp_consegna.
     *
     * Con tutt'e due sotto, questa riga si e' potuta rimettere.
     *
     * ! IL GUADAGNO E' MISURATO ED E' PICCOLO: la stretta e' 490 ms in tutto —
     * magazzino delle CA 60, DNS e connessione 320 — non i «venti secondi» che
     * stavano scritti in tre posti fino al 3 settembre. I sei PASSI la
     * intervallavano gia'; questo aggiunge il respiro DENTRO le attese di rete
     * di ogni passo.
     *
     * ! E DENTRO UN SINGOLO PASSO NON SI RESPIRA COMUNQUE. Un x25519 o una
     * verifica di firma sono un blocco solo: li' non si legge, si calcola.
     * Spezzarli vorrebbe dire portare un gancio dentro excurva e exbig, e con
     * 150 ms come pezzo piu' lungo non lo merita.
     * ================================================================== */
    g_stato_tls.sotto.leggi  = stretta_leggi;
    g_stato_tls.sotto.scrivi = g_stato_tls.tcp.scrivi;

    adesso_in(adesso, sizeof(adesso));

    r = extls_stretta(g_stato_tls.tls, &g_stato_tls.sotto, host, g_magazzino,
                      adesso, casuale);
    if (r != EXTLS_OK) {
        g_stato_tls.tcp.chiudi(g_stato_tls.tcp.stato);
        strncpy(g_tls_errore, extls_perche(r), sizeof(g_tls_errore) - 1);
        g_tls_errore[sizeof(g_tls_errore) - 1] = 0;

        /* ! E SE E' LA CATENA, SI DICE QUALE DEI NOVE CASI. extls tiene il
         * codice in `motivo` proprio per questo — sta scritto accanto alla
         * chiamata a excert_catena_valida — ma finora non lo leggeva nessuno,
         * e chi vedeva «certificato non verificabile» non aveva modo di sapere
         * se gli mancasse una CA o se avesse l'orologio sbagliato. */
        if (r == EXTLS_ERR_CERTIFICATO) {
            char coda[96];

            sprintf(coda, "%s: %s (anello %d)", extls_perche(r),
                    excert_perche(extls_motivo(g_stato_tls.tls)),
                    extls_anello(g_stato_tls.tls));
            strncpy(g_tls_errore, coda, sizeof(g_tls_errore) - 1);
            g_tls_errore[sizeof(g_tls_errore) - 1] = 0;
        }
        return 0;
    }

    t->stato  = &g_stato_tls;
    t->leggi  = tls_leggi;
    t->quanti = tls_quanti;
    t->scrivi = tls_scrivi;
    t->chiudi = tls_chiudi;
    return 1;
}

/* ! IL CORPO DI UN POST SI POSA QUI PRIMA DELLO SCAMBIO, e non passa per la
 * firma di exhttp_scambio: quella firma la usano `scarica`, il browser e le
 * immagini, e allargarla per un caso solo vorrebbe dire toccare tutti. Chi
 * manda un modulo chiama exhttp_posta(), che posa il corpo e lo toglie di
 * mezzo appena fatto — un POST non si ripete alla redirezione successiva, e
 * questa e' anche la regola dell'HTTP. */
static const char *g_corpo = 0;

/* =============================================================================
 * I BISCOTTI — exhttp non li tiene, li chiede e li consegna
 *
 * ! LA DISPENSA STA NEL BROWSER, e non qui, per la stessa ragione per cui la
 * rete di exdom sta nel browser: chi tiene i biscotti deve sapere che cosa e'
 * un dominio, quando una scadenza e' passata e dove si scrivono su disco.
 * Sono tre cose che una libreria di trasporto non ha motivo di sapere, e la
 * quarta e' che con la dispensa qui dentro non si potrebbe piu' provare un
 * giro di richieste senza portarsi dietro anche la dispensa.
 *
 * ! MA IL GIRO LO DEVE FARE exhttp, ed e' il motivo per cui non basta un
 * parametro. Una redirezione e' il caso normale dei biscotti: il server manda
 * 302 e `Set-Cookie` insieme, e la richiesta DOPO — quella che exhttp fa da
 * se', dentro la stessa chiamata — deve gia' portarselo. Con un parametro
 * passato una volta sola, il secondo salto partirebbe a mani vuote, che e'
 * esattamente il caso che si voleva sistemare.
 *
 * ! SENZA GANCIO NON CAMBIA NIENTE: niente `Cookie:` in uscita, i `Set-Cookie`
 * che arrivano si leggono e si buttano. Un programma che di biscotti non sa
 * niente — ftp, telnet, chi scarica un file — non deve accorgersi che esistono.
 * ========================================================================== */
/* =============================================================================
 * L'ATTESA — leggere a pezzi senza smettere di essere vivi
 *
 * ! IL PROBLEMA NON E' LEGGERE A PEZZI: TCP CONSEGNA GIA' A PEZZI. Il problema
 * e' che fra un pezzo e l'altro chi legge DORME dentro `leggi`, e mentre dorme
 * il programma che l'ha chiamato non risponde piu' a niente. Su una pagina da
 * un megabyte sono decine di secondi di finestra morta.
 *
 * ! LA CURA E' CHIEDERE PRIMA E ADDORMENTARSI POI. `t->quanti` dice quanti
 * byte ci sono ADESSO; se sono zero si chiama il gancio, chi ospita fa il suo
 * giro — ridisegna, risponde al mouse — e si riprova. Si dorme dentro `leggi`
 * solo quando c'e' qualcosa da leggere, cioe' per un istante.
 *
 * ! E IL GANCIO PUO' DIRE DI SMETTERE. Rendendo 0 annulla la richiesta: e' il
 * tasto Esc di chi si e' stufato di aspettare, ed e' una cosa che senza questo
 * giro non si poteva nemmeno offrire.
 *
 * ! SENZA GANCIO O SENZA `quanti` SI LEGGE COME SI E' SEMPRE LETTO. Un
 * programma che non ha un ciclo di messaggi — ftp, telnet, chi scarica un file
 * — non deve accorgersi che questo meccanismo esiste.
 *
 * ! IL TETTO DI TEMPO RESTA QUELLO DI PRIMA, dieci secondi: quando scadono si
 * fa la lettura vera, che fallisce e da' lo stesso errore di sempre. Cosi' il
 * comportamento di un server muto non cambia — cambia solo che nel frattempo
 * la finestra era viva.
 * ========================================================================== */
static ExHttpAttesa g_attesa      = 0;
static void        *g_attesa_dato = 0;

void exhttp_attesa(ExHttpAttesa f, void *dato)
{
    g_attesa      = f;
    g_attesa_dato = dato;
}

#define ATTESA_MS  10000

static int leggi_a_pezzi(ExHttpTrasporto *t, unsigned char *dst,
                         unsigned int max, unsigned int ms, int *annullata)
{
    unsigned int inizio;

    if (annullata) *annullata = 0;
    if (ms == 0) ms = ATTESA_MS;
    if (!g_attesa || !t->quanti)
        return t->leggi(t->stato, dst, max, ms);

    inizio = uptime_ms();
    for (;;) {
        int q = t->quanti(t->stato);

        /* C'e' roba, oppure e' finita: in tutt'e due i casi la lettura vera
         * risponde subito e sa dire quale dei due. */
        if (q != 0) return t->leggi(t->stato, dst, max, ms);

        if (!g_attesa(g_attesa_dato)) {
            if (annullata) *annullata = 1;
            return -1;
        }

        /* ! E SI DORME UN ISTANTE, O SI AFFAMA LO STACK. Senza questa riga il
         * ciclo chiede «quanti byte ci sono?» centinaia di volte al secondo:
         * ogni domanda e' un messaggio al processo che fa la rete, e quel
         * processo non arriva piu' a occuparsi dei pacchetti che ARRIVANO.
         * Il sintomo e' una pagina che non si apre mai, senza un errore da
         * nessuna parte — e non e' un caso di scuola, e' successo. Dieci
         * millisecondi costano al piu' dieci millisecondi di latenza per
         * lettura e riducono le domande a cento al secondo. */
        usleep(10000);

        /* ! SCADUTO IL TEMPO SI FA LA LETTURA VERA, e fallisce come sempre.
         * Rendere un errore da qui vorrebbe dire due strade per lo stesso
         * guasto, con due messaggi da tenere d'accordo. */
        if (uptime_ms() - inizio >= ms)
            return t->leggi(t->stato, dst, max, ms);
    }
}

static ExHttpBiscottiChiedi   g_bis_chiedi   = 0;
static ExHttpBiscottoArrivato g_bis_arrivato = 0;
static void                  *g_bis_dato     = 0;

void exhttp_biscotti(ExHttpBiscottiChiedi chiedi,
                     ExHttpBiscottoArrivato arrivato, void *dato)
{
    g_bis_chiedi   = chiedi;
    g_bis_arrivato = arrivato;
    g_bis_dato     = dato;
}

/* =============================================================================
 * LA CONNESSIONE CHE RESTA APERTA
 *
 * ! SU https LA STRETTA DI MANO E' TUTTO IL COSTO. Chiave effimera, catena di
 * certificati, firma. Una pagina di Wikipedia con dieci immagini la pagava
 * dieci volte, e la barra di stato diceva «immagine 9 di 9» a lungo —
 * sembrava bloccata, e in un certo senso lo era. *
 * ! IL NUMERO E' STATO MISURATO, E NON E' QUELLO CHE C'ERA SCRITTO. Dentro
 * EX-OS, in QEMU, verso google.com: magazzino delle CA 60 ms, DNS piu'
 * connessione 320 ms, stretta di mano 490 ms. Meno di un secondo in tutto —
 * non venti. La frase qui sopra e' di quando la connessione non si riusava e
 * la misura non si era mai fatta; si tiene, corretta, perche' e' quella che ha
 * fatto sembrare la stretta il problema principale piu' a lungo del dovuto.
 * Il ragionamento resta valido: la stretta e' comunque il pezzo caro, e
 * riusare la connessione e' comunque il guadagno. Solo, in secondi, e' UNO.
 *
 * ! SI RIUSA SOLO SE SI SA DOVE FINISCE IL CORPO. Con Content-Length o con i
 * pezzi si sa esattamente quando la risposta e' finita, e allora la
 * connessione e' pulita per la prossima. Senza, la fine E' la chiusura: non
 * c'e' niente da riusare, e provarci vorrebbe dire aspettare per sempre byte
 * che non arriveranno.
 *
 * ! E SI RIPROVA UNA VOLTA, SEMPRE. Una connessione tenuta aperta puo' essere
 * chiusa dall'altra parte in qualunque momento, senza dirlo: e' normale, non
 * e' un errore. Chi la riusa deve mettere in conto un fallimento e rifarla da
 * capo — senza, il primo timeout del server diventa una pagina che non si
 * apre.
 * ========================================================================== */
static ExHttpTrasporto g_vivo;
static int             g_vivo_c = 0;        /* 1 = ce n'e' una aperta */
static char            g_vivo_host[HTTP_HOST_MAX];
static unsigned int    g_vivo_porta = 0;
static int             g_vivo_cifrato = 0;
static int             g_riusabile = 0;     /* l'ultimo scambio l'ha lasciata pulita */

static void vivo_chiudi(void)
{
    if (!g_vivo_c) return;
    g_vivo.chiudi(g_vivo.stato);
    g_vivo_c = 0;
}

static int stesso_posto(const HttpUrl *u)
{
    int i;

    if (!g_vivo_c) return 0;
    if (g_vivo_porta != u->porta || g_vivo_cifrato != u->cifrato) return 0;

    for (i = 0; g_vivo_host[i] || u->host[i]; i++)
        if (g_vivo_host[i] != u->host[i]) return 0;
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
    /* ! DODICI KILOBYTE PERCHE' CI DEVE STARE IL CORPO DI UN POST, e i quattro
     * di prima non bastavano: il modulo di consenso di google.com fa quasi
     * ottomila byte da solo. Quando non ci sta si rende un errore — «richiesta
     * troppo lunga» — invece di mandarne meta', ed e' l'unica cosa da fare:
     * un corpo tagliato e' una richiesta che il server rifiuta, o peggio
     * accetta a meta'. */
    static char   req[12 * 1024];
    int           n, fine = 0, annullata = 0;
    HttpPezzi     pezzi;
    unsigned int  corpo_gia = 0;

    if (!t || !u || !buf || !e || !r) return 0;

    /* ! SI CHIEDE DI TENERLA APERTA SEMPRE, e si decide DOPO se si e' potuto:
     * il server risponde come vuole, e cio' che conta e' se il corpo ha una
     * fine dichiarata. Chiedere «keep-alive» a un server che chiude comunque
     * non costa niente. */
    g_riusabile = 0;
    {
        /* ! SI CHIEDONO A OGNI SALTO, non una volta per chiamata: dopo una
         * redirezione l'host puo' essere un altro, e i biscotti sono di chi
         * li ha messi. */
        static char bis[1024];

        bis[0] = '\0';
        if (g_bis_chiedi)
            g_bis_chiedi(g_bis_dato, u->host, u->percorso, u->cifrato,
                         bis, sizeof(bis));
        n = http_richiesta_corpo(req, sizeof(req), u, "EX-OS", g_corpo, 1, bis);
    }
    if (n <= 0) { strcpy(e->errore, "richiesta troppo lunga"); return 0; }
    if (t->scrivi(t->stato, (const unsigned char *)req, (unsigned int)n) != n) {
        strcpy(e->errore, "non riesco a mandare la richiesta");
        return 0;
    }

    /* --- le intestazioni ---------------------------------------------- */
    for (;;) {
        /* ! PRIMA SI GUARDA COSA C'E', POI SI DICE CHE NON CI STA. Il
         * controllo sul buffer pieno stava in cima, e con l'HTTP in chiaro non
         * si notava: TCP consegna a pezzi di un chilo e mezzo, quindi il
         * buffer non si riempiva mai in una volta. Il TLS consegna un RECORD
         * intero — fino a sedici kilobyte, cioe' esattamente quanto questo
         * buffer — e allora la prima lettura lo riempiva tutta insieme: al
         * giro dopo si usciva con «intestazioni troppo lunghe» senza aver mai
         * guardato le intestazioni, che stavano nei primi settecento byte.
         *
         * Si e' visto su news.ycombinator.com, che manda una pagina grande in
         * un record solo. */
        fine = http_intestazioni(acc, acc_n, r);
        if (fine < 0) { strcpy(e->errore, "risposta malformata"); return 0; }
        if (fine > 0) {
            /* ! I BISCOTTI SI CONSEGNANO QUI, PRIMA DEL CORPO E PRIMA DI
             * SEGUIRE UNA REDIREZIONE: e' l'unico momento in cui il salto dopo
             * puo' ancora portarseli. Consegnarli alla fine dello scambio
             * vorrebbe dire darli a chi non ne ha piu' bisogno. */
            if (g_bis_arrivato) {
                int b;

                for (b = 0; b < r->n_biscotti; b++)
                    g_bis_arrivato(g_bis_dato, u->host, r->biscotti[b]);
            }
            break;
        }

        if (acc_n >= sizeof(acc)) {
            /* ! IL MESSAGGIO PORTA I PRIMI BYTE DI CIO' CHE E' ARRIVATO, e
             * cambia tutto: «intestazioni troppo lunghe» manda a cercare un
             * buffer da allargare, mentre vedere cosa c'e' davvero dice in un
             * colpo se e' una risposta HTTP, un errore del server o
             * spazzatura. */
            char primi[24];
            unsigned int k;

            for (k = 0; k < sizeof(primi) - 1 && k < acc_n; k++)
                primi[k] = (acc[k] >= 32 && acc[k] < 127) ? (char)acc[k] : '.';
            primi[k] = '\0';

            sprintf(e->errore, "%u byte senza fine intestazioni: \"%s\"",
                    acc_n, primi);
            return 0;
        }

        n = leggi_a_pezzi(t, acc + acc_n, sizeof(acc) - acc_n, 0, &annullata);
        if (annullata) { strcpy(e->errore, "fermata"); return 0; }
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
                if (pezzi.stato == HTTP_P_FATTO) {
                    g_riusabile = !r->chiude && !e->troncata;
                    return 1;
                }
            } else if (r->ha_lunghezza) {
                if (e->byte >= r->lunghezza) {
                    g_riusabile = !r->chiude && !e->troncata;
                    return 1;
                }
            }

            /* ! SENZA LUNGHEZZA NE' PEZZI, LA FINE E' LA CHIUSURA, ed e'
             * legittimo: HTTP/1.0 fa cosi', e con «Connection: close» anche
             * l'1.1 puo'. Percio' una lettura che rende 0 non e' un errore. */
            n = leggi_a_pezzi(t, blocco, sizeof(blocco), 0, &annullata);
            /* ! UNA FERMATA NON E' UNA FINE: cio' che e' arrivato fin qui e'
             * meta' pagina, e darlo per buono vorrebbe dire mostrare una
             * pagina tagliata come se fosse intera. */
            if (annullata) { strcpy(e->errore, "fermata"); return 0; }
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

/* =============================================================================
 * exhttp_posta — come exhttp_prendi, ma con un corpo
 *
 * ! IL CORPO VALE PER LA PRIMA RICHIESTA E BASTA, e non e' una scorciatoia: e'
 * la regola dell'HTTP. Un 301 o un 302 dopo un POST si ri-segue in GET — lo
 * fanno tutti i browser da vent'anni, e un 307 che chiederebbe di ripetere il
 * POST qui si comporta come gli altri. Ripetere un invio a un indirizzo diverso
 * da quello a cui l'utente l'aveva mandato e' il modo in cui si ordina due
 * volte la stessa cosa.
 * ========================================================================== */
int exhttp_posta(const char *url, const char *corpo,
                 unsigned char *buf, unsigned int max, ExHttpEsito *e)
{
    int r;

    g_corpo = corpo;
    r = exhttp_prendi(url, buf, max, e);
    g_corpo = 0;
    return r;
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
        int             ok, riusata = 0;

        e->salti = salto;
        strncpy(e->finale, adesso, sizeof(e->finale) - 1);
        e->finale[sizeof(e->finale) - 1] = '\0';

        if (!http_url(adesso, &u)) { strcpy(e->errore, "indirizzo illeggibile"); return 0; }

riprova:
        /* ! LA CONNESSIONE DI PRIMA, SE E' PER LO STESSO POSTO. E' tutto il
         * guadagno: su https una richiesta nuova costa la stretta di mano, i
         * byte non contano quasi. */
        if (stesso_posto(&u)) {
            t = g_vivo;
            riusata = 1;
            goto collegato;
        }

        vivo_chiudi();

        /* ! LO SCHEMA SCEGLIE IL TRASPORTO, E BASTA. Da qui in giu' non c'e'
         * una riga che sappia se sotto ci sia un tubo cifrato: e' la divisione
         * che questo file aveva gia' il giorno prima che il TLS esistesse. */
        if (u.cifrato) {
            if (!exhttp_tls(&t, u.host, u.porta)) {
                if (g_tls_errore[0])
                    strncpy(e->errore, g_tls_errore, sizeof(e->errore) - 1);
                else if (g_ultimo_errore == -1)
                    strcpy(e->errore, "la rete non e' pronta");
                else if (g_ultimo_errore == -2)
                    strcpy(e->errore, "il nome non si risolve");
                else
                    sprintf(e->errore, "non riesco a connettermi (%d)",
                            g_ultimo_errore);
                return 0;
            }
            goto collegato;
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

collegato:
        ok = exhttp_scambio(&t, &u, buf, max, e, &r);
        g_corpo = 0;            /* la redirezione dopo un POST si segue in GET */

        /* ! UNA CONNESSIONE RIUSATA CHE FALLISCE SI RIFA' DA CAPO, UNA VOLTA.
         * L'altra parte puo' averla chiusa in qualunque momento senza dirlo —
         * e' normale, non e' un errore — e chi la riusa deve metterlo in
         * conto. Senza questa riga il primo timeout del server diventerebbe
         * una pagina che non si apre. */
        if (!ok && riusata) {
            g_vivo_c = 0;               /* e' gia' morta: non si richiude */
            riusata  = 0;
            memset(e, 0, sizeof(*e));
            e->salti = salto;
            strncpy(e->finale, adesso, sizeof(e->finale) - 1);
            e->finale[sizeof(e->finale) - 1] = '\0';
            goto riprova;
        }

        if (ok && g_riusabile) {
            /* Si tiene: la prossima richiesta allo stesso posto la trovera'. */
            g_vivo   = t;
            g_vivo_c = 1;
            g_vivo_porta   = u.porta;
            g_vivo_cifrato = u.cifrato;
            strncpy(g_vivo_host, u.host, sizeof(g_vivo_host) - 1);
            g_vivo_host[sizeof(g_vivo_host) - 1] = '\0';
        } else {
            g_vivo_c = 0;
            t.chiudi(t.stato);
        }

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
