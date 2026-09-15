/* =============================================================================
 * bin/dhcp/dhcp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Chiede alla rete un indirizzo e configura lo stack.
 *
 *   dhcp            chiede e applica
 *   dhcp -n         chiede e stampa cosa avrebbe applicato, senza applicarlo
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' UN PROGRAMMA E NON UN PEZZO DELLO STACK
 *
 * DHCP e' un protocollo applicativo: sta sopra UDP come ci sta un client
 * DNS o un browser. Metterlo dentro /dev/ip.drv vorrebbe dire che lo stack
 * — il pezzo che deve restare acceso — contiene anche la logica di
 * negoziazione, i timer di rinnovo e l'analisi di una tabella di opzioni
 * scritta da qualcun altro. Un errore li' dentro spegne la rete; qui
 * dentro fa fallire un comando.
 *
 * -----------------------------------------------------------------------------
 * ! SI PARTE AZZERANDO L'INDIRIZZO, E NON E' UN EFFETTO COLLATERALE
 *
 * Un client DHCP deve mandare i propri pacchetti con indirizzo sorgente
 * 0.0.0.0: e' l'unico modo di dire «non ho un indirizzo, e' per questo che
 * sto chiedendo». Lo stack mette come sorgente quello configurato, quindi
 * PRIMA di cominciare si configura 0.0.0.0.
 *
 * La configurazione precedente viene salvata e RIMESSA se la negoziazione
 * fallisce. Senza, un `dhcp` andato male su una macchina configurata a
 * mano la lascerebbe senza rete — cioe' il comando che doveva dare un
 * indirizzo lo toglie, ed e' il modo peggiore di fallire.
 *
 * -----------------------------------------------------------------------------
 * ! IL BIT DI BROADCAST NELLA RICHIESTA
 *
 * Nel campo `flags` si alza il bit alto. Dice al server: «rispondimi in
 * broadcast, non al mio indirizzo». Deve farlo, perche' l'indirizzo che
 * sta per darci non ce l'abbiamo ancora e non risponderemmo a un ARP per
 * quell'indirizzo. Senza questo bit un server rigoroso manda la risposta a
 * un indirizzo che nessuno rivendica, e la negoziazione non finisce mai.
 *
 * -----------------------------------------------------------------------------
 * COSA NON FA
 *
 *   - NON RINNOVA. Prende la concessione e finisce. Il rinnovo vuole un
 *     processo che resti acceso a meta' del tempo di scadenza, ed e' un
 *     programma diverso da questo — che deve poter essere lanciato a mano
 *     e finire.
 *   - NON RILASCIA (DHCPRELEASE) e non fa DECLINE.
 *   - PRENDE LA PRIMA OFFERTA. Con piu' server si dovrebbe aspettare un
 *     po' e scegliere; qui la prima che arriva va bene, perche' su una
 *     rete con un solo server — cioe' quasi sempre — il risultato e' lo
 *     stesso e l'attesa e' tempo buttato.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "rete.h"

/* +0.001 a ogni modifica: `dhcp -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("dhcp", "0.002");

/* Porte fissate dalla specifica: non si scelgono. */
#define PORTA_CLIENT   68
#define PORTA_SERVER   67

/* Parte fissa di un pacchetto BOOTP, prima delle opzioni */
#define BOOTP_FISSO    236

#define OP_RICHIESTA   1
#define HTYPE_ETH      1

/* Tipi di messaggio (opzione 53) */
#define DHCP_DISCOVER  1
#define DHCP_OFFER     2
#define DHCP_REQUEST   3
#define DHCP_ACK       5
#define DHCP_NAK       6

/* Opzioni che ci interessano */
#define OPZ_MASCHERA   1
#define OPZ_ROUTER     3
#define OPZ_DNS        6
#define OPZ_IP_CHIESTO 50
#define OPZ_SCADENZA   51
#define OPZ_TIPO       53
#define OPZ_SERVER     54
#define OPZ_RICHIESTE  55
#define OPZ_FINE       255

#define TENTATIVI      4
#define ATTESA_MS      3000

static int pid_ip = 0;
static unsigned char g_mac[6];
static unsigned int  g_xid;

/* --------------------------------------------------------------------------
 * Dialogo con lo stack
 * -------------------------------------------------------------------------- */
static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
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

static int esito_stack(unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpEsito       e;

    if (attendi(IP_MSG_ESITO, buf, &len, ms) != 0 || len < sizeof(e)) return -1;
    memcpy(&e, buf, sizeof(e));
    return e.codice;
}

static int leggi_stato(IpStato *s)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;

    if (ipc_send(pid_ip, IP_MSG_STATO, NULL, 0) < 0) return -1;
    if (attendi(IP_MSG_STATO_R, buf, &len, 2000) != 0 || len < sizeof(*s))
        return -1;
    memcpy(s, buf, sizeof(*s));
    return 0;
}

static int applica(const IpConfig *c)
{
    if (ipc_send(pid_ip, IP_MSG_CONFIG, c, sizeof(*c)) < 0) return -1;
    return (esito_stack(2000) == 0) ? 0 : -1;
}

/* --------------------------------------------------------------------------
 * Composizione del pacchetto
 * -------------------------------------------------------------------------- */
static void metti32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static unsigned int componi(unsigned char *p, int tipo,
                            const unsigned char *ip_chiesto,
                            const unsigned char *server)
{
    unsigned int o;

    memset(p, 0, BOOTP_FISSO + 4);

    p[0] = OP_RICHIESTA;
    p[1] = HTYPE_ETH;
    p[2] = 6;                       /* lunghezza dell'indirizzo hardware */
    p[3] = 0;                       /* hops */
    metti32(p + 4, g_xid);
    p[10] = 0x80;                   /* flags: rispondimi in broadcast */
    memcpy(p + 28, g_mac, 6);       /* chaddr */

    /* La "parola magica" che distingue DHCP dal BOOTP puro. Senza, il
     * server ignora tutte le opzioni che seguono. */
    p[236] = 99; p[237] = 130; p[238] = 83; p[239] = 99;

    o = 240;
    p[o++] = OPZ_TIPO;  p[o++] = 1; p[o++] = (unsigned char)tipo;

    if (ip_chiesto) {
        p[o++] = OPZ_IP_CHIESTO; p[o++] = 4;
        memcpy(p + o, ip_chiesto, 4); o += 4;
    }
    if (server) {
        p[o++] = OPZ_SERVER; p[o++] = 4;
        memcpy(p + o, server, 4); o += 4;
    }

    /* Si chiede esplicitamente cosa serve. Un server puo' mandare solo
     * l'indirizzo se nessuno gli chiede altro, e un indirizzo senza
     * maschera ne' gateway non basta a uscire dalla rete locale. */
    p[o++] = OPZ_RICHIESTE; p[o++] = 3;
    p[o++] = OPZ_MASCHERA;  p[o++] = OPZ_ROUTER; p[o++] = OPZ_DNS;

    p[o++] = OPZ_FINE;

    /* ! SI RIEMPIE FINO A 300 BYTE. Certi server (e certi apparati di
     * rete che rilanciano il traffico DHCP) scartano i pacchetti piu'
     * corti del minimo BOOTP. Costa qualche zero e toglie di mezzo una
     * categoria di guasti che si manifesta solo su una rete e non su
     * un'altra. */
    while (o < 300) p[o++] = 0;

    return o;
}

/* Cerca un'opzione. Ritorna la lunghezza e mette il puntatore in *val. */
static unsigned int opzione(const unsigned char *p, unsigned int len,
                            unsigned char cercata, const unsigned char **val)
{
    unsigned int o = 240;

    if (len < 244) return 0;
    if (p[236] != 99 || p[237] != 130 || p[238] != 83 || p[239] != 99) return 0;

    while (o < len) {
        unsigned char t = p[o];
        unsigned int  l;

        if (t == OPZ_FINE) return 0;
        if (t == 0) { o++; continue; }          /* riempimento */
        if (o + 1 >= len) return 0;

        l = p[o + 1];
        /* ! Una lunghezza che esce dal pacchetto e' un pacchetto
         * malformato, non un'opzione lunga: seguirla vorrebbe dire leggere
         * memoria che non fa parte di quello che e' arrivato. */
        if (o + 2 + l > len) return 0;

        if (t == cercata) { if (val) *val = p + o + 2; return l; }
        o += 2 + l;
    }
    return 0;
}

static void stampa_ip(const unsigned char *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

/* --------------------------------------------------------------------------
 * Uno scambio: manda, aspetta una risposta del tipo giusto
 * -------------------------------------------------------------------------- */
/* Uno scambio: manda, aspetta una risposta del tipo giusto.
 *
 * ! SI PRENOTA LA RICEZIONE PRIMA DI MANDARE, e l'ordine non e' un
 * dettaglio di stile.
 *
 * Lo stack non tiene in coda i datagrammi per cui nessuno sta aspettando:
 * li conta e li butta (vedi ip_proto.h, e c'e' un motivo). La prima
 * versione di questa funzione mandava il DISCOVER, aspettava la conferma
 * di invio, e SOLO POI chiedeva un datagramma. In quella finestra la
 * risposta del server arriva gia' — su una rete emulata torna in
 * microsecondi — e finiva scartata.
 *
 * Non si vedeva come un guasto: si vedeva come un DHCP che ci mette tre
 * secondi in piu' del necessario, perche' la ritrasmissione successiva
 * andava a buon fine. L'ho trovato guardando i contatori: tre datagrammi
 * inviati dove ne bastavano due.
 *
 * ! E SI ACCETTANO I DUE MESSAGGI IN QUALUNQUE ORDINE. Dopo l'invio
 * possono arrivare l'esito dello stack e il datagramma del server, e non
 * e' detto quale prima. Aspettare esplicitamente l'esito significherebbe
 * scartare il datagramma se arriva per primo — che e' lo stesso guasto di
 * sopra spostato di due righe. */
static int scambio(int tipo_inviato, int tipo_atteso,
                   const unsigned char *ip_chiesto, const unsigned char *server,
                   unsigned char *risposta, unsigned int *lunghezza)
{
    static unsigned char msg[sizeof(IpUdpInvia) + 512];
    unsigned char        buf[IPC_MSG_MAX_DATA];
    IpUdpInvia           inv;
    IpUdpApri            a;
    IpcMessage           meta;
    unsigned int         n, scadenza;
    int                  tentativo;

    a.porta = PORTA_CLIENT;

    for (tentativo = 0; tentativo < TENTATIVI; tentativo++) {

        /* Prima la prenotazione. */
        if (ipc_send(pid_ip, IP_MSG_UDP_RICEVI, &a, sizeof(a)) < 0) return -1;

        n = componi(msg + sizeof(inv), tipo_inviato, ip_chiesto, server);

        inv.ip[0] = inv.ip[1] = inv.ip[2] = inv.ip[3] = 255;   /* a tutti */
        inv.porta        = PORTA_SERVER;
        inv.porta_locale = PORTA_CLIENT;
        memcpy(msg, &inv, sizeof(inv));

        if (ipc_send(pid_ip, IP_MSG_UDP_INVIA, msg, sizeof(inv) + n) < 0)
            return -1;

        scadenza = uptime_ms() + ATTESA_MS;

        while ((int)(uptime_ms() - scadenza) < 0) {
            IpUdpDati            d;
            const unsigned char *p, *v;

            if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) break;
            if ((int)meta.sender_pid != pid_ip) continue;

            if (meta.tipo == IP_MSG_ESITO) {
                IpEsito e;

                if (meta.len >= sizeof(e)) {
                    memcpy(&e, buf, sizeof(e));
                    if (e.codice != 0) return -1;   /* non e' nemmeno partito */
                }
                continue;
            }

            if (meta.tipo != IP_MSG_UDP_DATI) continue;
            if (meta.len < sizeof(d)) continue;

            memcpy(&d, buf, sizeof(d));
            if (d.len < 244 || sizeof(d) + d.len > meta.len) continue;
            p = buf + sizeof(d);

            /* ! SI CONTROLLA xid E chaddr. Su una rete con piu' client che
             * chiedono insieme, le risposte agli altri arrivano anche a noi
             * (sono in broadcast): prendere la prima che passa
             * significherebbe configurarsi con l'indirizzo di un altro. */
            if (p[0] != 2) goto riprenota;                 /* non e' una risposta */
            if (((unsigned int)p[4] << 24 | (unsigned int)p[5] << 16 |
                 (unsigned int)p[6] << 8  | p[7]) != g_xid) goto riprenota;
            if (memcmp(p + 28, g_mac, 6) != 0) goto riprenota;

            if (opzione(p, d.len, OPZ_TIPO, &v) != 1) goto riprenota;
            if (v[0] == DHCP_NAK) return -2;               /* rifiuto esplicito */
            if (v[0] != tipo_atteso) goto riprenota;

            memcpy(risposta, p, d.len);
            *lunghezza = d.len;
            return 0;

riprenota:
            /* Il datagramma non era per noi: la prenotazione si consuma
             * comunque, quindi va rifatta o il prossimo verrebbe buttato. */
            if (ipc_send(pid_ip, IP_MSG_UDP_RICEVI, &a, sizeof(a)) < 0) return -1;
        }
    }
    return -1;
}

/* =============================================================================
 * ! ASPETTARE LO STACK — perche' `dhcp` sta in [modules] di kernel.cfg
 *
 * Fino al 24 agosto 2026 la rete si accendeva da /boot/autoexec.sh, dove le
 * righe sono in ordine e ognuna aspetta la precedente. Adesso i quattro anelli
 * — bus, scheda, stack, indirizzo — sono voci di [modules], e il kernel li
 * avvia TUTTI INSIEME: quando questo programma comincia, lo stack IP puo'
 * ancora star chiedendo alla scheda chi e'.
 *
 * Due secondi bastavano a un servizio lanciato con '&' due righe sopra; non
 * bastano a una catena intera che si accende in parallelo su una macchina
 * lenta. E arrendersi qui vuol dire una macchina che si avvia senza indirizzo
 * — e che al riavvio dopo ce l'ha, perche' i tempi cambiano.
 *
 * ! MA CHI HA BATTUTO `dhcp` A MANO NON DEVE ASPETTARE DIECI SECONDI per
 * sentirsi dire che la rete non e' accesa. La distinzione non e' «chi mi ha
 * lanciato»: e' se una catena stia salendo o no. Passata la pazienza breve,
 * se non e' acceso NEMMENO il primo anello — il bus PCI — non c'e' niente in
 * arrivo, e si risponde subito. All'avvio il bus si registra in poche decine
 * di millisecondi, quindi il caso vero passa di la'.
 * ============================================================================= */
#define ATTESA_STACK_MS    10000
#define PAZIENZA_MS         2000
#define PASSO_MS             100

static int attendi_stack(void)
{
    unsigned int t;
    int          pid;

    for (t = 0; ; t += PASSO_MS) {
        pid = ipc_lookup(IP_SERVIZIO);
        if (pid > 0) return pid;
        if (t >= ATTESA_STACK_MS) return pid;
        if (t >= PAZIENZA_MS && rete_primo_mancante() == RETE_PASSO_PCI)
            return pid;                  /* nessuna catena in salita */
        usleep(PASSO_MS * 1000);
    }
}

/* =============================================================================
 * Cosa c'e' dentro una risposta del server
 *
 * ! UNA SOLA VOLTA, PERCHE' LA USANO IN DUE. La prima richiesta e il rinnovo
 * leggono la stessa risposta, e due copie di questo codice sarebbero due copie
 * che col tempo divergono — quella del rinnovo per prima, perche' e' la meno
 * guardata.
 * ========================================================================== */
static unsigned int g_scadenza_letta = 0;

static int leggi_config(const unsigned char *risposta, unsigned int len,
                        IpConfig *fuori)
{
    const unsigned char *v;

    memset(fuori, 0, sizeof(*fuori));
    memcpy(fuori->ip, risposta + 16, 4);

    if (opzione(risposta, len, OPZ_MASCHERA, &v) == 4)
        memcpy(fuori->maschera, v, 4);
    if (opzione(risposta, len, OPZ_ROUTER, &v) >= 4)
        memcpy(fuori->gateway, v, 4);
    if (opzione(risposta, len, OPZ_DNS, &v) >= 4)
        memcpy(fuori->dns, v, 4);

    if (opzione(risposta, len, OPZ_SCADENZA, &v) == 4)
        g_scadenza_letta = ((unsigned int)v[0] << 24) |
                           ((unsigned int)v[1] << 16) |
                           ((unsigned int)v[2] << 8)  | v[3];

    /* ! SENZA MASCHERA NON SI PUO' INSTRADARE NIENTE, e certi server non la
     * mandano se non gliela si chiede (noi lo facciamo, ma non tutti
     * rispondono). Si mette quella implicita dalla classe dell'indirizzo: e'
     * una regola vecchia e imprecisa, ma e' meglio di una maschera nulla, che
     * renderebbe locale ogni indirizzo del mondo. */
    if ((fuori->maschera[0] | fuori->maschera[1] |
         fuori->maschera[2] | fuori->maschera[3]) == 0) {
        fuori->maschera[0] = 255;
        if (fuori->ip[0] >= 128) fuori->maschera[1] = 255;
        if (fuori->ip[0] >= 192) fuori->maschera[2] = 255;
        printf("dhcp: il server non ha mandato la maschera, uso ");
        stampa_ip(fuori->maschera); printf("\n");
    }

    return 0;
}

/* =============================================================================
 * IL RINNOVO — restare acceso e richiedere prima che scada
 *
 * ! UNA CONCESSIONE CHE NON SI RINNOVA E' UN INDIRIZZO CHE SPARISCE, e sparisce
 * senza che nessuno lo dica. Il server lo assegna per un tempo — su una rete
 * di casa dodici o ventiquattro ore, in un albergo anche dieci minuti — e
 * quando quel tempo finisce lo riassegna a chi capita. La macchina resta
 * convinta di avercelo, e da quel momento parla a vuoto: i pacchetti escono e
 * non torna niente, che e' il guasto piu' difficile da capire perche' non
 * assomiglia a un guasto.
 *
 * ! SI RINNOVA A META', NON ALLA FINE, e non e' prudenza esagerata: e' quello
 * che dice il protocollo. Il tempo T1 della RFC 2131 e' meta' della
 * concessione, e serve proprio perche' un rinnovo puo' fallire — il server
 * puo' essere spento un momento, la rete staccata — e a meta' strada c'e'
 * ancora tutta l'altra meta' per riprovare.
 *
 * ! E IL RINNOVO E' UN REQUEST DIRETTO AL SERVER, senza il giro del DISCOVER:
 * si sa gia' chi ce l'ha dato e cosa ci ha dato, e si chiede di tenerlo. Se
 * quello non risponde, allora si ricomincia da capo — ma solo allora.
 *
 * ! CHI RESTA ACCESO NON DEVE PARLARE, se non ha niente da dire. Un
 * sorvegliante che scrive una riga a ogni giro copre tutto il resto del log:
 * si dice quando l'indirizzo CAMBIA, quando un rinnovo fallisce, e basta.
 * E' la stessa lezione di automount.
 * ========================================================================== */
static unsigned int g_scadenza_s = 0;   /* durata della concessione, secondi */

/* Quanti secondi aspettare prima del prossimo tentativo. */
static unsigned int meta_concessione(void)
{
    /* ! UN MINIMO C'E', E SERVE. Un server che desse una concessione di dieci
     * secondi farebbe girare questo ciclo cinque volte al minuto per sempre.
     * Sotto il minuto non si scende. */
    unsigned int t = g_scadenza_s / 2;

    if (g_scadenza_s == 0) return 3600;      /* non l'ha detto: un'ora */
    if (t < 60) return 60;
    return t;
}

static void sorveglia(const unsigned char *server, IpConfig *attuale)
{
    unsigned char risposta[600];
    unsigned int  len;
    int           falliti = 0;

    printf("dhcp: resto acceso e rinnovo fra %u s.\n", meta_concessione());

    for (;;) {
        IpConfig nuova;

        /* usleep vuole microsecondi, e un'ora ci sta in trentadue bit
         * solo se si conta a pezzi. Si dorme un minuto per volta. */
        unsigned int restano = meta_concessione();

        while (restano > 0) {
            unsigned int fetta = (restano > 60) ? 60 : restano;

            usleep(fetta * 1000000u);
            restano -= fetta;
        }

        /* Un identificativo nuovo per ogni scambio: due richieste con lo
         * stesso numero sono, per il server, la stessa richiesta. */
        g_xid = (uptime_ms() << 8) ^ ((unsigned int)getpid() << 20) ^ 0x52494E4Fu;

        nuova = *attuale;

        if (scambio(DHCP_REQUEST, DHCP_ACK, server, attuale->ip,
                    risposta, &len) != 0) {
            falliti++;
            printf("dhcp: il rinnovo non e' riuscito (%d di fila).\n", falliti);

            /* ! DOPO TRE FALLIMENTI SI RICOMINCIA DA CAPO. Il server puo'
             * essere cambiato — un altro router, un'altra rete — e insistere
             * con chi non c'e' piu' non porta da nessuna parte. */
            if (falliti >= 3) {
                printf("dhcp: ricomincio la ricerca da zero.\n");
                return;
            }
            continue;
        }

        falliti = 0;

        if (leggi_config(risposta, len, &nuova) == 0 &&
            memcmp(nuova.ip, attuale->ip, 4) != 0) {
            printf("dhcp: l'indirizzo e' cambiato: ");
            stampa_ip(nuova.ip);
            printf("\n");
            applica(&nuova);
            *attuale = nuova;
        }
    }
}

int main(int argc, char **argv)
{
    unsigned char risposta[600];
    unsigned int  len;
    IpStato       s;
    IpConfig      precedente, vuota, nuova;
    const unsigned char *v;
    unsigned char server[4];
    int           solo_prova = 0, resta = 0, rc;
    int           i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) { solo_prova = 1; continue; }
        if (strcmp(argv[i], "-r") == 0) { resta = 1; continue; }
        printf("uso: dhcp        chiede un indirizzo e lo applica\n");
        printf("     dhcp -r     e poi resta acceso a rinnovarlo (con '&')\n");
        printf("     dhcp -n     chiede e stampa, senza applicare\n\n");
        printf("! LA CONCESSIONE SCADE. Il server assegna l'indirizzo per un\n");
        printf("  tempo — in casa dodici ore, in un albergo dieci minuti — e\n");
        printf("  poi lo riassegna a chi capita. Senza -r la macchina resta\n");
        printf("  convinta di averlo e parla a vuoto, che e' il guasto piu'\n");
        printf("  difficile da capire perche' non sembra un guasto.\n");
        return 1;
    }

    pid_ip = attendi_stack();
    if (pid_ip <= 0) {
        /* ! LE ISTRUZIONI LE STAMPA rete_istruzioni() E NON QUESTO FILE: sono
         * la stessa catena che spiegano `ping`, `host` e `nettest`, e scritta
         * qui sarebbe la quinta copia — quella che resta indietro. */
        printf("Il servizio '%s' non e' attivo.\n", IP_SERVIZIO);
        rete_istruzioni();
        return 1;
    }

    if (leggi_stato(&s) != 0) {
        printf("dhcp: lo stack non risponde.\n");
        return 1;
    }
    memcpy(g_mac, s.mac, 6);
    precedente = s.cfg;

    /* Un identificativo che non collida con quello di un altro client
     * sulla stessa rete: il tempo trascorso mescolato al nostro PID. */
    g_xid = (uptime_ms() << 8) ^ ((unsigned int)getpid() << 20) ^ 0x45584F53u;

    {
        IpUdpApri a;

        a.porta = PORTA_CLIENT;
        if (ipc_send(pid_ip, IP_MSG_UDP_APRI, &a, sizeof(a)) < 0) return 1;
        rc = esito_stack(2000);
        if (rc != (int)PORTA_CLIENT) {
            printf("dhcp: non si puo' aprire la porta %d (%d).\n", PORTA_CLIENT, rc);
            printf("      Un altro dhcp e' gia' in esecuzione?\n");
            return 1;
        }
    }

    /* Indirizzo a zero: vedi il commento di testa. */
    memset(&vuota, 0, sizeof(vuota));
    if (applica(&vuota) != 0) {
        printf("dhcp: lo stack ha rifiutato la configurazione vuota.\n");
        return 1;
    }

    printf("dhcp: cerco un server (");
    printf("%02x:%02x:%02x:%02x:%02x:%02x)...\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);

    rc = scambio(DHCP_DISCOVER, DHCP_OFFER, NULL, NULL, risposta, &len);
    if (rc != 0) {
        printf("dhcp: nessuna offerta.\n");
        printf("      La rete funziona? Provala con  ping\n");
        applica(&precedente);
        return 1;
    }

    memset(server, 0, 4);
    if (opzione(risposta, len, OPZ_SERVER, &v) == 4) memcpy(server, v, 4);

    printf("dhcp: offerta di ");
    stampa_ip(server); printf(": ");
    stampa_ip(risposta + 16); printf("\n");

    /* yiaddr dell'offerta e' l'indirizzo che si chiede nella REQUEST. */
    {
        unsigned char chiesto[4];

        memcpy(chiesto, risposta + 16, 4);
        rc = scambio(DHCP_REQUEST, DHCP_ACK, chiesto,
                     (server[0] | server[1] | server[2] | server[3]) ? server : NULL,
                     risposta, &len);
    }

    if (rc == -2) {
        printf("dhcp: il server ha rifiutato la richiesta (NAK).\n");
        applica(&precedente);
        return 1;
    }
    if (rc != 0) {
        printf("dhcp: nessuna conferma alla richiesta.\n");
        applica(&precedente);
        return 1;
    }

    leggi_config(risposta, len, &nuova);
    g_scadenza_s = g_scadenza_letta;

    printf("\n  indirizzo  "); stampa_ip(nuova.ip);
    printf("\n  maschera   "); stampa_ip(nuova.maschera);
    printf("\n  gateway    "); stampa_ip(nuova.gateway);
    printf("\n  DNS        "); stampa_ip(nuova.dns);
    if (g_scadenza_s) printf("\n  concessione %u s", g_scadenza_s);
    printf("\n\n");

    if (solo_prova) {
        printf("dhcp: -n, non applico. Rimetto la configurazione di prima.\n");
        applica(&precedente);
        return 0;
    }

    if (applica(&nuova) != 0) {
        printf("dhcp: lo stack ha rifiutato la configurazione.\n");
        applica(&precedente);
        return 1;
    }

    printf("dhcp: configurato.\n");

    if (!resta) {
        printf("      la concessione dura %u s e NON viene rinnovata.\n",
               g_scadenza_s ? g_scadenza_s : 0);
        printf("      Con `dhcp &` resto acceso e la rinnovo da solo.\n");
        return 0;
    }

    /* ! DA QUI NON SI TORNA, e per questo va lanciato con '&'. Il ciclo si
     * interrompe solo se il server sparisce per tre rinnovi di fila: allora
     * si ricomincia da capo, perche' puo' essere cambiata la rete. */
    for (;;) {
        sorveglia(server, &nuova);

        printf("dhcp: cerco di nuovo un server...\n");
        if (scambio(DHCP_DISCOVER, DHCP_OFFER, NULL, NULL,
                    risposta, &len) != 0) {
            printf("dhcp: nessun server. Riprovo fra un minuto.\n");
            usleep(60000000u);
            continue;
        }
        if (opzione(risposta, len, OPZ_SERVER, &v) == 4) memcpy(server, v, 4);

        if (scambio(DHCP_REQUEST, DHCP_ACK, risposta + 16, server,
                    risposta, &len) != 0) {
            printf("dhcp: il server ha offerto e non ha confermato.\n");
            usleep(60000000u);
            continue;
        }

        leggi_config(risposta, len, &nuova);
        g_scadenza_s = g_scadenza_letta;
        applica(&nuova);
        printf("dhcp: nuovo indirizzo ");
        stampa_ip(nuova.ip);
        printf("\n");
    }
}
