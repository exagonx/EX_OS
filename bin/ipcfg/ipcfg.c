/* =============================================================================
 * bin/ipcfg/ipcfg.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Mostra e cambia la configurazione dello stack IP.
 *
 *   ipcfg                                        indirizzo e contatori
 *   ipcfg -a 192.168.1.10 -m 255.255.255.0 -g 192.168.1.1
 *   ipcfg -r                                     tabella ARP
 *
 * -----------------------------------------------------------------------------
 * PERCHE' I CONTATORI SONO LA META' DEL PROGRAMMA
 *
 * Quando la rete non va, la domanda vera non è «va o non va»: è dove si
 * ferma. I contatori dello stack la separano in pezzi che si possono
 * guardare uno per uno.
 *
 *   ip ricevuti a zero          nessun pacchetto arriva: guarda il driver
 *                               (`nettest -c` dice se la SCHEDA ha ricevuto)
 *   scartati che salgono        arrivano pacchetti ma non per noi:
 *                               l'indirizzo configurato è sbagliato
 *   somme errate che salgono    i byte arrivano corrotti, oppure la nostra
 *                               somma di controllo ha un difetto
 *   frammenti che salgono       qualcuno manda pacchetti più grandi della
 *                               nostra MTU: non li riassembliamo
 *   risposte ARP date > 0       qualcuno ci cerca e noi rispondiamo:
 *                               metà del cammino funziona di sicuro
 *
 * Senza questi numeri ogni guasto di rete diventa una serie di tentativi.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "dns.h"
#include "rete.h"

static int pid_ip = 0;


static void stampa_ip(const unsigned char *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 16; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, 2000) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;
        if (meta.tipo != tipo) continue;
        if (len) *len = meta.len;
        return 0;
    }
    return -1;
}

static int mostra_stato(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpStato       s;

    if (ipc_send(pid_ip, IP_MSG_STATO, NULL, 0) < 0) return 1;
    if (attendi(IP_MSG_STATO_R, buf, &len) != 0 || len < sizeof(s)) {
        printf("ipcfg: lo stack non risponde.\n");
        return 1;
    }
    memcpy(&s, buf, sizeof(s));

    printf("indirizzo   "); stampa_ip(s.cfg.ip);       printf("\n");
    printf("maschera    "); stampa_ip(s.cfg.maschera); printf("\n");
    printf("gateway     ");
    if ((s.cfg.gateway[0] | s.cfg.gateway[1] |
         s.cfg.gateway[2] | s.cfg.gateway[3]) == 0) printf("nessuno\n");
    else { stampa_ip(s.cfg.gateway); printf("\n"); }
    printf("DNS         ");
    if ((s.cfg.dns[0] | s.cfg.dns[1] | s.cfg.dns[2] | s.cfg.dns[3]) == 0)
        printf("nessuno\n");
    else { stampa_ip(s.cfg.dns); printf("\n"); }
    printf("MAC         %02x:%02x:%02x:%02x:%02x:%02x\n\n",
           s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5]);

    printf("IP inviati        %u\n", s.ip_inviati);
    printf("IP ricevuti       %u\n", s.ip_ricevuti);
    printf("scartati          %u\n", s.scartati);
    printf("somme errate      %u\n", s.checksum_errati);
    printf("frammenti         %u\n", s.frammenti);
    printf("ARP chiesti       %u\n", s.arp_richieste_inviate);
    printf("ARP risposti      %u\n", s.arp_risposte_date);
    printf("echo serviti      %u\n", s.echo_serviti);
    printf("UDP inviati       %u\n", s.udp_inviati);
    printf("UDP ricevuti      %u\n", s.udp_ricevuti);
    printf("UDP senza porta   %u\n", s.udp_senza_porta);
    return 0;
}

static int mostra_arp(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len, i;
    IpArpTabella  t;

    if (ipc_send(pid_ip, IP_MSG_ARP, NULL, 0) < 0) return 1;
    if (attendi(IP_MSG_ARP_R, buf, &len) != 0 || len < sizeof(t)) {
        printf("ipcfg: lo stack non risponde.\n");
        return 1;
    }
    memcpy(&t, buf, sizeof(t));

    if (t.n == 0) {
        printf("tabella ARP vuota.\n");
        printf("Si riempie da sola: ogni frame ARP che passa insegna un\n");
        printf("indirizzo, anche le richieste altrui.\n");
        return 0;
    }

    printf("%-16s %-18s scade fra\n", "indirizzo", "MAC");
    for (i = 0; i < t.n; i++) {
        char riga[20];

        sprintf(riga, "%u.%u.%u.%u", t.voce[i].ip[0], t.voce[i].ip[1],
                t.voce[i].ip[2], t.voce[i].ip[3]);
        printf("%-16s %02x:%02x:%02x:%02x:%02x:%02x  %u s\n", riga,
               t.voce[i].mac[0], t.voce[i].mac[1], t.voce[i].mac[2],
               t.voce[i].mac[3], t.voce[i].mac[4], t.voce[i].mac[5],
               t.voce[i].scade_fra_ms / 1000);
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpConfig      c;
    IpStato       s;
    IpEsito       e;
    int           i, cambia = 0;

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    if (argc == 1) return mostra_stato();
    if (argc == 2 && strcmp(argv[1], "-r") == 0) return mostra_arp();

    /* Si parte dalla configurazione ATTUALE e si cambiano solo i campi
     * indicati: `ipcfg -g 1.2.3.4` non deve azzerare l'indirizzo solo
     * perché non è stato ripetuto sulla riga di comando. */
    if (ipc_send(pid_ip, IP_MSG_STATO, NULL, 0) < 0) return 1;
    if (attendi(IP_MSG_STATO_R, buf, &len) != 0 || len < sizeof(s)) {
        printf("ipcfg: lo stack non risponde.\n");
        return 1;
    }
    memcpy(&s, buf, sizeof(s));
    c = s.cfg;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            if (!ip_da_stringa(argv[++i], c.ip)) goto malformato;
            cambia = 1;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            if (!ip_da_stringa(argv[++i], c.maschera)) goto malformato;
            cambia = 1;
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            if (!ip_da_stringa(argv[++i], c.gateway)) goto malformato;
            cambia = 1;
        } else {
            printf("uso: ipcfg                     indirizzo e contatori\n");
            printf("     ipcfg -a IND [-m MASC] [-g GW]   cambia la configurazione\n");
            printf("     ipcfg -r                  tabella ARP\n");
            return 1;
        }
    }

    if (!cambia) return mostra_stato();

    if (ipc_send(pid_ip, IP_MSG_CONFIG, &c, sizeof(c)) < 0) return 1;
    if (attendi(IP_MSG_ESITO, buf, &len) != 0 || len < sizeof(e)) {
        printf("ipcfg: lo stack non ha confermato.\n");
        return 1;
    }
    memcpy(&e, buf, sizeof(e));
    if (e.codice != 0) {
        printf("ipcfg: rifiutata (%d)\n", e.codice);
        return 1;
    }

    return mostra_stato();

malformato:
    printf("ipcfg: '%s' non e' un indirizzo IPv4.\n", argv[i]);
    return 1;
}
