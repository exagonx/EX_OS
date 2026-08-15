/* =============================================================================
 * bin/telnet/telnet.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Client Telnet (RFC 854).
 *
 *     telnet nome-o-indirizzo [porta]
 *
 * Si esce con Ctrl+] .
 *
 * -----------------------------------------------------------------------------
 * ! TELNET MANDA TUTTO IN CHIARO, PASSWORD COMPRESA
 *
 * Non e' un difetto di questo programma: e' il protocollo. Chiunque stia
 * sul percorso legge nome utente e password cosi' come sono, e legge
 * anche tutto quello che si scrive dopo. Il programma lo dice una volta,
 * all'avvio, invece di lasciarlo intendere.
 *
 * L'alternativa si chiamera' SSH quando ci sara' TLS — non «telnet con
 * una toppa».
 *
 * -----------------------------------------------------------------------------
 * COME FA A SENTIRE LA RETE E LA TASTIERA INSIEME
 *
 * Non c'e' select() e non ci sono i thread. C'e' pero' una cosa migliore
 * per questo caso: in EX-OS TUTTO passa dalla stessa cassetta postale.
 *
 *   - allo stack IP si PRENOTA una ricezione (IP_MSG_TCP_RICEVI): lo
 *     stack risponde quando arrivano dati, non prima;
 *   - al servizio tastiera si PRENOTA un tasto (KBD_MSG_READKEY);
 *   - poi si aspetta con un solo ipc_recv_timeout(), e si guarda CHI ha
 *     risposto.
 *
 * Le due prenotazioni sono indipendenti e si riarmano ognuna per conto
 * suo. Non c'e' niente che possa bloccare: se il server tace si continua
 * a ricevere tasti, se nessuno digita si continua a ricevere dati.
 *
 * ! LA SCADENZA SERVE ANCHE QUANDO NON SCADE NIENTE. La modalita' raw
 * della tastiera se ne va da sola ogni volta che qualcun altro chiede una
 * riga (vedi drivers/kbd/kbd_proto.h): senza un risveglio periodico che
 * la riafferma, una tastiera tornata in cooked lascerebbe questo
 * programma in attesa di un tasto che il driver non consegnera' mai.
 *
 * -----------------------------------------------------------------------------
 * LA NEGOZIAZIONE DELLE OPZIONI, E PERCHE' NON SI PUO' SALTARE
 *
 * Telnet intreccia ai dati dei comandi che cominciano con il byte 255
 * (IAC). Un client che li ignorasse stamperebbe caratteri di controllo a
 * schermo e — molto peggio — lascerebbe il server ad ASPETTARE una
 * risposta che non arriva: parecchi server non mandano nemmeno il
 * "login:" finche' non hanno finito di negoziare.
 *
 * ! LA REGOLA E' «RIFIUTA TUTTO QUELLO CHE NON SAI FARE», e va detta
 * esplicitamente: al DO di un'opzione sconosciuta si risponde WONT, al
 * WILL si risponde DONT. Il silenzio non e' un rifiuto — e' un server che
 * aspetta.
 *
 * ! E NON SI RISPONDE MAI A UNA RISPOSTA. Se a un DO si replicasse
 * sempre, due implementazioni educate si rimpallerebbero la stessa
 * opzione all'infinito. Si risponde solo quando la richiesta cambia
 * qualcosa: e' la regola che RFC 1143 chiama «loop prevention», qui nella
 * sua forma minima — si tiene lo stato di cio' che si e' gia' concesso.
 * ============================================================================= */

#include "libc.h"
#include "ip_proto.h"
#include "kbd_proto.h"
#include "dns.h"
#include "rete.h"

/* --- Comandi Telnet --- */
#define IAC     255
#define DONT    254
#define DO      253
#define WONT    252
#define WILL    251
#define SB      250
#define GA      249
#define SE      240

/* --- Opzioni --- */
#define OPT_ECHO    1
#define OPT_SGA     3       /* Suppress Go Ahead */
#define OPT_TTYPE  24       /* Terminal Type */
#define OPT_NAWS   31       /* Negotiate About Window Size */

#define TT_IS       0
#define TT_SEND     1

#define USCITA      ']'     /* Ctrl+] chiude, come nel telnet di sempre */
#define ATTESA_MS   400

static int pid_ip  = 0;
static int pid_kbd = 0;
static int g_id    = 0;
static unsigned g_console = 0;

/* Stato della negoziazione: cosa abbiamo gia' concesso o rifiutato. Serve
 * a non rispondere due volte alla stessa cosa — vedi il commento di
 * testa sul rimpallo. */
static unsigned char g_noi_will[256];
static unsigned char g_lui_will[256];

/* Il server fa l'eco di quello che scriviamo? Finche' non lo dice, lo
 * facciamo noi: una riga che non si vede mentre la si scrive e' peggio di
 * una riga scritta due volte. */
static int g_eco_remoto = 0;

/* =============================================================================
 * Invio verso il server
 * ============================================================================= */
static int manda(const unsigned char *dati, unsigned int len)
{
    unsigned char msg[sizeof(IpTcpDati) + 512];
    IpTcpDati     d;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           i;

    if (len == 0 || len > 512) return -1;

    d.id  = (unsigned int)g_id;
    d.len = len;
    memcpy(msg, &d, sizeof(d));
    memcpy(msg + sizeof(d), dati, len);

    if (ipc_send(pid_ip, IP_MSG_TCP_INVIA, msg, sizeof(d) + len) < 0) return -1;

    /* ! SI ASPETTA L'ESITO, e non e' pignoleria: lo stack risponde a
     * ogni INVIA, e una risposta non raccolta resta nella cassetta
     * postale. Il ciclo principale la troverebbe al giro dopo e la
     * scambierebbe per qualcos'altro. */
    for (i = 0; i < 8; i++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 3000) < 0) return -1;
        if ((int)meta.sender_pid != pid_ip) continue;
        if (meta.tipo == IP_MSG_ESITO) {
            IpEsito e;

            if (meta.len < sizeof(e)) return -1;
            memcpy(&e, buf, sizeof(e));
            return e.codice;
        }
        /* Dati arrivati mentre mandavamo: si stampano invece di buttarli. */
        if (meta.tipo == IP_MSG_TCP_DATI) {
            IpTcpDati in;

            if (meta.len >= sizeof(in)) {
                memcpy(&in, buf, sizeof(in));
                if (in.len > 0) write(1, buf + sizeof(in), in.len);
            }
        }
    }
    return -1;
}

static void comando(unsigned char c, unsigned char opt)
{
    unsigned char m[3];

    m[0] = IAC; m[1] = c; m[2] = opt;
    manda(m, 3);
}

/* =============================================================================
 * Negoziazione
 * ============================================================================= */
static void rispondi_will(unsigned char opt)      /* il server dice: io faro' X */
{
    int accetto = (opt == OPT_ECHO || opt == OPT_SGA);

    if (g_lui_will[opt] == (accetto ? 1 : 2)) return;   /* gia' risposto */
    g_lui_will[opt] = (unsigned char)(accetto ? 1 : 2);

    comando(accetto ? DO : DONT, opt);
    if (opt == OPT_ECHO && accetto) g_eco_remoto = 1;
}

static void rispondi_wont(unsigned char opt)      /* il server dice: io NON faro' X */
{
    if (g_lui_will[opt] == 2) return;
    g_lui_will[opt] = 2;

    comando(DONT, opt);
    if (opt == OPT_ECHO) g_eco_remoto = 0;
}

static void rispondi_do(unsigned char opt)        /* il server chiede: fallo tu */
{
    int accetto = (opt == OPT_SGA || opt == OPT_TTYPE || opt == OPT_NAWS);

    if (g_noi_will[opt] == (accetto ? 1 : 2)) return;
    g_noi_will[opt] = (unsigned char)(accetto ? 1 : 2);

    comando(accetto ? WILL : WONT, opt);

    /* La dimensione dello schermo si manda subito: e' un dato, non una
     * promessa, e il server la vuole per impaginare. 80x25 e' la VGA in
     * modo testo, cioe' l'unica che questo sistema abbia. */
    if (accetto && opt == OPT_NAWS) {
        unsigned char m[9];

        m[0] = IAC; m[1] = SB; m[2] = OPT_NAWS;
        m[3] = 0; m[4] = 80;
        m[5] = 0; m[6] = 25;
        m[7] = IAC; m[8] = SE;
        manda(m, 9);
    }
}

static void rispondi_dont(unsigned char opt)
{
    if (g_noi_will[opt] == 2) return;
    g_noi_will[opt] = 2;
    comando(WONT, opt);
}

/* Sottonegoziazione: qui serve solo per dire che terminale siamo. */
static void sotto(const unsigned char *d, unsigned int len)
{
    if (len >= 2 && d[0] == OPT_TTYPE && d[1] == TT_SEND) {
        unsigned char m[16];
        const char   *nome = "EXOS";
        unsigned int  i = 0, j;

        m[i++] = IAC; m[i++] = SB; m[i++] = OPT_TTYPE; m[i++] = TT_IS;
        for (j = 0; nome[j]; j++) m[i++] = (unsigned char)nome[j];
        m[i++] = IAC; m[i++] = SE;
        manda(m, i);
    }
}

/* =============================================================================
 * Il flusso in arrivo: si separano i comandi dai dati
 *
 * ! LO STATO E' STATICO E NON LOCALE. Una sequenza IAC puo' essere
 * spezzata fra due segmenti TCP — TCP consegna byte, non messaggi — e uno
 * stato azzerato a ogni chiamata farebbe stampare a schermo la meta' di
 * un comando e rispondere all'altra meta' come se fosse un comando
 * diverso. E' il difetto classico di chi legge telnet un pacchetto per
 * volta.
 * ============================================================================= */
static int           g_stato = 0;       /* 0 dati, 1 IAC, 2 verbo, 3 SB, 4 SB+IAC */
static unsigned char g_verbo = 0;
static unsigned char g_sb[64];
static unsigned int  g_sb_len = 0;

static void in_arrivo(const unsigned char *d, unsigned int len)
{
    unsigned char testo[512];
    unsigned int  t = 0, i;

    for (i = 0; i < len; i++) {
        unsigned char c = d[i];

        switch (g_stato) {
        case 0:
            if (c == IAC) { g_stato = 1; break; }
            if (t < sizeof(testo)) testo[t++] = c;
            break;

        case 1:
            if (c == IAC) {                 /* IAC IAC = un 255 nei dati */
                if (t < sizeof(testo)) testo[t++] = IAC;
                g_stato = 0;
            } else if (c == SB) {
                g_stato = 3; g_sb_len = 0;
            } else if (c == WILL || c == WONT || c == DO || c == DONT) {
                g_verbo = c; g_stato = 2;
            } else {
                g_stato = 0;                /* GA e gli altri: si ignorano */
            }
            break;

        case 2:
            /* ! SI SVUOTA IL TESTO PRIMA DI RISPONDERE. La risposta e'
             * una manda(), che aspetta l'esito e puo' consumare altri
             * messaggi: quello che si e' gia' letto va sullo schermo
             * adesso, o esce dopo — cioe' nell'ordine sbagliato. */
            if (t) { write(1, testo, t); t = 0; }

            if      (g_verbo == WILL) rispondi_will(c);
            else if (g_verbo == WONT) rispondi_wont(c);
            else if (g_verbo == DO)   rispondi_do(c);
            else                      rispondi_dont(c);
            g_stato = 0;
            break;

        case 3:
            if (c == IAC) g_stato = 4;
            else if (g_sb_len < sizeof(g_sb)) g_sb[g_sb_len++] = c;
            break;

        case 4:
            if (c == SE) {
                if (t) { write(1, testo, t); t = 0; }
                sotto(g_sb, g_sb_len);
                g_stato = 0;
            } else {
                if (g_sb_len < sizeof(g_sb)) g_sb[g_sb_len++] = c;
                g_stato = 3;
            }
            break;
        }
    }

    if (t) write(1, testo, t);
}

/* =============================================================================
 * Tastiera
 * ============================================================================= */
static void kbd_modo(unsigned int modo)
{
    KbdSetMode m;

    if (pid_kbd <= 0) return;
    m.modo    = modo;
    m.console = g_console;
    ipc_send(pid_kbd, KBD_MSG_SETMODE, &m, sizeof(m));
}

static void prenota_tasto(void)
{
    if (pid_kbd > 0) ipc_send(pid_kbd, KBD_MSG_READKEY, &g_console,
                              sizeof(g_console));
}

static void prenota_dati(void)
{
    IpTcpRif r;

    r.id = (unsigned int)g_id;
    ipc_send(pid_ip, IP_MSG_TCP_RICEVI, &r, sizeof(r));
}

/* Traduce un evento tasto nei byte da mandare. Ritorna quanti, 0 se il
 * tasto non si manda, -1 se e' la combinazione di uscita. */
static int tasto_in_byte(unsigned int ev, unsigned char *out)
{
    unsigned int k = ev & KBD_KEY_MASK;

    if ((ev & KBD_MOD_CTRL) && (k == USCITA)) return -1;

    if (ev & KBD_MOD_CTRL) {
        if (k >= 'a' && k <= 'z') { out[0] = (unsigned char)(k - 'a' + 1); return 1; }
        if (k >= 'A' && k <= 'Z') { out[0] = (unsigned char)(k - 'A' + 1); return 1; }
    }

    switch (k) {
    /* ! INVIO E' CR LF, non un solo LF. Il terminale virtuale di telnet
     * (NVT) vuole che un CR sia sempre seguito da LF o da NUL, e i server
     * che applicano la regola alla lettera con un LF solo non fanno
     * niente. */
    case '\n': case '\r':  out[0] = '\r'; out[1] = '\n'; return 2;

    /* ! IL BACKSPACE SI MANDA COME 0x7F, non come 0x08 che e' quello
     * che il tasto produce qui. Sui sistemi Unix il carattere di
     * cancellazione predefinito e' DEL: mandando 0x08 la riga non si
     * accorcia e compare `^H`, che sembra un difetto della tastiera
     * mentre e' una convenzione dall'altra parte. */
    case '\b':             out[0] = 0x7F; return 1;
    case KBD_K_DEL:        out[0] = 0x7F; return 1;

    /* I cursori come sequenze ANSI: e' quello che si aspetta qualunque
     * cosa giri dall'altra parte. */
    case KBD_K_UP:    out[0]=0x1B; out[1]='['; out[2]='A'; return 3;
    case KBD_K_DOWN:  out[0]=0x1B; out[1]='['; out[2]='B'; return 3;
    case KBD_K_RIGHT: out[0]=0x1B; out[1]='['; out[2]='C'; return 3;
    case KBD_K_LEFT:  out[0]=0x1B; out[1]='['; out[2]='D'; return 3;
    case KBD_K_HOME:  out[0]=0x1B; out[1]='['; out[2]='H'; return 3;
    case KBD_K_END:   out[0]=0x1B; out[1]='['; out[2]='F'; return 3;
    default: break;
    }

    if (k >= 32 && k < 256 && k != 127) { out[0] = (unsigned char)k; return 1; }
    return 0;
}

/* =============================================================================
 * main
 * ============================================================================= */
static void uso(void)
{
    printf("uso: telnet NOME|INDIRIZZO [PORTA]\n\n");
    printf("Apre una sessione interattiva. Si esce con Ctrl+]\n\n");
    printf("! Telnet manda tutto in chiaro, password compresa: non e' un\n");
    printf("   difetto del programma, e' il protocollo.\n");
}

int main(int argc, char **argv)
{
    unsigned char ip[4], buf[IPC_MSG_MAX_DATA];
    IpTcpApri     a;
    IpTcpRif      r;
    ConsoleInfo   ci;
    int           rc, giri, uscito = 0;

    if (argc < 2) { uso(); return 1; }
    if (strcmp(argv[1], "-h") == 0) { uso(); return 0; }

    pid_ip = rete_richiedi(IP_SERVIZIO);
    if (pid_ip <= 0) return 1;

    pid_kbd = ipc_lookup(KBD_SERVICE_NAME);
    if (pid_kbd <= 0) {
        printf("telnet: senza /dev/kbd.drv non c'e' la modalita' raw, e una\n");
        printf("        sessione interattiva ha bisogno dei tasti uno per uno.\n");
        printf("        Si dichiara in [modules] di /boot/kernel.cfg.\n");
        return 1;
    }
    if (console_info(&ci) == 0) g_console = ci.mia;

    rc = dns_risolvi(argv[1], ip);
    if (rc != 0) {
        printf("telnet: non riesco a risolvere '%s' (%d)\n", argv[1], rc);
        return 1;
    }

    memcpy(a.ip, ip, 4);
    a.porta      = (argc >= 3) ? (unsigned int)atoi(argv[2]) : 23u;
    a.timeout_ms = 5000;

    printf("Connessione a %u.%u.%u.%u:%u ...\n",
           ip[0], ip[1], ip[2], ip[3], a.porta);

    for (giri = 0; giri < 10; giri++) {
        IpcMessage meta;
        IpEsito    e;
        int        i;

        if (ipc_send(pid_ip, IP_MSG_TCP_APRI, &a, sizeof(a)) < 0) return 1;
        g_id = -1;
        for (i = 0; i < 8; i++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 7000) < 0) break;
            if ((int)meta.sender_pid != pid_ip) continue;
            if (meta.tipo != IP_MSG_ESITO || meta.len < sizeof(e)) continue;
            memcpy(&e, buf, sizeof(e));
            g_id = e.codice;
            break;
        }
        if (g_id != -EAGAIN) break;
        usleep(200 * 1000);
    }

    if (g_id <= 0) {
        printf("telnet: connessione fallita (%d)\n", g_id);
        return 1;
    }

    printf("Connesso. Si esce con Ctrl+]\n");
    printf("! Quello che scrivi viaggia in chiaro.\n\n");

    memset(g_noi_will, 0, sizeof(g_noi_will));
    memset(g_lui_will, 0, sizeof(g_lui_will));

    kbd_modo(KBD_MODE_RAW);
    prenota_dati();
    prenota_tasto();

    while (!uscito) {
        IpcMessage meta;

        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) {
            /* Scaduta: si riafferma la modalita' raw e si riarmano le
             * prenotazioni. Vedi il commento di testa: una tastiera
             * tornata in cooked lascerebbe questo programma ad aspettare
             * per sempre. */
            kbd_modo(KBD_MODE_RAW);
            prenota_tasto();
            continue;
        }

        if ((int)meta.sender_pid == pid_ip && meta.tipo == IP_MSG_TCP_DATI) {
            IpTcpDati in;

            if (meta.len < sizeof(in)) continue;
            memcpy(&in, buf, sizeof(in));

            if (in.len == 0) {              /* l'altro ha chiuso */
                printf("\n\nConnessione chiusa dal server.\n");
                break;
            }
            in_arrivo(buf + sizeof(in), in.len);
            prenota_dati();
            continue;
        }

        if ((int)meta.sender_pid == pid_kbd && meta.tipo == KBD_MSG_KEY) {
            unsigned int  ev = 0;
            unsigned char b[4];
            int           n;

            if (meta.len >= sizeof(ev)) memcpy(&ev, buf, sizeof(ev));

            n = tasto_in_byte(ev, b);
            if (n < 0) { uscito = 1; }
            else if (n > 0) {
                if (!g_eco_remoto) write(1, b, (unsigned int)n);
                if (manda(b, (unsigned int)n) < 0) {
                    printf("\n\nInvio fallito: connessione persa.\n");
                    break;
                }
            }
            prenota_tasto();
            continue;
        }
    }

    kbd_modo(KBD_MODE_COOKED);

    r.id = (unsigned int)g_id;
    ipc_send(pid_ip, IP_MSG_TCP_CHIUDI, &r, sizeof(r));

    if (uscito) printf("\n\nChiuso.\n");
    return 0;
}
