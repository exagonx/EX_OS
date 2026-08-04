/* =============================================================================
 * bin/ping/ping.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Manda richieste ICMP di echo e misura quanto ci mettono a tornare.
 *
 *   ping 10.0.2.2
 *   ping 10.0.2.2 -n 10 -w 3000 -l 56
 *
 * -----------------------------------------------------------------------------
 * QUESTO PROGRAMMA NON SA COS'E' ICMP
 *
 * Non compone intestazioni, non calcola somme di controllo, non conosce
 * ARP. Manda un messaggio a /dev/ip.drv e stampa la risposta. Tutto il
 * protocollo sta nello stack, che è un processo a sé — e deve restare
 * così: il giorno che arriveranno UDP e TCP, i loro client saranno
 * altrettanto ignoranti.
 *
 * Il confronto è con `nettest`, che invece compone un frame ARP a mano:
 * quello è uno strumento per provare il DRIVER quando lo stack non c'è o
 * non funziona. Questo è un programma normale.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ TRE ESITI DIVERSI, NON «FUNZIONA / NON FUNZIONA»
 *
 *   risposta       l'host c'è e risponde
 *   -EHOSTUNREACH  nessuno ha risposto all'ARP: sulla rete locale non c'è
 *                  nessuno a quell'indirizzo (o al gateway per arrivarci)
 *   -ETIMEDOUT     l'ARP è andato, il pacchetto è partito, la risposta no
 *
 * La differenza fra gli ultimi due è dove cercare: nel primo caso non si
 * è nemmeno usciti dal cavo, nel secondo il problema è più in là. Un
 * unico «non raggiungibile» costringerebbe a rifare da capo la diagnosi
 * ogni volta.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"

static int pid_ip = 0;

static int leggi_ip(const char *s, unsigned char *out)
{
    int i, v, cifre;

    for (i = 0; i < 4; i++) {
        v = 0; cifre = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; cifre++; }
        if (cifre == 0 || v > 255) return 0;
        out[i] = (unsigned char)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    return (*s == '\0');
}

static void stampa_ip(const unsigned char *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

/* Come in ogni client IPC di questo sistema: si controlla CHI ha
 * risposto, perché ipc_recv consegna il prossimo messaggio della mailbox
 * e non «la risposta alla mia domanda». */
static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 16; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;
        if (meta.tipo != tipo) continue;
        if (len) *len = meta.len;
        return 0;
    }
    return -1;
}

static const char *spiega(int codice)
{
    switch (codice) {
    case -EHOSTUNREACH:
        return "nessuna risposta all'ARP: a quell'indirizzo non c'e' nessuno";
    case -ETIMEDOUT:
        return "il pacchetto e' partito, la risposta non e' arrivata";
    case -ENETUNREACH:
        return "fuori dalla rete locale e nessun gateway configurato";
    case -EBUSY:
        return "lo stack sta gia' servendo un'altra richiesta";
    default:
        return "errore";
    }
}

static void uso(void)
{
    printf("uso: ping INDIRIZZO [-n QUANTI] [-w MS] [-l BYTE]\n\n");
    printf("  -n  quante richieste mandare (predefinito 4)\n");
    printf("  -w  quanto aspettare ciascuna risposta, in ms (predefinito 2000)\n");
    printf("  -l  byte di riempimento dopo l'intestazione ICMP (predefinito 32)\n");
}

int main(int argc, char **argv)
{
    unsigned char ip[4], buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    int  quanti = 4, timeout = 2000, carico = 32;
    int  i, inviati = 0, ricevuti = 0;
    unsigned int min = 0xFFFFFFFFu, max = 0, somma = 0;

    if (argc < 2) { uso(); return 1; }
    if (!leggi_ip(argv[1], ip)) {
        printf("ping: '%s' non e' un indirizzo IPv4.\n", argv[1]);
        return 1;
    }

    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)      quanti  = atoi(argv[++i]);
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) timeout = atoi(argv[++i]);
        else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) carico  = atoi(argv[++i]);
        else { uso(); return 1; }
    }
    if (quanti  < 1)    quanti  = 1;
    if (timeout < 100)  timeout = 100;
    if (carico  < 0)    carico  = 0;
    if (carico  > 1024) carico  = 1024;

    pid_ip = ipc_lookup(IP_SERVIZIO);
    if (pid_ip <= 0) {
        printf("ping: lo stack IP non e' attivo.\n");
        printf("      Avvia nell'ordine:\n\n");
        printf("        /dev/pci.drv &\n");
        printf("        netdetect -c\n");
        printf("        /dev/ip.drv &\n");
        return 1;
    }

    printf("ping "); stampa_ip(ip);
    printf(" con %d byte di dati\n\n", carico);

    for (i = 1; i <= quanti; i++) {
        IpEcho         e;
        IpEchoRisposta r;

        memcpy(e.ip, ip, 4);
        e.seq        = (unsigned int)i;
        e.payload    = (unsigned int)carico;
        e.timeout_ms = (unsigned int)timeout;

        if (ipc_send(pid_ip, IP_MSG_ECHO, &e, sizeof(e)) < 0) {
            printf("ping: lo stack non accetta richieste.\n");
            return 1;
        }
        inviati++;

        /* Si aspetta un po' più della scadenza dello stack: è LUI a dover
         * dichiarare fallita la richiesta, non noi. Se scadessimo prima,
         * la sua risposta arriverebbe fuori tempo e verrebbe letta come
         * risposta alla richiesta SUCCESSIVA — cioè un ping che riporta il
         * tempo di quello prima. */
        if (attendi(IP_MSG_ECHO_R, buf, &len, (unsigned int)timeout + 1000) != 0 ||
            len < sizeof(r)) {
            printf("  seq=%d  lo stack non ha risposto\n", i);
            continue;
        }
        memcpy(&r, buf, sizeof(r));

        if (r.codice == 0) {
            ricevuti++;
            somma += r.rtt_ms;
            if (r.rtt_ms < min) min = r.rtt_ms;
            if (r.rtt_ms > max) max = r.rtt_ms;

            printf("  %d byte da ", carico + 28);
            stampa_ip(r.da);
            printf(": seq=%u ttl=%u ", r.seq, r.ttl);

            /* ⚠️ «0 ms» DICHIAREREBBE UNA PRECISIONE CHE NON ABBIAMO.
             * uptime_ms() conta i tick del PIT, che batte a 100 Hz:
             * avanza a scatti di 10 ms. Una risposta che torna in 300
             * microsecondi — cioe' tutte, su una rete emulata — cade
             * dentro lo stesso tick della partenza e misura zero.
             * Stampare «<10 ms» dice la stessa cosa senza far credere
             * che il tempo sia stato misurato e sia risultato nullo. */
            if (r.rtt_ms == 0) printf("tempo<10 ms\n");
            else               printf("tempo=%u ms\n", r.rtt_ms);
        } else {
            printf("  seq=%d  %s\n", i, spiega(r.codice));
        }

        if (i < quanti) usleep(1000 * 1000);
    }

    printf("\n--- statistiche per "); stampa_ip(ip); printf(" ---\n");
    printf("%d inviati, %d ricevuti, %d%% persi\n",
           inviati, ricevuti,
           inviati ? ((inviati - ricevuti) * 100 / inviati) : 0);
    if (ricevuti > 0) {
        /* Se nemmeno la risposta piu' lenta ha raggiunto un tick, stampare
         * tre zeri sarebbe una tabella di misure che non sono state fatte.
         * Si dice una cosa sola e vera. */
        if (max == 0)
            printf("tempi: tutti sotto i 10 ms, che e' il passo dell'orologio\n"
                   "       (il PIT batte a 100 Hz: piu' fine non si misura)\n");
        else
            printf("tempi: minimo %u ms, medio %u ms, massimo %u ms\n",
                   min, somma / (unsigned int)ricevuti, max);
    }

    return (ricevuti > 0) ? 0 : 1;
}
