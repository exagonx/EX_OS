/* =============================================================================
 * bin/wifi/wifi.c — the WiFi networks, from the command line (@WIFI)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     wifi                      the state: network, signal, speed, address
 *     wifi -config              the configuration screen (a menu)
 *     wifi -showlist            the networks the radio hears
 *     wifi -connect:<SSID>      join it (asks the password); -save keeps it,
 *                               -default also makes it the default
 *     wifi -disconnect          leave the network (and stay off)
 *     wifi -forget:<SSID>       forget a saved network
 *     wifi -default:<SSID>      the saved network to prefer ("" = none)
 *     wifi -boot:on|off         root: connect at start-up, before login
 *
 * Version 0.002 (2026-10-14) is a new program: 0.001 spoke to the driver; this
 * one speaks to wifid (drivers/net/wifid_proto.h), which keeps the saved
 * networks, reconnects on its own and gives the address. If wifid is not
 * running it is started here, as the user who runs `wifi` - and that is
 * exactly the rule the user asked for: root's wifid (started at boot) is the
 * system's and changes only with root's privileges; without it, each user
 * has his own.
 *
 * ! THE PASSWORD IS READ HERE, with asterisks (exuser_leggi_password), and
 * travels to wifid in one IPC message; wifid derives the key and forgets it.
 * If the console cannot be put in raw mode the password is not read at all:
 * showing it would be worse than refusing.
 * ============================================================================= */
#include "libc.h"
#include "exuser.h"
#include "wifi_proto.h"
#include "wifid_proto.h"

EX_VERSIONE("wifi", "0.002");

static int g_pid = 0;          /* wifid */

/* =============================================================================
 * Talking to wifid
 * ============================================================================= */
static int trova_wifid(void)
{
    int i;

    g_pid = ipc_lookup(WIFID_SERVIZIO);
    if (g_pid > 0) return 0;
    {
        char *av[2] = { "/bin/wifid", NULL };

        if (spawn_ex("/bin/wifid", av, environ, NULL, 0) < 0) {
            printf("wifi: non riesco ad avviare /bin/wifid\n");
            return -1;
        }
    }
    for (i = 0; i < 40 && (g_pid = ipc_lookup(WIFID_SERVIZIO)) <= 0; i++) usleep(100000);
    if (g_pid <= 0) { printf("wifi: wifid non risponde\n"); return -1; }
    return 0;
}

typedef struct { int pid; unsigned int a, b; } Atteso;

static int filtro(const IpcMessage *m, void *d)
{
    const Atteso *x = (const Atteso *)d;

    if ((int)m->sender_pid != x->pid) return IPC_ALTRUI;
    if (m->tipo == WIFID_MSG_EVENTO) return IPC_BUTTA;
    return (m->tipo == x->a || m->tipo == x->b) ? IPC_MIO : IPC_ALTRUI;
}

static int attendi(unsigned int a, unsigned int b, unsigned char *buf, unsigned int *len,
                   unsigned int *quale, unsigned int ms)
{
    IpcMessage meta;
    Atteso     x;

    x.pid = g_pid; x.a = a; x.b = b;
    if (ipc_scegli(filtro, &x, &meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
    if (len) *len = meta.len;
    if (quale) *quale = meta.tipo;
    return 0;
}

/* a request that answers WIFID_MSG_ESITO: prints the reason, returns the code */
static int chiedi(unsigned int tipo, const void *dati, unsigned int n, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    WifiEsito     e;

    if (ipc_send((unsigned int)g_pid, tipo, dati, n) < 0) { printf("wifi: wifid non risponde\n"); return -1; }
    if (attendi(WIFID_MSG_ESITO, WIFID_MSG_ESITO, buf, NULL, NULL, ms) != 0) {
        printf("wifi: nessuna risposta da wifid\n");
        return -1;
    }
    memcpy(&e, buf, sizeof(e));
    e.perche[sizeof(e.perche) - 1] = 0;
    printf("  %s\n", e.perche);
    return e.codice;
}

static int stato(WifidStato *s)
{
    unsigned char buf[IPC_MSG_MAX_DATA];

    if (ipc_send((unsigned int)g_pid, WIFID_MSG_STATO, NULL, 0) < 0) return -1;
    if (attendi(WIFID_MSG_STATO_R, WIFID_MSG_STATO_R, buf, NULL, NULL, 5000) != 0) return -1;
    memcpy(s, buf, sizeof(*s));
    return 0;
}

/* =============================================================================
 * Printing
 * ============================================================================= */
static const char *quanto_forte(int dbm)
{
    if (dbm >= -50) return "ottimo";
    if (dbm >= -60) return "buono";
    if (dbm >= -70) return "discreto";
    if (dbm >= -80) return "debole";
    return "quasi niente";
}

/* five bars, for the eye: -50 dBm or better is full */
static void barre(int dbm, char *out)
{
    int n = (dbm + 100) / 10, i;

    if (n < 0) n = 0;
    if (n > 5) n = 5;
    for (i = 0; i < 5; i++) out[i] = i < n ? '#' : '.';
    out[5] = 0;
}

static void ip_stampa(const char *nome, const unsigned char *ip)
{
    printf("  %-11s %u.%u.%u.%u\n", nome, ip[0], ip[1], ip[2], ip[3]);
}

static int mostra_stato(void)
{
    WifidStato s;
    char       b[6];

    if (stato(&s) != 0) { printf("wifi: wifid non risponde\n"); return 1; }
    if (!s.scheda) {
        printf("WiFi: nessuna scheda WiFi risponde (nessun driver per 'wifi0').\n");
        return 1;
    }
    printf("WiFi: %s\n", wifi_stato_nome(s.radio.stato));
    if (s.radio.stato == WIFI_STATO_CONNESSA) {
        barre(s.radio.potenza, b);
        printf("  %-11s %s\n", "rete", s.radio.ssid);
        printf("  %-11s [%s] %d dBm, %s\n", "segnale", b, s.radio.potenza, quanto_forte(s.radio.potenza));
        printf("  %-11s %u Mbit/s\n", "velocita'", s.radio.velocita);
        printf("  %-11s %s, canale %d\n", "sicurezza", wifi_auth_nome(s.radio.auth), s.radio.canale);
        if (s.ip_ok) {
            ip_stampa("indirizzo", s.ip);
            ip_stampa("gateway", s.gateway);
            printf("  %-11s %s\n", "metodo", s.ip_metodo == WIFID_IP_STATICO ? "statico" : "DHCP");
        } else {
            printf("  %-11s nessuno (ancora)\n", "indirizzo");
        }
    }
    printf("  %-11s %s\n", "scheda", s.radio.scheda);
    if (s.sistema) printf("  (e' la connessione di SISTEMA: si cambia solo da root)\n");
    return 0;
}

/* =============================================================================
 * The network list
 * ============================================================================= */
static int elenco(WifidRete *out, int max, int stampa)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len, quale;
    int           n = 0;

    if (ipc_send((unsigned int)g_pid, WIFID_MSG_CERCA, NULL, 0) < 0) return -1;
    printf("Cerco le reti...\n");
    for (;;) {
        if (attendi(WIFID_MSG_RETE, WIFID_MSG_FINE, buf, &len, &quale, n ? 5000 : 20000) != 0) break;
        if (quale == WIFID_MSG_FINE) break;
        if (len >= sizeof(WifidRete) && n < max) memcpy(&out[n++], buf, sizeof(WifidRete));
    }
    if (stampa) {
        int i;

        if (n == 0) { printf("  nessuna rete si sente.\n"); return 0; }
        printf("\n   %-24s %-8s %-6s %-5s %s\n", "rete", "sicurezza", "canale", "dBm", "segnale");
        for (i = 0; i < n; i++) {
            char b[6];

            barre(out[i].rete.potenza, b);
            printf("%2d %-24s %-8s %-6d %-5d [%s]%s%s\n", i + 1, out[i].rete.ssid,
                   wifi_auth_nome(out[i].rete.auth), out[i].rete.canale, out[i].rete.potenza, b,
                   out[i].salvata ? " salvata" : "", out[i].predefinita ? " (predefinita)" : "");
        }
    }
    return n;
}

/* =============================================================================
 * Joining
 * ============================================================================= */
static int leggi_parola(char *dst, int max)
{
    exuser_prendi_console();
    if (exuser_leggi_password(dst, max) < 0) {
        printf("\nwifi: non riesco a chiedere la password senza mostrarla. Non ho letto niente.\n");
        return -1;
    }
    printf("\n");
    return 0;
}

static int collegati(WifidProfilo *p, int chiedi_parola)
{
    int r;

    if (chiedi_parola && p->auth != WIFI_AUTH_APERTA) {
        printf("Password di %s: ", p->ssid);
        if (leggi_parola(p->parola, sizeof(p->parola)) != 0) return 1;
    }
    printf("Mi collego a %s...\n", p->ssid);
    r = chiedi(WIFID_MSG_CONNETTI, p, sizeof(*p), 90000);
    memset(p->parola, 0, sizeof(p->parola));
    if (r == 0 || r == WIFID_ERR_IP) mostra_stato();
    return r == 0 ? 0 : 1;
}

static int si_no(const char *domanda)
{
    char r[8];

    printf("%s [s/n] ", domanda);
    if (exuser_leggi_riga(r, sizeof(r)) < 0) return 0;
    return r[0] == 's' || r[0] == 'S' || r[0] == 'y' || r[0] == 'Y';
}

static int leggi_ip(const char *domanda, unsigned char *ip)
{
    char         r[32];
    unsigned int a, b, c, d;

    for (;;) {
        printf("%s: ", domanda);
        if (exuser_leggi_riga(r, sizeof(r)) < 0) return -1;
        if (sscanf(r, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256) {
            ip[0] = (unsigned char)a; ip[1] = (unsigned char)b; ip[2] = (unsigned char)c; ip[3] = (unsigned char)d;
            return 0;
        }
        printf("  scrivi quattro numeri separati da punti, per esempio 192.168.1.20\n");
    }
}

/* how the address is obtained: asked for a new network, or for a hidden one */
static void chiedi_ip(WifidProfilo *p)
{
    if (!si_no("Indirizzo IP manuale (statico)? Se no: DHCP")) { p->ip_metodo = WIFID_IP_DHCP; return; }
    p->ip_metodo = WIFID_IP_STATICO;
    leggi_ip("  indirizzo", p->ip);
    leggi_ip("  maschera ", p->maschera);
    leggi_ip("  gateway  ", p->gateway);
    leggi_ip("  DNS      ", p->dns);
}

static int menu_collega(void)
{
    WifidRete    reti[32];
    WifidProfilo p;
    char         r[16];
    int          n = elenco(reti, 32, 1), k;

    if (n <= 0) return 1;
    printf("\nNumero della rete (Invio per tornare): ");
    if (exuser_leggi_riga(r, sizeof(r)) < 0 || !r[0]) return 0;
    k = atoi(r);
    if (k < 1 || k > n) { printf("  numero fuori elenco\n"); return 1; }

    memset(&p, 0, sizeof(p));
    strncpy(p.ssid, reti[k - 1].rete.ssid, WIFI_SSID_MAX - 1);
    p.auth = reti[k - 1].rete.auth;
    if (reti[k - 1].salvata) return collegati(&p, 0);      /* it has its key and settings */

    chiedi_ip(&p);
    p.salva = si_no("Salvare la rete (si ricollega da sola)?");
    if (p.salva) p.predefinita = si_no("Come rete predefinita (scelta prima delle altre)?");
    return collegati(&p, 1);
}

static int menu_nascosta(void)
{
    WifidProfilo p;
    char         r[40];

    memset(&p, 0, sizeof(p));
    p.nascosta = 1;
    printf("Rete nascosta: la rete non si annuncia, quindi i dati vanno scritti tutti.\n");
    printf("  SSID: ");
    if (exuser_leggi_riga(p.ssid, WIFI_SSID_MAX) < 0 || !p.ssid[0]) return 1;
    printf("  Sicurezza: 1 aperta, 2 WEP, 3 WPA, 4 WPA2 [4]: ");
    if (exuser_leggi_riga(r, sizeof(r)) < 0) return 1;
    switch (r[0]) {
    case '1': p.auth = WIFI_AUTH_APERTA; break;
    case '2': p.auth = WIFI_AUTH_WEP;    break;
    case '3': p.auth = WIFI_AUTH_WPA;    break;
    default:  p.auth = WIFI_AUTH_WPA2;   break;
    }
    chiedi_ip(&p);
    p.salva = si_no("Salvare la rete?");
    if (p.salva) p.predefinita = si_no("Come rete predefinita?");
    return collegati(&p, 1);
}

static int menu_salvate(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len, quale;
    WifidProfilo  prof[WIFID_SALVATE_MAX];
    int           n = 0, i, k;
    char          r[16];

    if (ipc_send((unsigned int)g_pid, WIFID_MSG_SALVATE, NULL, 0) < 0) return 1;
    while (attendi(WIFID_MSG_PROFILO, WIFID_MSG_FINE, buf, &len, &quale, 5000) == 0 && quale != WIFID_MSG_FINE)
        if (n < WIFID_SALVATE_MAX) memcpy(&prof[n++], buf, sizeof(WifidProfilo));
    if (n == 0) { printf("  nessuna rete salvata.\n"); return 0; }
    for (i = 0; i < n; i++)
        printf("%2d %-24s %-8s %s%s%s\n", i + 1, prof[i].ssid, wifi_auth_nome(prof[i].auth),
               prof[i].ip_metodo == WIFID_IP_STATICO ? "IP statico" : "DHCP",
               prof[i].nascosta ? ", nascosta" : "", prof[i].predefinita ? ", PREDEFINITA" : "");
    printf("\nNumero della rete (Invio per tornare): ");
    if (exuser_leggi_riga(r, sizeof(r)) < 0 || !r[0]) return 0;
    k = atoi(r);
    if (k < 1 || k > n) return 1;
    printf("  p = rendila predefinita, n = nessuna predefinita, d = dimenticala: ");
    if (exuser_leggi_riga(r, sizeof(r)) < 0) return 1;
    if (r[0] == 'p') return chiedi(WIFID_MSG_PREDEFINITA, prof[k - 1].ssid, WIFI_SSID_MAX, 5000) ? 1 : 0;
    if (r[0] == 'n') return chiedi(WIFID_MSG_PREDEFINITA, "", 1, 5000) ? 1 : 0;
    if (r[0] == 'd') return chiedi(WIFID_MSG_DIMENTICA, prof[k - 1].ssid, WIFI_SSID_MAX, 5000) ? 1 : 0;
    return 0;
}

static int configura(void)
{
    char r[8];

    for (;;) {
        WifidStato s;

        printf("\n=== WiFi ===\n");
        mostra_stato();
        stato(&s);
        printf("\n  1  reti disponibili: collegati\n");
        printf("  2  rete nascosta: inserisci i dati\n");
        printf("  3  reti salvate: predefinita, dimentica\n");
        printf("  4  disconnetti\n");
        if (getuid() == 0)
            printf("  5  collegamento all'avvio, prima del login: %s\n", s.avvio ? "ACCESO (spegni)" : "spento (accendi)");
        printf("  0  esci\n> ");
        if (exuser_leggi_riga(r, sizeof(r)) < 0) return 0;
        switch (r[0]) {
        case '1': menu_collega();  break;
        case '2': menu_nascosta(); break;
        case '3': menu_salvate();  break;
        case '4': chiedi(WIFID_MSG_STACCA, NULL, 0, 10000); break;
        case '5': if (getuid() == 0) { int v = !s.avvio; chiedi(WIFID_MSG_AVVIO, &v, sizeof(v), 5000); } break;
        case '0': case 'q': return 0;
        default: break;
        }
    }
}

/* =============================================================================
 * main
 * ============================================================================= */
static void uso(void)
{
    printf("uso: wifi                       stato: rete, segnale, velocita', indirizzo\n");
    printf("     wifi -config               la schermata di configurazione\n");
    printf("     wifi -showlist             le reti che si sentono\n");
    printf("     wifi -connect:<SSID> [-save] [-default]\n");
    printf("                                collegati (chiede la password); -save la salva,\n");
    printf("                                -default la rende anche predefinita\n");
    printf("     wifi -disconnect           stacca (e non si ricollega da sola)\n");
    printf("     wifi -forget:<SSID>        dimentica una rete salvata\n");
    printf("     wifi -default:<SSID>       la rete salvata da preferire (vuoto = nessuna)\n");
    printf("     wifi -boot:on|off          root: collegati all'avvio, prima del login\n");
}

int main(int argc, char **argv)
{
    int i, salva = 0, predefinita = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { uso(); return 0; }
        if (!strcmp(argv[i], "-save")) salva = 1;
        if (!strcmp(argv[i], "-default")) salva = predefinita = 1;
    }
    if (trova_wifid() != 0) return 1;
    if (argc == 1) return mostra_stato();

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!strcmp(a, "-config")) return configura();
        if (!strcmp(a, "-showlist") || !strcmp(a, "-list")) {
            WifidRete reti[32];

            return elenco(reti, 32, 1) < 0 ? 1 : 0;
        }
        if (!strncmp(a, "-connect:", 9)) {
            WifidProfilo p;
            WifidRete    reti[32];
            int          n, k, salvata = 0;

            memset(&p, 0, sizeof(p));
            strncpy(p.ssid, a + 9, WIFI_SSID_MAX - 1);
            p.auth = -1;
            n = elenco(reti, 32, 0);
            for (k = 0; k < n; k++)
                if (!strcmp(reti[k].rete.ssid, p.ssid)) { p.auth = reti[k].rete.auth; salvata = reti[k].salvata; }
            if (p.auth < 0 && !salvata) {
                printf("wifi: '%s' non si sente. Se e' una rete nascosta: wifi -config, voce 2.\n", p.ssid);
                return 1;
            }
            p.salva = salva;
            p.predefinita = predefinita;
            return collegati(&p, !salvata || salva);
        }
        if (!strcmp(a, "-disconnect") || !strcmp(a, "-stacca")) return chiedi(WIFID_MSG_STACCA, NULL, 0, 10000) ? 1 : 0;
        if (!strncmp(a, "-forget:", 8))  return chiedi(WIFID_MSG_DIMENTICA, a + 8, (unsigned int)strlen(a + 8) + 1, 5000) ? 1 : 0;
        if (!strncmp(a, "-default:", 9)) return chiedi(WIFID_MSG_PREDEFINITA, a + 9, (unsigned int)strlen(a + 9) + 1, 5000) ? 1 : 0;
        if (!strncmp(a, "-boot:", 6)) {
            int v = !strcmp(a + 6, "on");

            return chiedi(WIFID_MSG_AVVIO, &v, sizeof(v), 5000) ? 1 : 0;
        }
        if (!strcmp(a, "-status")) return mostra_stato();
        if (!strcmp(a, "-save") || !strcmp(a, "-default")) continue;
        uso();
        return 2;
    }
    return 0;
}
