/* =============================================================================
 * drivers/net/ip_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Protocollo IPC dello stack IPv4 (/dev/ip.drv).
 *
 * Lo stack è un PROCESSO A SÉ, non una libreria e non parte del driver.
 * Sta fra i client e il driver di scheda:
 *
 *   ping ──IPC──> ip.drv ──IPC──> ne2k.drv ──porte I/O──> scheda
 *
 * Tre motivi per cui è un processo separato e non codice dentro il
 * driver:
 *
 *   - ARP, IP e ICMP sono uguali su qualunque scheda. Metterli nel driver
 *     significherebbe riscriverli per la PCnet, e avere due stack che si
 *     comportano in modo leggermente diverso.
 *   - Lo stack ha dei TEMPI (scadenze ARP, attese di risposta) mentre il
 *     driver deve solo rispondere all'hardware il più in fretta
 *     possibile. Sono due ritmi diversi nello stesso ciclo.
 *   - Se lo stack sbaglia, si riavvia lo stack. Il driver — cioè la
 *     scheda — resta acceso e configurato.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ COSA QUESTO STACK NON FA, DETTO SUBITO
 *
 *   - NON FRAMMENTA e non riassembla. Un datagramma che non entra nella
 *     MTU viene rifiutato con -EMSGSIZE invece di essere spezzato, e uno
 *     in arrivo con l'offset di frammento diverso da zero viene contato e
 *     scartato. Riassemblare vuol dire tenere pezzi in attesa con una
 *     scadenza per ciascuno, ed è il posto classico dove uno stack
 *     giovane prende bug che si vedono solo sotto carico.
 *   - NON HA UNA TABELLA DI ROUTING. C'è una rete locale e un gateway: se
 *     la destinazione è dentro la maschera si va diretti, altrimenti al
 *     gateway. È quello che serve a una macchina che non fa da router.
 *   - NON FA DHCP DA SÉ, e non deve: DHCP sta sopra UDP come un client
 *     DNS, ed è /bin/dhcp a farlo. Qui c'è solo quello che serve a
 *     permetterglielo — UDP, il broadcast, e l'accettazione dei pacchetti
 *     non indirizzati a noi.
 *   - NON RISOLVE NOMI. Tiene l'indirizzo del DNS perché glielo dice il
 *     DHCP, ma non lo usa.
 *   - UNA OPERAZIONE IN SOSPESO PER VOLTA (un ping, oppure un datagramma
 *     UDP in attesa di ARP). Più richieste insieme vorrebbero una tabella
 *     di operazioni pendenti, che serve quando ci sarà TCP e non prima.
 * ============================================================================= */

#ifndef IP_PROTO_H
#define IP_PROTO_H

/* Nome del servizio nel registro IPC */
#define IP_SERVIZIO      "ip"

/* --- Richieste (client -> stack) ---------------------------------------- */
#define IP_MSG_CONFIG      1   /* IpConfig            -> IP_MSG_ESITO */
#define IP_MSG_STATO       2   /*                     -> IP_MSG_STATO_R */
#define IP_MSG_ECHO        3   /* IpEcho              -> IP_MSG_ECHO_R */
#define IP_MSG_ARP         4   /*                     -> IP_MSG_ARP_R */
#define IP_MSG_UDP_APRI    5   /* IpUdpApri           -> IP_MSG_ESITO */
#define IP_MSG_UDP_CHIUDI  6   /* IpUdpApri           -> IP_MSG_ESITO */
#define IP_MSG_UDP_INVIA   7   /* IpUdpInvia + dati   -> IP_MSG_ESITO */
#define IP_MSG_UDP_RICEVI  8   /* IpUdpApri           -> IP_MSG_UDP_DATI */

/* --- Risposte (stack -> client) ----------------------------------------- */
#define IP_MSG_ESITO     128
#define IP_MSG_STATO_R   129
#define IP_MSG_ECHO_R    130
#define IP_MSG_ARP_R     131
#define IP_MSG_UDP_DATI  132   /* IpUdpDati + dati */

/* Quante voci tiene la tabella ARP. Sedici bastano a una rete locale
 * piccola; oltre, si butta la più vecchia. Una tabella che cresce senza
 * limite è un modo lento di finire la memoria a causa di un vicino
 * chiacchierone. */
#define IP_ARP_VOCI      16

/* Configurazione dell'interfaccia */
typedef struct {
    unsigned char ip[4];
    unsigned char maschera[4];
    unsigned char gateway[4];    /* 0.0.0.0 = nessuno */
    /* Il DNS lo stack non lo usa: non risolve nomi. Lo tiene perché è il
     * DHCP a comunicarlo, e buttarlo via costringerebbe chi un giorno
     * scriverà il risolutore a rifare tutto il giro per riaverlo. */
    unsigned char dns[4];
} IpConfig;

/* Richiesta di echo (il "ping").
 *
 * `id` lo sceglie lo stack, non il client: serve a riconoscere le
 * risposte destinate a NOI fra quelle che girano sulla rete, ed è una
 * decisione dello stack come lo è il numero di porta. */
typedef struct {
    unsigned char ip[4];
    unsigned int  seq;
    unsigned int  payload;       /* byte di riempimento dopo l'intestazione ICMP */
    unsigned int  timeout_ms;
} IpEcho;

/* Esito di una richiesta di echo.
 *
 * `codice` 0 = risposta arrivata. Gli errori sono distinti di proposito:
 * «nessuno risponde all'ARP» e «l'host non risponde al ping» sono due
 * guasti diversi — nel primo caso non si è nemmeno usciti dalla rete
 * locale — e un unico -ETIMEDOUT li confonderebbe. */
typedef struct {
    int           codice;        /* 0, oppure -errno */
    unsigned int  seq;
    unsigned int  rtt_ms;
    unsigned int  ttl;           /* TTL del pacchetto di risposta */
    unsigned char da[4];         /* chi ha risposto */
} IpEchoRisposta;

/* Stato e contatori */
typedef struct {
    IpConfig      cfg;
    unsigned char mac[6];
    unsigned int  ip_inviati;
    unsigned int  ip_ricevuti;
    unsigned int  scartati;          /* non per noi, o malformati */
    unsigned int  checksum_errati;
    unsigned int  frammenti;         /* scartati: non riassembliamo */
    unsigned int  arp_richieste_inviate;
    unsigned int  arp_risposte_date;  /* qualcuno ha chiesto chi siamo */
    unsigned int  echo_serviti;       /* ping ricevuti e a cui abbiamo risposto */
    unsigned int  udp_inviati;
    unsigned int  udp_ricevuti;
    unsigned int  udp_senza_porta;    /* arrivati per una porta non aperta */
} IpStato;

/* Una voce della tabella ARP, come la vede un client */
typedef struct {
    unsigned char ip[4];
    unsigned char mac[6];
    unsigned int  scade_fra_ms;
} IpArpVoce;

typedef struct {
    unsigned int n;
    IpArpVoce    voce[IP_ARP_VOCI];
} IpArpTabella;

/* =============================================================================
 * UDP
 *
 * ⚠️ SI "APRE" UNA PORTA E NON SI CREA UNA PRESA. Non c'è nessun
 * descrittore: il client dice quale porta vuole, e da quel momento i
 * datagrammi per quella porta sono suoi. Basta a un client DHCP e a un
 * risolutore DNS, che è quello che serve adesso; se un giorno servirà una
 * vera API a prese, si costruirà SOPRA questa, non al posto suo.
 *
 * ⚠️ ANCHE QUI IL DRIVER NON SPINGE NIENTE. Vale la stessa regola dei
 * frame (vedi net_proto.h): lo stack risponde a un IP_MSG_UDP_RICEVI
 * pendente e non manda datagrammi di sua iniziativa, altrimenti due
 * processi possono restare fermi ognuno dentro la propria ipc_send.
 *
 * ⚠️ NIENTE CODA PER I DATAGRAMMI NON RICHIESTI. Se arriva qualcosa per
 * una porta aperta e nessuno sta aspettando, si scarta e si conta. Un UDP
 * senza coda perde pacchetti — ma UDP li perde comunque per definizione, e
 * una coda che cresce mentre nessuno legge è un modo lento di finire la
 * memoria per colpa di chi manda.
 * ============================================================================= */

/* Porta locale. 0 in IP_MSG_UDP_APRI significa «scegline una tu». */
typedef struct {
    unsigned int porta;
} IpUdpApri;

/* Intestazione di IP_MSG_UDP_INVIA: i dati seguono nello stesso messaggio.
 * Destinazione 255.255.255.255 = broadcast, e in quel caso non si fa ARP:
 * si manda all'indirizzo Ethernet di tutti. È ciò che permette a un client
 * DHCP di parlare prima di avere un indirizzo. */
typedef struct {
    unsigned char ip[4];
    unsigned int  porta;          /* porta di destinazione */
    unsigned int  porta_locale;   /* da quale porta aperta si parla */
} IpUdpInvia;

/* Intestazione di IP_MSG_UDP_DATI: i dati seguono nello stesso messaggio. */
typedef struct {
    unsigned char ip[4];          /* chi ha mandato */
    unsigned int  porta;          /* la sua porta */
    unsigned int  porta_locale;   /* la nostra */
    unsigned int  len;            /* byte di dati che seguono */
} IpUdpDati;

/* Risposta generica */
typedef struct {
    int codice;
} IpEsito;

#endif /* IP_PROTO_H */
