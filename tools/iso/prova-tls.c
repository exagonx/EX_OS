/* =============================================================================
 * tools/iso/prova-tls.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Un client TLS 1.3 dentro EX-OS, con la libssl di OpenSSL sopra il nostro
 * stack IP.
 *
 *     provatls <host> [porta] [percorso]
 *
 * -----------------------------------------------------------------------------
 * ! EX-OS NON HA I SOCKET, E NON SERVONO. OpenSSL vuole leggere e scrivere
 * byte, non un descrittore: gli si danno due BIO DI MEMORIA e si fa il
 * facchino — cio' che lui vuole spedire lo si manda con IP_MSG_TCP_INVIA, cio'
 * che arriva glielo si mette dentro. E' la stessa strada che usa chi mette TLS
 * dentro un protocollo che non e' TCP, ed e' documentata: `SSL_set_bio` con
 * BIO di memoria e' il modo previsto, non un aggiramento.
 *
 * ! ED E' ANCHE IL MOTIVO PER CUI SI PUO' FARE ADESSO. La nota che diceva
 * «libssl non si costruisce: ssl/rio vuole fd_set e il polling sui socket» era
 * vera su un albero dei sorgenti a cui mancavano 822 file — fra cui proprio
 * `ssl/rio/build.info`. Con i sorgenti interi libssl.a si costruisce senza un
 * errore, e questo programma non chiede nessun fd_set.
 *
 * ! LA VERIFICA DEL CERTIFICATO SI DICE, NON SI FA FINTA. Con il magazzino
 * delle CA (`-CAfile`, che qui e' /exos/ssl/certi.pem se c'e') si verifica e
 * si stampa il verdetto; senza, si stampa CHE NON SI E' VERIFICATO. Un
 * programma che scrive «connesso» senza dire quale delle due cose ha fatto e'
 * il modo in cui una prova diventa una bugia.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "dns.h"
#include "rete.h"
/* ! GLI HEADER DELLA LIBC SI PRENDONO DAL SYSROOT, NON CON -I lib/include, e
 * la differenza si vede proprio qui. GCC installa un proprio <limits.h> — che
 * e' l'unico a sapere quanto e' largo un `int` — e da li' fa `#include_next`
 * per prendere anche il nostro. Mettendo lib/include davanti con -I, il nostro
 * vince, INT_MAX sparisce, e l'errore compare dentro err.h di OpenSSL: sembra
 * un difetto loro ed e' un ordine di ricerca nostro.
 *
 * Sta scritto anche in cima a lib/include/limits.h; l'ho riscoperto sbagliando
 * la riga di compilazione. */
#include <limits.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>

EX_VERSIONE("provatls", "0.001");

/* ! IL MAGAZZINO SI CERCA IN PIU' POSTI, come la libc condivisa fa con
 * libc.so, e per la stessa ragione: gli strumenti stanno in /exos quando li si
 * e' installati con `toolinst`, e sotto /cdrom quando il CD e' nel lettore. Un
 * percorso solo vuol dire che il programma funziona in uno dei due casi e
 * nell'altro dice «non verifico» — che e' quello che ha fatto alla prima
 * prova. */
static const char *const MAGAZZINI[] = {
    "/exos/ssl/certi.pem",
    "/cdrom/exos/ssl/certi.pem",
    "/ssl/certi.pem"
};

static int pid_ip = 0;
static unsigned int conn_id = 0;

/* --- il trasporto: IPC verso ip.drv, esattamente come tcptest -------------- */
static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 64; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;
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

static int tcp_apri(const unsigned char ip[4], unsigned int porta)
{
    IpTcpApri a;
    int       id = -1, giri;

    memcpy(a.ip, ip, 4);
    a.porta      = porta;
    a.timeout_ms = 5000;

    /* -EAGAIN vuol dire «sto chiedendo l'ARP»: si riprova, come da contratto. */
    for (giri = 0; giri < 10; giri++) {
        if (ipc_send(pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0) return -1;
        id = esito(7000);
        if (id != -EAGAIN) break;
        usleep(200 * 1000);
    }
    return id;
}

static int tcp_invia(const unsigned char *b, unsigned int n)
{
    unsigned char msg[IPC_MSG_MAX_DATA];
    IpTcpDati     d;

    if (n > IP_TCP_DATI_MAX) n = IP_TCP_DATI_MAX;
    d.id  = conn_id;
    d.len = n;
    memcpy(msg, &d, sizeof(d));
    memcpy(msg + sizeof(d), b, n);

    if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + n) < 0) return -1;
    return esito(5000);
}

/* Rende quanti byte, 0 se l'altro ha chiuso, -1 se non e' arrivato niente. */
static int tcp_ricevi(unsigned char *b, unsigned int max, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     in;

    r.id = conn_id;
    if (ipc_send(pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) return -1;
    if (attendi(IP_MSG_TCP_DATI, buf, &len, ms) != 0) return -1;
    if (len < sizeof(in)) return -1;
    memcpy(&in, buf, sizeof(in));
    if (in.len == 0) return 0;
    if (in.len > max) in.len = max;
    memcpy(b, buf + sizeof(in), in.len);
    return (int)in.len;
}

/* =============================================================================
 * Il facchinaggio fra i BIO e la rete
 *
 * ! SI SVUOTA SEMPRE PRIMA CIO' CHE OPENSSL VUOLE SPEDIRE. Se si legge prima,
 * si aspetta una risposta a una domanda che non e' ancora partita — e
 * l'handshake si ferma li', senza errori, per sempre.
 * ============================================================================= */
static int spingi(BIO *rete)
{
    unsigned char b[IP_TCP_DATI_MAX];
    int n, mandati = 0;

    while ((n = BIO_read(rete, (char *)b, (int)sizeof(b))) > 0) {
        if (tcp_invia(b, (unsigned int)n) < 0) return -1;
        mandati += n;
    }
    return mandati;
}

static int tira(BIO *rete, unsigned int ms)
{
    unsigned char b[IP_TCP_DATI_MAX];
    int n = tcp_ricevi(b, sizeof(b), ms);

    if (n <= 0) return n;
    BIO_write(rete, (const char *)b, n);
    return n;
}

static void errori_openssl(const char *dove)
{
    unsigned long e;
    char           buf[160];

    printf("provatls: %s\n", dove);
    while ((e = ERR_get_error()) != 0) {
        ERR_error_string_n(e, buf, sizeof(buf));
        printf("          %s\n", buf);
    }
}

/* =============================================================================
 * ! IL MAGAZZINO SI LEGGE CON open/read, NON CON IL BIO DI FILE DI OPENSSL
 *
 * `SSL_CTX_load_verify_locations` sul CD rende «x509 certificate routines::BIO
 * lib»: il file c'e', si legge con `ls` e con `cp`, ma la sua stdio non ci
 * arriva. E' un difetto della nostra libc — o di cosa il BIO di file si aspetta
 * da fseek/ftell — e va cercato, ma non e' questo il posto: qui serve il
 * magazzino, e leggerlo con le chiamate diritte funziona.
 *
 * ! E VA DETTO INVECE CHE AGGIRATO IN SILENZIO. Un aggiramento che nessuno
 * scrive diventa il motivo per cui, fra sei mesi, «openssl non legge i file»
 * sembra normale.
 * ============================================================================= */
static int carica_magazzino(SSL_CTX *ctx, const char *percorso)
{
    static unsigned char testo[512 * 1024];
    X509_STORE *magazzino = SSL_CTX_get_cert_store(ctx);
    BIO        *mem;
    int         fd, letti, quanti = 0;

    fd = open(percorso, O_RDONLY);
    if (fd < 0) return -1;
    letti = (int)read(fd, testo, sizeof(testo) - 1);
    close(fd);
    if (letti <= 0) return -1;

    mem = BIO_new_mem_buf(testo, letti);
    if (!mem) return -1;

    for (;;) {
        X509 *c = PEM_read_bio_X509(mem, 0, 0, 0);

        if (!c) break;
        if (X509_STORE_add_cert(magazzino, c) == 1) quanti++;
        X509_free(c);
    }
    BIO_free(mem);

    /* Gli errori dell'ultimo PEM_read_bio_X509 — quello che ha detto «finito»
     * — non sono guasti: si buttano, o il primo errore vero dopo sembrerebbe
     * questo. */
    ERR_clear_error();

    if (quanti > 0) printf("provatls: %d certificati nel magazzino\n", quanti);
    return quanti;
}

int main(int argc, char **argv)
{
    unsigned char ip[4];
    const char   *host, *percorso = "/";
    unsigned int  porta = 443;
    SSL_CTX      *ctx;
    SSL          *ssl;
    BIO          *interno, *rete;
    int           rc, id, verifica = 0;
    struct stat   st;

    if (argc < 2) {
        printf("uso: provatls <host> [porta] [percorso]\n\n");
        printf("Fa un handshake TLS con la libssl di OpenSSL sopra lo stack\n");
        printf("IP di EX-OS, poi chiede una pagina e stampa cio' che torna.\n");
        return 1;
    }
    host = argv[1];
    if (argc >= 3) porta = (unsigned int)atoi(argv[2]);
    if (argc >= 4) percorso = argv[3];

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    if (dns_risolvi(host, ip) != 0) {
        printf("provatls: non riesco a risolvere '%s'\n", host);
        return 1;
    }
    printf("provatls: %s e' %u.%u.%u.%u, porta %u\n",
           host, ip[0], ip[1], ip[2], ip[3], porta);

    id = tcp_apri(ip, porta);
    if (id <= 0) { printf("provatls: la connessione non si apre (%d)\n", id); return 1; }
    conn_id = (unsigned int)id;
    printf("provatls: TCP aperta (id %d)\n", id);

    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { errori_openssl("SSL_CTX_new fallita"); return 1; }

    /* ! IL MAGAZZINO SI CARICA SE C'E', E SE NON C'E' SI DICE. */
    {
        unsigned int i;

        for (i = 0; i < sizeof(MAGAZZINI) / sizeof(MAGAZZINI[0]); i++) {
            if (stat(MAGAZZINI[i], &st) != 0) continue;

            /* ! «IL FILE C'E' MA NON SI CARICA» E' UN GUASTO DIVERSO DA «NON
             * C'E'», e tacerlo manda a cercare nel posto sbagliato: la prima
             * volta ho creduto a un percorso sbagliato mentre il file era
             * li'. Se c'e' e non si carica, si dice, con l'errore di OpenSSL
             * accanto. */
            if (carica_magazzino(ctx, MAGAZZINI[i]) <= 0) {
                printf("provatls: %s c'e' (%u byte) ma non si carica\n",
                       MAGAZZINI[i], (unsigned int)st.st_size);
                errori_openssl("  motivo:");
                continue;
            }
            SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, 0);
            verifica = 1;
            printf("provatls: magazzino %s, il certificato SI VERIFICA\n",
                   MAGAZZINI[i]);
            break;
        }
        if (!verifica) {
            printf("provatls: NIENTE MAGAZZINO: il certificato NON si verifica.\n");
            printf("          La connessione sarebbe cifrata con CHIUNQUE "
                   "risponda.\n");
        }
    }

    ssl = SSL_new(ctx);
    if (!ssl) { errori_openssl("SSL_new fallita"); return 1; }

    interno = 0; rete = 0;
    if (BIO_new_bio_pair(&interno, 0, &rete, 0) != 1) {
        errori_openssl("BIO_new_bio_pair fallita");
        return 1;
    }
    SSL_set_bio(ssl, interno, interno);
    SSL_set_connect_state(ssl);

    /* SNI e verifica del nome: senza, si verifica una catena qualunque. */
    SSL_set_tlsext_host_name(ssl, host);
    /* ! SSL_set1_host NON C'E' IN QUESTA VERSIONE degli header: si passa dal
     * parametro di verifica, che e' la strada di sotto e quella che
     * SSL_set1_host userebbe comunque. */
    if (verifica)
        X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl), host, 0);

    /* --- l'handshake, girando la manovella ------------------------------- */
    {
        int giri;

        for (giri = 0; giri < 200; giri++) {
            rc = SSL_do_handshake(ssl);
            if (rc == 1) break;

            {
                int e = SSL_get_error(ssl, rc);

                if (spingi(rete) < 0) { printf("provatls: invio fallito\n"); return 1; }

                if (e == SSL_ERROR_WANT_READ) {
                    if (tira(rete, 8000) <= 0) {
                        printf("provatls: l'altro non risponde piu'\n");
                        return 1;
                    }
                } else if (e != SSL_ERROR_WANT_WRITE) {
                    errori_openssl("handshake fallito");
                    return 1;
                }
            }
        }
        if (rc != 1) { printf("provatls: handshake non finito\n"); return 1; }
    }
    spingi(rete);

    printf("\nprovatls: HANDSHAKE FATTO\n");
    printf("          protocollo %s\n", SSL_get_version(ssl));
    printf("          cifrario   %s\n", SSL_get_cipher(ssl));

    {
        X509 *c = SSL_get1_peer_certificate(ssl);

        if (c) {
            char nome[256];

            X509_NAME_oneline(X509_get_subject_name(c), nome, sizeof(nome));
            printf("          soggetto   %s\n", nome);
            X509_NAME_oneline(X509_get_issuer_name(c), nome, sizeof(nome));
            printf("          emittente  %s\n", nome);
            X509_free(c);
        }
        if (verifica) {
            long v = SSL_get_verify_result(ssl);
            printf("          verifica   %s (%ld)\n",
                   v == X509_V_OK ? "OK" : X509_verify_cert_error_string(v), v);
        } else {
            printf("          verifica   NON FATTA\n");
        }
    }

    /* --- una richiesta, e cio' che torna ---------------------------------- */
    {
        char richiesta[512];
        int  n;

        n = snprintf(richiesta, sizeof(richiesta),
                     "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
                     "User-Agent: EX-OS/provatls\r\n\r\n", percorso, host);
        SSL_write(ssl, richiesta, n);
        spingi(rete);

        printf("\n--- risposta ---\n");
        {
            char b[2048];
            int  totale = 0, giri;

            for (giri = 0; giri < 400; giri++) {
                int letti = SSL_read(ssl, b, sizeof(b));

                if (letti > 0) { write(1, b, (unsigned int)letti); totale += letti; continue; }

                {
                    int e = SSL_get_error(ssl, letti);

                    if (e == SSL_ERROR_WANT_READ) {
                        spingi(rete);
                        if (tira(rete, 5000) <= 0) break;
                    } else break;
                }
            }
            printf("\n--- %d byte in chiaro ---\n", totale);
        }
    }

    {
        IpTcpRif r;
        r.id = conn_id;
        ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
        esito(2000);
    }
    return 0;
}
