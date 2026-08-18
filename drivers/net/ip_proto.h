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
 * ! COSA QUESTO STACK NON FA, DETTO SUBITO
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
#define IP_MSG_TCP_APRI    9   /* IpTcpApri  -> IP_MSG_ESITO (id, o -errno) */
#define IP_MSG_TCP_INVIA  10   /* IpTcpRif + dati -> IP_MSG_ESITO (byte presi) */
#define IP_MSG_TCP_RICEVI 11   /* IpTcpRif   -> IP_MSG_TCP_DATI */
#define IP_MSG_TCP_CHIUDI 12   /* IpTcpRif   -> IP_MSG_ESITO */
#define IP_MSG_TCP_STATO  13   /* IpTcpRif   -> IP_MSG_TCP_INFO */

/* -----------------------------------------------------------------------------
 * Il lato SERVITORE — dal 18 agosto 2026
 *
 * ! FINO A IERI IL TCP DI EX-OS SAPEVA SOLO CHIAMARE. C'era tcp_apri(), cioe'
 * connect, e nient'altro: nessun programma poteva ASPETTARE una connessione, e
 * quindi nessun servizio di rete poteva esistere — ne' telnet, ne' un giorno
 * ssh. Questi due messaggi sono quel mattone.
 *
 * ASCOLTA prende una porta e rende l'id di un ascoltatore, che non e' una
 * connessione: non ci si legge e non ci si scrive, gli si chiede ACCETTA.
 * ACCETTA rende l'id di una connessione vera, gia' aperta, da usare come se
 * fosse uscita da tcp_apri.
 *
 * ! SONO DUE MESSAGGI E NON UNO, come su Unix, e la ragione e' che un
 * ascoltatore serve MOLTE connessioni: se ascolta e accetta fossero la stessa
 * cosa, ogni cliente in arrivo vorrebbe una porta nuova.
 * --------------------------------------------------------------------------- */
#define IP_MSG_TCP_ASCOLTA 14  /* IpTcpAscolta -> IP_MSG_ESITO (id, o -errno) */
#define IP_MSG_TCP_ACCETTA 15  /* IpTcpAccetta -> IP_MSG_ESITO (id, o -errno) */

/* --- Risposte (stack -> client) ----------------------------------------- */
#define IP_MSG_ESITO     128
#define IP_MSG_STATO_R   129
#define IP_MSG_ECHO_R    130
#define IP_MSG_ARP_R     131
#define IP_MSG_UDP_DATI  132   /* IpUdpDati + dati */
#define IP_MSG_TCP_DATI  133   /* IpTcpDati + dati */
#define IP_MSG_TCP_INFO  134   /* IpTcpInfo */

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
 * ! SI "APRE" UNA PORTA E NON SI CREA UNA PRESA. Non c'è nessun
 * descrittore: il client dice quale porta vuole, e da quel momento i
 * datagrammi per quella porta sono suoi. Basta a un client DHCP e a un
 * risolutore DNS, che è quello che serve adesso; se un giorno servirà una
 * vera API a prese, si costruirà SOPRA questa, non al posto suo.
 *
 * ! ANCHE QUI IL DRIVER NON SPINGE NIENTE. Vale la stessa regola dei
 * frame (vedi net_proto.h): lo stack risponde a un IP_MSG_UDP_RICEVI
 * pendente e non manda datagrammi di sua iniziativa, altrimenti due
 * processi possono restare fermi ognuno dentro la propria ipc_send.
 *
 * ! NIENTE CODA PER I DATAGRAMMI NON RICHIESTI. Se arriva qualcosa per
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

/* =============================================================================
 * TCP
 *
 * -----------------------------------------------------------------------------
 * ! SOLO CONNESSIONI IN USCITA, E NON E' UNA MANCANZA CASUALE
 *
 * Si puo' aprire una connessione verso qualcuno; non si puo' METTERSI IN
 * ASCOLTO. Manca quindi tutto il ramo LISTEN/SYN_RECEIVED della macchina
 * a stati, che e' circa la meta' del lavoro e serve a fare da SERVER.
 *
 * La ragione e' che il primo cliente di questo TCP e' un client FTP, e un
 * client FTP in modo PASSIVO (PASV) apre lui stesso anche la connessione
 * dati. Il modo ATTIVO — quello in cui il server si ricollega al client —
 * richiederebbe l'ascolto, e non funziona comunque dietro un NAT come
 * quello di QEMU. Si fa la meta' che serve, e si dice che e' meta'.
 *
 * -----------------------------------------------------------------------------
 * ! COSA MANCA, DETTO SUBITO
 *
 *   - NIENTE RIORDINO. Un segmento che arriva fuori sequenza viene
 *     SCARTATO, non tenuto da parte: chi l'ha mandato lo ritrasmettera'.
 *     E' corretto ma non efficiente, e su una rete che perde molto si
 *     vede. Tenere i pezzi vuole una lista con le sue scadenze, ed e' il
 *     posto dove un TCP giovane prende i bug peggiori.
 *   - NIENTE CONTROLLO DI CONGESTIONE. Non c'e' slow start, non c'e'
 *     congestion window: si manda quello che la finestra dell'altro
 *     consente. Su una rete locale non cambia niente; su Internet
 *     significa essere maleducati sotto perdita.
 *   - NIENTE SACK, niente window scaling, niente timestamp.
 *   - RTO FISSO. Non si misura il tempo di andata e ritorno: si ritrasmette
 *     a intervalli raddoppianti da un valore di partenza. Misurarlo
 *     davvero (Karn, Jacobson) e' il passo successivo, non questo.
 *
 * -----------------------------------------------------------------------------
 * ! ANCHE QUI NIENTE VIENE SPINTO
 *
 * Stessa regola di UDP e dei frame: lo stack risponde a un
 * IP_MSG_TCP_RICEVI pendente e non manda dati di sua iniziativa. Un
 * client che vuole leggere prenota, e la prenotazione si consuma a ogni
 * consegna.
 * ============================================================================= */

/* Quante connessioni insieme. Quattro bastavano a un client FTP (una di
 * controllo e una dati) con margine; ognuna costa i suoi due buffer.
 *
 * ! DA OTTO DA QUANDO C'E' L'ASCOLTO, e il motivo e' che un ascoltatore
 * OCCUPA UNO SLOT senza essere una connessione: un servitore con due clienti
 * collegati ne usa gia' tre, e con quattro in tutto il primo che bussa mentre
 * gli altri parlano si sente rifiutare. Otto per 8 KB di buffer sono 64 KB. */
#define IP_TCP_CONNESSIONI  8

/* Stati visibili a un client. Sono meno di quelli veri della macchina a
 * stati: a chi usa la connessione interessa sapere se puo' scrivere, se
 * deve solo leggere, o se e' finita. */
#define IP_TCP_CHIUSA       0
#define IP_TCP_IN_APERTURA  1
#define IP_TCP_APERTA       2
#define IP_TCP_IN_CHIUSURA  3
#define IP_TCP_RESET        4   /* l'altro ha rifiutato o interrotto */

typedef struct {
    unsigned char ip[4];
    unsigned int  porta;
    unsigned int  timeout_ms;    /* per la sola apertura; 0 = predefinito */
} IpTcpApri;

/* Identifica una connessione aperta. `id` e' quello restituito da
 * IP_MSG_TCP_APRI. */
typedef struct {
    unsigned int id;
} IpTcpRif;

/* La porta su cui mettersi in ascolto. */
typedef struct {
    unsigned int porta;
} IpTcpAscolta;

/* ! LA SCADENZA E' OBBLIGATORIA E NON PUO' ESSERE INFINITA. Un servitore che
 * aspetta per sempre dentro una singola richiesta IPC e' un servitore che non
 * puo' fare altro — nemmeno accorgersi che l'utente vuole fermarlo. Chi vuole
 * aspettare a lungo richiama accetta in un ciclo, ed e' quel ciclo il posto
 * giusto per guardare anche il resto. */
typedef struct {
    unsigned int id;             /* l'ascoltatore */
    unsigned int timeout_ms;     /* 0 = non aspettare: c'e' o non c'e' */
} IpTcpAccetta;

/* Intestazione di IP_MSG_TCP_INVIA e di IP_MSG_TCP_DATI: i byte seguono
 * nello stesso messaggio. */
typedef struct {
    unsigned int id;
    unsigned int len;
} IpTcpDati;

typedef struct {
    unsigned int  id;
    unsigned int  stato;         /* IP_TCP_* */
    unsigned int  in_coda_rx;    /* byte gia' arrivati e non ancora letti */
    unsigned int  in_coda_tx;    /* byte accettati e non ancora confermati */
    unsigned char ip[4];
    unsigned int  porta;
} IpTcpInfo;

/* Risposta generica.
 *
 * ! `codice` PORTA ANCHE VALORI POSITIVI, non solo 0 o -errno: per
 * IP_MSG_TCP_APRI e' l'identificativo della connessione, per
 * IP_MSG_TCP_INVIA e' il numero di byte accettati (che puo' essere meno
 * di quelli offerti, se il buffer di trasmissione e' quasi pieno). Chi
 * chiama deve guardare il segno prima del valore. */
typedef struct {
    int codice;
} IpEsito;

#endif /* IP_PROTO_H */
