/* =============================================================================
 * bin/nettest/nettest.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Prova un driver di rete: invia un frame vero e mostra cosa torna.
 *
 *   nettest              stato dell'interfaccia
 *   nettest -a IP        manda una richiesta ARP e aspetta la risposta
 *   nettest -s [N]       ascolta e stampa N frame (predefinito 5)
 *   nettest -c           contatori del driver
 *
 * -----------------------------------------------------------------------------
 * PERCHE' ARP E NON PING
 *
 * Perche' ARP e' il primo scambio che si puo' fare senza avere uno stack.
 * Sta subito sopra Ethernet — niente IP, niente checksum, niente
 * frammentazione — e una risposta che arriva dimostra in un colpo solo
 * che la scheda trasmette, che il cavo (o il NAT dell'emulatore) porta il
 * frame a destinazione, che la scheda riceve e che la catena
 * driver -> IPC -> programma consegna i byte giusti.
 *
 * Se ARP funziona, quello che manca a `ping` e' solo software. Se ARP non
 * funziona, `ping` non direbbe DOVE si e' rotto.
 *
 * ⚠️ QUESTO PROGRAMMA NON E' UNO STACK DI RETE e non deve diventarlo.
 * Compone un frame a mano per provare il driver. Lo stack vero sara' un
 * processo autonomo con la sua tabella ARP, i suoi timer e le sue
 * ritrasmissioni; questo file resta uno strumento di diagnosi, cioe' la
 * cosa che si usa quando lo stack non funziona e serve sapere se la colpa
 * e' sua o della scheda.
 * ============================================================================= */

#include "libc.h"
#include "net_proto.h"

#define ETH_TIPO_ARP   0x0806
#define ETH_TIPO_IP    0x0800
#define ETH_TIPO_IPV6  0x86DD

#define ARP_RICHIESTA  1
#define ARP_RISPOSTA   2

/* Indirizzo di partenza. In QEMU con la rete "user" l'ospite e' 10.0.2.15
 * e il gateway 10.0.2.2: sono i valori predefiniti dello slirp, e finche'
 * non c'e' un client DHCP sono anche gli unici che si possono usare. */
static unsigned char g_mio_ip[4] = { 10, 0, 2, 15 };

static int pid_rete = 0;

/* --------------------------------------------------------------------------
 * Dialogo col driver
 *
 * Come in netdetect: si controlla CHI ha risposto, perche' ipc_recv
 * consegna il prossimo messaggio della mailbox e non "la risposta alla
 * mia domanda", e si usa la versione con scadenza perche' un driver che
 * muore fra domanda e risposta lascerebbe questo programma fermo per
 * sempre.
 * -------------------------------------------------------------------------- */
static int attendi(unsigned int tipo_atteso, unsigned char *buf,
                   unsigned int *out_len, unsigned int ms)
{
    IpcMessage meta;
    int        tentativi;

    for (tentativi = 0; tentativi < 16; tentativi++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_rete) continue;
        if (meta.tipo != tipo_atteso) continue;
        if (out_len) *out_len = meta.len;
        return 0;
    }
    return -1;
}

static int leggi_stato(NetStato *s)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;

    if (ipc_send(pid_rete, NET_MSG_INFO, NULL, 0) < 0) return -1;
    if (attendi(NET_MSG_STATO, buf, &len, 2000) != 0) return -1;
    if (len < sizeof(*s)) return -1;
    memcpy(s, buf, sizeof(*s));
    return 0;
}

static void stampa_mac(const unsigned char *m)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void stampa_ip(const unsigned char *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

/* "10.0.2.2" -> quattro byte. Ritorna 0 se la stringa non e' un
 * indirizzo: dirlo e' meglio che accettare "pippo" come 0.0.0.0 e poi
 * far cercare all'utente perche' nessuno risponde. */
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

/* --------------------------------------------------------------------------
 * ARP
 * -------------------------------------------------------------------------- */
static unsigned int componi_arp(unsigned char *f, const unsigned char *mio_mac,
                                const unsigned char *ip_cercato)
{
    int i;

    for (i = 0; i < 6; i++) f[i] = 0xFF;              /* a tutti */
    for (i = 0; i < 6; i++) f[6 + i] = mio_mac[i];
    f[12] = ETH_TIPO_ARP >> 8; f[13] = ETH_TIPO_ARP & 0xFF;

    f[14] = 0x00; f[15] = 0x01;                       /* hardware: Ethernet */
    f[16] = 0x08; f[17] = 0x00;                       /* protocollo: IPv4 */
    f[18] = 6;    f[19] = 4;                          /* lunghezze */
    f[20] = 0x00; f[21] = ARP_RICHIESTA;

    for (i = 0; i < 6; i++) f[22 + i] = mio_mac[i];
    for (i = 0; i < 4; i++) f[28 + i] = g_mio_ip[i];
    for (i = 0; i < 6; i++) f[32 + i] = 0x00;         /* quello che chiediamo */
    for (i = 0; i < 4; i++) f[38 + i] = ip_cercato[i];

    /* 42 byte. Il driver li riempie fino a 60 da solo: e' un vincolo del
     * mezzo, non di chi compone il pacchetto. */
    return 42;
}

static const char *nome_tipo(unsigned int t)
{
    switch (t) {
    case ETH_TIPO_ARP:  return "ARP";
    case ETH_TIPO_IP:   return "IPv4";
    case ETH_TIPO_IPV6: return "IPv6";
    default:            return "?";
    }
}

static void stampa_frame(const unsigned char *f, unsigned int len)
{
    unsigned int tipo = ((unsigned int)f[12] << 8) | f[13];

    printf("  %u byte  ", len);
    stampa_mac(f + 6); printf(" -> "); stampa_mac(f);
    printf("  %s (0x%04x)\n", nome_tipo(tipo), tipo);

    if (tipo == ETH_TIPO_ARP && len >= 42) {
        unsigned int op = ((unsigned int)f[20] << 8) | f[21];

        printf("      ARP %s: ", (op == ARP_RISPOSTA) ? "risposta" :
                                 (op == ARP_RICHIESTA) ? "richiesta" : "?");
        stampa_ip(f + 28); printf(" e' "); stampa_mac(f + 22);
        printf(", cerca "); stampa_ip(f + 38); printf("\n");
    } else if (tipo == ETH_TIPO_IP && len >= 34) {
        printf("      IPv4 "); stampa_ip(f + 26);
        printf(" -> "); stampa_ip(f + 30);
        printf("  protocollo %u\n", f[23]);
    }
}

static int prova_arp(const unsigned char *ip)
{
    unsigned char frame[NET_FRAME_MAX], buf[IPC_MSG_MAX_DATA];
    unsigned int  len, n;
    NetStato      s;
    NetEsito      e;
    int           giri;

    if (leggi_stato(&s) != 0) {
        printf("nettest: il driver non risponde a NET_MSG_INFO.\n");
        return 1;
    }

    n = componi_arp(frame, s.mac, ip);

    printf("chi ha "); stampa_ip(ip);
    printf("? lo chiede "); stampa_mac(s.mac); printf(" (");
    stampa_ip(g_mio_ip); printf(")\n\n");

    if (ipc_send(pid_rete, NET_MSG_INVIA, frame, n) < 0) {
        printf("nettest: invio fallito (IPC)\n");
        return 1;
    }
    if (attendi(NET_MSG_ESITO, buf, &len, 2000) != 0 || len < sizeof(e)) {
        printf("nettest: il driver non ha confermato l'invio.\n");
        return 1;
    }
    memcpy(&e, buf, sizeof(e));
    if (e.codice != 0) {
        printf("nettest: il driver ha rifiutato il frame (%d)\n", e.codice);
        return 1;
    }

    /* Si aspetta una RISPOSTA per il nostro indirizzo. Sul cavo passa
     * anche altro — richieste ARP di terzi, IPv6, DHCP — quindi si
     * scartano i frame che non sono la risposta cercata invece di
     * dichiarare vittoria al primo che arriva. */
    for (giri = 0; giri < 12; giri++) {
        if (ipc_send(pid_rete, NET_MSG_RICEVI, NULL, 0) < 0) break;
        if (attendi(NET_MSG_FRAME, buf, &len, 1000) != 0) continue;
        if (len < 14) continue;

        stampa_frame(buf, len);

        if (len >= 42 &&
            buf[12] == 0x08 && buf[13] == 0x06 &&
            buf[21] == ARP_RISPOSTA &&
            buf[28] == ip[0] && buf[29] == ip[1] &&
            buf[30] == ip[2] && buf[31] == ip[3]) {
            printf("\nRisposta ricevuta: "); stampa_ip(ip);
            printf(" ha indirizzo "); stampa_mac(buf + 22); printf("\n");
            printf("La catena scheda -> driver -> IPC funziona in entrambi i sensi.\n");
            return 0;
        }
    }

    printf("\nNessuna risposta da "); stampa_ip(ip); printf(".\n");
    printf("Il frame e' partito (il driver l'ha confermato): o non c'e'\n");
    printf("nessuno a quell'indirizzo, o la ricezione non funziona.\n");
    printf("`nettest -c` dice se la scheda ha ricevuto qualcosa.\n");
    return 1;
}

static int ascolta(int quanti)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    int           visti = 0, giri;

    printf("in ascolto (%d frame)...\n\n", quanti);

    for (giri = 0; giri < quanti * 20 && visti < quanti; giri++) {
        if (ipc_send(pid_rete, NET_MSG_RICEVI, NULL, 0) < 0) return 1;
        if (attendi(NET_MSG_FRAME, buf, &len, 1000) != 0) continue;
        if (len < 14) continue;
        stampa_frame(buf, len);
        visti++;
    }

    if (visti == 0) printf("nessun frame in arrivo.\n");
    return 0;
}

static int contatori(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    NetContatori  c;

    if (ipc_send(pid_rete, NET_MSG_CONTATORI, NULL, 0) < 0) return 1;
    if (attendi(NET_MSG_CONTEGGI, buf, &len, 2000) != 0 || len < sizeof(c)) {
        printf("nettest: il driver non risponde.\n");
        return 1;
    }
    memcpy(&c, buf, sizeof(c));

    printf("inviati        %u\n", c.inviati);
    printf("ricevuti       %u\n", c.ricevuti);
    printf("errori invio   %u\n", c.errori_tx);
    printf("errori ricez.  %u\n", c.errori_rx);
    printf("troppo grandi  %u\n", c.troppo_grandi);
    printf("persi in coda  %u\n", c.persi_coda);
    printf("traboccamenti  %u\n", c.overflow);
    printf("notifiche IRQ  %u\n", c.notifiche_irq);
    printf("battiti        %u\n", c.battiti);
    return 0;
}

int main(int argc, char **argv)
{
    pid_rete = ipc_lookup(NET_SERVIZIO_0);
    if (pid_rete <= 0) {
        printf("nettest: il servizio '%s' non e' attivo.\n", NET_SERVIZIO_0);
        printf("         Avvia prima il bus e poi la scheda:\n\n");
        printf("           /dev/pci.drv &\n");
        printf("           /dev/ne2k.drv &\n");
        return 1;
    }

    if (argc == 1) {
        NetStato s;

        if (leggi_stato(&s) != 0) {
            printf("nettest: il driver non risponde.\n");
            return 1;
        }
        printf("interfaccia %s\n", NET_SERVIZIO_0);
        printf("  scheda  %s\n", s.modello);
        printf("  MAC     "); stampa_mac(s.mac); printf("\n");
        printf("  MTU     %u\n", s.mtu);
        printf("  porte   0x%x\n", s.porta_base);
        printf("  IRQ     %u\n", s.irq);
        return 0;
    }

    if (strcmp(argv[1], "-a") == 0 && argc == 3) {
        unsigned char ip[4];

        if (!leggi_ip(argv[2], ip)) {
            printf("nettest: '%s' non e' un indirizzo IPv4.\n", argv[2]);
            return 1;
        }
        return prova_arp(ip);
    }

    if (strcmp(argv[1], "-s") == 0) {
        int quanti = (argc == 3) ? atoi(argv[2]) : 5;

        if (quanti < 1) quanti = 1;
        return ascolta(quanti);
    }

    if (strcmp(argv[1], "-c") == 0 && argc == 2) return contatori();

    printf("uso: nettest              stato dell'interfaccia\n");
    printf("     nettest -a IP        richiesta ARP, aspetta la risposta\n");
    printf("     nettest -s [N]       ascolta e stampa N frame\n");
    printf("     nettest -c           contatori del driver\n");
    return 1;
}
