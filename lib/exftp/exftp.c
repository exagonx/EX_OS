/* =============================================================================
 * lib/exftp/exftp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il client FTP condiviso. Il perche' di questa libreria sta in exftp.h.
 *
 * -----------------------------------------------------------------------------
 * ! UNA PRENOTAZIONE PER VOLTA, E NON E' UN DETTAGLIO
 *
 * Lo stack consegna i byte a chi ha prenotato (IP_MSG_TCP_RICEVI) e LA
 * PRENOTAZIONE SI CONSUMA A OGNI CONSEGNA. Chi aspetta un messaggio preciso e
 * butta quelli che non sta cercando butta insieme la prenotazione che li ha
 * portati, e da quel momento non riceve piu' niente: e' il difetto che il 15
 * settembre 2026 ha reso sorda una sessione telnet — vedi il commento sopra
 * attendi() in bin/telnetd/telnetd.c.
 *
 * Qui il problema non si pone, ma solo perche' il codice e' scritto apposta:
 * fra il canale di controllo e quello dei dati c'e' SEMPRE UNA SOLA
 * PRENOTAZIONE APERTA. Si legge dal controllo, oppure dai dati, mai insieme; e
 * l'esito di ogni invio si consuma prima di fare altro. Chi tocca questo file
 * tenga la stessa regola.
 * ============================================================================= */
#include "libc.h"
#include "ip_proto.h"
#include "rete.h"
#include "dns.h"
#include "exftp.h"

static int g_pid_ip = 0;

/* -----------------------------------------------------------------------------
 * Lo strato di sotto: la connessione TCP
 * --------------------------------------------------------------------------- */
static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;
    int        i;

    for (i = 0; i < 32; i++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != g_pid_ip) continue;
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

/* Apre una connessione, ritentando su -EAGAIN: la prima volta l'ARP non ha
 * ancora la risposta, e quello non e' un errore ma un «non ancora». */
static int tcp_apri(const unsigned char *ip, unsigned int porta)
{
    IpTcpApri a;
    int       id = -1, giri;

    memcpy(a.ip, ip, 4);
    a.porta      = porta;
    a.timeout_ms = 8000;

    for (giri = 0; giri < 10; giri++) {
        if (ipc_send(g_pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0) return -1;
        id = esito(10000);
        if (id != -EAGAIN) break;
        usleep(200 * 1000);
    }
    return id;
}

static void tcp_chiudi(int id)
{
    IpTcpRif r;

    if (id <= 0) return;
    r.id = (unsigned int)id;
    ipc_send(g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    esito(2000);
}

/* Rende i byte letti, 0 se l'altro ha chiuso, <0 su errore. */
static int tcp_leggi(int id, unsigned char *dst, unsigned int max, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     d;

    r.id = (unsigned int)id;
    if (ipc_send(g_pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) return -1;
    if (attendi(IP_MSG_TCP_DATI, buf, &len, ms) != 0) return -1;
    if (len < sizeof(d)) return -1;

    memcpy(&d, buf, sizeof(d));
    if (d.len == 0) return 0;
    /* =========================================================================
     * ! IL BUFFER DI CHI LEGGE DEV'ESSERE GRANDE QUANTO UNA CONSEGNA INTERA,
     * E IL 15 SETTEMBRE 2026 NON LO ERA.
     *
     * Lo stack consegna fino a IP_TCP_DATI_MAX byte in un colpo (1520: un
     * messaggio IPC meno l'intestazione). Con un buffer da 1024 la riga qui
     * sotto tagliava il resto e non lo diceva a nessuno: i byte in piu' non
     * finivano da nessuna parte e la prenotazione era gia' stata consumata.
     *
     * ! IL SINTOMO NON SOMIGLIAVA A UNA PERDITA DI DATI: `soccorso` elencava
     * SETTE librerie invece di dodici, e le cinque mancanti erano CONSECUTIVE —
     * cioe' mezzo kilobyte di elenco sparito in mezzo. Sembrava un difetto del
     * riconoscimento dei nomi, e invece era un buco nel testo.
     *
     * Adesso chi chiama passa un buffer da IPC_MSG_MAX_DATA e questa riga non
     * scatta mai; resta come rete, perche' un buffer piccolo non deve
     * diventare una corruzione silenziosa.
     * ========================================================================= */
    if (d.len > max) d.len = max;
    if (d.len > len - sizeof(d)) d.len = len - (unsigned int)sizeof(d);

    memcpy(dst, buf + sizeof(d), d.len);
    return (int)d.len;
}

static int tcp_scrivi(int id, const unsigned char *src, unsigned int n)
{
    unsigned char msg[sizeof(IpTcpDati) + 1024];
    IpTcpDati     d;
    unsigned int  fatti = 0;
    int           attese = 0;

    while (fatti < n) {
        unsigned int q = n - fatti;
        int          rc;

        if (q > 1024u) q = 1024u;

        d.id  = (unsigned int)id;
        d.len = q;
        memcpy(msg, &d, sizeof(d));
        memcpy(msg + sizeof(d), src + fatti, q);

        if (ipc_send(g_pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + q) < 0) return -1;
        rc = esito(8000);
        if (rc < 0) return rc;

        /* ! ZERO BYTE PRESI NON E' UN ERRORE, E' «RIPROVA FRA POCO»: il buffer
         * di trasmissione e' pieno e si svuota mentre l'altro legge. Si aspetta
         * con una scadenza, pero', perche' un server che ha smesso di leggere
         * non si distingue da uno lento se non dal tempo. */
        if (rc == 0) {
            if (++attese > 600) return -ETIMEDOUT;   /* 30 s */
            usleep(50 * 1000);
            continue;
        }
        attese = 0;
        fatti += (unsigned int)rc;
    }
    return (int)fatti;
}

/* Il nostro indirizzo, che serve a PORT. */
static int mio_indirizzo(unsigned char ip[4])
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpStato       s;

    if (ipc_send(g_pid_ip, IP_MSG_STATO, 0, 0) < 0) return -1;
    if (attendi(IP_MSG_STATO_R, buf, &len, 3000) != 0 || len < sizeof(s)) return -1;

    memcpy(&s, buf, sizeof(s));
    memcpy(ip, s.cfg.ip, 4);
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il canale di controllo: righe e risposte a tre cifre
 * --------------------------------------------------------------------------- */

/* Estrae una riga dall'accumulatore. 1 se c'era, 0 se serve leggere ancora. */
static int riga_pronta(ExFtp *f, char *out, unsigned int max)
{
    unsigned int i, n;

    for (i = 0; i < f->acc_len; i++) {
        if (f->acc[i] != '\n') continue;

        n = i;
        if (n > 0 && f->acc[n - 1] == '\r') n--;
        if (n > max - 1) n = max - 1;

        memcpy(out, f->acc, n);
        out[n] = '\0';

        memmove(f->acc, f->acc + i + 1, f->acc_len - i - 1);
        f->acc_len -= i + 1;
        return 1;
    }
    return 0;
}

static int riga(ExFtp *f, char *out, unsigned int max, unsigned int ms)
{
    while (!riga_pronta(f, out, max)) {
        int n;

        if (f->acc_len >= sizeof(f->acc) - 1) return -1;   /* riga infinita */

        n = tcp_leggi(f->ctrl, (unsigned char *)f->acc + f->acc_len,
                      (unsigned int)(sizeof(f->acc) - f->acc_len), ms);
        if (n <= 0) return -1;
        f->acc_len += (unsigned int)n;
    }
    return 0;
}

/* =============================================================================
 * La risposta, che puo' essere di piu' righe
 *
 * ! IL SEGNO E' IL QUARTO CARATTERE, NON IL CODICE. «220-» vuol dire «continua»
 * e «220 » vuol dire «ho finito»: leggere solo la prima riga di un benvenuto di
 * tre lascia due righe nell'accumulatore, e la risposta DOPO comincia con la
 * coda di quella prima. Il sintomo e' un dialogo che va fuori sincrono a meta'
 * sessione, cioe' il guasto piu' difficile da leggere che ci sia.
 * ========================================================================== */
static int risposta(ExFtp *f, unsigned int ms)
{
    char r[EXFTP_RIGA_MAX];
    char primo[4];
    int  giri = 0;

    if (riga(f, r, sizeof(r), ms) != 0) return -1;

    if (f->verboso) printf("  < %s\n", r);

    if (strlen(r) < 4 || r[0] < '0' || r[0] > '9') { f->codice = 0; return -1; }

    memcpy(primo, r, 3);
    primo[3] = '\0';
    f->codice = atoi(primo);
    strncpy(f->risposta, r, sizeof(f->risposta) - 1);
    f->risposta[sizeof(f->risposta) - 1] = '\0';

    /* Una riga sola: il quarto carattere e' uno spazio. */
    while (r[3] == '-' && giri++ < 200) {
        if (riga(f, r, sizeof(r), ms) != 0) return -1;
        if (f->verboso) printf("  < %s\n", r);

        if (strlen(r) >= 4 && strncmp(r, primo, 3) == 0 && r[3] == ' ') {
            strncpy(f->risposta, r, sizeof(f->risposta) - 1);
            f->risposta[sizeof(f->risposta) - 1] = '\0';
            break;
        }
        /* Una riga di mezzo non ha per forza il codice davanti: si continua. */
        if (strlen(r) < 4) continue;
    }

    return f->codice;
}

/* Manda «verbo argomento» e legge la risposta. Rende il codice, o -1. */
static int comando(ExFtp *f, const char *verbo, const char *arg, unsigned int ms)
{
    char r[EXFTP_RIGA_MAX];
    int  n;

    if (f->ctrl <= 0) return -1;

    if (arg && arg[0]) n = snprintf(r, sizeof(r), "%s %s\r\n", verbo, arg);
    else               n = snprintf(r, sizeof(r), "%s\r\n", verbo);

    if (n <= 0) return -1;

    /* ! LA PASSWORD NON SI STAMPA NEMMENO IN MODO VERBOSO. Chi accende -v lo fa
     * per capire un guasto, non per farsi leggere la password da chi gli sta
     * dietro — o da un registro che finisce in un referto. */
    if (f->verboso)
        printf("  > %s\n", strcmp(verbo, "PASS") == 0 ? "PASS ***" : r);

    if (tcp_scrivi(f->ctrl, (const unsigned char *)r, (unsigned int)n) < 0)
        return -1;

    return risposta(f, ms);
}

static int buono(int codice)      { return codice >= 200 && codice < 300; }
static int intermedio(int codice) { return codice >= 100 && codice < 200; }

/* -----------------------------------------------------------------------------
 * Aprire, entrare, chiudere
 * --------------------------------------------------------------------------- */
int exftp_apri(ExFtp *f, const char *host, unsigned int porta,
               const char *utente, const char *password)
{
    if (f == NULL || host == NULL) return -EINVAL;

    memset(f, 0, sizeof(*f));
    f->passiva = 1;
    f->binario = 1;
    f->porta   = porta ? porta : 21;

    if (g_pid_ip <= 0) {
        g_pid_ip = rete_richiedi(IP_SERVIZIO);
        if (g_pid_ip <= 0) return -ENETDOWN;
    }

    if (dns_risolvi(host, f->ip) != 0) return -EHOSTUNREACH;

    f->ctrl = tcp_apri(f->ip, f->porta);
    if (f->ctrl <= 0) return f->ctrl < 0 ? f->ctrl : -ECONNREFUSED;

    if (!buono(risposta(f, 15000))) { exftp_chiudi(f); return -ECONNREFUSED; }

    /* ! L'ACCESSO E' IN DUE TEMPI, e il primo puo' bastare: a «USER» un server
     * che non vuole password risponde 230 invece di 331. Mandare «PASS» a quel
     * punto e' un comando fuori posto. */
    if (comando(f, "USER", utente ? utente : "anonymous", 15000) < 0) {
        exftp_chiudi(f);
        return -ECONNRESET;
    }

    if (f->codice == 331 || f->codice == 332) {
        if (comando(f, "PASS", password ? password : "", 15000) < 0) {
            exftp_chiudi(f);
            return -ECONNRESET;
        }
    }

    if (!buono(f->codice)) { exftp_chiudi(f); return -EACCES; }

    /* Binario di partenza: un file non e' testo, e il modo testo su un binario
     * lo rovina in silenzio traducendo i fine riga. */
    exftp_binario(f, 1);
    return 0;
}

void exftp_chiudi(ExFtp *f)
{
    if (f == NULL) return;
    if (f->ctrl > 0) {
        comando(f, "QUIT", 0, 2000);
        tcp_chiudi(f->ctrl);
    }
    f->ctrl = 0;
}

int exftp_binario(ExFtp *f, int si)
{
    int rc = comando(f, "TYPE", si ? "I" : "A", 8000);

    if (!buono(rc)) return -EIO;
    f->binario = si ? 1 : 0;
    return 0;
}

void exftp_passiva(ExFtp *f, int si) { if (f) f->passiva = si ? 1 : 0; }

/* -----------------------------------------------------------------------------
 * Il canale dei dati
 *
 * ! DUE STRADE, E SI SCELGONO PRIMA DEL COMANDO. In passiva e' il server a
 * dire dove chiamarlo, e ci si collega noi; in attiva siamo noi a dire dove
 * chiamare, e il server si collega a NOI — quindi bisogna stare in ascolto
 * PRIMA di mandare il comando, o la sua chiamata trova la porta chiusa.
 * --------------------------------------------------------------------------- */

/* PASV: dal «227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)» si tirano fuori
 * sei numeri. Non si cerca la parentesi: certi server scrivono la frase in un
 * altro modo, ma i numeri separati da virgola ci sono sempre. */
static int pasv_apri(ExFtp *f)
{
    const char   *p;
    unsigned int  tutti[12];
    unsigned char ip[4];
    unsigned int  porta;
    int           n = 0;

    if (!buono(comando(f, "PASV", 0, 10000))) return -EIO;

    /* ! SI PRENDONO GLI ULTIMI SEI NUMERI DELLA RIGA, e non si cerca la
     * parentesi. «227 Entering Passive Mode (91,204,4,7,195,80)» ha SETTE
     * numeri: il primo e' il codice della risposta. E la frase in mezzo la
     * scrive ogni server a modo suo — c'e' chi la traduce, chi aggiunge
     * l'indirizzo per esteso — mentre i sei numeri in coda, separati da
     * virgole, ci sono sempre e sono sempre gli ultimi. */
    for (p = f->risposta; *p && n < 12; p++) {
        if (*p < '0' || *p > '9') continue;
        tutti[n] = 0;
        while (*p >= '0' && *p <= '9') {
            tutti[n] = tutti[n] * 10 + (unsigned)(*p - '0');
            p++;
        }
        n++;
        if (*p == '\0') break;
    }
    if (n < 6) return -EIO;

    ip[0] = (unsigned char)tutti[n - 6];
    ip[1] = (unsigned char)tutti[n - 5];
    ip[2] = (unsigned char)tutti[n - 4];
    ip[3] = (unsigned char)tutti[n - 3];
    porta = tutti[n - 2] * 256u + tutti[n - 1];

    return tcp_apri(ip, porta);
}

/* PORT: si apre un ascolto e si dice al server dove chiamarci. Rende l'id
 * dell'ASCOLTATORE (non della connessione: quella arriva dopo il comando). */
static int port_ascolta(ExFtp *f, unsigned int *porta_usata)
{
    IpTcpAscolta a;
    unsigned char mio[4];
    char          arg[64];
    unsigned int  porta;
    int           asc = -1;

    if (mio_indirizzo(mio) != 0) return -ENETDOWN;

    /* ! LA PORTA SE LA SCEGLIE IL CLIENT, E PUO' ESSERE OCCUPATA. Si prova
     * qualche numero alto invece di insistere su uno solo: due trasferimenti
     * ravvicinati lascerebbero il primo in chiusura e il secondo senza porta. */
    for (porta = 40001; porta < 40011; porta++) {
        a.porta = porta;
        if (ipc_send(g_pid_ip, IP_MSG_TCP_ASCOLTA, &a, sizeof(a)) < 0) return -EIO;
        asc = esito(5000);
        if (asc >= 0) break;
    }
    if (asc < 0) return -EADDRINUSE;

    snprintf(arg, sizeof(arg), "%u,%u,%u,%u,%u,%u",
             mio[0], mio[1], mio[2], mio[3], porta / 256u, porta % 256u);

    if (!buono(comando(f, "PORT", arg, 10000))) {
        IpTcpRif r;
        r.id = (unsigned int)asc;
        ipc_send(g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
        esito(2000);
        return -EIO;
    }

    if (porta_usata) *porta_usata = porta;
    return asc;
}

/* Dopo il comando di trasferimento: accetta la chiamata del server. */
static int port_accetta(int asc)
{
    IpTcpAccetta ac;

    ac.id         = (unsigned int)asc;
    ac.timeout_ms = 15000;

    if (ipc_send(g_pid_ip, IP_MSG_TCP_ACCETTA, &ac, sizeof(ac)) < 0) return -EIO;
    return esito(20000);
}

/* =============================================================================
 * Un trasferimento, tutto intero
 *
 * Mette insieme le due strade: apre il canale dati, manda il comando, e rende
 * l'id della connessione da cui leggere o su cui scrivere. `asc` rende
 * l'ascoltatore da chiudere alla fine (0 in passiva).
 * ========================================================================== */
static int dati_apri(ExFtp *f, const char *verbo, const char *arg, int *asc)
{
    int dati;

    *asc = 0;

    if (f->passiva) {
        dati = pasv_apri(f);
        if (dati <= 0) return dati < 0 ? dati : -EIO;

        if (!intermedio(comando(f, verbo, arg, 15000)) && !buono(f->codice)) {
            tcp_chiudi(dati);
            return -EIO;
        }
        return dati;
    }

    {
        int a = port_ascolta(f, 0);

        if (a < 0) return a;

        /* ! IL COMANDO VA MANDATO CON L'ASCOLTO GIA' APERTO: e' il server a
         * chiamare, e chiama appena ha letto il comando. */
        if (!intermedio(comando(f, verbo, arg, 15000)) && !buono(f->codice)) {
            IpTcpRif r;
            r.id = (unsigned int)a;
            ipc_send(g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
            esito(2000);
            return -EIO;
        }

        dati = port_accetta(a);
        if (dati <= 0) {
            IpTcpRif r;
            r.id = (unsigned int)a;
            ipc_send(g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
            esito(2000);
            return -ETIMEDOUT;
        }
        *asc = a;
        return dati;
    }
}

/* Chiude il canale dati e legge il «226 Transfer complete» di chiusura. */
static int dati_chiudi(ExFtp *f, int dati, int asc)
{
    tcp_chiudi(dati);
    if (asc > 0) {
        IpTcpRif r;
        r.id = (unsigned int)asc;
        ipc_send(g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
        esito(2000);
    }
    /* ! LA RISPOSTA FINALE SI LEGGE SEMPRE, anche se non interessa: lasciarla
     * nel canale vuol dire che il comando DOPO legge questa e va fuori
     * sincrono per il resto della sessione. */
    return risposta(f, 15000);
}

/* -----------------------------------------------------------------------------
 * Directory e file
 * --------------------------------------------------------------------------- */
int exftp_cd(ExFtp *f, const char *dir)
{
    return buono(comando(f, "CWD", dir, 10000)) ? 0 : -ENOENT;
}

int exftp_mkd(ExFtp *f, const char *dir)
{
    int rc = comando(f, "MKD", dir, 10000);

    if (buono(rc)) return 0;
    /* 550 su MKD vuol dire quasi sempre «c'e' gia'», e per chi allinea una
     * directory non e' un guasto: e' il caso normale del secondo giro. */
    if (f->codice == 550) return -EEXIST;
    return -EIO;
}

int exftp_rmd(ExFtp *f, const char *dir)
{
    return buono(comando(f, "RMD", dir, 10000)) ? 0 : -EIO;
}

int exftp_dele(ExFtp *f, const char *file)
{
    return buono(comando(f, "DELE", file, 10000)) ? 0 : -EIO;
}

long exftp_size(ExFtp *f, const char *file)
{
    const char *p;

    if (!buono(comando(f, "SIZE", file, 10000))) return -ENOENT;

    for (p = f->risposta; *p && (*p < '0' || *p > '9'); p++) ;
    while (*p >= '0' && *p <= '9') p++;          /* il codice */
    while (*p == ' ') p++;

    return (long)strtoul(p, 0, 10);
}

/* «213 YYYYMMDDHHMMSS» -> time_t UTC. */
long exftp_mdtm(ExFtp *f, const char *file)
{
    const char *p;
    char        c[15];
    struct tm   t;
    int         i;

    if (!buono(comando(f, "MDTM", file, 10000))) return -ENOENT;

    p = f->risposta;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    for (i = 0; i < 14 && p[i] >= '0' && p[i] <= '9'; i++) c[i] = p[i];
    if (i < 14) return -EINVAL;
    c[14] = '\0';

    memset(&t, 0, sizeof(t));
    t.tm_year = (c[0]-'0')*1000 + (c[1]-'0')*100 + (c[2]-'0')*10 + (c[3]-'0') - 1900;
    t.tm_mon  = (c[4]-'0')*10 + (c[5]-'0') - 1;
    t.tm_mday = (c[6]-'0')*10 + (c[7]-'0');
    t.tm_hour = (c[8]-'0')*10 + (c[9]-'0');
    t.tm_min  = (c[10]-'0')*10 + (c[11]-'0');
    t.tm_sec  = (c[12]-'0')*10 + (c[13]-'0');

    return (long)mktime(&t);
}

/* MFMT: la data di un file remoto si puo' anche SCRIVERE, ed e' cio' che
 * rende stabile un allineamento. Senza, il file appena caricato ha la data
 * del server e al giro dopo sembra sempre piu' nuovo del locale. */
int exftp_mfmt(ExFtp *f, const char *file, long quando)
{
    char      arg[EXFTP_NOME_MAX + 32];
    struct tm t;
    time_t    q = (time_t)quando;

    if (gmtime_r(&q, &t) == 0) return -EINVAL;

    snprintf(arg, sizeof(arg), "%04d%02d%02d%02d%02d%02d %s",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, file);

    return buono(comando(f, "MFMT", arg, 10000)) ? 0 : -EIO;
}

/* =============================================================================
 * L'elenco
 *
 * ! MLSD PRIMA, LIST SOLO SE NON C'E'. MLSD rende righe fatte per le macchine:
 * «type=file;size=12;modify=20260915120000; nome». LIST rende quello che il
 * server ritiene leggibile da un umano, e cambia da server a server — su un
 * elenco in formato DOS, «02-14-26» e' una data e «<DIR>» e' un tipo, mentre
 * su uno Unix le colonne sono nove e la data non ha l'anno se e' recente.
 * Decidere che cosa scaricare guardando quella roba vuol dire sbagliare, presto
 * o tardi, sul file di qualcun altro.
 * ========================================================================== */
static void mlsd_una(const char *r, ExFtpVoce *v)
{
    const char *p = r, *sp;

    memset(v, 0, sizeof(*v));
    v->dimensione = -1;
    v->quando     = -1;

    /* I fatti stanno prima del primo spazio, separati da «;». */
    sp = r;
    while (*sp && *sp != ' ') sp++;

    while (p < sp) {
        const char *fine = p;

        while (fine < sp && *fine != ';') fine++;

        if (strncasecmp(p, "type=", 5) == 0) {
            if (strncasecmp(p + 5, "dir", 3) == 0) v->directory = 1;
            /* cdir e pdir sono «questa» e «quella sopra»: non sono voci. */
            if (strncasecmp(p + 5, "cdir", 4) == 0 ||
                strncasecmp(p + 5, "pdir", 4) == 0) v->directory = 2;
        } else if (strncasecmp(p, "size=", 5) == 0) {
            v->dimensione = (long)strtoul(p + 5, 0, 10);
        } else if (strncasecmp(p, "modify=", 7) == 0) {
            char      c[15];
            struct tm t;
            int       i;

            for (i = 0; i < 14 && p[7 + i] >= '0' && p[7 + i] <= '9'; i++)
                c[i] = p[7 + i];
            if (i == 14) {
                c[14] = '\0';
                memset(&t, 0, sizeof(t));
                t.tm_year = (c[0]-'0')*1000 + (c[1]-'0')*100 +
                            (c[2]-'0')*10 + (c[3]-'0') - 1900;
                t.tm_mon  = (c[4]-'0')*10 + (c[5]-'0') - 1;
                t.tm_mday = (c[6]-'0')*10 + (c[7]-'0');
                t.tm_hour = (c[8]-'0')*10 + (c[9]-'0');
                t.tm_min  = (c[10]-'0')*10 + (c[11]-'0');
                t.tm_sec  = (c[12]-'0')*10 + (c[13]-'0');
                v->quando = (long)mktime(&t);
            }
        }

        p = (fine < sp) ? fine + 1 : sp;
    }

    while (*sp == ' ') sp++;
    strncpy(v->nome, sp, sizeof(v->nome) - 1);
    v->nome[sizeof(v->nome) - 1] = '\0';
}

int exftp_elenco(ExFtp *f, const char *dir, ExFtpElenca g, void *dato)
{
    char          r[EXFTP_RIGA_MAX];
    unsigned char buf[IPC_MSG_MAX_DATA];
    char          acc[EXFTP_ACC_MAX];
    unsigned int  acc_len = 0;
    int           dati, asc, n, righe = 0;

    dati = dati_apri(f, "MLSD", dir ? dir : "", &asc);
    if (dati < 0) return dati;

    for (;;) {
        unsigned int i, inizio = 0;

        n = tcp_leggi(dati, buf, sizeof(buf), 20000);
        if (n <= 0) break;

        if (acc_len + (unsigned int)n > sizeof(acc)) {
            /* Una riga piu' lunga del buffer non e' una riga: si lascia
             * perdere invece di fingere di averla capita. */
            acc_len = 0;
            continue;
        }
        memcpy(acc + acc_len, buf, (unsigned int)n);
        acc_len += (unsigned int)n;

        for (i = 0; i < acc_len; i++) {
            ExFtpVoce v;
            unsigned int lung;

            if (acc[i] != '\n') continue;

            lung = i - inizio;
            if (lung > 0 && acc[i - 1] == '\r') lung--;
            if (lung > sizeof(r) - 1) lung = sizeof(r) - 1;

            memcpy(r, acc + inizio, lung);
            r[lung] = '\0';
            inizio = i + 1;

            if (r[0] == '\0') continue;

            mlsd_una(r, &v);
            if (v.directory == 2) continue;          /* cdir / pdir */
            if (v.nome[0] == '\0') continue;
            if (strcmp(v.nome, ".") == 0 || strcmp(v.nome, "..") == 0) continue;

            righe++;
            if (g && !g(dato, &v)) { inizio = acc_len; break; }
        }

        if (inizio > 0) {
            memmove(acc, acc + inizio, acc_len - inizio);
            acc_len -= inizio;
        }
    }

    dati_chiudi(f, dati, asc);

    /* ! UN ELENCO VUOTO NON E' UN ERRORE: una directory appena creata non ha
     * niente dentro, ed e' il caso normale del primo allineamento. */
    return righe;
}

/* -----------------------------------------------------------------------------
 * I trasferimenti
 * --------------------------------------------------------------------------- */
int exftp_scarica(ExFtp *f, const char *remoto, const char *locale)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           dati, asc, fd, n, guaio = 0;

    fd = open(locale, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -errno;

    dati = dati_apri(f, "RETR", remoto, &asc);
    if (dati < 0) { close(fd); remove(locale); return dati; }

    for (;;) {
        int scritti = 0;

        n = tcp_leggi(dati, buf, sizeof(buf), 30000);
        if (n < 0) { guaio = 1; break; }
        if (n == 0) break;

        while (scritti < n) {
            int k = (int)write(fd, buf + scritti, (unsigned int)(n - scritti));

            if (k <= 0) { guaio = 1; break; }
            scritti += k;
        }
        if (guaio) break;
    }

    close(fd);
    dati_chiudi(f, dati, asc);

    if (guaio) { remove(locale); return -EIO; }

    /* ! LA RISPOSTA FINALE DICE SE E' ANDATA. Un canale dati che si chiude
     * senza errori e un «451 Transfer aborted» sono la stessa cosa da qui
     * dentro, e solo il codice li distingue. */
    if (!buono(f->codice)) { remove(locale); return -EIO; }
    return 0;
}

int exftp_carica(ExFtp *f, const char *locale, const char *remoto)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           dati, asc, fd, n, guaio = 0;

    fd = open(locale, O_RDONLY);
    if (fd < 0) return -errno;

    dati = dati_apri(f, "STOR", remoto, &asc);
    if (dati < 0) { close(fd); return dati; }

    for (;;) {
        n = (int)read(fd, buf, sizeof(buf));
        if (n < 0) { guaio = 1; break; }
        if (n == 0) break;

        if (tcp_scrivi(dati, buf, (unsigned int)n) < 0) { guaio = 1; break; }
    }

    close(fd);
    dati_chiudi(f, dati, asc);

    if (guaio || !buono(f->codice)) return -EIO;
    return 0;
}
