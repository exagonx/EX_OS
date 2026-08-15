/* =============================================================================
 * lib/dns.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Risoluzione dei nomi via DNS. Il contratto sta in lib/include/dns.h,
 * insieme al perché questo è un modulo e non un servizio.
 *
 * -----------------------------------------------------------------------------
 * ! LA COMPRESSIONE DEI NOMI E' IL PUNTO PERICOLOSO DI QUESTO FILE
 *
 * In una risposta DNS i nomi non sono scritti per esteso ogni volta: un
 * nome può finire con un PUNTATORE a un punto precedente del messaggio
 * (due byte con i bit alti a 1). Serve a non ripetere "www.esempio.it" in
 * ogni record.
 *
 * Il pericolo è che quel puntatore lo scrive il server, e niente gli
 * impedisce di farlo puntare a sé stesso o in avanti. Un lettore ingenuo
 * che segue i puntatori entra in un ciclo infinito, e siccome questo
 * codice gira dentro il programma di chi ha chiesto, il ciclo se lo prende
 * lui. Qui i puntatori si seguono solo per LEGGERE un nome, con un tetto
 * al numero di salti; per SALTARE un nome non si seguono affatto, perché
 * un puntatore chiude sempre il nome e la lunghezza è nota.
 *
 * -----------------------------------------------------------------------------
 * ! SI PRENOTA LA RICEZIONE PRIMA DI MANDARE
 *
 * Lo stack non tiene in coda i datagrammi per cui nessuno sta aspettando:
 * li conta e li butta (vedi drivers/net/ip_proto.h). Chiedere il
 * datagramma dopo aver mandato la domanda lascia una finestra in cui la
 * risposta arriva e sparisce — su una rete veloce è la norma, non
 * l'eccezione. È lo stesso errore che ha fatto costare tre secondi in più
 * al primo client DHCP, trovato contando i datagrammi inviati.
 * ============================================================================= */

#include "libc.h"
#include "dns.h"
#include "ip_proto.h"

#define PORTA_DNS      53
#define TENTATIVI      3
#define ATTESA_MS      2000
#define MSG_MAX        512      /* un messaggio DNS su UDP si ferma qui */

#define TIPO_A         1
#define TIPO_CNAME     5
#define CLASSE_IN      1

#define SALTI_MAX      8        /* tetto ai puntatori di compressione */

/* Codici di risposta (i quattro bit bassi del secondo byte di flag) */
#define RCODE_OK       0
#define RCODE_NXDOMAIN 3

int ip_da_stringa(const char *s, unsigned char *out)
{
    int i, v, cifre;

    if (s == NULL || out == NULL) return 0;

    for (i = 0; i < 4; i++) {
        v = 0; cifre = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            s++;
            /* Si esce subito su un numero troppo grande invece di lasciarlo
             * traboccare: "99999999999.1.1.1" non deve diventare un
             * indirizzo per caso. */
            if (++cifre > 3 || v > 255) return 0;
        }
        if (cifre == 0) return 0;
        out[i] = (unsigned char)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    return (*s == '\0');
}

/* =============================================================================
 * Composizione della domanda
 * ============================================================================= */

/* Scrive il nome in forma DNS: ogni etichetta preceduta dalla propria
 * lunghezza, e uno zero alla fine. "a.bc" -> 1 'a' 2 'b' 'c' 0
 *
 * Ritorna i byte scritti, 0 se il nome non è utilizzabile. */
static unsigned int scrivi_nome(unsigned char *p, unsigned int spazio,
                                const char *nome)
{
    unsigned int o = 0, inizio;
    const char  *s = nome;

    if (*s == '\0' || *s == '.') return 0;

    while (*s) {
        unsigned int len = 0;

        inizio = o;
        if (o + 1 >= spazio) return 0;
        o++;                                /* posto per la lunghezza */

        while (*s && *s != '.') {
            if (o >= spazio) return 0;
            /* ! Un'etichetta oltre 63 byte non è rappresentabile: i due
             * bit alti del byte di lunghezza sono riservati alla
             * compressione, e scriverci 64 produrrebbe un puntatore. */
            if (len >= 63) return 0;
            p[o++] = (unsigned char)*s++;
            len++;
        }

        if (len == 0) return 0;             /* due punti di fila, o punto finale */
        p[inizio] = (unsigned char)len;

        if (*s == '.') s++;
    }

    if (o >= spazio) return 0;
    p[o++] = 0;
    return o;
}

/* =============================================================================
 * Lettura della risposta
 * ============================================================================= */

/* Salta un nome a partire da `o`. Ritorna l'offset successivo, 0 in caso
 * di messaggio malformato.
 *
 * ! NON SEGUE I PUNTATORI, e non deve: un puntatore chiude sempre il nome,
 * quindi per SALTARE basta contare i suoi due byte. Seguirlo qui vorrebbe
 * dire poter girare in tondo su un messaggio ostile senza alcun guadagno. */
static unsigned int salta_nome(const unsigned char *m, unsigned int len,
                               unsigned int o)
{
    while (o < len) {
        unsigned char b = m[o];

        if ((b & 0xC0) == 0xC0) {           /* puntatore: due byte, poi basta */
            return (o + 2 <= len) ? o + 2 : 0;
        }
        if ((b & 0xC0) != 0) return 0;      /* forma di etichetta riservata */
        if (b == 0) return o + 1;           /* fine del nome */
        o += 1 + b;
    }
    return 0;
}

/* =============================================================================
 * Dialogo con lo stack
 * ============================================================================= */
static int attendi_da(int pid_ip, unsigned int tipo, unsigned char *buf,
                      unsigned int *len, unsigned int ms)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 32; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;
        if (meta.tipo != tipo) continue;
        if (len) *len = meta.len;
        return 0;
    }
    return -1;
}

int dns_risolvi_da(const char *nome, unsigned char *ip, unsigned char *da)
{
    unsigned char  buf[IPC_MSG_MAX_DATA];
    unsigned char  msg[sizeof(IpUdpInvia) + MSG_MAX];
    unsigned char *dom = msg + sizeof(IpUdpInvia);
    IpUdpInvia     inv;
    IpUdpApri      apri;
    IpStato        st;
    unsigned int   len, n, id, porta;
    int            pid_ip, tentativo, rc;

    if (nome == NULL || ip == NULL) return -EINVAL;

    /* Un indirizzo in cifre è già una risposta. */
    if (ip_da_stringa(nome, ip)) {
        if (da) memset(da, 0, 4);
        return 0;
    }

    pid_ip = ipc_lookup(IP_SERVIZIO);
    if (pid_ip <= 0) return -ENODEV;

    if (ipc_send(pid_ip, IP_MSG_STATO, NULL, 0) < 0) return -ENODEV;
    if (attendi_da(pid_ip, IP_MSG_STATO_R, buf, &len, ATTESA_MS) != 0 ||
        len < sizeof(st)) return -ENODEV;
    memcpy(&st, buf, sizeof(st));

    if ((st.cfg.dns[0] | st.cfg.dns[1] | st.cfg.dns[2] | st.cfg.dns[3]) == 0)
        return -ENETDOWN;

    /* Porta effimera: la sceglie lo stack, che risponde col numero. */
    apri.porta = 0;
    if (ipc_send(pid_ip, IP_MSG_UDP_APRI, &apri, sizeof(apri)) < 0) return -EIO;
    if (attendi_da(pid_ip, IP_MSG_ESITO, buf, &len, ATTESA_MS) != 0 ||
        len < sizeof(IpEsito)) return -EIO;
    { IpEsito e; memcpy(&e, buf, sizeof(e)); rc = e.codice; }
    if (rc <= 0) return -EIO;
    porta = (unsigned int)rc;
    apri.porta = porta;

    /* Un identificativo che cambi fra una richiesta e l'altra: il tempo
     * mescolato al PID. Serve solo a riconoscere la NOSTRA risposta fra
     * quelle che arrivano alla stessa porta. */
    id = (uptime_ms() ^ ((unsigned int)getpid() << 7)) & 0xFFFF;

    rc = -ETIMEDOUT;

    for (tentativo = 0; tentativo < TENTATIVI; tentativo++) {
        unsigned int o;

        /* --- composizione --- */
        dom[0] = (unsigned char)(id >> 8); dom[1] = (unsigned char)id;
        dom[2] = 0x01; dom[3] = 0x00;      /* domanda, ricorsione desiderata */
        dom[4] = 0; dom[5] = 1;            /* una domanda */
        dom[6] = 0; dom[7] = 0;
        dom[8] = 0; dom[9] = 0;
        dom[10] = 0; dom[11] = 0;

        n = scrivi_nome(dom + 12, MSG_MAX - 12 - 4, nome);
        if (n == 0) { rc = -EINVAL; break; }
        o = 12 + n;
        dom[o++] = 0; dom[o++] = TIPO_A;
        dom[o++] = 0; dom[o++] = CLASSE_IN;

        /* --- prenotazione PRIMA dell'invio: vedi il commento di testa --- */
        if (ipc_send(pid_ip, IP_MSG_UDP_RICEVI, &apri, sizeof(apri)) < 0) {
            rc = -EIO; break;
        }

        memcpy(inv.ip, st.cfg.dns, 4);
        inv.porta        = PORTA_DNS;
        inv.porta_locale = porta;
        memcpy(msg, &inv, sizeof(inv));

        if (ipc_send(pid_ip, IP_MSG_UDP_INVIA, msg, sizeof(inv) + o) < 0) {
            rc = -EIO; break;
        }

        /* --- attesa: esito e dato possono arrivare in qualunque ordine --- */
        {
            unsigned int scadenza = uptime_ms() + ATTESA_MS;
            IpcMessage   meta;

            while ((int)(uptime_ms() - scadenza) < 0) {
                IpUdpDati            d;
                const unsigned char *r;
                unsigned int         p, ancount, k;

                if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0)
                    break;
                if ((int)meta.sender_pid != pid_ip) continue;

                if (meta.tipo == IP_MSG_ESITO) {
                    IpEsito e;
                    if (meta.len >= sizeof(e)) {
                        memcpy(&e, buf, sizeof(e));
                        if (e.codice != 0) { rc = -EIO; goto fine; }
                    }
                    continue;
                }
                if (meta.tipo != IP_MSG_UDP_DATI) continue;
                if (meta.len < sizeof(d)) continue;

                memcpy(&d, buf, sizeof(d));
                if (d.len < 12 || sizeof(d) + d.len > meta.len) continue;
                r = buf + sizeof(d);

                /* Non è la nostra risposta: si riprenota e si aspetta. */
                if (((unsigned int)r[0] << 8 | r[1]) != id) goto riprenota;
                if ((r[2] & 0x80) == 0) goto riprenota;      /* non è una risposta */

                {
                    unsigned int rcode = r[3] & 0x0F;

                    if (rcode == RCODE_NXDOMAIN) { rc = -ENOENT; goto fine; }
                    if (rcode != RCODE_OK)       { rc = -EIO;    goto fine; }
                }

                /* Si saltano le domande ripetute nella risposta. */
                p = 12;
                for (k = 0; k < ((unsigned int)r[4] << 8 | r[5]); k++) {
                    p = salta_nome(r, d.len, p);
                    if (p == 0 || p + 4 > d.len) { rc = -EIO; goto fine; }
                    p += 4;
                }

                ancount = (unsigned int)r[6] << 8 | r[7];
                if (ancount == 0) { rc = -ENOENT; goto fine; }

                for (k = 0; k < ancount; k++) {
                    unsigned int tipo, classe, rdlen;

                    p = salta_nome(r, d.len, p);
                    if (p == 0 || p + 10 > d.len) { rc = -EIO; goto fine; }

                    tipo   = (unsigned int)r[p]     << 8 | r[p + 1];
                    classe = (unsigned int)r[p + 2] << 8 | r[p + 3];
                    rdlen  = (unsigned int)r[p + 8] << 8 | r[p + 9];
                    p += 10;

                    if (p + rdlen > d.len) { rc = -EIO; goto fine; }

                    /* ! SI SALTANO I CNAME INVECE DI SEGUIRLI. Un server
                     * che risponde con un alias mette quasi sempre anche il
                     * record A finale nella stessa risposta: prenderlo di
                     * lì costa un giro di ciclo, mentre seguire la catena
                     * vorrebbe dire fare altre domande. Se il record A non
                     * c'è, si dice che il nome non si risolve — che è vero
                     * per questo risolutore. */
                    if (tipo == TIPO_A && classe == CLASSE_IN && rdlen == 4) {
                        memcpy(ip, r + p, 4);
                        if (da) memcpy(da, d.ip, 4);
                        rc = 0;
                        goto fine;
                    }
                    p += rdlen;
                }

                rc = -ENOENT;       /* risposta senza nessun indirizzo */
                goto fine;

riprenota:
                /* La prenotazione si consuma anche su un datagramma che non
                 * ci interessa: va rifatta, o il prossimo verrebbe buttato. */
                if (ipc_send(pid_ip, IP_MSG_UDP_RICEVI, &apri, sizeof(apri)) < 0) {
                    rc = -EIO;
                    goto fine;
                }
            }
        }
    }

fine:
    ipc_send(pid_ip, IP_MSG_UDP_CHIUDI, &apri, sizeof(apri));
    /* La conferma della chiusura non interessa, ma va CONSUMATA: lasciarla
     * in mailbox significherebbe che la prossima ipc_recv del chiamante
     * trova un IP_MSG_ESITO che non aspettava. */
    attendi_da(pid_ip, IP_MSG_ESITO, buf, &len, 500);

    return rc;
}

int dns_risolvi(const char *nome, unsigned char *ip)
{
    return dns_risolvi_da(nome, ip, NULL);
}
