/* =============================================================================
 * bin/tcptest/tcptest.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Apre una connessione TCP, manda una riga, stampa quello che torna.
 *
 *   tcptest 10.0.2.2 8080
 *   tcptest example.com 80 "GET / HTTP/1.0"
 *
 * -----------------------------------------------------------------------------
 * PERCHE' ESISTE
 *
 * Sta a TCP come `nettest` sta al driver di scheda: serve a provare UN
 * livello per volta. Quando un client FTP non funzionera', la domanda
 * sara' se sia rotto FTP o TCP, e senza questo comando bisogna
 * indovinare.
 *
 * Manda una riga di testo terminata da CRLF perche' e' quello che si
 * aspettano i protocolli testuali (HTTP, FTP, SMTP): con un server HTTP
 * dall'altra parte si vede subito una risposta leggibile, e una risposta
 * leggibile e' una prova che i byte sono arrivati NELL'ORDINE giusto —
 * non solo che sono arrivati.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "dns.h"
#include "rete.h"

/* +0.001 a ogni modifica: `tcptest -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("tcptest", "0.001");

static int pid_ip = 0;

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

static const char *spiega(int c)
{
    switch (c) {
    case -ETIMEDOUT:    return "nessuna risposta: l'host non e' raggiungibile o la porta e' chiusa e filtrata";
    case -ECONNRESET:   return "connessione rifiutata (RST): la porta e' chiusa";
    case -ENETUNREACH:  return "fuori dalla rete locale e nessun gateway";
    case -EAGAIN:       return "MAC non ancora noto, riprova fra un istante";
    case -ENFILE:       return "nessuna connessione libera";
    default:            return "errore";
    }
}

int main(int argc, char **argv)
{
    unsigned char ip[4], buf[IPC_MSG_MAX_DATA];
    unsigned char msg[sizeof(IpTcpDati) + 512];
    unsigned int  len;
    IpTcpApri     a;
    IpTcpDati     d;
    IpTcpRif      r;
    const char   *richiesta = "GET / HTTP/1.0";
    int           id, rc, giri, ricevuti = 0;

    if (argc < 3) {
        printf("uso: tcptest NOME|INDIRIZZO PORTA [\"riga da mandare\"]\n\n");
        printf("Apre una connessione, manda la riga (predefinita: una\n");
        printf("richiesta HTTP minima) e stampa quello che torna.\n");
        return 1;
    }
    if (argc >= 4) richiesta = argv[3];

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    rc = dns_risolvi(argv[1], ip);
    if (rc != 0) {
        printf("tcptest: non riesco a risolvere '%s' (%d)\n", argv[1], rc);
        return 1;
    }

    memcpy(a.ip, ip, 4);
    a.porta      = (unsigned int)atoi(argv[2]);
    a.timeout_ms = 5000;

    printf("connessione a %u.%u.%u.%u:%u ...\n",
           ip[0], ip[1], ip[2], ip[3], a.porta);

    /* ! -EAGAIN NON E' UN FALLIMENTO: significa che lo stack ha appena
     * chiesto l'ARP del prossimo salto e non ha ancora la risposta. Si
     * riprova, che e' esattamente quello che il contratto chiede. */
    for (giri = 0; giri < 10; giri++) {
        if (ipc_send(pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0) return 1;
        id = esito(7000);
        if (id != -EAGAIN) break;
        usleep(200 * 1000);
    }

    if (id <= 0) {
        printf("tcptest: %s (%d)\n", spiega(id), id);
        return 1;
    }
    printf("connessa (id %d)\n\n", id);

    /* --- invio --- */
    {
        unsigned int n = 0;

        while (richiesta[n] && n < 500u) n++;
        d.id  = (unsigned int)id;
        d.len = n + 4u;                 /* la riga piu' CRLF CRLF */
        memcpy(msg, &d, sizeof(d));
        memcpy(msg + sizeof(d), richiesta, n);
        memcpy(msg + sizeof(d) + n, "\r\n\r\n", 4);

        if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + n + 4u) < 0) return 1;
        rc = esito(5000);
        if (rc < 0) { printf("tcptest: invio rifiutato (%d)\n", rc); return 1; }
        printf("mandati %d byte\n\n", rc);
    }

    /* --- lettura fino alla chiusura --- */
    r.id = (unsigned int)id;
    for (giri = 0; giri < 40; giri++) {
        IpTcpDati in;

        if (ipc_send(pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) break;
        if (attendi(IP_MSG_TCP_DATI, buf, &len, 5000) != 0) break;
        if (len < sizeof(in)) break;
        memcpy(&in, buf, sizeof(in));

        if (in.len == 0) break;         /* l'altro ha chiuso */

        write(1, buf + sizeof(in), in.len);
        ricevuti += (int)in.len;
    }

    printf("\n\n--- ricevuti %d byte ---\n", ricevuti);

    ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    esito(2000);

    return (ricevuti > 0) ? 0 : 1;
}
