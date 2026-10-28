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

/* IP_MSG_LINK (ip.drv 0.010+, 2026-10-14): move the stack onto another card
 * service while running. Payload: the service name, NUL-terminated (at most
 * IPC_NAME_LEN bytes), e.g. "rete0" or the WiFi link "wifi0".
 * -> IP_MSG_ESITO: 0, or -ENOENT if no such service is registered.
 * The address configuration is NOT changed: who switches (wifid) gives the
 * new one afterwards, with DHCP or IP_MSG_CONFIG. The ARP table is emptied,
 * since it belonged to the other network. */
#define IP_MSG_LINK       16
#define IP_LINK_WIFI      "wifi0"    /* a WiFi driver: control AND link, one name */

/* IP_MSG_TCP_TABELLA (ip.drv 0.015, 26 October 2026): every busy slot of the
 * TCP table, read only, for `ipcfg -tcp` - the internal state, the ports,
 * the other end, the owner, the bytes queued; never the data. Anyone may ask,
 * as anyone may run netstat on Linux. Payload: IpTcpChiediTabella (the first
 * slot wanted) -> IP_MSG_TCP_TABELLA_R, IpTcpTabella: up to
 * IP_TCP_RIGHE_MAX rows per answer, `prossimo` is where to ask again (0 when
 * the table is over). Born of the MariaDB stress test: the question "what is
 * filling the table?" had no answer from outside. */
#define IP_MSG_TCP_TABELLA 17

/* the internal states as ip.c numbers them (S_* there) */
#define IP_TCPS_LIBERA     0
#define IP_TCPS_SYN_INVIA  1
#define IP_TCPS_APERTA     2
#define IP_TCPS_FIN_MIO    3
#define IP_TCPS_FIN_SUO    4
#define IP_TCPS_ULTIMO_ACK 5
#define IP_TCPS_MORTA      6
#define IP_TCPS_ASCOLTA    7
#define IP_TCPS_SYN_RICEV  8

typedef struct {
    unsigned int da;
} IpTcpChiediTabella;

typedef struct {
    unsigned int  id;            /* slot + 1, as in IpTcpInfo */
    unsigned int  stato;         /* IP_TCPS_* */
    unsigned int  porta_loc, porta_rem;
    unsigned char ip[4];         /* the other end */
    unsigned int  proprietario;  /* the process group that owns it */
    unsigned int  rx, tx;        /* bytes waiting to be read / acknowledged */
    unsigned int  segni;         /* 1 = closed by its owner, 2 = accepted */
} IpTcpRiga;

#define IP_TCP_RIGHE_MAX 40

typedef struct {
    unsigned int totale;         /* how many the stack takes (tcpmax) */
    unsigned int occupati;       /* busy slots in the whole table */
    unsigned int prossimo;       /* the slot to ask from next, 0 = done */
    unsigned int n;
    IpTcpRiga    riga[IP_TCP_RIGHE_MAX];
} IpTcpTabella;

/* =============================================================================
 * WAITING FOR CONNECTIONS TO BE READY, WITHOUT ASKING (ip.drv 0.016, 26
 * October 2026)
 *
 * Until 0.015 poll() on a socket asked IP_MSG_TCP_STATO every 10 ms, and a
 * server waiting on a thousand connections would have asked a thousand times
 * per round. Now the stack wakes whoever waits:
 *
 *   IP_MSG_TCP_ASPETTA  IpTcpAspetta (up to IP_ASPETTA_MAX connections, each
 *                       with the events it wants) -> IP_MSG_TCP_PRONTE as
 *                       soon as one of them is ready, or with n = 0 when
 *                       timeout_ms passes (0 = no timeout). What poll() does.
 *
 *   IP_MSG_INS_CREA     -> IP_MSG_ESITO (id of a new set, or -errno)
 *   IP_MSG_INS_CTL      IpInsCtl: add, change or drop a connection of the set
 *                       -> IP_MSG_ESITO. What epoll_ctl() does.
 *   IP_MSG_INS_ATTENDI  IpInsAttendi -> IP_MSG_INS_EVENTI when something in
 *                       the set is ready (n = 0 when timeout_ms passes, or
 *                       when IP_MSG_INS_SVEGLIA asked). What epoll_wait() does.
 *   IP_MSG_INS_SVEGLIA  IpInsRif -> IP_MSG_ESITO; whoever waits on the set
 *                       gets n = 0 now (or at its next wait if none waits).
 *   IP_MSG_INS_CHIUDI   IpInsRif -> IP_MSG_ESITO.
 *   IP_MSG_TCP_SPEGNI   IpTcpRif -> IP_MSG_ESITO: shutdown(SHUT_RDWR). The
 *                       connection closes as with IP_MSG_TCP_CHIUDI but stays
 *                       in its epoll set, ready for reading once (the end):
 *                       that is how a server polling it hears it, as on Linux.
 *                       The IP_MSG_TCP_CHIUDI that follows (close) only takes
 *                       it out of the set.
 *   IP_MSG_TCP_MEZZA    IpTcpRif -> IP_MSG_ESITO: shutdown(SHUT_WR), ip.drv
 *                       0.018. Our FIN leaves after the data still queued;
 *                       the connection keeps receiving until the other side
 *                       closes too (Apache's lingering close waits for that).
 *
 * A connection that its program closes leaves every set by itself, as on
 * Linux. Events are the Linux EPOLL* values (IP_EV_*); IP_EV_ONESHOT and
 * IP_EV_ET as in epoll. IP_INS_FORZA in `op` says the program already holds
 * unread bytes of this connection (libc's buffer): ready for reading once.
 * ============================================================================= */
#define IP_MSG_TCP_ASPETTA 18
#define IP_MSG_INS_CREA    19
#define IP_MSG_INS_CTL     20
#define IP_MSG_INS_ATTENDI 21
#define IP_MSG_INS_SVEGLIA 22
#define IP_MSG_INS_CHIUDI  23
#define IP_MSG_TCP_SPEGNI  24
#define IP_MSG_TCP_MEZZA   25

#define IP_EV_IN       0x001u
#define IP_EV_OUT      0x004u
#define IP_EV_ERR      0x008u
#define IP_EV_HUP      0x010u
#define IP_EV_RDHUP    0x2000u
#define IP_EV_ONESHOT  (1u << 30)
#define IP_EV_ET       (1u << 31)

#define IP_INS_ADD     1
#define IP_INS_DEL     2
#define IP_INS_MOD     3
#define IP_INS_FORZA   0x100u

#define IP_ASPETTA_MAX 64
/* timeout_ms of IpTcpAspetta and IpInsAttendi: 0 waits with no timeout,
 * this one answers at once with what is ready now (maybe nothing). */
#define IP_ASPETTA_SUBITO 0xFFFFFFFFu

typedef struct {
    unsigned int id;             /* connection (or listener) */
    unsigned int eventi;         /* wanted (IpTcpAspetta), happened (reply) */
} IpTcpVoce;

typedef struct {
    unsigned int timeout_ms;
    unsigned int n;
    IpTcpVoce    voce[IP_ASPETTA_MAX];
} IpTcpAspetta;                  /* also the IP_MSG_TCP_PRONTE reply: only
                                    the ready ones, timeout_ms unused */

typedef struct { unsigned int ins; } IpInsRif;

typedef struct {
    unsigned int ins, op, id, eventi;
    unsigned int dati[2];        /* epoll_data, handed back as it came */
} IpInsCtl;

typedef struct {
    unsigned int ins, max, timeout_ms;
} IpInsAttendi;

typedef struct {
    unsigned int eventi;
    unsigned int dati[2];
} IpInsEvento;

#define IP_INS_EVENTI_MAX 120

typedef struct {
    unsigned int n;
    IpInsEvento  ev[IP_INS_EVENTI_MAX];
} IpInsEventi;

/* --- Risposte (stack -> client) ----------------------------------------- */
#define IP_MSG_ESITO     128
#define IP_MSG_STATO_R   129
#define IP_MSG_ECHO_R    130
#define IP_MSG_ARP_R     131
#define IP_MSG_UDP_DATI  132   /* IpUdpDati + dati */
#define IP_MSG_TCP_DATI  133   /* IpTcpDati + dati */
#define IP_MSG_TCP_INFO  134   /* IpTcpInfo */
#define IP_MSG_TCP_TABELLA_R 135 /* IpTcpTabella */
#define IP_MSG_TCP_PRONTE 136  /* IpTcpAspetta: the ready connections */
#define IP_MSG_INS_EVENTI 137  /* IpInsEventi */

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

    /* ! I TRE DI TCP SERVONO A DECIDERE UN NUMERO, non a fare bella figura in
     * `ipcfg`: la finestra di ricezione e' 4 KB (TCP_BUF in drivers/ip/ip.c) e
     * nessuno aveva mai misurato quanto costi. Vedi @DIF-TCPBUF.
     *
     *   tcp_riaperture   gli ACK vuoti mandati per dire «adesso c'e' posto»,
     *                    cioe' quante volte chi legge e' rimasto indietro
     *   tcp_fin_zero     quante volte la finestra annunciata era ZERO: li' il
     *                    mittente si ferma e riparte solo col proprio timer,
     *                    che raddoppia l'attesa ogni volta
     *   tcp_byte_letti   quanti byte sono stati consegnati a chi legge: e' il
     *                    denominatore, senza il quale gli altri due non
     *                    vogliono dire niente */
    unsigned int  tcp_riaperture;
    unsigned int  tcp_fin_zero;
    unsigned int  tcp_byte_letti;

    /* ! E QUESTI DUE SONO GLI SCARTI SILENZIOSI DI TCP, cioe' i segmenti che
     * arrivano interi e vengono buttati da NOI. Sono due, e sono diversi:
     *
     *   tcp_fuori_seq  arrivato con un numero di sequenza che non e' quello
     *                  atteso: un pacchetto prima di lui si e' perso, o ha
     *                  preso un'altra strada. Dal 9 ottobre 2026 si tiene da
     *                  parte (fino a otto tratti) e si riconferma dove si era
     *                  rimasti; prima si scartava. Il numero resta il segno
     *                  di quanto la strada consegna in disordine
     *   tcp_pieno      arrivato in ordine ma senza posto nel buffer: quello
     *                  si', e' la finestra che non basta
     *
     * ! SENZA DI LORO «SI PERDONO PACCHETTI» NON HA UN DOVE. Sommati ai
     * contatori del driver (persi_coda, persi_scheda) dicono in quale dei
     * quattro punti della catena un byte sparisce, e la cura e' diversa in
     * ognuno. Vedi @DIF-TCPBUF. */
    unsigned int  tcp_fuori_seq;
    unsigned int  tcp_pieno;
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
 *   - IL RIORDINO C'E' DAL 9 OTTOBRE 2026 (ip.drv 0.009), ed e' piccolo:
 *     fino a otto tratti arrivati in anticipo restano nel buffer di
 *     ricezione, che e' diventato di 63 KB. Oltre, si scarta come prima.
 *   - NIENTE CONTROLLO DI CONGESTIONE. Non c'e' slow start, non c'e'
 *     congestion window: si manda quello che la finestra dell'altro
 *     consente. Su una rete locale non cambia niente; su Internet
 *     significa essere maleducati sotto perdita.
 *   - NIENTE SACK, niente window scaling (la finestra si ferma a 63 KB),
 *     niente timestamp.
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
 * gli altri parlano si sente rifiutare.
 *
 * ! DA VENTIQUATTRO DA QUANDO PIU' PROGRAMMI VANNO IN RETE INSIEME. Otto era
 * gia' stretto per un browser solo: una pagina tiene aperta la connessione per
 * riusarla, e intanto un servitore ascolta e un altro programma scarica. Con
 * piu' finestre di navigazione, o un browser e un client di posta accesi
 * insieme, otto si esauriscono senza che niente sia rotto.
 * Ventiquattro per 8 KB di buffer sono 192 KB — su una macchina da 32 MB e'
 * una voce che si vede, ed e' dichiarata qui. */
/* ! 4096 AND NOT 24 (ip.drv 0.016, 26 October 2026): the MariaDB stress test
 * wants a thousand clients and more. The slots are not a static table any
 * more: one is allocated (8 KB) the first time the table grows that far and
 * then reused, so a machine that opens a few connections pays for a few. */
/* ! NOT A CEILING ANY MORE (ip.drv 0.017, 27 October 2026): the fewest the
 * stack takes. The real number is `tcpmax` in kernel.cfg, `ip.drv -t N`, or
 * one connection per 32 KB of RAM (65536 on 2 GB); IpTcpTabella.totale
 * says it. */
#define IP_TCP_CONNESSIONI  4096

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

/* ! QUANTI BYTE STANNO IN UNA CONSEGNA, e non e' una scelta di comodo: e'
 * quello che avanza in un messaggio IPC dopo l'intestazione. Consegnarne di
 * piu' vuol dire dichiarare una lunghezza che nel messaggio non c'e', e chi
 * legge copia la coda dal proprio buffer — spazzatura che sembra dati. */
#define IP_TCP_DATI_MAX  (IPC_MSG_MAX_DATA - 16)

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
    /* ! IN FONDO, dalla 0.005 di ip.drv: la porta di QUESTA parte, per
     * getsockname(). Chi legge solo i campi di prima non se ne accorge. */
    unsigned int  porta_loc;
    /* At the end too, from ip.drv 0.015: 1 when this is a listener and
     * in_coda_rx counts the connections waiting for accept(). An older
     * ip.drv sends a shorter answer, the reader finds 0 here and knows that
     * in_coda_rx of a listener means nothing (poll() in the libc). */
    unsigned int  conta_in_attesa;
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
