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

typedef struct {
    int  porta;
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
static int tcp_scrivi(int id, const unsigned char *d, unsigned int n)
{
    unsigned char msg[sizeof(IpTcpDati) + 512];
    IpTcpDati     h;

    while (n > 0) {
        unsigned int q = (n > 512) ? 512 : n;

        h.id = (unsigned int)id;
        h.len = q;
        memcpy(msg, &h, sizeof(h));
        memcpy(msg + sizeof(h), d, q);

        if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(h) + q) < 0) return -1;
        if (esito(4000) < 0) return -1;

        d += q;
        n -= q;
    }
    return 0;
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
    unsigned char pulito[IPC_MSG_MAX_DATA];
    struct pollfd v[2];
    SpawnRedir    red[3];
    IpTcpRif      r;
    IpTcpDati     d;
    unsigned int  len;
    char         *argv[6];
    const char   *prog = con_login ? cfg->shell : "/bin/sh";
    int           fd[2], figlio, prenotato = 0, i, na = 0;

    if (pty_apri(fd) != 0) {
        printf("telnetd: niente pty libero\n");
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

    tcp_scrivi(id, apertura, sizeof(apertura));

    r.id = (unsigned int)id;

    for (;;) {
        int stato = 0;

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

        if (poll(v, 2, 500) < 0) break;

        /* Dal programma verso la rete. */
        if (v[0].revents & POLLIN) {
            int n = (int)read(fd[0], buf, 512);

            if (n > 0) {
                if (tcp_scrivi(id, buf, (unsigned int)n) != 0) break;
            } else if (n == 0) {
                break;                       /* lo slave non c'e' piu' */
            }
        }

        /* Dalla rete verso il programma. */
        if (v[1].revents & POLLIN) {
            IpcMessage meta;
            int        got = ipc_recv_timeout(&meta, buf, sizeof(buf), 0);

            if (got < 0) continue;
            if ((int)meta.sender_pid != pid_ip) continue;

            if (meta.tipo == IP_MSG_TCP_DATI && meta.len >= sizeof(d)) {
                unsigned int q;

                memcpy(&d, buf, sizeof(d));
                prenotato = 0;

                len = d.len;
                if (len > sizeof(pulito)) len = sizeof(pulito);

                q = filtra(id, fd[0], buf + sizeof(d), len, pulito);
                if (q > 0) write(fd[0], pulito, q);
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
    int          asc, con_login = 1, porta = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0)      con_login = 0;
        else if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            strncpy(g_cfg_file, argv[++i], sizeof(g_cfg_file) - 1);
            g_cfg_file[sizeof(g_cfg_file) - 1] = '\0';
        }
        else if (strcmp(argv[i], "-h") == 0) {
            printf("uso: telnetd [PORTA] [-s] [-v] [-f FILE]\n\n");
            printf("  Serve una sessione per volta su una connessione TCP.\n");
            printf("  Senza argomenti legge /boot/telnetd.cfg: porta, shell,\n");
            printf("  utenti concessi e negati, e da quali indirizzi.\n\n");
            printf("  -s  da' la shell senza chiedere l'accesso\n");
            printf("  -f  un'altra configurazione\n");
            printf("  La porta e -s scritti qui vincono sul file.\n\n");
            printf("  I dati viaggiano IN CHIARO, password compresa.\n");
            return 0;
        }
        else porta = atoi(argv[i]);
    }

    config_leggi(&cfg);

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
