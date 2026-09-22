/* =============================================================================
 * bin/telnetd/telnetd.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * telnetd — una sessione su una connessione TCP
 *
 *     telnetd            legge /boot/telnetd.cfg e serve una sessione per volta
 *     telnetd 2323       la porta della riga di comando vince sul file
 *     telnetd -s         la shell senza accesso: vince anche questa
 *     telnetd -f FILE    un'altra configurazione
 *
 * ! NON E' UN PEZZO NUOVO: E' L'ASSEMBLAGGIO DI QUATTRO GIA' PROVATI. listen e
 * accept (oggi), il pty con la sua disciplina di linea (oggi), login che sa
 * autenticare e scendere con setuid (17 agosto), e l'interruzione. Questo file
 * mette in fila i byte fra una connessione e uno pseudo-terminale, e non fa
 * nient'altro — se qualcosa non funziona, il difetto e' in uno dei quattro.
 *
 * ! ED E' IN CHIARO, CON TUTTO QUELLO CHE COMPORTA. La password viaggia
 * leggibile sul cavo: telnet e' nato prima che qualcuno ascoltasse. Sta qui
 * perche' PROVA L'IMPIANTO di una sessione remota — accettare, dare un
 * terminale, autenticare, ripulire — senza la crittografia in mezzo: se
 * qualcosa non torna, si vede in chiaro, che e' esattamente quello che serve
 * la prima volta. Su una rete di cui non ci si fida non si accende.
 *
 * -----------------------------------------------------------------------------
 * ! IL CLIENT VA MESSO IN MODO CARATTERE, O SI VEDE DOPPIO
 *
 * Un client telnet, appena collegato, fa l'eco da se' e manda una riga per
 * volta. Ma l'eco qui la fa gia' la disciplina del pty: senza dire niente si
 * vedrebbe ogni lettera due volte, e il Backspace correggerebbe una riga che
 * il client tiene per conto suo. Le due opzioni che si negoziano — ECHO e
 * SUPPRESS GO AHEAD — servono a dirgli: «ci penso io, mandami i tasti».
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "rete.h"

/* +0.001 a ogni modifica: `telnetd -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("telnetd", "0.009");

/* I comandi del protocollo, quelli che servono. */
#define IAC     255
#define DONT    254
#define DO      253
#define WONT    252
#define WILL    251
#define SB      250
#define SE      240

#define OPT_ECHO    1
#define OPT_SGA     3
/* ! NAWS E' L'UNICA SOTTONEGOZIAZIONE CHE CI SERVE: e' con quella che un
 * client dice quanto e' grande la sua finestra, all'inizio e ogni volta che
 * cambia. RFC 1073. */
#define OPT_NAWS    31

static int pid_ip = 0;
static int g_verboso = 0;

/* =============================================================================
 * LA POSTA MESSA DA PARTE
 *
 * Qui finisce cio' che arriva nella casella mentre si sta aspettando un
 * messaggio precisa — cioe' mentre tcp_scrivi() aspetta il proprio esito. Il
 * perche' sta per esteso sopra attendi(); in breve: sono i tasti del client, e
 * buttarli vuol dire restare sordi per tutta la sessione.
 *
 * ! UNA SOLA CASELLA BASTA, e non e' un'approssimazione: lo stack consegna solo
 * a chi ha prenotato, una prenotazione per volta, e la prossima la fa
 * sessione() dopo aver ritirato questa.
 * ========================================================================== */
static unsigned char coda_msg[IPC_MSG_MAX_DATA];
static unsigned int  coda_len;      /* 0 = niente da ritirare                */
static int           coda_chiusa;   /* lo stack ha detto che non c'e' piu'
                                       nessuno dall'altra parte              */

/* -----------------------------------------------------------------------------
 * La configurazione — /boot/telnetd.cfg
 *
 * ! SI RILEGGE A OGNI CONNESSIONE, e non e' uno spreco: e' un file di poche
 * righe, e il caso che conta e' quello di chi si accorge che sta entrando
 * qualcuno che non dovrebbe. Correggere il file e vedere la regola in vigore
 * alla connessione dopo, senza fermare il servizio, e' cio' che rende un
 * elenco di permessi utile davvero.
 *
 * ! E UNA LISTA VUOTA VUOL DIRE «NESSUN FILTRO», che e' il valore permissivo.
 * L'alternativa — vuoto uguale «nessuno» — sembra piu' prudente e in pratica
 * e' peggio: chi accende il servizio senza configurazione si trova un
 * programma che rifiuta tutti e nessun indizio sul perche', e la prima cosa
 * che fa e' spegnere i controlli.
 * --------------------------------------------------------------------------- */
#define CFG_LISTA_MAX   256

/* =============================================================================
 * `avvio` — l'unica riga che decide se questa macchina si fa guidare da fuori
 *
 * ! LA SCELTA PERICOLOSA DEVE ESSERE SCRITTA, non dedotta. /boot/avvio.sh
 * contiene sempre la riga `telnetd -auto &`, ma quella riga da sola non apre
 * niente: con -auto il programma legge questa chiave e, se dice «no» — ed e'
 * il valore predefinito, anche quando il file non esiste — esce senza dire una
 * parola. Chi vuole la porta aperta lo scrive qui, in un file che si legge,
 * invece di aggiungere una riga a uno script che poi hwconfig riscrive.
 *
 *   avvio = no      non parte (predefinito)
 *   avvio = login   parte e chiede nome e password
 *   avvio = root    parte e da' una shell da amministratore SENZA CHIEDERE
 *
 * ! «root» E' TELNET, QUINDI E' IN CHIARO E SENZA PASSWORD. Su una rete di
 * casa, per guidare una macchina che sta in un'altra stanza, e' esattamente
 * quello che serve; su qualunque rete che non sia la propria e' una porta
 * aperta a chiunque. La riga `da` qui sotto e' il modo di stringerla.
 * =========================================================================== */
#define AVVIO_NO      0
#define AVVIO_LOGIN   1
#define AVVIO_ROOT    2

typedef struct {
    int  porta;
    int  avvio;                   /* AVVIO_*: che farne all'accensione */
    char shell[96];
    char utenti[CFG_LISTA_MAX];   /* concessi; vuoto = tutti          */
    char nega[CFG_LISTA_MAX];     /* negati; vince sui concessi       */
    char da[CFG_LISTA_MAX];       /* indirizzi e reti; vuoto = tutti  */
} Config;

static char g_cfg_file[128] = "/boot/telnetd.cfg";

static void taglia(char *s)
{
    int i = 0, j;

    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) { for (j = 0; s[i + j]; j++) s[j] = s[i + j]; s[j] = '\0'; }

    j = (int)strlen(s);
    while (j > 0 && (s[j-1] == ' ' || s[j-1] == '\t' ||
                     s[j-1] == '\r' || s[j-1] == '\n')) s[--j] = '\0';
}

static void copia_valore(char *dst, unsigned int max, const char *src)
{
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

/* Riempie `c` con i valori predefiniti e poi con quelli del file, se c'e'. */
static void config_leggi(Config *c)
{
    char buf[2048];
    int  fd, n, i, riga0 = 0;

    memset(c, 0, sizeof(*c));
    c->porta = 23;
    copia_valore(c->shell, sizeof(c->shell), "/bin/login");

    fd = open(g_cfg_file, O_RDONLY);
    if (fd < 0) return;             /* non c'e': restano i predefiniti */

    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    for (i = 0; i <= n; i++) {
        char riga[256], *uguale;

        if (buf[i] != '\n' && buf[i] != '\0') continue;

        {
            int len = i - riga0;

            if (len > (int)sizeof(riga) - 1) len = (int)sizeof(riga) - 1;
            memcpy(riga, buf + riga0, (unsigned int)len);
            riga[len] = '\0';
        }
        riga0 = i + 1;

        taglia(riga);
        if (riga[0] == '\0' || riga[0] == '#') continue;

        uguale = strchr(riga, '=');
        if (uguale == 0) continue;

        *uguale = '\0';
        taglia(riga);
        uguale++;
        taglia(uguale);

        if      (strcmp(riga, "porta")  == 0) c->porta = atoi(uguale);
        else if (strcmp(riga, "shell")  == 0) copia_valore(c->shell,  sizeof(c->shell),  uguale);
        else if (strcmp(riga, "utenti") == 0) copia_valore(c->utenti, sizeof(c->utenti), uguale);
        else if (strcmp(riga, "nega")   == 0) copia_valore(c->nega,   sizeof(c->nega),   uguale);
        else if (strcmp(riga, "da")     == 0) copia_valore(c->da,     sizeof(c->da),     uguale);
        else if (strcmp(riga, "avvio")  == 0) {
            if      (strcmp(uguale, "root")  == 0) c->avvio = AVVIO_ROOT;
            else if (strcmp(uguale, "login") == 0) c->avvio = AVVIO_LOGIN;
            else                                   c->avvio = AVVIO_NO;
        }
        /* Una chiave sconosciuta si salta in silenzio: un file scritto per una
         * versione piu' nuova non deve impedire di partire a una piu' vecchia. */
    }
}

/* -----------------------------------------------------------------------------
 * «Da dove» — indirizzi singoli e reti
 *
 * ! IL CONFRONTO SI FA SUI BIT, NON SULLE STRINGHE. «10.0.0.7» dentro
 * «10.0.0.70» e' vero come testo e falso come indirizzo, e un controllo di
 * accesso che si sbaglia in quel verso lascia entrare chi non deve.
 * --------------------------------------------------------------------------- */
static int leggi_ip(const char **p, unsigned char ip[4])
{
    const char *s = *p;
    int i;

    for (i = 0; i < 4; i++) {
        int v = 0, cifre = 0;

        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; cifre++; }
        if (!cifre || v > 255) return 0;
        ip[i] = (unsigned char)v;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    *p = s;
    return 1;
}

static unsigned int ip32(const unsigned char ip[4])
{
    return ((unsigned int)ip[0] << 24) | ((unsigned int)ip[1] << 16) |
           ((unsigned int)ip[2] << 8)  |  (unsigned int)ip[3];
}

/* Rende 1 se `ip` e' ammesso dalla lista `lista`. Lista vuota = chiunque. */
static int indirizzo_ammesso(const char *lista, const unsigned char ip[4])
{
    const char *p = lista;

    if (lista == 0 || lista[0] == '\0') return 1;

    while (*p) {
        unsigned char rete[4];
        int  bit = 32;

        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (*p == '\0') break;

        if (!leggi_ip(&p, rete)) {           /* voce malformata: si salta */
            while (*p && *p != ',') p++;
            continue;
        }

        if (*p == '/') {
            p++;
            bit = 0;
            while (*p >= '0' && *p <= '9') { bit = bit * 10 + (*p - '0'); p++; }
            if (bit < 0 || bit > 32) bit = 32;
        }

        {
            /* ! LA MASCHERA DI /0 NON SI CALCOLA CON UNO SPOSTAMENTO DI 32:
             * spostare di quanto e' largo il tipo e' comportamento indefinito,
             * e su x86 sposta di zero — cioe' «nessun bit in comune» diventa
             * «tutti», che qui vuol dire far entrare chiunque. */
            unsigned int m = (bit == 0) ? 0u : (0xFFFFFFFFu << (32 - bit));

            if ((ip32(rete) & m) == (ip32(ip) & m)) return 1;
        }

        while (*p && *p != ',') p++;
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * ! QUEL CHE ARRIVA MENTRE SI ASPETTA UN ESITO NON SI BUTTA, E FINO AL 15
 * SETTEMBRE 2026 SI BUTTAVA.
 *
 * Questa funzione pesca dalla casella finche' non trova il messaggio che le
 * hanno chiesto, e tutto il resto le passava fra le mani: `continue`, e via.
 * Fra quel resto c'e' IP_MSG_TCP_DATI, cioe' I TASTI CHE IL CLIENT HA BATTUTO.
 *
 * ! E NON ERA IL CASO RARO: ERA QUASI LA REGOLA, ALL'INIZIO DI OGNI SESSIONE.
 * telnetd manda la negoziazione, il client risponde subito, e intanto la shell
 * ha scritto il suo invito: il ciclo trova il pty pronto, chiama tcp_scrivi()
 * per mandare l'invito e si mette ad aspettare l'esito — e nella casella la
 * risposta del client e' gia' li', davanti all'esito.
 *
 * ! PERDERE QUEL MESSAGGIO NON PERDEVA SOLO QUEI BYTE: PERDEVA L'ORECCHIO. Lo
 * stack consegna a chi ha prenotato e LA PRENOTAZIONE SI CONSUMA A OGNI
 * CONSEGNA (tcp_consegna(), drivers/ip/ip.c: `c->attesa_pid = 0`). Buttata la
 * consegna, sessione() restava convinta di avere una prenotazione in piedi —
 * `prenotato` a 1 — e non ne faceva mai piu' una: da quel momento nessun tasto
 * arrivava piu', per sempre. Il sintomo era l'invito che compare e una
 * sessione che da li' in poi non risponde a niente, mentre la connessione TCP
 * e' viva e il client non ha nessun motivo di sospettare.
 *
 * Adesso il fuori programma si mette da parte e lo ritira il ciclo di
 * sessione(), che e' il solo posto che sa che farne.
 * --------------------------------------------------------------------------- */
static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 32; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;

        if (meta.tipo == tipo) {
            if (len) *len = meta.len;
            return 0;
        }

        if (meta.tipo == IP_MSG_TCP_DATI) {
            unsigned int n = meta.len;

            if (n > sizeof(coda_msg)) n = sizeof(coda_msg);

            /* Se mai ne arrivasse una seconda a casella piena si tiene la piu'
             * vecchia: i byte di un terminale sono una fila, e scavalcarli
             * sarebbe peggio che perderli. */
            if (coda_len == 0 && n > 0) {
                memcpy(coda_msg, buf, n);
                coda_len = n;
            }
            continue;
        }

        if (meta.tipo == IP_MSG_TCP_INFO && meta.len >= sizeof(IpTcpInfo)) {
            IpTcpInfo info;

            /* E' la risposta alla domanda a orologio di sessione(). Vale quanto
             * varrebbe letta la': se la connessione non c'e' piu', la sessione
             * e' finita. */
            memcpy(&info, buf, sizeof(info));
            if (info.stato != IP_TCP_APERTA && info.stato != IP_TCP_IN_APERTURA)
                coda_chiusa = 1;
        }
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

/* Manda dei byte sulla connessione. Rende 0 o -1. */
/* ! LO STACK PUO' PRENDERNE MENO DI QUANTI GLIENE OFFRI, E PRIMA NON SI
 * GUARDAVA. E' il difetto che ha reso questo programma inservibile per il
 * lavoro a cui serve, e sta scritto nero su bianco in ip_proto.h, sopra
 * IpEsito: «per IP_MSG_TCP_INVIA e' il numero di byte accettati (che puo'
 * essere meno di quelli offerti, se il buffer di trasmissione e' quasi
 * pieno). Chi chiama deve guardare il segno prima del valore».
 *
 * Qui si guardava SOLO il segno:
 *
 *     if (esito(4000) < 0) return -1;
 *     d += q;  n -= q;
 *
 * cioe' si avanzava di tutti e 512 i byte qualunque cosa ne fosse stato
 * preso. Finche' l'uscita e' corta il buffer non si riempie mai e non si
 * vede niente; su un'uscita lunga — `mkfs`, `install` — il buffer si
 * riempie, lo stack comincia ad accettarne meno, e il resto SI BUTTA.
 *
 * ! IL SINTOMO NON SEMBRAVA UNA PERDITA DI DATI. La prima cosa che si
 * vedeva era una riga troncata a meta' parola; subito dopo, piu' niente,
 * perche' a buffer pieno ogni pezzo successivo veniva accettato per zero
 * byte e scartato — e tcp_scrivi rendeva 0, cioe' «fatto». La sessione non
 * finiva, il comando girava fino in fondo e usciva con codice 0, e da fuori
 * sembrava che la macchina si fosse piantata. E siccome telnetd serve UNA
 * SESSIONE PER VOLTA, da li' in poi accettava le connessioni senza
 * servirle: il sospetto cadeva sulla rete, che era l'unica cosa a posto.
 *
 * ! ZERO BYTE PRESI NON E' UN ERRORE, E' «RIPROVA FRA POCO». Il buffer si
 * svuota da solo mentre il client legge: basta aspettare. Si aspetta con
 * una scadenza, pero', perche' un client che ha smesso di leggere davvero
 * non si distingue da uno lento se non dal tempo che passa. */
#define TCP_ATTESE_MAX  600      /* 600 x 50 ms = 30 secondi */

static int tcp_scrivi(int id, const unsigned char *d, unsigned int n)
{
    unsigned char msg[sizeof(IpTcpDati) + 512];
    IpTcpDati     h;
    int           attese = 0;

    while (n > 0) {
        unsigned int q = (n > 512) ? 512 : n;
        int          preso;

        h.id = (unsigned int)id;
        h.len = q;
        memcpy(msg, &h, sizeof(h));
        memcpy(msg + sizeof(h), d, q);

        if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(h) + q) < 0) return -1;

        preso = esito(4000);
        if (preso < 0) return -1;

        if (preso == 0) {
            /* Il buffer e' pieno: si lascia respirare la connessione e si
             * riprova con GLI STESSI byte. */
            if (++attese > TCP_ATTESE_MAX) {
                if (g_verboso)
                    printf("telnetd: il client non legge piu': rinuncio\n");
                return -1;
            }
            usleep(50000);
            continue;
        }

        attese = 0;

        /* ! SI AVANZA DI QUEL CHE HA PRESO, NON DI QUEL CHE SI E' OFFERTO.
         * Se ne ha presi meno, il resto si rioffre al giro dopo. */
        if ((unsigned int)preso > q) preso = (int)q;   /* non dovrebbe, ma */
        d += (unsigned int)preso;
        n -= (unsigned int)preso;
    }
    return 0;
}

/* =============================================================================
 * DALLA PARTE NOSTRA VERSO LA RETE: il capo riga dell'NVT, e il 255
 *
 * ! IL PROTOCOLLO NON PARLA ASCII, PARLA NVT, e la differenza sta in due byte.
 * RFC 854 dice che la fine di una riga sul cavo e' CR LF, DUE caratteri. Un
 * LF da solo vuol dire «scendi di una riga», e basta: il cursore resta nella
 * colonna dov'era. Mandando solo LF — che e' quel che questo programma ha
 * fatto finora — ogni riga comincia dove e' finita quella prima, e l'uscita
 * scende a scaletta verso destra.
 *
 * ! E SULLA CONSOLE LOCALE NON SI VEDEVA, il che e' il motivo per cui e'
 * rimasto li' tanto: li' il capo riga lo sistema il driver di terminale, che
 * su '\n' va a capo E torna a sinistra. Passando dal pty alla rete quella
 * cortesia non c'e' piu'. Il difetto era quindi visibile SOLO da telnet, e
 * sembrava un difetto del programma che stampava.
 *
 * ! IL CR DA SOLO DIVENTA CR NUL, e non e' pedanteria: nell'NVT il CR e' un
 * comando («torna a sinistra»), e per dire «CR e nient'altro» si manda un NUL
 * dietro. Senza, un client rigoroso puo' aspettare il byte dopo per decidere
 * cosa fare. Un programma che disegna una barra di avanzamento con dei CR
 * secchi e' esattamente il caso.
 *
 * ! E IL 255 SI RADDOPPIA. Il byte 255 e' IAC, l'inizio di un comando: se
 * l'uscita di un programma ne contiene uno e lo si manda cosi' com'e', il
 * client si mette ad aspettare un comando che non arrivera' mai e si mangia i
 * byte che seguono. `IAC IAC` vuol dire «un 255 vero, uno solo» — e questo
 * programma lo sapeva gia' nella direzione opposta (vedi filtra()), ma non in
 * questa. Una direzione sola non e' un protocollo: e' meta'.
 *
 * ! IL BUFFER E' IL DOPPIO, perche' il caso peggiore lo e': un'uscita fatta di
 * soli LF, o di soli 255, raddoppia esattamente.
 *
 * ! UN CR IN FONDO AL PEZZO SI TRATTA COME CR DA SOLO, e si puo': se il LF che
 * lo seguiva arriva nella lettura dopo, il client vede CR NUL LF invece di
 * CR LF — e il NUL nell'NVT non fa niente. Si vede la stessa cosa. Tenere uno
 * stato fra una lettura e l'altra per guadagnare zero non vale il difetto che
 * ci si nasconde dentro.
 * ========================================================================= */
static int nvt_scrivi(int id, const unsigned char *d, unsigned int n)
{
    unsigned char fuori[1024];
    unsigned int  i, k = 0;

    for (i = 0; i < n; i++) {
        unsigned char c = d[i];

        if (c == '\n') {
            fuori[k++] = '\r';
            fuori[k++] = '\n';
        } else if (c == '\r') {
            fuori[k++] = '\r';
            if (i + 1 < n && d[i + 1] == '\n') { fuori[k++] = '\n'; i++; }
            else                                 fuori[k++] = '\0';
        } else if (c == IAC) {
            fuori[k++] = IAC;
            fuori[k++] = IAC;
        } else {
            fuori[k++] = c;
        }
    }

    return tcp_scrivi(id, fuori, k);
}

/* -----------------------------------------------------------------------------
 * Il filtro del protocollo
 *
 * ! I COMANDI NON DEVONO ARRIVARE ALLA SHELL. Un IAC e' il byte 255: lasciarlo
 * passare vorrebbe dire consegnare al programma dei byte che il cliente non ha
 * battuto — e le negoziazioni arrivano a raffica appena ci si collega, quindi
 * il primo comando che si vedrebbe sarebbe spazzatura.
 *
 * ! E `IAC IAC` E' UN 255 VERO, uno solo. Senza questo caso, un byte 255 nei
 * dati diventerebbe l'inizio di un comando e si mangerebbe il byte dopo.
 *
 * Rende quanti byte ha messo in `fuori`. Le risposte alla negoziazione le
 * scrive direttamente sulla connessione: sono poche e non aspettano.
 * --------------------------------------------------------------------------- */
static unsigned int filtra(int id, int pty, const unsigned char *d, unsigned int n,
                           unsigned char *fuori)
{
    unsigned int i = 0, k = 0;

    while (i < n) {
        /* ! UN INVIO SUL CAVO E' DUE BYTE, E IL SECONDO NON VA CONSEGNATO. Il
         * protocollo dice che un CR viaggia sempre seguito da LF o da NUL:
         * passarli tutt'e due vuol dire che dopo ogni comando arriva una riga
         * in piu' — e con NUL, un byte zero all'inizio della riga SEGUENTE. Il
         * sintomo era «comando non trovato: id» su un id battuto giusto, con
         * un carattere invisibile attaccato. */
        if (d[i] == '\r') {
            fuori[k++] = '\n';
            i++;
            if (i < n && (d[i] == '\n' || d[i] == '\0')) i++;
            continue;
        }

        if (d[i] != IAC) { fuori[k++] = d[i++]; continue; }

        if (i + 1 >= n) break;              /* comando spezzato: si scarta */

        if (d[i + 1] == IAC) { fuori[k++] = IAC; i += 2; continue; }

        if (d[i + 1] == WILL || d[i + 1] == WONT ||
            d[i + 1] == DO   || d[i + 1] == DONT) {
            unsigned char r[3];
            unsigned int  cmd = d[i + 1], opt;

            if (i + 2 >= n) break;
            opt = d[i + 2];

            /* ! SI RISPONDE SEMPRE, E SI DICE DI NO A TUTTO IL RESTO. Un
             * client che chiede qualcosa e non riceve risposta la richiede, e
             * due che si aspettano a vicenda restano fermi. «No» e' una
             * risposta; il silenzio no. */
            r[0] = IAC;
            if (cmd == DO)        r[1] = (opt == OPT_ECHO || opt == OPT_SGA) ? WILL : WONT;
            else if (cmd == DONT) r[1] = WONT;
            else if (cmd == WILL) r[1] = (opt == OPT_SGA ||
                                          opt == OPT_NAWS) ? DO : DONT;
            else                  r[1] = DONT;
            r[2] = (unsigned char)opt;

            tcp_scrivi(id, r, 3);
            i += 3;
            continue;
        }

        if (d[i + 1] == SB) {
            /* Sottonegoziazione: si legge fino a IAC SE. Vanno consumate
             * comunque, o i loro byte finiscono nel testo. */
            unsigned int j = i + 2;

            while (j + 1 < n && !(d[j] == IAC && d[j + 1] == SE)) j++;

            /* =================================================================
             * ! LA MISURA DELLA FINESTRA ARRIVA QUI, E FINO AL 18 AGOSTO 2026
             * FINIVA NEL NULLA. Il pty restava convinto di essere 80x24 per
             * tutta la sessione: un programma a schermo pieno disegnava dentro
             * un rettangolo che non c'era piu', e il sintomo — testo che va a
             * capo dove non deve — non somiglia per niente a «ho
             * ridimensionato la finestra».
             *
             * IAC SB 31 w1 w0 h1 h0 IAC SE, in big endian, e chi manda una
             * misura con dentro un 255 la scrive IAC IAC: e' il caso raro che
             * si sbaglia sempre, e qui vale la pena scriverlo perche' una
             * finestra larga 255 colonne non e' assurda.
             * ================================================================= */
            if (i + 2 < n && d[i + 2] == OPT_NAWS) {
                unsigned char v[4];
                unsigned int  q = i + 3, c = 0;

                while (q < j && c < 4) {
                    if (d[q] == IAC && q + 1 < j && d[q + 1] == IAC) q++;
                    v[c++] = d[q++];
                }

                if (c == 4) {
                    unsigned int colonne = ((unsigned int)v[0] << 8) | v[1];
                    unsigned int righe   = ((unsigned int)v[2] << 8) | v[3];

                    /* Zero vuol dire «non lo so», non «zero»: si lascia stare
                     * invece di dare a un programma una geometria in cui
                     * dividere per zero. */
                    if (colonne && righe) {
                        pty_ctl(pty, PTY_CTL_MISURA, (righe << 16) | colonne);
                        if (g_verboso)
                            printf("telnetd: il terminale adesso e' %ux%u\n",
                                   colonne, righe);
                    }
                }
            }

            i = (j + 1 < n) ? j + 2 : n;
            continue;
        }

        i += 2;                              /* comando a due byte: si ignora */
    }
    return k;
}

/* -----------------------------------------------------------------------------
 * Un blocco arrivato dalla rete: si ripulisce dal protocollo e si consegna al
 * programma.
 *
 * ! STA IN UNA FUNZIONE PERCHE' I POSTI DA CUI ARRIVA SONO DUE — la casella
 * letta dal ciclo, e la posta che attendi() ha messo da parte mentre si
 * scriveva sulla connessione. Due copie dello stesso pezzo sarebbero due posti
 * dove dimenticarsi del filtro, e un IAC consegnato alla shell e' spazzatura
 * battuta da nessuno.
 *
 * `msg` e' il messaggio INTERO, intestazione compresa.
 * --------------------------------------------------------------------------- */
static void consegna(int id, int pty, const unsigned char *msg, unsigned int n)
{
    unsigned char pulito[IPC_MSG_MAX_DATA];
    IpTcpDati     d;
    unsigned int  len, q;

    if (n < sizeof(d)) return;

    memcpy(&d, msg, sizeof(d));

    /* ! LA LUNGHEZZA DICHIARATA SI ACCORCIA A QUELLA DEL MESSAGGIO. Fidarsi di
     * d.len vorrebbe dire filtrare byte che nel messaggio non ci sono, cioe'
     * la coda del proprio buffer: e' lo stesso difetto che in ip.c si e'
     * pagato con una firma SSH che non tornava. */
    len = d.len;
    if (len > n - sizeof(d)) len = n - (unsigned int)sizeof(d);
    if (len > sizeof(pulito)) len = sizeof(pulito);

    q = filtra(id, pty, msg + sizeof(d), len, pulito);
    if (q > 0) write(pty, pulito, q);
}

/* -----------------------------------------------------------------------------
 * Una sessione: la connessione da una parte, un pty dall'altra
 * --------------------------------------------------------------------------- */
static void sessione(int id, const Config *cfg, int con_login)
{
    static const unsigned char apertura[] = {
        IAC, WILL, OPT_ECHO,        /* l'eco la fa il nostro pty */
        IAC, WILL, OPT_SGA,         /* niente «vai avanti»: modo carattere */
        IAC, DO,   OPT_SGA,
        /* ! LA MISURA SI CHIEDE, NON SI ASPETTA. Un client la manda da se'
         * solo se qualcuno gli ha detto DO NAWS; senza, resta il valore di
         * partenza per tutta la sessione. */
        IAC, DO,   OPT_NAWS
    };
    unsigned char buf[IPC_MSG_MAX_DATA];
    struct pollfd v[2];
    SpawnRedir    red[3];
    IpTcpRif      r;
    char         *argv[6];
    const char   *prog = con_login ? cfg->shell : "/bin/sh";
    int           fd[2], figlio, prenotato = 0, i, na = 0;
    int           fermi = 0;   /* giri di seguito senza niente da fare */
    unsigned int  ultimo_stato;   /* quando si e' chiesto l'ultima volta */

    if (pty_apri(fd) != 0) {
        /* ! CHI NON PUO' SERVIRE LO DICE, E CHIUDE. Prima qui c'era un printf
         * sulla console della macchina e un `return`: la connessione restava
         * APERTA e senza nessuno dietro, e il client — che non ha ancora
         * ricevuto la negoziazione, quindi fa l'eco da se' — mostrava i
         * comandi battuti e non eseguiva niente. E' il sintomo per cui si e'
         * detto per giorni «la macchina si impianta»: sembrava caduto il
         * sistema, ed era un server che sapeva di non poter servire e non
         * l'aveva detto a nessuno.
         *
         * ! I PSEUDO-TERMINALI SONO QUATTRO (PTY_MAX, kernel/include/pty.h), e
         * uno resta occupato finche' TUTTI e due i capi sono chiusi: se la
         * shell di una sessione finita lascia un figlio vivo, quel pty non
         * torna. Il numero quattro e' anche il motivo per cui il guaio si
         * vedeva «dopo quattro o cinque collegamenti». */
        static const char scusa[] =
            "\r\ntelnetd: non c'e' un pseudo-terminale libero"
            " (questo sistema ne tiene quattro).\r\n"
            "         Riprova fra un minuto; se non passa, guarda i processi "
            "rimasti da una sessione finita male.\r\n";
        IpTcpRif rif;

        printf("telnetd: niente pty libero: rifiuto la connessione\n");
        tcp_scrivi(id, (const unsigned char *)scusa, sizeof(scusa) - 1);

        rif.id = (unsigned int)id;
        ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &rif, sizeof(rif));
        esito(2000);
        return;
    }

    /* 80x24 e' la misura che un client telnet si aspetta se nessuno gliene
     * dice un'altra. Chi ne ha una vera la manda con NAWS, subito dopo la
     * negoziazione e a ogni ridimensionamento: vedi filtra(). */
    pty_ctl(fd[0], PTY_CTL_MISURA, (24u << 16) | 80u);

    for (i = 0; i < 3; i++) {
        red[i].fd = i; red[i].flags = 0; red[i].percorso = 0;
        red[i].fd_padre = fd[1];
    }

    argv[na++] = (char *)prog;

    /* ! GLI ELENCHI DEGLI UTENTI LI FA RISPETTARE login, E NON QUESTO
     * PROGRAMMA, perche' e' login a sapere chi ha bussato: qui, quando si
     * decide chi lanciare, un nome utente non e' ancora stato battuto. Passare
     * le liste per argomento e' anche cio' che le rende visibili in un elenco
     * dei processi — chi guarda vede quale regola sta girando. */
    if (con_login) {
        if (cfg->utenti[0]) { argv[na++] = "-c"; argv[na++] = (char *)cfg->utenti; }
        if (cfg->nega[0])   { argv[na++] = "-n"; argv[na++] = (char *)cfg->nega; }
    }
    argv[na] = 0;

    figlio = spawn_ex(prog, argv, environ, red, 3);

    /* ! LO SLAVE SI CHIUDE SUBITO, come con le pipe: finche' lo teniamo aperto
     * noi, il pty conta un capo vivo dalla parte della shell e la fine dei
     * dati non arriva mai — la sessione non finirebbe nemmeno quando il figlio
     * muore. */
    close(fd[1]);

    if (figlio < 0) {
        printf("telnetd: non riesco ad avviare %s\n", prog);
        close(fd[0]);
        return;
    }

    printf("telnetd: sessione aperta, %s ha il PID %d\n", prog, figlio);

    fermi = 0;
    ultimo_stato = uptime_ms();

    /* ! LA POSTA MESSA DA PARTE E' DI QUESTA SESSIONE E BASTA: quel che fosse
     * avanzato dalla precedente riguarda una connessione che non c'e' piu', e
     * consegnarlo vorrebbe dire scrivere sul terminale di un altro i tasti di
     * chi se n'e' gia' andato. */
    coda_len    = 0;
    coda_chiusa = 0;

    tcp_scrivi(id, apertura, sizeof(apertura));

    r.id = (unsigned int)id;

    for (;;) {
        int stato = 0;

        /* ! PRIMA DI TUTTO SI RITIRA LA POSTA MESSA DA PARTE da attendi()
         * mentre si scriveva sulla connessione. Sono byte GIA' CONSEGNATI
         * dallo stack: la prenotazione che li ha portati e' gia' stata
         * consumata, e se non se ne fa un'altra non arriva piu' niente. Vedi
         * il commento sopra attendi(). */
        if (coda_chiusa) {
            if (g_verboso) printf("telnetd: il client se n'e' andato\n");
            break;
        }

        if (coda_len > 0) {
            unsigned int n = coda_len;

            coda_len  = 0;
            prenotato = 0;          /* la consegna l'ha consumata */

            /* Si copia prima di consegnare: filtra() risponde alla
             * negoziazione, e rispondere vuol dire tcp_scrivi(), cioe' un
             * altro giro di attendi() che in questa casella puo' scrivere. */
            memcpy(buf, coda_msg, n);
            consegna(id, fd[0], buf, n);
        }

        /* ! SI CHIEDE ALLO STACK SE LA CONNESSIONE E' ANCORA VIVA, e prima
         * non si chiedeva a nessuno. Le tre vie d'uscita di questo ciclo
         * erano: il figlio esce, il pty dice EOF, una prenotazione torna con
         * esito negativo. Ne mancava la piu' comune — IL CLIENT SE NE VA: chi
         * chiude il socket dall'altra parte non fa uscire la shell (quella
         * aspetta un tasto) e non produce nessun esito, perche' la
         * prenotazione resta li'. Il ciclo girava per sempre, e siccome
         * telnetd serve UNA SESSIONE PER VOLTA, da quel momento la macchina
         * accettava le connessioni senza servirle.
         *
         * ! MA SI CHIEDE SOLO QUANDO NON PASSA NIENTE, ed e' la correzione
         * della correzione. La prima versione interrogava ogni quattro giri
         * del ciclo: con molto output da stampare il ciclo gira in fretta, le
         * interrogazioni si accumulano, e ogni risposta finisce nella stessa
         * casella da cui tcp_scrivi() aspetta il proprio esito. attendi() le
         * scarta — non le confonde — ma intanto la casella si riempie, e
         * quando e' piena lo stack non riesce piu' a consegnare i dati. Il
         * sintomo era un `fdisk` che stampava la sua tabella e si fermava a
         * META' PAROLA, e da li' la sessione non rispondeva piu'.
         *
         * ! E CHIEDERLO A RIPOSO NON E' UN RIPIEGO, E' IL POSTO GIUSTO. Un
         * client che se n'e' andato non manda e non riceve niente: e'
         * esattamente la condizione in cui il ciclo non ha nulla da fare. Se
         * invece i byte scorrono, che la connessione sia viva lo dimostrano i
         * byte stessi. */
        /* ! LA CONDIZIONE E' A OROLOGIO, E NON SOLO A GIRI FERMI. Contare i
         * giri a vuoto misura il riposo, ma se il ciclo per qualunque ragione
         * non riposa mai — una poll che torna pronta e una ricezione che non
         * rende niente, per dirne una — `fermi` resta a zero e non si chiede
         * piu' niente a nessuno: la sessione non finisce mai, e siccome se ne
         * serve UNA PER VOLTA la macchina accetta le connessioni senza
         * rispondere una sola parola. E' successo il 14 settembre 2026: la
         * porta 23 apriva e restava muta, nemmeno la negoziazione iniziale.
         *
         * ! E IL TEMPO E' ANCHE CIO' CHE IMPEDISCE L'INGORGO. La domanda a
         * riposo resta com'era, ogni due secondi al massimo; quando invece i
         * byte scorrono se ne fa una ogni quindici, che e' troppo poco per
         * riempire la casella (era quello il guaio della prima correzione:
         * una domanda ogni quattro giri, con molto da stampare, sono decine
         * di risposte al secondo nella stessa casella dei dati) ed e'
         * abbastanza per accorgersi entro un quarto di minuto di un ciclo
         * che si e' impuntato. */
        {
            unsigned int ora   = uptime_ms();
            unsigned int quando = (fermi >= 4) ? 2000u : 15000u;

            if (ora - ultimo_stato >= quando) {
                ultimo_stato = ora;
                fermi = 0;
                ipc_send(pid_ip, IP_MSG_TCP_STATO, &r, sizeof(r));
            }
        }

        /* Il figlio se n'e' andato: la sessione e' finita. */
        if (waitpid(figlio, &stato, WNOHANG) == figlio) {
            if (g_verboso) printf("telnetd: %s e' uscito (%d)\n", prog, (int)stato);
            break;
        }

        /* ! UNA PRENOTAZIONE PER VOLTA, e non una a ogni giro: lo stack
         * consegna a chi ha prenotato, e prenotare due volte vorrebbe dire due
         * consegne per gli stessi byte. */
        if (!prenotato) {
            if (ipc_send(pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) break;
            prenotato = 1;
        }

        /* ! SI ASPETTA SU TUTT'E DUE INSIEME, ed e' esattamente il caso per cui
         * poll() esiste in questo sistema: da una parte il pty, dall'altra la
         * mailbox da cui arrivano i dati della rete. Aspettarne una sola per
         * volta vorrebbe dire non accorgersi dell'altra finche' la prima non
         * si muove — cioe' una sessione che risponde solo se si batte
         * qualcosa. */
        v[0].fd = fd[0];  v[0].events = POLLIN; v[0].revents = 0;
        v[1].fd = FD_IPC; v[1].events = POLLIN; v[1].revents = 0;

        {
            int pronti = poll(v, 2, 500);

            if (pronti < 0) break;
            if (pronti == 0) fermi++;   /* scaduto senza niente: a riposo */
            else             fermi = 0;
        }

        /* Dal programma verso la rete.
         *
         * ! SI LEGGONO AL PIU' 512 BYTE PERCHE' LA TRADUZIONE PUO' RADDOPPIARLI
         * e il buffer di nvt_scrivi e' 1024. Chiedere di piu' vorrebbe dire
         * scrivere fuori da quel buffer nel caso peggiore, che non e' raro:
         * un'uscita fatta di righe corte e' quasi tutta capi riga. */
        if (v[0].revents & POLLIN) {
            int n = (int)read(fd[0], buf, 512);

            if (n > 0) {
                if (nvt_scrivi(id, buf, (unsigned int)n) != 0) break;
            } else if (n == 0) {
                break;                       /* lo slave non c'e' piu' */
            }
        }

        /* Dalla rete verso il programma. */
        if (v[1].revents & POLLIN) {
            IpcMessage meta;
            /* =================================================================
             * ! LA SCADENZA QUI NON E' UN DI PIU': E' CIO' CHE IMPEDISCE
             * L'ATTESA ETERNA, E FINO AL 15 SETTEMBRE 2026 ERA ZERO.
             *
             * `ipc_recv_timeout(..., 0)` NON vuol dire «non aspettare»: vuol
             * dire ASPETTA PER SEMPRE — e' scritto sopra il prototipo in
             * libc.h, «timeout_ms == 0 = attesa senza scadenza, cioe'
             * esattamente ipc_recv». Qui si arrivava convinti del contrario,
             * tanto che la riga sotto tratta il caso «non rende niente».
             *
             * ! E LA CASELLA PUO' ESSERE VUOTA ANCHE SE LA poll HA DETTO DI SI'.
             * `revents` e' la fotografia di UN ISTANTE FA, e fra quell'istante
             * e questa riga c'e' il ramo di sopra — quello dal pty verso la
             * rete — che chiama tcp_scrivi(), quindi attendi(), che dalla
             * casella PESCA. Se il messaggio che aveva fatto scattare la poll
             * era proprio quello che attendi() ha ritirato, qui non c'e' piu'
             * niente e il processo si ferma per sempre dentro una syscall.
             *
             * ! COSI' MUORE UNA SESSIONE SENZA CHE NESSUNO POSSA ACCORGERSENE:
             * telnetd serve UNA SESSIONE PER VOLTA, e la domanda a orologio che
             * dovrebbe scoprire il client andato via sta NEL CICLO — un ciclo
             * che non gira piu' non chiede piu' niente. Dal di fuori si vede la
             * porta 23 che accetta il TCP e non manda un byte, nemmeno la
             * negoziazione, per sempre. E' @DIF-TELNETMUTO, cercata per due
             * giorni dalla parte dello stack.
             *
             * Cento millisecondi: abbastanza corti da non farsi sentire in una
             * sessione interattiva, e una scadenza vale quanto un messaggio —
             * il ciclo riprende, il contatore del riposo avanza, e la domanda
             * allo stack riparte.
             * ================================================================= */
            int        got = ipc_recv_timeout(&meta, buf, sizeof(buf), 100);

            /* ! UNA CASELLA PRONTA CHE NON RENDE NIENTE CONTA COME RIPOSO.
             * Senza questo, una poll che dice «c'e' posta» e una ricezione che
             * non trova niente si rincorrono a giri vuoti con `fermi` inchiodato
             * a zero: il ciclo non dorme piu' e non si chiede piu' nulla. */
            if (got < 0) { fermi++; continue; }
            if ((int)meta.sender_pid != pid_ip) continue;

            if (meta.tipo == IP_MSG_TCP_DATI) {
                prenotato = 0;      /* lo stack l'ha consumata consegnando */
                consegna(id, fd[0], buf, meta.len);
            } else if (meta.tipo == IP_MSG_TCP_INFO && meta.len >= sizeof(IpTcpInfo)) {
                IpTcpInfo info;

                memcpy(&info, buf, sizeof(info));

                /* Aperta o in apertura: si continua. Qualunque altra cosa —
                 * chiusa, in chiusura, reset — vuol dire che dall'altra parte
                 * non c'e' piu' nessuno a cui parlare. */
                if (info.stato != IP_TCP_APERTA &&
                    info.stato != IP_TCP_IN_APERTURA) {
                    if (g_verboso)
                        printf("telnetd: il client se n'e' andato (stato %u)\n",
                               info.stato);
                    break;
                }
            } else if (meta.tipo == IP_MSG_ESITO) {
                IpEsito e;

                memcpy(&e, buf, sizeof(e));
                /* ! LA CONNESSIONE CHIUSA ARRIVA COSI', come esito negativo di
                 * una prenotazione che non si potra' soddisfare. */
                if (e.codice < 0 && e.codice != -ETIMEDOUT) break;
                prenotato = 0;
            }
        }
    }

    /* ! IL FIGLIO SI INTERROMPE, NON SI ASPETTA. Se e' ancora vivo vuol dire
     * che il cliente ha chiuso mentre lui stava li' ad aspettare un tasto:
     * senza questo, resterebbe attaccato a un pty che non ha piu' nessuno
     * dall'altra parte — un processo per ogni sessione finita male. */
    interrompi(figlio);

    close(fd[0]);
    ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    esito(2000);

    printf("telnetd: sessione chiusa\n");
}

int main(int argc, char **argv)
{
    IpTcpAscolta a;
    IpTcpAccetta ac;
    Config       cfg;
    int          asc, con_login = 1, porta = 0, i, auto_avvio = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0)      con_login = 0;
        else if (strcmp(argv[i], "-auto") == 0) auto_avvio = 1;
        else if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            strncpy(g_cfg_file, argv[++i], sizeof(g_cfg_file) - 1);
            g_cfg_file[sizeof(g_cfg_file) - 1] = '\0';
        }
        else if (strcmp(argv[i], "-h") == 0) {
            printf("uso: telnetd [PORTA] [-s] [-auto] [-v] [-f FILE]\n\n");
            printf("  Serve una sessione per volta su una connessione TCP.\n");
            printf("  Senza argomenti legge /boot/telnetd.cfg: porta, shell,\n");
            printf("  utenti concessi e negati, e da quali indirizzi.\n\n");
            printf("  -s     da' la shell senza chiedere l'accesso\n");
            printf("  -auto  parte solo se il file dice 'avvio = login'\n");
            printf("         oppure 'avvio = root'; altrimenti esce zitto.\n");
            printf("         E' la riga che sta in /boot/avvio.sh.\n");
            printf("  -f     un'altra configurazione\n");
            printf("  La porta e -s scritti qui vincono sul file.\n\n");
            printf("  I dati viaggiano IN CHIARO, password compresa.\n");
            return 0;
        }
        else porta = atoi(argv[i]);
    }

    config_leggi(&cfg);

    /* ! CON -auto DECIDE IL FILE, E IL SILENZIO E' IL PUNTO. Questa riga sta
     * in /boot/avvio.sh su ogni macchina, compresa quella di chi non ha mai
     * sentito nominare telnet: se la chiave non c'e' o dice «no», qui non si
     * stampa niente e non si apre niente. Un messaggio del tipo «non parto»
     * a ogni accensione insegnerebbe solo a non leggere piu' quel che scorre
     * all'avvio. */
    if (auto_avvio) {
        if (cfg.avvio == AVVIO_NO) return 0;
        con_login = (cfg.avvio == AVVIO_LOGIN);
    }

    /* ! LA RIGA DI COMANDO VINCE SUL FILE, e vale la pena dirlo: e' la regola
     * che permette di provare una configurazione senza scriverla, e di aprire
     * una porta diversa per un istante senza toccare quella di sempre. */
    if (porta > 0) cfg.porta = porta;

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    a.porta = (unsigned int)cfg.porta;
    if (ipc_send(pid_ip, IP_MSG_TCP_ASCOLTA, &a, sizeof(a)) < 0) return 1;

    asc = esito(3000);
    if (asc < 0) {
        printf("telnetd: non posso ascoltare sulla porta %d (%d)%s\n",
               cfg.porta, asc,
               asc == -EADDRINUSE ? ": c'e' gia' qualcuno li'" : "");
        return 1;
    }

    printf("telnetd: in ascolto sulla porta %d, %s\n", cfg.porta,
           con_login ? cfg.shell : "/bin/sh SENZA accesso (-s)");
    if (cfg.da[0])     printf("telnetd: solo da %s\n", cfg.da);
    if (cfg.utenti[0]) printf("telnetd: solo gli utenti %s\n", cfg.utenti);
    if (cfg.nega[0])   printf("telnetd: negati %s\n", cfg.nega);

    for (;;) {
        int id;

        ac.id         = (unsigned int)asc;
        ac.timeout_ms = 10000;

        if (ipc_send(pid_ip, IP_MSG_TCP_ACCETTA, &ac, sizeof(ac)) < 0) break;

        id = esito(12000);
        if (id == -ETIMEDOUT || id == -EAGAIN) continue;
        if (id < 0) {
            printf("telnetd: accetta fallita (%d)\n", id);
            break;
        }

        /* ! LA CONFIGURAZIONE SI RILEGGE ADESSO, a connessione accettata e
         * prima di decidere che farne: vedi config_leggi. */
        config_leggi(&cfg);

        /* Chi ha bussato? Lo dice lo stack, che l'indirizzo ce l'ha. */
        {
            unsigned char buf[IPC_MSG_MAX_DATA];
            unsigned int  len;
            IpTcpRif      r;
            IpTcpInfo     info;

            r.id = (unsigned int)id;
            memset(&info, 0, sizeof(info));

            if (ipc_send(pid_ip, IP_MSG_TCP_STATO, &r, sizeof(r)) >= 0 &&
                attendi(IP_MSG_TCP_INFO, buf, &len, 2000) == 0 &&
                len >= sizeof(info)) {
                memcpy(&info, buf, sizeof(info));
            }

            printf("telnetd: qualcuno si e' collegato da %u.%u.%u.%u "
                   "(connessione %d)\n",
                   info.ip[0], info.ip[1], info.ip[2], info.ip[3], id);

            if (!indirizzo_ammesso(cfg.da, info.ip)) {
                /* ! SI CHIUDE E BASTA, SENZA SPIEGARE NIENTE ALL'ALTRO CAPO.
                 * Un messaggio del tipo «non sei nella lista» direbbe a chi
                 * bussa che dietro quella porta c'e' qualcosa e che il filtro
                 * e' per indirizzo: informazioni gratis per chi sta provando.
                 * Nel registro locale invece si scrive per esteso. */
                printf("telnetd: rifiutato: %u.%u.%u.%u non e' fra gli "
                       "indirizzi ammessi (%s)\n",
                       info.ip[0], info.ip[1], info.ip[2], info.ip[3], cfg.da);
                ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
                esito(2000);
                continue;
            }
        }

        sessione(id, &cfg, con_login);
    }

    {
        IpTcpRif r;

        r.id = (unsigned int)asc;
        ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
        esito(2000);
    }
    return 0;
}
