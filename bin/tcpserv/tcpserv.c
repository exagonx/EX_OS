/* =============================================================================
 * bin/tcpserv/tcpserv.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * tcpserv — aspetta una connessione e risponde
 *
 *     tcpserv 7                    ascolta sulla porta 7 e rimanda indietro
 *     tcpserv 7 -1                 una connessione sola, poi esce
 *
 * ! STA A listen/accept COME tcptest STA A connect. Quando un servizio di rete
 * non funzionera', la domanda sara' se sia rotto il servizio o l'ascolto, e
 * senza questo comando bisogna indovinare — e' la stessa ragione per cui
 * tcptest esiste accanto a ftp.
 *
 * ! E RIMANDA INDIETRO QUELLO CHE RICEVE, che non e' un giochino: un'eco prova
 * in un colpo solo che la stretta di mano passiva e' finita davvero, che i dati
 * arrivano nel verso giusto e che la risposta esce dalla connessione GIUSTA —
 * cioe' le tre cose che listen e accept devono garantire.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "rete.h"

/* +0.001 a ogni modifica: `tcpserv -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("tcpserv", "0.001");

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

/* Rimanda indietro tutto quello che arriva, finche' l'altro non chiude.
 * Rende il numero di byte rimbalzati. */
static int servi(int id)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned char msg[sizeof(IpTcpDati) + 512];
    IpTcpRif      r;
    IpTcpDati     d;
    unsigned int  len;
    int           totale = 0, giri;

    r.id = (unsigned int)id;

    for (giri = 0; giri < 200; giri++) {
        /* ! SI PRENOTA UNA LETTURA PER VOLTA, come vuole il protocollo dello
         * stack: lo stack non manda dati di sua iniziativa, li consegna a chi
         * ha prenotato. Vedi il commento in ip_proto.h. */
        if (ipc_send(pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) break;

        if (attendi(IP_MSG_TCP_DATI, buf, &len, 3000) != 0) {
            IpTcpInfo info;

            /* Niente dati: si guarda se la connessione e' ancora viva. Una
             * chiusura dall'altra parte non e' un errore — e' la fine. */
            if (ipc_send(pid_ip, IP_MSG_TCP_STATO, &r, sizeof(r)) < 0) break;
            if (attendi(IP_MSG_TCP_INFO, buf, &len, 2000) != 0) break;
            memcpy(&info, buf, sizeof(info));
            if (info.stato != IP_TCP_APERTA) break;
            continue;
        }

        if (len < sizeof(d)) break;
        memcpy(&d, buf, sizeof(d));
        if (d.len == 0) continue;

        {
            unsigned int q = d.len;

            if (q > 512) q = 512;
            d.id  = (unsigned int)id;
            d.len = q;
            memcpy(msg, &d, sizeof(d));
            memcpy(msg + sizeof(d), buf + sizeof(IpTcpDati), q);

            if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + q) < 0) break;
            if (esito(3000) < 0) break;

            totale += (int)q;
            printf("tcpserv: rimbalzati %u byte\n", q);
        }
    }

    ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    esito(2000);
    return totale;
}

int main(int argc, char **argv)
{
    IpTcpAscolta a;
    IpTcpAccetta ac;
    int          asc, quante = 1, servite = 0;

    if (argc < 2) {
        printf("uso: tcpserv PORTA [QUANTE]\n\n");
        printf("Ascolta sulla porta, accetta una connessione e rimanda\n");
        printf("indietro quello che riceve. QUANTE = -1 per non fermarsi.\n");
        return 1;
    }
    if (argc >= 3) quante = atoi(argv[2]);

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    a.porta = (unsigned int)atoi(argv[1]);

    if (ipc_send(pid_ip, IP_MSG_TCP_ASCOLTA, &a, sizeof(a)) < 0) {
        printf("tcpserv: non riesco a parlare con lo stack IP\n");
        return 1;
    }

    asc = esito(3000);
    if (asc < 0) {
        printf("tcpserv: ascolto sulla porta %u rifiutato (%d)%s\n",
               a.porta, asc,
               asc == -EADDRINUSE ? ": c'e' gia' qualcuno in ascolto li'" : "");
        return 1;
    }

    printf("tcpserv: in ascolto sulla porta %u\n", a.porta);

    while (quante < 0 || servite < quante) {
        int id;

        ac.id         = (unsigned int)asc;
        ac.timeout_ms = 10000;

        if (ipc_send(pid_ip, IP_MSG_TCP_ACCETTA, &ac, sizeof(ac)) < 0) break;

        id = esito(12000);
        if (id == -ETIMEDOUT || id == -EAGAIN) {
            printf("tcpserv: nessuno si e' collegato\n");
            continue;
        }
        if (id < 0) {
            printf("tcpserv: accetta fallita (%d)\n", id);
            break;
        }

        printf("tcpserv: connessione %d accettata\n", id);
        servi(id);
        servite++;
    }

    {
        IpTcpRif r;

        r.id = (unsigned int)asc;
        ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
        esito(2000);
    }

    printf("tcpserv: %d connessioni servite\n", servite);
    return 0;
}
