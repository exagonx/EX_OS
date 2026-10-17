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
 * The shared FTP/FTPS client. Why it exists, and what it can and cannot do,
 * is in exftp.h.
 *
 * Rewritten on 17 October 2026 for FTPS: the control and data connections
 * are now ExHttpTrasporto streams, each with its OWN state, so TLS can sit on
 * either of them. (netspeed's first FTPS client opened both through
 * exhttp_tcp, whose state is one static: opening the data connection
 * silently turned the control transport into a second handle on the data
 * connection. A library that owns its connections cannot make that mistake.)
 * ============================================================================= */
#include "libc.h"
#include "ip_proto.h"
#include "rete.h"
#include "dns.h"
#include "exftp.h"

static int g_pid_ip = 0;

/* =============================================================================
 * The layer below: one TCP connection per Tcp, as an ExHttpTrasporto
 * ============================================================================= */

/* ! THE REST OF A DELIVERY IS KEPT. The stack hands over whole segments
 * (up to IP_TCP_DATI_MAX bytes); TLS asks for five bytes of record header
 * first. Without this the rest of the segment would be lost - the defect
 * exhttp.c describes above its own TcpStato. */
typedef struct {
    int           id;               /* the connection, on the stack's side */
    unsigned char avanzo[IPC_MSG_MAX_DATA];
    unsigned int  a_pos, a_fine;
} Tcp;

/* The stack's replies are ours; everything else stays in the mailbox for
 * whoever waits for it (window events, in a graphical program). Stale
 * replies of the stack are dropped: see the long note in exhttp.c above
 * filtro_stack - the protocol does not number its requests. */
static int filtro_stack(const IpcMessage *m, void *dato)
{
    unsigned int voluto = *(const unsigned int *)dato;

    if ((int)m->sender_pid != g_pid_ip) return IPC_ALTRUI;
    if (m->tipo == voluto)              return IPC_MIO;
    if (m->tipo == IP_MSG_TCP_DATI)     return IPC_ALTRUI;
    return IPC_BUTTA;
}

static int attendi(unsigned int tipo, unsigned char *buf, unsigned int *len,
                   unsigned int ms)
{
    IpcMessage meta;

    if (ipc_scegli(filtro_stack, &tipo, &meta, buf, IPC_MSG_MAX_DATA, ms) < 0)
        return -1;
    if (len) *len = meta.len;
    return 0;
}

static int esito(unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpEsito       e;

    if (attendi(IP_MSG_ESITO, buf, &len, ms) != 0 || len < sizeof(e)) return -ETIMEDOUT;
    memcpy(&e, buf, sizeof(e));
    return e.codice;
}

static int ip_pronto(void)
{
    if (g_pid_ip > 0) return 1;
    g_pid_ip = rete_richiedi(IP_SERVIZIO);
    return g_pid_ip > 0;
}

static void chiudi_id(int id)
{
    IpTcpRif r;

    if (id <= 0) return;
    r.id = (unsigned int)id;
    ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));
    (void)esito(2000);
}

static int t_leggi(void *st, unsigned char *dst, unsigned int max, unsigned int ms)
{
    Tcp          *s = (Tcp *)st;
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpDati     d;

    if (s->a_pos < s->a_fine) {
        unsigned int q = s->a_fine - s->a_pos;

        if (q > max) q = max;
        memcpy(dst, s->avanzo + s->a_pos, q);
        s->a_pos += q;
        return (int)q;
    }
    if (s->id <= 0) return 0;

    r.id = (unsigned int)s->id;
    if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r)) < 0) return -1;
    if (attendi(IP_MSG_TCP_DATI, buf, &len, ms) != 0) return -1;
    if (len < sizeof(d)) return -1;

    memcpy(&d, buf, sizeof(d));
    if (d.len == 0) return 0;
    if (d.len > len - sizeof(d)) return -1;

    if (d.len <= max) {
        memcpy(dst, buf + sizeof(d), d.len);
        return (int)d.len;
    }
    memcpy(dst, buf + sizeof(d), max);
    s->a_fine = d.len - max;
    s->a_pos  = 0;
    memcpy(s->avanzo, buf + sizeof(d) + max, s->a_fine);
    return (int)max;
}

static int t_quanti(void *st)
{
    Tcp          *s = (Tcp *)st;
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpTcpRif      r;
    IpTcpInfo     info;

    if (s->a_pos < s->a_fine) return (int)(s->a_fine - s->a_pos);
    if (s->id <= 0) return -1;

    r.id = (unsigned int)s->id;
    if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_STATO, &r, sizeof(r)) < 0) return -1;
    if (attendi(IP_MSG_TCP_INFO, buf, &len, 2000) != 0 || len < sizeof(info)) return -1;
    memcpy(&info, buf, sizeof(info));
    if (info.in_coda_rx > 0) return (int)info.in_coda_rx;
    if (info.stato != IP_TCP_APERTA) return -1;
    return 0;
}

static int t_scrivi(void *st, const unsigned char *src, unsigned int n)
{
    Tcp          *s = (Tcp *)st;
    unsigned char msg[sizeof(IpTcpDati) + IP_TCP_DATI_MAX];
    unsigned int  fatti = 0;
    int           fermi = 0;

    if (s->id <= 0) return -1;
    while (fatti < n) {
        unsigned int q = n - fatti;
        IpTcpDati    d;
        int          rc;

        if (q > IP_TCP_DATI_MAX) q = IP_TCP_DATI_MAX;
        d.id  = (unsigned int)s->id;
        d.len = q;
        memcpy(msg, &d, sizeof(d));
        memcpy(msg + sizeof(d), src + fatti, q);

        if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + q) < 0)
            return -1;
        rc = esito(8000);
        if (rc < 0) return fatti > 0 ? (int)fatti : -1;

        /* ! ZERO TAKEN MEANS "TRY AGAIN SOON": the send buffer is full. Give
         * up after 30 s without a single byte accepted - a server that stopped
         * reading cannot be told from a slow one except by time. */
        if (rc == 0) {
            if (++fermi > 600) return fatti > 0 ? (int)fatti : -1;
            usleep(50 * 1000);
            continue;
        }
        fermi = 0;
        fatti += (unsigned int)rc;
    }
    return (int)fatti;
}

static void t_chiudi(void *st)
{
    Tcp *s = (Tcp *)st;

    if (!s) return;
    chiudi_id(s->id);
    free(s);
}

/* Wraps a connection id the stack gave us. 1 on success; on failure the
 * connection is closed. */
static int t_avvolgi(ExHttpTrasporto *t, int id)
{
    Tcp *s = (Tcp *)calloc(1, sizeof(*s));

    if (!s) { chiudi_id(id); return 0; }
    s->id     = id;
    t->stato  = s;
    t->leggi  = t_leggi;
    t->scrivi = t_scrivi;
    t->chiudi = t_chiudi;
    t->quanti = t_quanti;
    return 1;
}

/* Opens a connection, retrying on -EAGAIN: the first time ARP may not have
 * the answer yet, and that is "not yet", not an error. Returns 0 or -errno. */
static int t_apri(ExHttpTrasporto *t, const unsigned char *ip, unsigned int porta)
{
    IpTcpApri a;
    int       id = -1, giri;

    memcpy(a.ip, ip, 4);
    a.porta      = porta;
    a.timeout_ms = 8000;

    for (giri = 0; giri < 10; giri++) {
        if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0) return -EIO;
        id = esito(10000);
        if (id != -EAGAIN) break;
        usleep(200 * 1000);
    }
    if (id <= 0) return id < 0 ? id : -ECONNREFUSED;
    return t_avvolgi(t, id) ? 0 : -ENOMEM;
}

/* Our own address, which PORT needs. */
static int mio_indirizzo(unsigned char ip[4])
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    IpStato       s;

    if (ipc_send((unsigned int)g_pid_ip, IP_MSG_STATO, 0, 0) < 0) return -1;
    if (attendi(IP_MSG_STATO_R, buf, &len, 3000) != 0 || len < sizeof(s)) return -1;
    memcpy(&s, buf, sizeof(s));
    memcpy(ip, s.cfg.ip, 4);
    return 0;
}

static int scrivi_tutto(ExHttpTrasporto *t, const void *dati, unsigned int n)
{
    const unsigned char *p = (const unsigned char *)dati;

    while (n > 0) {
        int r = t->scrivi(t->stato, p, n);

        if (r <= 0) return -EIO;
        p += r;
        n -= (unsigned int)r;
    }
    return 0;
}

/* =============================================================================
 * The log
 * ============================================================================= */
static void nota(ExFtp *f, int verso, const char *riga)
{
    if (f->registro) f->registro(f->registro_dato, verso, riga);
    if (f->verboso) printf("  %c %s\n", verso, riga);
}

static void errore(ExFtp *f, const char *fmt, const char *arg)
{
    snprintf(f->errore, sizeof(f->errore), fmt, arg ? arg : "");
    nota(f, '!', f->errore);
}

/* =============================================================================
 * The control channel: lines and three-digit replies
 * ============================================================================= */

/* One line from the accumulator: 1 if there was one, 0 if more must be read. */
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

        /* A line longer than the accumulator is dropped, not misread. */
        if (f->acc_len >= sizeof(f->acc) - 1) f->acc_len = 0;
        n = f->ctrl.leggi(f->ctrl.stato, (unsigned char *)f->acc + f->acc_len,
                          (unsigned int)(sizeof(f->acc) - f->acc_len), ms);
        if (n <= 0) return -1;
        f->acc_len += (unsigned int)n;
    }
    return 0;
}

/* =============================================================================
 * A reply, which may span many lines
 *
 * ! THE MARK IS THE FOURTH CHARACTER, NOT THE CODE. "220-" means "more
 * follows", "220 " means "done". Reading only the first line of a three-line
 * greeting leaves two lines in the accumulator, and the NEXT reply starts
 * with them: the dialogue goes out of step halfway through a session, the
 * hardest failure there is to read.
 * ========================================================================== */
static int risposta(ExFtp *f, unsigned int ms)
{
    char r[EXFTP_RIGA_MAX];
    char primo[4];
    int  giri = 0;

    if (!f->aperto) return -1;
    if (riga(f, r, sizeof(r), ms) != 0) { f->codice = 0; return -1; }
    nota(f, '<', r);

    if (strlen(r) < 4 || r[0] < '1' || r[0] > '5') {
        f->codice = 0;
        strncpy(f->risposta, r, sizeof(f->risposta) - 1);
        f->risposta[sizeof(f->risposta) - 1] = '\0';
        return -1;
    }
    memcpy(primo, r, 3);
    primo[3] = '\0';
    f->codice = atoi(primo);
    strncpy(f->risposta, r, sizeof(f->risposta) - 1);
    f->risposta[sizeof(f->risposta) - 1] = '\0';

    while (r[3] == '-' && giri++ < 500) {
        if (riga(f, r, sizeof(r), ms) != 0) return -1;
        nota(f, '<', r);
        /* A middle line need not carry the code: only "NNN " ends it. */
        if (strlen(r) >= 4 && strncmp(r, primo, 3) == 0 && r[3] == ' ') {
            strncpy(f->risposta, r, sizeof(f->risposta) - 1);
            f->risposta[sizeof(f->risposta) - 1] = '\0';
            break;
        }
    }
    return f->codice;
}

static int comando_ms(ExFtp *f, const char *verbo, const char *arg, unsigned int ms)
{
    char r[EXFTP_RIGA_MAX];
    int  n;

    if (!f->aperto) return -1;
    if (arg && arg[0]) n = snprintf(r, sizeof(r), "%s %s\r\n", verbo, arg);
    else               n = snprintf(r, sizeof(r), "%s\r\n", verbo);
    if (n <= 0 || n >= (int)sizeof(r)) return -1;

    /* ! THE PASSWORD IS NEVER LOGGED, not even with -v: whoever turns the log
     * on wants to understand a failure, not show the password to the person
     * behind them - or to a report sent somewhere. */
    if (strcasecmp(verbo, "PASS") == 0) {
        nota(f, '>', "PASS ****");
    } else {
        r[n - 2] = '\0';
        nota(f, '>', r);
        r[n - 2] = '\r';
    }

    n = scrivi_tutto(&f->ctrl, r, (unsigned int)n);
    memset(r, 0, sizeof(r));                    /* for PASS: the password */
    if (n < 0) return -1;
    return risposta(f, ms);
}

int exftp_comando(ExFtp *f, const char *verbo, const char *arg)
{
    if (!f || !verbo) return -1;
    return comando_ms(f, verbo, arg, 20000);
}

static int buono(int codice)      { return codice >= 200 && codice < 300; }
static int intermedio(int codice) { return codice >= 100 && codice < 200; }

/* =============================================================================
 * Opening, logging in, closing
 * ============================================================================= */
void exftp_prepara(ExFtp *f)
{
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->passiva = 1;
    f->binario = 1;
}

/* Puts TLS over the control connection. 0 or -errno. */
static int tls_controllo(ExFtp *f)
{
    ExHttpTrasporto tcp = f->ctrl;

    if (!f->tls(&f->ctrl, &tcp, f->host, f->errore, sizeof(f->errore))) {
        char e[EXFTP_ERRORE_MAX];

        snprintf(e, sizeof(e), "TLS: %s", f->errore);
        strncpy(f->errore, e, sizeof(f->errore) - 1);
        nota(f, '!', f->errore);
        f->ctrl = tcp;                         /* still the plain one */
        return -EPROTO;
    }
    f->cifrato = 1;
    nota(f, '!', "connessione dei comandi cifrata (TLS 1.3)");
    return 0;
}

int exftp_connetti(ExFtp *f, const char *host, unsigned int porta, int modo)
{
    int rc;

    if (!f || !host || !host[0]) return -EINVAL;
    if (f->aperto) exftp_chiudi(f);

    f->cifrato = f->prot_p = f->dentro = f->no_epsv = 0;
    f->acc_len = 0;
    f->codice  = 0;
    f->risposta[0] = f->errore[0] = '\0';
    f->porta = porta ? porta : (modo == EXFTP_TLS_IMPLICITO ? 990u : 21u);
    strncpy(f->host, host, sizeof(f->host) - 1);
    f->host[sizeof(f->host) - 1] = '\0';

    if (modo != EXFTP_CHIARO && !f->tls) {
        errore(f, "FTPS non disponibile in questo programma%s", 0);
        return -EPROTONOSUPPORT;
    }
    if (!ip_pronto()) { errore(f, "la rete non c'e' (ip.drv non risponde)%s", 0); return -ENETDOWN; }
    if (dns_risolvi(host, f->ip) != 0) { errore(f, "il nome %s non si risolve", host); return -EHOSTUNREACH; }

    {
        char e[64];

        snprintf(e, sizeof(e), "connessione a %u.%u.%u.%u:%u",
                 f->ip[0], f->ip[1], f->ip[2], f->ip[3], f->porta);
        nota(f, '!', e);
    }
    rc = t_apri(&f->ctrl, f->ip, f->porta);
    if (rc != 0) { errore(f, "connessione rifiutata o senza risposta%s", 0); return rc; }
    f->aperto = 1;

    if (modo == EXFTP_TLS_IMPLICITO && (rc = tls_controllo(f)) != 0) {
        exftp_chiudi(f);
        return rc;
    }
    if (!buono(risposta(f, 20000))) {
        if (!f->errore[0]) errore(f, "il server non si presenta come FTP%s", 0);
        exftp_chiudi(f);
        return -ECONNREFUSED;
    }
    if (modo == EXFTP_TLS) {
        if (comando_ms(f, "AUTH", "TLS", 15000) != 234) {
            errore(f, "il server non accetta FTPS (AUTH TLS)%s", 0);
            exftp_chiudi(f);
            return -EPROTONOSUPPORT;
        }
        if ((rc = tls_controllo(f)) != 0) {
            /* ! THE SERVER IS HALFWAY INTO A HANDSHAKE: the connection cannot
             * go back to plain text. Closed, not QUIT. */
            f->ctrl.chiudi(f->ctrl.stato);
            f->aperto = 0;
            return rc;
        }
    }
    return 0;
}

int exftp_accedi(ExFtp *f, const char *utente, const char *password)
{
    if (!f || !f->aperto) return -ENOTCONN;

    /* ! LOGIN IS TWO STEPS, and the first may be enough: a server that wants
     * no password answers USER with 230 instead of 331. */
    if (comando_ms(f, "USER", (utente && utente[0]) ? utente : "anonymous", 15000) < 0)
        return -ECONNRESET;
    if (f->codice == 331 || f->codice == 332) {
        if (comando_ms(f, "PASS", password ? password : "", 20000) < 0)
            return -ECONNRESET;
    }
    if (!buono(f->codice)) return -EACCES;
    f->dentro = 1;

    if (f->cifrato) {
        comando_ms(f, "PBSZ", "0", 10000);
        if (comando_ms(f, "PROT", "P", 10000) == 200) {
            f->prot_p = 1;
        } else {
            /* Rare, and worth saying: the data would travel in clear. */
            nota(f, '!', "il server rifiuta PROT P: i dati viaggeranno in chiaro");
        }
    }

    /* Binary to start with: a file is not text, and text mode silently
     * damages a binary file by translating its line ends. */
    exftp_binario(f, 1);
    return 0;
}

int exftp_apri(ExFtp *f, const char *host, unsigned int porta,
               const char *utente, const char *password)
{
    int            verboso, rc;
    ExFtpRegistro  reg;
    void          *reg_d;
    ExFtpTls       tls;

    if (!f || !host) return -EINVAL;
    /* The hooks and switches the caller set survive (until 17/10/2026 the
     * memset here also wiped ftpswap's -v). */
    verboso = f->verboso; reg = f->registro; reg_d = f->registro_dato; tls = f->tls;
    exftp_prepara(f);
    f->verboso = verboso; f->registro = reg; f->registro_dato = reg_d; f->tls = tls;

    rc = exftp_connetti(f, host, porta, EXFTP_CHIARO);
    if (rc != 0) return rc;
    rc = exftp_accedi(f, utente, password);
    if (rc != 0) exftp_chiudi(f);
    return rc;
}

void exftp_chiudi(ExFtp *f)
{
    if (!f || !f->aperto) return;
    comando_ms(f, "QUIT", 0, 3000);
    f->ctrl.chiudi(f->ctrl.stato);
    f->aperto = f->dentro = f->cifrato = f->prot_p = 0;
    f->acc_len = 0;
}

int exftp_binario(ExFtp *f, int si)
{
    if (!buono(comando_ms(f, "TYPE", si ? "I" : "A", 10000))) return -EIO;
    f->binario = si ? 1 : 0;
    return 0;
}

void exftp_passiva(ExFtp *f, int si) { if (f) f->passiva = si ? 1 : 0; }

int exftp_protezione(ExFtp *f, int privata)
{
    if (!f || !f->cifrato) return -EPROTONOSUPPORT;
    if (comando_ms(f, "PROT", privata ? "P" : "C", 10000) != 200) return -EIO;
    f->prot_p = privata ? 1 : 0;
    return 0;
}

/* =============================================================================
 * The data channel
 *
 * ! TWO ROADS, CHOSEN BEFORE THE COMMAND. Passive: the server says where to
 * call and we connect. Active: we say where to call and the server connects
 * to US - so the listener must be open BEFORE the command goes out.
 * ========================================================================== */

/* The port in "229 Entering Extended Passive Mode (|||6446|)". */
static int porta_epsv(const char *r)
{
    const char  *p = strchr(r, '(');
    unsigned int v = 0;

    if (!p) return -1;
    p++;
    if (p[0] == '\0' || p[1] != p[0] || p[2] != p[0]) return -1;
    p += 3;
    while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned int)(*p++ - '0');
    return (v > 0 && v < 65536) ? (int)v : -1;
}

/* The port of a 227 reply: the LAST six numbers on the line. "227 Entering
 * Passive Mode (91,204,4,7,195,80)" has seven - the first is the code - and
 * every server words the sentence its own way. */
static int porta_pasv(const char *r, unsigned char ip[4])
{
    unsigned int tutti[16];
    int          n = 0;

    for (; *r && n < 16; r++) {
        if (*r < '0' || *r > '9') continue;
        tutti[n] = 0;
        while (*r >= '0' && *r <= '9') tutti[n] = tutti[n] * 10 + (unsigned int)(*r++ - '0');
        n++;
        if (*r == '\0') break;
    }
    if (n < 7) return -1;
    ip[0] = (unsigned char)tutti[n - 6];
    ip[1] = (unsigned char)tutti[n - 5];
    ip[2] = (unsigned char)tutti[n - 4];
    ip[3] = (unsigned char)tutti[n - 3];
    return (int)(tutti[n - 2] * 256u + tutti[n - 1]);
}

static int passiva_apri(ExFtp *f, ExHttpTrasporto *d)
{
    unsigned char ip[4];
    int           porta = -1;
    char          e[96];

    if (!f->no_epsv) {
        int c = comando_ms(f, "EPSV", 0, 10000);

        if (c == 229) porta = porta_epsv(f->risposta);
        else if (c >= 500) f->no_epsv = 1;      /* not known: PASV from now on */
        else if (c < 0) return -EIO;
    }
    if (porta < 0) {
        if (comando_ms(f, "PASV", 0, 10000) != 227) return -EIO;
        porta = porta_pasv(f->risposta, ip);
        if (porta <= 0) return -EIO;
        if (memcmp(ip, f->ip, 4) != 0) {
            snprintf(e, sizeof(e), "il server annuncia %u.%u.%u.%u: uso il suo indirizzo vero",
                     ip[0], ip[1], ip[2], ip[3]);
            nota(f, '!', e);
        }
    }
    return t_apri(d, f->ip, (unsigned int)porta);
}

/* PORT: a listener, and the server told where to call. Returns the
 * LISTENER's id (the connection itself comes after the command). */
static int port_ascolta(ExFtp *f)
{
    IpTcpAscolta  a;
    unsigned char mio[4];
    char          arg[64];
    unsigned int  porta;
    int           asc = -1;

    if (mio_indirizzo(mio) != 0) return -ENETDOWN;

    /* ! THE CLIENT PICKS THE PORT, AND IT MAY BE BUSY: two transfers close
     * together leave the first one closing. A few high numbers are tried. */
    for (porta = 40001; porta < 40021; porta++) {
        a.porta = porta;
        if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_ASCOLTA, &a, sizeof(a)) < 0) return -EIO;
        asc = esito(5000);
        if (asc > 0) break;
    }
    if (asc <= 0) return -EADDRINUSE;

    snprintf(arg, sizeof(arg), "%u,%u,%u,%u,%u,%u",
             mio[0], mio[1], mio[2], mio[3], porta / 256u, porta % 256u);
    if (!buono(comando_ms(f, "PORT", arg, 10000))) {
        chiudi_id(asc);
        return -EIO;
    }
    return asc;
}

static int port_accetta(int asc)
{
    IpTcpAccetta ac;

    ac.id         = (unsigned int)asc;
    ac.timeout_ms = 15000;
    if (ipc_send((unsigned int)g_pid_ip, IP_MSG_TCP_ACCETTA, &ac, sizeof(ac)) < 0) return -EIO;
    return esito(20000);
}

/* =============================================================================
 * One transfer's data connection, whole: opens the channel, sends REST if
 * asked and the command, starts TLS when PROT P is on. 0 and `d` filled, or
 * -errno with the reason in risposta/errore.
 * ========================================================================== */
static int dati_apri(ExFtp *f, const char *verbo, const char *arg, ExHttpTrasporto *d)
{
    ExHttpTrasporto tcp;
    int             rc, asc = 0;

    if (!f->aperto) return -ENOTCONN;

    if (f->passiva) {
        rc = passiva_apri(f, &tcp);
        if (rc != 0) { f->riparti = 0; return rc; }
    } else {
        asc = port_ascolta(f);
        if (asc < 0) { f->riparti = 0; return asc; }
    }

    if (f->riparti > 0) {
        char n[24];

        snprintf(n, sizeof(n), "%ld", f->riparti);
        f->riparti = 0;
        if (comando_ms(f, "REST", n, 10000) != 350) {
            if (f->passiva) tcp.chiudi(tcp.stato); else chiudi_id(asc);
            return -ENOTSUP;
        }
    }

    rc = comando_ms(f, verbo, arg, 20000);
    if (!intermedio(rc) && !buono(rc)) {
        if (f->passiva) tcp.chiudi(tcp.stato); else chiudi_id(asc);
        return rc < 0 ? -ECONNRESET : (f->codice == 550 ? -ENOENT : -EIO);
    }

    if (!f->passiva) {
        int id = port_accetta(asc);

        chiudi_id(asc);                        /* one call is all we take */
        if (id <= 0) { risposta(f, 3000); return -ETIMEDOUT; }
        if (!t_avvolgi(&tcp, id)) return -ENOMEM;
    }

    if (f->prot_p) {
        if (!f->tls(d, &tcp, f->host, f->errore, sizeof(f->errore))) {
            char e[EXFTP_ERRORE_MAX];

            snprintf(e, sizeof(e), "TLS sui dati: %s", f->errore);
            strncpy(f->errore, e, sizeof(f->errore) - 1);
            nota(f, '!', f->errore);
            tcp.chiudi(tcp.stato);
            risposta(f, 10000);                /* the server's word on it */
            return -EPROTO;
        }
    } else {
        *d = tcp;
    }
    return 0;
}

/* Closes the data channel and reads the closing "226 Transfer complete".
 * ! THE FINAL REPLY IS ALWAYS READ, even when nobody cares: left in the
 * channel, the NEXT command would read it and the session would stay out of
 * step to its end. */
static int dati_chiudi(ExFtp *f, ExHttpTrasporto *d)
{
    d->chiudi(d->stato);
    return risposta(f, 30000);
}

/* A transfer stopped halfway: data connection closed first, then ABOR, and
 * every reply that comes (426 then 226, or one 225/226) is read, so the next
 * command does not find them in its way. */
static void abbandona(ExFtp *f, ExHttpTrasporto *d)
{
    int giri;

    d->chiudi(d->stato);
    if (comando_ms(f, "ABOR", 0, 5000) < 0) return;
    for (giri = 0; giri < 3; giri++)
        if (risposta(f, 1500) < 0) break;
}

/* =============================================================================
 * Directories and files
 * ============================================================================= */
int exftp_pwd(ExFtp *f, char *dst, unsigned int max)
{
    const char  *p;
    unsigned int n = 0;

    if (!dst || max == 0) return -EINVAL;
    dst[0] = '\0';
    if (comando_ms(f, "PWD", 0, 10000) != 257) return -EIO;

    /* 257 "/the/dir" is current - a quote inside the name is doubled. */
    p = strchr(f->risposta, '"');
    if (!p) return -EIO;
    for (p++; *p && n + 1 < max; p++) {
        if (*p == '"') {
            if (p[1] != '"') break;
            p++;
        }
        dst[n++] = *p;
    }
    dst[n] = '\0';
    return 0;
}

int exftp_cd(ExFtp *f, const char *dir)
{
    return buono(comando_ms(f, "CWD", dir, 10000)) ? 0 : -ENOENT;
}

int exftp_cdup(ExFtp *f)
{
    return buono(comando_ms(f, "CDUP", 0, 10000)) ? 0 : -ENOENT;
}

int exftp_mkd(ExFtp *f, const char *dir)
{
    int rc = comando_ms(f, "MKD", dir, 10000);

    if (buono(rc)) return 0;
    /* 550 on MKD almost always means "already there", and for whoever keeps
     * a directory in step that is the normal case of the second round. */
    if (f->codice == 550) return -EEXIST;
    return -EIO;
}

int exftp_rmd(ExFtp *f, const char *dir)
{
    return buono(comando_ms(f, "RMD", dir, 10000)) ? 0 : -EIO;
}

int exftp_dele(ExFtp *f, const char *file)
{
    return buono(comando_ms(f, "DELE", file, 10000)) ? 0 : -EIO;
}

int exftp_rinomina(ExFtp *f, const char *da, const char *a)
{
    if (comando_ms(f, "RNFR", da, 10000) != 350) return -ENOENT;
    return buono(comando_ms(f, "RNTO", a, 10000)) ? 0 : -EIO;
}

long exftp_size(ExFtp *f, const char *file)
{
    const char *p;

    if (!buono(comando_ms(f, "SIZE", file, 10000))) return -ENOENT;
    p = f->risposta + 3;
    while (*p == ' ') p++;
    return (long)strtoul(p, 0, 10);
}

/* YYYYMMDDHHMMSS -> time_t UTC, or -1. */
static long data_ftp(const char *p)
{
    struct tm t;
    char      c[14];
    int       i;

    for (i = 0; i < 14; i++) {
        if (p[i] < '0' || p[i] > '9') return -1;
        c[i] = (char)(p[i] - '0');
    }
    memset(&t, 0, sizeof(t));
    t.tm_year = c[0] * 1000 + c[1] * 100 + c[2] * 10 + c[3] - 1900;
    t.tm_mon  = c[4] * 10 + c[5] - 1;
    t.tm_mday = c[6] * 10 + c[7];
    t.tm_hour = c[8] * 10 + c[9];
    t.tm_min  = c[10] * 10 + c[11];
    t.tm_sec  = c[12] * 10 + c[13];
    return (long)mktime(&t);
}

long exftp_mdtm(ExFtp *f, const char *file)
{
    const char *p;
    long        q;

    if (!buono(comando_ms(f, "MDTM", file, 10000))) return -ENOENT;
    p = f->risposta + 3;
    while (*p == ' ') p++;
    q = data_ftp(p);
    return q < 0 ? -EINVAL : q;
}

/* MFMT: a remote file's date can be WRITTEN too, and that is what makes
 * keeping a directory in step stable: without it the file just uploaded has
 * the server's date and looks newer than the local one for ever. */
int exftp_mfmt(ExFtp *f, const char *file, long quando)
{
    char      arg[EXFTP_NOME_MAX + 32];
    struct tm t;
    time_t    q = (time_t)quando;

    if (gmtime_r(&q, &t) == 0) return -EINVAL;
    snprintf(arg, sizeof(arg), "%04d%02d%02d%02d%02d%02d %s",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, file);
    return buono(comando_ms(f, "MFMT", arg, 10000)) ? 0 : -EIO;
}

/* =============================================================================
 * Lines from a data connection
 * ============================================================================= */
static int righe_dati(ExFtp *f, ExHttpTrasporto *d, ExFtpRiga g, void *dato)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    char          acc[EXFTP_ACC_MAX];
    unsigned int  acc_len = 0;
    int           righe = 0, basta = 0;

    (void)f;
    for (;;) {
        unsigned int i, inizio = 0;
        int          n = d->leggi(d->stato, buf, sizeof(buf), 30000);

        if (n <= 0) break;
        if (basta) continue;                   /* drained, not used */
        if (acc_len + (unsigned int)n > sizeof(acc)) {
            acc_len = 0;                       /* a line too long: dropped */
            continue;
        }
        memcpy(acc + acc_len, buf, (unsigned int)n);
        acc_len += (unsigned int)n;

        for (i = 0; i < acc_len; i++) {
            unsigned int l;

            if (acc[i] != '\n') continue;
            l = i;
            if (l > inizio && acc[l - 1] == '\r') l--;
            acc[l] = '\0';
            if (acc[inizio]) {
                righe++;
                if (g && !g(dato, acc + inizio)) { basta = 1; inizio = acc_len; break; }
            }
            inizio = i + 1;
        }
        if (inizio > 0) {
            memmove(acc, acc + inizio, acc_len - inizio);
            acc_len -= inizio;
        }
    }
    if (!basta && acc_len > 0 && acc_len < sizeof(acc)) {   /* no final newline */
        acc[acc_len] = '\0';
        righe++;
        if (g) g(dato, acc);
    }
    return righe;
}

int exftp_righe(ExFtp *f, const char *verbo, const char *arg, ExFtpRiga g, void *dato)
{
    ExHttpTrasporto d;
    int             rc, righe;

    rc = dati_apri(f, verbo, arg, &d);
    if (rc != 0) return rc;
    righe = righe_dati(f, &d, g, dato);
    rc = dati_chiudi(f, &d);
    if (rc < 0) return -ECONNRESET;
    if (!buono(rc)) return -EIO;
    return righe;
}

/* =============================================================================
 * The listing
 *
 * ! MLSD FIRST, LIST ONLY IF IT IS MISSING. MLSD's lines are made for
 * programs: "type=file;size=12;modify=20260915120000; name". LIST gives what
 * the server thinks a human likes, and it changes from server to server.
 * ========================================================================== */
static void mlsd_una(const char *r, ExFtpVoce *v)
{
    const char *p = r, *sp;

    memset(v, 0, sizeof(*v));
    v->dimensione = -1;
    v->quando     = -1;

    /* The facts come before the first space, separated by ';'. */
    sp = r;
    while (*sp && *sp != ' ') sp++;

    while (p < sp) {
        const char *fine = p;

        while (fine < sp && *fine != ';') fine++;
        if (strncasecmp(p, "type=", 5) == 0) {
            if (strncasecmp(p + 5, "dir", 3) == 0) v->directory = 1;
            /* cdir and pdir are "this one" and "the one above": not entries. */
            if (strncasecmp(p + 5, "cdir", 4) == 0 ||
                strncasecmp(p + 5, "pdir", 4) == 0) v->directory = 2;
        } else if (strncasecmp(p, "size=", 5) == 0) {
            v->dimensione = (long)strtoul(p + 5, 0, 10);
        } else if (strncasecmp(p, "modify=", 7) == 0) {
            v->quando = data_ftp(p + 7);
        }
        p = (fine < sp) ? fine + 1 : sp;
    }
    if (*sp == ' ') sp++;                      /* exactly one space: names may start with one */
    strncpy(v->nome, sp, sizeof(v->nome) - 1);
    v->nome[sizeof(v->nome) - 1] = '\0';
}

/* The n-th blank-separated field of r (from 0); the pointer to it, or 0. */
static const char *campo(const char *r, int n)
{
    while (*r == ' ') r++;
    while (n-- > 0) {
        while (*r && *r != ' ') r++;
        while (*r == ' ') r++;
        if (!*r) return 0;
    }
    return *r ? r : 0;
}

static int mese(const char *m)
{
    static const char nomi[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;

    for (i = 0; i < 12; i++)
        if (strncasecmp(m, nomi + i * 3, 3) == 0) return i;
    return -1;
}

/* One LIST line, Unix ("drwxr-xr-x 2 u g 4096 Oct 17 10:22 name") or DOS
 * ("10-17-26  10:22AM  <DIR>  name"). 0 if it is neither. */
static int list_una(const char *r, ExFtpVoce *v)
{
    const char *p;

    memset(v, 0, sizeof(*v));
    v->dimensione = -1;
    v->quando     = -1;

    if (strchr("-dlbcps", r[0]) && strlen(r) > 10 &&
        strchr("r-", r[1]) && strchr("w-", r[2])) {
        const char *dim = campo(r, 4), *m = campo(r, 5), *g = campo(r, 6), *ao = campo(r, 7);
        struct tm   t;
        int         me;

        if (!dim || !m || !g || !ao) return 0;
        p = campo(r, 8);
        if (!p) return 0;
        v->directory  = r[0] == 'd';
        v->dimensione = (long)strtoul(dim, 0, 10);
        me = mese(m);
        if (me >= 0) {
            memset(&t, 0, sizeof(t));
            t.tm_mon  = me;
            t.tm_mday = atoi(g);
            const char *dp = strchr(ao, ':'), *sp = strchr(ao, ' ');

            if (dp && (!sp || dp < sp)) {
                time_t    ora = time(0);
                struct tm o;

                gmtime_r(&ora, &o);
                t.tm_year = o.tm_year;
                t.tm_hour = atoi(ao);
                t.tm_min  = atoi(strchr(ao, ':') + 1);
                /* No year means "within the last six months". */
                if (t.tm_mon > o.tm_mon + 1) t.tm_year--;
            } else {
                t.tm_year = atoi(ao) - 1900;
            }
            v->quando = (long)mktime(&t);
        }
        /* A link: "name -> target"; the name is what is listed. */
        strncpy(v->nome, p, sizeof(v->nome) - 1);
        v->nome[sizeof(v->nome) - 1] = '\0';
        if (r[0] == 'l') {
            char *fr = strstr(v->nome, " -> ");

            if (fr) *fr = '\0';
        }
        return v->nome[0] != '\0';
    }

    if (r[0] >= '0' && r[0] <= '9' && r[2] == '-' && r[5] == '-') {
        const char *tipo = campo(r, 2);

        p = campo(r, 3);
        if (!tipo || !p) return 0;
        if (strncmp(tipo, "<DIR>", 5) == 0) v->directory = 1;
        else v->dimensione = (long)strtoul(tipo, 0, 10);
        strncpy(v->nome, p, sizeof(v->nome) - 1);
        v->nome[sizeof(v->nome) - 1] = '\0';
        return 1;
    }
    return 0;
}

typedef struct {
    ExFtpElenca g;
    void       *dato;
    int         mlsd;
    int         voci;
    int         fermo;
} Elenco;

static int elenco_riga(void *dato, const char *r)
{
    Elenco   *e = (Elenco *)dato;
    ExFtpVoce v;

    if (e->mlsd) {
        mlsd_una(r, &v);
        if (v.directory == 2) return 1;
    } else if (!list_una(r, &v)) {
        return 1;                              /* "total 12" and the like */
    }
    if (v.nome[0] == '\0' || strcmp(v.nome, ".") == 0 || strcmp(v.nome, "..") == 0) return 1;
    e->voci++;
    if (e->g && !e->g(e->dato, &v)) { e->fermo = 1; return 0; }
    return 1;
}

int exftp_elenco(ExFtp *f, const char *dir, ExFtpElenca g, void *dato)
{
    Elenco e;
    int    rc;

    memset(&e, 0, sizeof(e));
    e.g = g; e.dato = dato; e.mlsd = 1;

    rc = exftp_righe(f, "MLSD", dir ? dir : "", elenco_riga, &e);
    /* 500/502: MLSD unknown; LIST is in every server since 1985. */
    if (rc < 0 && rc != -ENOENT && f->codice >= 500 && f->codice != 550) {
        memset(&e, 0, sizeof(e));
        e.g = g; e.dato = dato; e.mlsd = 0;
        rc = exftp_righe(f, "LIST", dir ? dir : "", elenco_riga, &e);
    }
    /* ! AN EMPTY LISTING IS NOT AN ERROR: a directory just made has
     * nothing in it, the normal case of a first round. */
    return rc < 0 ? rc : e.voci;
}

/* =============================================================================
 * Transfers
 *
 * ! TEXT MODE CONVERTS LINE ENDS: CR LF on the wire (RFC 959), LF here. A CR
 * at the end of a piece is held back until the next one says whether an LF
 * follows it.
 * ========================================================================== */
static int scrivi_fd(int fd, const unsigned char *p, unsigned int n)
{
    while (n > 0) {
        int k = (int)write(fd, p, n);

        if (k <= 0) return -1;
        p += k;
        n -= (unsigned int)k;
    }
    return 0;
}

int exftp_scarica(ExFtp *f, const char *remoto, const char *locale)
{
    unsigned char   buf[IPC_MSG_MAX_DATA], testo[IPC_MSG_MAX_DATA + 1];
    ExHttpTrasporto d;
    long            totale = -1, fatti = 0, da = f ? f->riparti : 0;
    int             fd, n, rc, guaio = 0, cr = 0;

    if (!f || !remoto || !locale) return -EINVAL;
    if (!f->aperto) return -ENOTCONN;

    if (f->avanza && f->binario) totale = exftp_size(f, remoto);
    f->riparti = da;

    fd = open(locale, da > 0 ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC));
    if (fd < 0) { f->riparti = 0; return -errno; }

    rc = dati_apri(f, "RETR", remoto, &d);
    if (rc != 0) {
        close(fd);
        if (da == 0) remove(locale);
        return rc;
    }
    fatti = da;

    for (;;) {
        n = d.leggi(d.stato, buf, sizeof(buf), 60000);
        if (n < 0) { guaio = -EIO; break; }
        if (n == 0) break;
        if (f->binario) {
            if (scrivi_fd(fd, buf, (unsigned int)n) != 0) { guaio = -EIO; break; }
        } else {
            unsigned int i, k = 0;

            for (i = 0; i < (unsigned int)n; i++) {
                if (cr) { cr = 0; if (buf[i] != '\n') testo[k++] = '\r'; }
                if (buf[i] == '\r') { cr = 1; continue; }
                testo[k++] = buf[i];
            }
            if (scrivi_fd(fd, testo, k) != 0) { guaio = -EIO; break; }
        }
        fatti += n;
        if (f->avanza && !f->avanza(f->avanza_dato, fatti, totale)) { guaio = -ECANCELED; break; }
    }
    if (cr && !guaio) scrivi_fd(fd, (const unsigned char *)"\r", 1);
    close(fd);

    if (guaio == -ECANCELED) {
        abbandona(f, &d);
        if (da == 0) remove(locale);
        return guaio;
    }
    rc = dati_chiudi(f, &d);
    if (guaio) { if (da == 0) remove(locale); return guaio; }

    /* ! THE FINAL REPLY SAYS WHETHER IT WORKED. A data connection that
     * closes cleanly and a "451 Transfer aborted" look the same from here;
     * only the code tells them apart. */
    if (!buono(rc)) { if (da == 0) remove(locale); return -EIO; }
    return 0;
}

/* Sends from a reader: a file or memory. */
typedef struct {
    int                  fd;
    const unsigned char *mem;
    unsigned long        n, pos;
} Fonte;

static int fonte_leggi(Fonte *s, unsigned char *dst, unsigned int max)
{
    if (s->mem) {
        unsigned long q = s->n - s->pos;

        if (q > max) q = max;
        memcpy(dst, s->mem + s->pos, (unsigned int)q);
        s->pos += q;
        return (int)q;
    }
    return (int)read(s->fd, dst, max);
}

static int carica_da(ExFtp *f, Fonte *s, long totale, const char *remoto, int accoda)
{
    unsigned char   buf[1024], testo[2048];
    ExHttpTrasporto d;
    long            fatti = f->riparti;
    int             n, rc, guaio = 0;

    rc = dati_apri(f, (accoda || f->riparti > 0) ? "APPE" : "STOR", remoto, &d);
    if (rc != 0) return rc;

    for (;;) {
        n = fonte_leggi(s, buf, sizeof(buf));
        if (n < 0) { guaio = -EIO; break; }
        if (n == 0) break;
        if (f->binario) {
            rc = scrivi_tutto(&d, buf, (unsigned int)n);
        } else {
            unsigned int i, k = 0;

            for (i = 0; i < (unsigned int)n; i++) {
                if (buf[i] == '\n' && (i == 0 || buf[i - 1] != '\r')) testo[k++] = '\r';
                testo[k++] = buf[i];
            }
            rc = scrivi_tutto(&d, testo, k);
        }
        if (rc != 0) { guaio = -EIO; break; }
        fatti += n;
        if (f->avanza && !f->avanza(f->avanza_dato, fatti, totale)) { guaio = -ECANCELED; break; }
    }
    if (guaio == -ECANCELED) { abbandona(f, &d); return guaio; }
    rc = dati_chiudi(f, &d);
    if (guaio) return guaio;
    if (!buono(rc)) return -EIO;
    return 0;
}

static int carica_file(ExFtp *f, const char *locale, const char *remoto, int accoda)
{
    Fonte s;
    long  totale;
    int   rc;

    if (!f || !locale || !remoto) return -EINVAL;
    if (!f->aperto) return -ENOTCONN;

    memset(&s, 0, sizeof(s));
    s.fd = open(locale, O_RDONLY);
    if (s.fd < 0) { f->riparti = 0; return -errno; }
    totale = (long)lseek(s.fd, 0, SEEK_END);
    if (f->riparti > 0 && f->riparti <= totale) lseek(s.fd, f->riparti, SEEK_SET);
    else { lseek(s.fd, 0, SEEK_SET); f->riparti = 0; }

    rc = carica_da(f, &s, totale, remoto, accoda);
    close(s.fd);
    return rc;
}

int exftp_carica(ExFtp *f, const char *locale, const char *remoto)
{
    return carica_file(f, locale, remoto, 0);
}

int exftp_accoda(ExFtp *f, const char *locale, const char *remoto)
{
    if (f) f->riparti = 0;
    return carica_file(f, locale, remoto, 1);
}

int exftp_carica_mem(ExFtp *f, const void *dati, unsigned long n, const char *remoto)
{
    Fonte s;

    if (!f || (!dati && n) || !remoto) return -EINVAL;
    if (!f->aperto) return -ENOTCONN;
    memset(&s, 0, sizeof(s));
    s.fd  = -1;
    s.mem = (const unsigned char *)dati;
    s.n   = n;
    f->riparti = 0;
    return carica_da(f, &s, (long)n, remoto, 0);
}

const char *exftp_perche(int e)
{
    switch (e) {
    case 0:                return "fatto";
    case -ENOTCONN:        return "non collegato";
    case -ENETDOWN:        return "la rete non c'e'";
    case -EHOSTUNREACH:    return "il nome non si risolve";
    case -ECONNREFUSED:    return "connessione rifiutata";
    case -ECONNRESET:      return "la connessione si e' chiusa";
    case -ETIMEDOUT:       return "nessuna risposta in tempo";
    case -EACCES:          return "accesso rifiutato";
    case -ENOENT:          return "non c'e' (o non si puo')";
    case -EEXIST:          return "c'e' gia'";
    case -EPROTO:          return "TLS non riuscito";
    case -EPROTONOSUPPORT: return "FTPS non disponibile";
    case -ENOTSUP:         return "il server non sa riprendere (REST)";
    case -ECANCELED:       return "interrotto";
    case -EADDRINUSE:      return "nessuna porta libera per PORT";
    case -ENOMEM:          return "memoria finita";
    default:               return "errore";
    }
}
