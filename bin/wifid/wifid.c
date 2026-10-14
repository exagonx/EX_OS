/* =============================================================================
 * bin/wifid/wifid.c — the WiFi connection manager (@WIFI)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     wifid            run as the WiFi manager of whoever starts it
 *     wifid -auto      at boot (from /boot/avvio.sh, as root): run only if
 *                      root has turned the start-up connection on; else exit
 *                      without a word
 *
 * The one process that decides which network the radio joins. The command
 * `wifi` and the desktop icon talk to it (drivers/net/wifid_proto.h); it is
 * the only one that talks to the WiFi driver ("wifi0", wifi_proto.h).
 *
 * WHAT IT DOES, ON ITS OWN, EVERY FEW SECONDS
 *   - not connected, and not told to stay off: it scans, and joins the
 *     DEFAULT network if it is heard (a hidden default is tried by name),
 *     otherwise the STRONGEST of the saved networks heard;
 *   - connected: it asks the driver how things are; a dropped link becomes
 *     an event and, at the next round, a new attempt;
 *   - once joined: ip.drv is moved onto the radio (IP_MSG_LINK "wifi0", or
 *     started on it if there is no ip.drv yet), then the address - DHCP
 *     (/bin/dhcp) or the static one of that network.
 * Every change is an EVENT sent to whoever asked to listen (the icon shows
 * it in its balloon).
 *
 * WHERE THE NETWORKS ARE KEPT, AND HOW
 *   root:   /cfg/wifi/root.cfg, key /cfg/wifi/root.key
 *   a user: $HOME/.exwin/config/wifi.cfg, key $HOME/.exwin/config/wifi.key
 * The key file holds 32 random bytes, readable by its owner only. Each saved
 * secret (the WPA key derived with PBKDF2 - never the passphrase - or the WEP
 * key) is stored encrypted with AES-256-GCM under that key, with its own
 * random nonce and the SSID as authenticated data.
 * ! WHAT THIS PROTECTS, SAID PLAINLY: a configuration file copied or read
 * elsewhere (another system, a backup, a disk taken out) does not give the
 * passwords without the key file. It does not protect against root, nor
 * against whoever can read the key file: the key must be readable at boot,
 * before anybody logs in, and no secret typed by a person is there then.
 *
 * WHO MAY CHANGE WHAT (the user's rule, 2026-10-14)
 *   A request that CHANGES something (connect, disconnect, save, forget,
 *   default, start-up) is accepted from the user wifid runs as, or from
 *   root. So root's system connection is changed only with root's
 *   privileges, and a user manages his own wifid when root's is not running.
 *   Reading the state and the list is for everybody.
 * ============================================================================= */
#include "libc.h"
#include "net_proto.h"
#include "ip_proto.h"
#include "wifi_proto.h"
#include "wifid_proto.h"
#include "excrypt.h"

EX_VERSIONE("wifid", "0.001");

#define GIRO_MS          1000     /* the request loop wakes at least this often */
#define CONTROLLO_GIRI      5     /* every 5 s: how is the link?                 */
#define RITENTA_GIRI       10     /* every 10 s, when off: look for a network    */
#define ASCOLTATORI_MAX     8

typedef struct {
    WifidProfilo  p;              /* parola[] is always empty in memory         */
    int           ha_segreto;
    unsigned char segreto[32];    /* WPA: the PSK; WEP: the key bytes           */
    unsigned int  segreto_n;
} Salvata;

static Salvata      g_salvate[WIFID_SALVATE_MAX];
static int          g_n_salvate = 0;
static int          g_avvio = 0;          /* root.cfg: avvio = si           */
static char         g_cfg[256], g_chiave_file[256];
static unsigned char g_chiave[32];
static int          g_mio_uid = 0;

static int          g_pid_wifi = 0;       /* the driver                      */
static WifidStato   g_st;
static WifidProfilo g_attiva;             /* the network we are on (or want) */
static int          g_collegata = 0;
static int          g_fermo = 0;          /* "disconnect" was asked: stay off */
static int          g_ascolta[ASCOLTATORI_MAX];

/* =============================================================================
 * Small helpers
 * ============================================================================= */
static void ip_testo(char *out, const unsigned char *ip)
{
    sprintf(out, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static int ip_leggi(const char *s, unsigned char *ip)
{
    unsigned int a, b, c, d;

    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return 0;
    ip[0] = (unsigned char)a; ip[1] = (unsigned char)b; ip[2] = (unsigned char)c; ip[3] = (unsigned char)d;
    return 1;
}

static void esa(char *out, const unsigned char *b, unsigned int n)
{
    static const char c[] = "0123456789abcdef";
    unsigned int i;

    for (i = 0; i < n; i++) { out[2 * i] = c[b[i] >> 4]; out[2 * i + 1] = c[b[i] & 15]; }
    out[2 * n] = 0;
}

static int da_esa(const char *s, unsigned char *b, unsigned int max)
{
    unsigned int n = 0;

    while (s[0] && s[1] && n < max) {
        int h, l;

        h = isdigit((unsigned char)s[0]) ? s[0] - '0' : (s[0] | 32) - 'a' + 10;
        l = isdigit((unsigned char)s[1]) ? s[1] - '0' : (s[1] | 32) - 'a' + 10;
        if (h < 0 || h > 15 || l < 0 || l > 15) return -1;
        b[n++] = (unsigned char)(h << 4 | l);
        s += 2;
    }
    return (int)n;
}

static const char *auth_parola(int a)
{
    switch (a) {
    case WIFI_AUTH_APERTA: return "aperta";
    case WIFI_AUTH_WEP:    return "wep";
    case WIFI_AUTH_WPA:    return "wpa";
    case WIFI_AUTH_WPA2:   return "wpa2";
    case WIFI_AUTH_WPA3:   return "wpa3";
    default:               return "auto";
    }
}

static int auth_da_parola(const char *s)
{
    if (!strcmp(s, "aperta")) return WIFI_AUTH_APERTA;
    if (!strcmp(s, "wep"))    return WIFI_AUTH_WEP;
    if (!strcmp(s, "wpa"))    return WIFI_AUTH_WPA;
    if (!strcmp(s, "wpa2"))   return WIFI_AUTH_WPA2;
    if (!strcmp(s, "wpa3"))   return WIFI_AUTH_WPA3;
    return -1;
}

/* =============================================================================
 * Events, to whoever listens
 * ============================================================================= */
static void evento(int tipo, const char *ssid, const char *testo)
{
    WifidEvento e;
    int         i;

    memset(&e, 0, sizeof(e));
    e.tipo = tipo;
    if (ssid) strncpy(e.ssid, ssid, sizeof(e.ssid) - 1);
    strncpy(e.testo, testo, sizeof(e.testo) - 1);
    printf("wifid: %s\n", testo);
    for (i = 0; i < ASCOLTATORI_MAX; i++) {
        if (g_ascolta[i] == 0) continue;
        /* a listener that is gone is forgotten at the first failed send */
        if (ipc_send((unsigned int)g_ascolta[i], WIFID_MSG_EVENTO, &e, sizeof(e)) < 0) g_ascolta[i] = 0;
    }
}

/* =============================================================================
 * The key file and the encrypted secrets
 * ============================================================================= */
static int chiave_prepara(void)
{
    int  fd = open(g_chiave_file, O_RDONLY);
    long n;

    if (fd >= 0) {
        n = read(fd, g_chiave, sizeof(g_chiave));
        close(fd);
        if (n == (long)sizeof(g_chiave)) return 0;
    }
    /* none yet: 32 random bytes, readable by the owner only */
    if (getrandom(g_chiave, sizeof(g_chiave), 0) != (int)sizeof(g_chiave)) return -1;
    fd = open(g_chiave_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    n = write(fd, g_chiave, sizeof(g_chiave));
    close(fd);
    chmod(g_chiave_file, 0600);
    return n == (long)sizeof(g_chiave) ? 0 : -1;
}

/* secret -> "nonce|ciphertext|tag" in hex */
static void segreto_cifra(const Salvata *s, char *out)
{
    GcmChiave     g;
    unsigned char iv[12], c[32], tag[16];

    getrandom(iv, sizeof(iv), 0);
    gcm_chiave(&g, g_chiave, 32);
    aes_gcm_cifra(&g, iv, (const unsigned char *)s->p.ssid, (unsigned int)strlen(s->p.ssid),
                  s->segreto, c, s->segreto_n, tag);
    esa(out, iv, 12);
    esa(out + 24, c, s->segreto_n);
    esa(out + 24 + 2 * s->segreto_n, tag, 16);
}

static int segreto_decifra(Salvata *s, const char *in)
{
    GcmChiave     g;
    unsigned char b[12 + 32 + 16];
    int           n = da_esa(in, b, sizeof(b));

    if (n < 12 + 1 + 16) return -1;
    s->segreto_n = (unsigned int)n - 12 - 16;
    gcm_chiave(&g, g_chiave, 32);
    if (aes_gcm_decifra(&g, b, (const unsigned char *)s->p.ssid, (unsigned int)strlen(s->p.ssid),
                        b + 12, s->segreto, s->segreto_n, b + 12 + s->segreto_n) != 0) {
        printf("wifid: la chiave salvata di '%s' non si decifra (un altro file di chiave?)\n", s->p.ssid);
        return -1;
    }
    s->ha_segreto = 1;
    return 0;
}

/* =============================================================================
 * The configuration file
 * ============================================================================= */
static void cfg_scrivi(void)
{
    char *t = malloc(16384), riga[512], a[16], m[16], gw[16], d[16];
    int   i, fd;

    if (!t) return;
    t[0] = 0;
    strcat(t, "# The saved WiFi networks - written by wifid, see bin/wifid/wifid.c.\n"
              "# The keys are encrypted with the key file next to this one.\n");
    if (g_mio_uid == 0) strcat(t, g_avvio ? "avvio = si\n" : "avvio = no\n");
    for (i = 0; i < g_n_salvate; i++) {
        const WifidProfilo *p = &g_salvate[i].p;

        ip_testo(a, p->ip); ip_testo(m, p->maschera); ip_testo(gw, p->gateway); ip_testo(d, p->dns);
        snprintf(riga, sizeof(riga),
                 "\n[rete]\nssid = %s\nsicurezza = %s\nnascosta = %s\npredefinita = %s\n"
                 "ip = %s\nindirizzo = %s\nmaschera = %s\ngateway = %s\ndns = %s\n",
                 p->ssid, auth_parola(p->auth), p->nascosta ? "si" : "no",
                 p->predefinita ? "si" : "no", p->ip_metodo == WIFID_IP_STATICO ? "statico" : "dhcp",
                 a, m, gw, d);
        strcat(t, riga);
        if (g_salvate[i].ha_segreto) {
            char cif[2 * (12 + 32 + 16) + 1];

            segreto_cifra(&g_salvate[i], cif);
            snprintf(riga, sizeof(riga), "chiave = %s\n", cif);
            strcat(t, riga);
        }
    }
    fd = open(g_cfg, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) { write(fd, t, strlen(t)); close(fd); chmod(g_cfg, 0600); }
    free(t);
}

static void cfg_leggi(void)
{
    FILE    *f = fopen(g_cfg, "r");
    char     riga[512];
    Salvata *s = NULL;

    g_n_salvate = 0;
    if (!f) return;
    while (fgets(riga, sizeof(riga), f)) {
        char *k = riga, *v, *e;

        while (*k == ' ' || *k == '\t') k++;
        if (*k == '#' || *k == '\n' || *k == 0) continue;
        if (!strncmp(k, "[rete]", 6)) {
            s = (g_n_salvate < WIFID_SALVATE_MAX) ? &g_salvate[g_n_salvate++] : NULL;
            if (s) { memset(s, 0, sizeof(*s)); s->p.auth = -1; }
            continue;
        }
        v = strchr(k, '=');
        if (!v) continue;
        *v++ = 0;
        for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == ' '); e--) e[-1] = 0;
        for (e = k + strlen(k); e > k && e[-1] == ' '; e--) e[-1] = 0;
        while (*v == ' ') v++;

        if (!strcmp(k, "avvio")) { g_avvio = !strcmp(v, "si"); continue; }
        if (!s) continue;
        if      (!strcmp(k, "ssid"))        strncpy(s->p.ssid, v, WIFI_SSID_MAX - 1);
        else if (!strcmp(k, "sicurezza"))   s->p.auth = auth_da_parola(v);
        else if (!strcmp(k, "nascosta"))    s->p.nascosta = !strcmp(v, "si");
        else if (!strcmp(k, "predefinita")) s->p.predefinita = !strcmp(v, "si");
        else if (!strcmp(k, "ip"))          s->p.ip_metodo = !strcmp(v, "statico") ? WIFID_IP_STATICO : WIFID_IP_DHCP;
        else if (!strcmp(k, "indirizzo"))   ip_leggi(v, s->p.ip);
        else if (!strcmp(k, "maschera"))    ip_leggi(v, s->p.maschera);
        else if (!strcmp(k, "gateway"))     ip_leggi(v, s->p.gateway);
        else if (!strcmp(k, "dns"))         ip_leggi(v, s->p.dns);
        else if (!strcmp(k, "chiave"))      segreto_decifra(s, v);
    }
    fclose(f);
}

static Salvata *salvata(const char *ssid)
{
    int i;

    for (i = 0; i < g_n_salvate; i++) if (!strcmp(g_salvate[i].p.ssid, ssid)) return &g_salvate[i];
    return NULL;
}

/* =============================================================================
 * The driver
 * ============================================================================= */
static int driver_c_e(void)
{
    if (g_pid_wifi > 0 && ipc_lookup(WIFI_SERVIZIO_0) == g_pid_wifi) return 1;
    g_pid_wifi = ipc_lookup(WIFI_SERVIZIO_0);
    return g_pid_wifi > 0;
}

/* Waiting for one answer while requests keep arriving: ipc_scegli takes the
 * answer and leaves everything else on the libc's shelf, in order, for the
 * main loop (which reads with ipc_scegli too, so the shelf is served first).
 * Never ipc_rimetti + read again: the shelf comes first, and it would hand
 * back the same message forever. */
typedef struct { int pid; unsigned int a, b; } Atteso;

static int filtro_atteso(const IpcMessage *m, void *d)
{
    const Atteso *x = (const Atteso *)d;

    return ((int)m->sender_pid == x->pid && (m->tipo == x->a || m->tipo == x->b)) ? IPC_MIO : IPC_ALTRUI;
}

static int filtro_tutto(const IpcMessage *m, void *d) { (void)m; (void)d; return IPC_MIO; }

static int attendi_da(int pid, unsigned int tipo, unsigned int tipo2, unsigned char *buf,
                      unsigned int *len, unsigned int *quale, unsigned int ms)
{
    IpcMessage meta;
    Atteso     x;

    x.pid = pid; x.a = tipo; x.b = tipo2;
    if (ipc_scegli(filtro_atteso, &x, &meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
    if (len) *len = meta.len;
    if (quale) *quale = meta.tipo;
    return 0;
}

static int dal_driver(unsigned int tipo, unsigned int tipo2, unsigned char *buf,
                      unsigned int *len, unsigned int *quale, unsigned int ms)
{
    return attendi_da(g_pid_wifi, tipo, tipo2, buf, len, quale, ms);
}

static int driver_cerca(WifiRete *out, int max)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len, quale;
    int           n = 0;

    if (!driver_c_e() || ipc_send((unsigned int)g_pid_wifi, WIFI_MSG_CERCA, NULL, 0) < 0) return -1;
    for (;;) {
        if (dal_driver(WIFI_MSG_RETI, WIFI_MSG_FINE, buf, &len, &quale, n ? 3000 : 15000) != 0) return n;
        if (quale == WIFI_MSG_FINE) return n;
        if (len >= sizeof(WifiRete) && n < max) memcpy(&out[n++], buf, sizeof(WifiRete));
    }
}

static int driver_stato(WifiStato *s)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;

    if (!driver_c_e() || ipc_send((unsigned int)g_pid_wifi, WIFI_MSG_STATO, NULL, 0) < 0) return -1;
    if (dal_driver(WIFI_MSG_STATO_R, WIFI_MSG_STATO_R, buf, &len, NULL, 3000) != 0 || len < sizeof(*s)) return -1;
    memcpy(s, buf, sizeof(*s));
    return 0;
}

static int driver_esito(unsigned int tipo, const void *dati, unsigned int n, WifiEsito *e)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;

    memset(e, 0, sizeof(*e));
    if (!driver_c_e() || ipc_send((unsigned int)g_pid_wifi, tipo, dati, n) < 0) {
        e->codice = WIFID_ERR_SCHEDA;
        strcpy(e->perche, "nessuna scheda WiFi risponde (wifi0)");
        return -1;
    }
    if (dal_driver(WIFI_MSG_ESITO, WIFI_MSG_ESITO, buf, &len, NULL, 30000) != 0) {
        e->codice = WIFI_ERR_TEMPO;
        strcpy(e->perche, "la scheda WiFi non ha risposto");
        return -1;
    }
    memcpy(e, buf, len < sizeof(*e) ? len : sizeof(*e));
    return e->codice;
}

/* =============================================================================
 * The address: ip.drv on the radio, then DHCP or the static one
 * ============================================================================= */
static int ip_esito(int pid, unsigned int tipo, const void *dati, unsigned int n)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    IpEsito       e;

    if (ipc_send((unsigned int)pid, tipo, dati, n) < 0) return -1;
    if (attendi_da(pid, IP_MSG_ESITO, IP_MSG_ESITO, buf, NULL, NULL, 5000) != 0) return -1;
    memcpy(&e, buf, sizeof(e));
    return e.codice;
}

static int esegui(const char *percorso, char *a1, char *a2, char *a3)
{
    char *av[5];
    int   pid, stato = -1;

    av[0] = (char *)percorso; av[1] = a1; av[2] = a2; av[3] = a3; av[4] = NULL;
    pid = spawn_ex(percorso, av, environ, NULL, 0);
    if (pid < 0) return -1;
    if (waitpid(pid, &stato, 0) < 0) return -1;
    return WEXITSTATUS(stato);
}

/* ! A dhcp LEFT RUNNING (`dhcp &`, the boot script's renewer) holds UDP port
 * 68, and a second one cannot start: "non si puo' aprire la porta 68". It
 * was renewing a lease on the card we are leaving, so it is stopped first.
 * Found in QEMU on an installed system, 2026-10-14. */
static void dhcp_ferma_vecchi(void)
{
    ProcInfo v[PROCINFO_MAX_BATCH];
    unsigned int inizio = 0;
    int n, i, io = getpid();

    while ((n = procinfo(v, PROCINFO_MAX_BATCH, inizio)) > 0) {
        for (i = 0; i < n; i++) {
            const char *b = strrchr(v[i].name, '/');

            b = b ? b + 1 : v[i].name;
            if ((int)v[i].pid != io && strcmp(b, "dhcp") == 0) kill((int)v[i].pid, SIGTERM);
        }
        inizio += (unsigned int)n;
    }
    usleep(200000);
}

/* Move ip.drv onto a card ("wifi0", or back to "rete0") and get an address
 * there by DHCP. Used when the radio drops and the Ethernet is there. */
static int uscita_su(const char *link)
{
    int pid_ip = ipc_lookup(IP_SERVIZIO);

    if (pid_ip <= 0 || ipc_lookup(link) <= 0) return -1;
    if (ip_esito(pid_ip, IP_MSG_LINK, link, (unsigned int)strlen(link) + 1) != 0) return -1;
    dhcp_ferma_vecchi();
    return esegui("/bin/dhcp", NULL, NULL, NULL) == 0 ? 0 : -1;
}

static int indirizzo_prendi(const WifidProfilo *p)
{
    int pid_ip = ipc_lookup(IP_SERVIZIO), i;

    g_st.ip_ok = 0;
    if (pid_ip <= 0) {
        /* no IP stack yet: start it on the radio */
        char *av[4] = { "/dev/ip.drv", "-l", IP_LINK_WIFI, NULL };

        if (spawn_ex("/dev/ip.drv", av, environ, NULL, 0) < 0) return -1;
        for (i = 0; i < 50 && (pid_ip = ipc_lookup(IP_SERVIZIO)) <= 0; i++) usleep(100000);
        if (pid_ip <= 0) return -1;
    } else if (ip_esito(pid_ip, IP_MSG_LINK, IP_LINK_WIFI, (unsigned int)strlen(IP_LINK_WIFI) + 1) != 0) {
        return -1;
    }
    g_st.uscita = 1;

    if (p->ip_metodo == WIFID_IP_STATICO) {
        IpConfig c;

        memcpy(c.ip, p->ip, 4); memcpy(c.maschera, p->maschera, 4);
        memcpy(c.gateway, p->gateway, 4); memcpy(c.dns, p->dns, 4);
        if (ip_esito(pid_ip, IP_MSG_CONFIG, &c, sizeof(c)) != 0) return -1;
    } else {
        dhcp_ferma_vecchi();
        if (esegui("/bin/dhcp", NULL, NULL, NULL) != 0) return -1;
    }
    /* what the stack says now, for the state */
    {
        unsigned char buf[IPC_MSG_MAX_DATA];

        if (ipc_send((unsigned int)pid_ip, IP_MSG_STATO, NULL, 0) == 0 &&
            attendi_da(pid_ip, IP_MSG_STATO_R, IP_MSG_STATO_R, buf, NULL, NULL, 3000) == 0) {
            IpStato st;

            memcpy(&st, buf, sizeof(st));
            memcpy(g_st.ip, st.cfg.ip, 4); memcpy(g_st.maschera, st.cfg.maschera, 4);
            memcpy(g_st.gateway, st.cfg.gateway, 4); memcpy(g_st.dns, st.cfg.dns, 4);
        }
    }
    g_st.ip_ok = 1;
    g_st.ip_metodo = p->ip_metodo;
    return 0;
}

/* =============================================================================
 * Joining a network
 * ============================================================================= */
static int collega(const WifidProfilo *p, const Salvata *s, WifiEsito *e)
{
    WifiConnetti c;
    char         t[96];

    memset(&c, 0, sizeof(c));
    strncpy(c.ssid, p->ssid, WIFI_SSID_MAX - 1);
    c.auth = p->auth;
    c.nascosta = p->nascosta;
    if (p->parola[0]) {
        /* given now: derive the key, and send that - as for a saved one */
        if (p->auth == WIFI_AUTH_WEP) strncpy(c.parola, p->parola, sizeof(c.parola) - 1);
        else if (p->auth != WIFI_AUTH_APERTA) {
            pbkdf2_sha1((const unsigned char *)p->parola, (unsigned int)strlen(p->parola),
                        (const unsigned char *)p->ssid, (unsigned int)strlen(p->ssid), 4096, c.psk, 32);
            c.ha_psk = 1;
        }
    } else if (s && s->ha_segreto) {
        if (p->auth == WIFI_AUTH_WEP) memcpy(c.parola, s->segreto, s->segreto_n);
        else { memcpy(c.psk, s->segreto, 32); c.ha_psk = 1; }
    }

    g_st.radio.stato = WIFI_STATO_AGGANCIA;
    if (driver_esito(WIFI_MSG_CONNETTI, &c, sizeof(c), e) != 0) {
        snprintf(t, sizeof(t), "%s: %s", p->ssid, e->perche);
        evento(WIFID_EV_ERRORE, p->ssid, t);
        return -1;
    }
    g_attiva = *p;
    memset(g_attiva.parola, 0, sizeof(g_attiva.parola));
    g_collegata = 1;
    g_fermo = 0;
    driver_stato(&g_st.radio);               /* the state says "connected" at once */
    snprintf(t, sizeof(t), "Collegata a %s", p->ssid);
    evento(WIFID_EV_COLLEGATA, p->ssid, t);

    if (indirizzo_prendi(p) != 0) {
        snprintf(t, sizeof(t), "%s: collegata, ma senza indirizzo IP", p->ssid);
        evento(WIFID_EV_ERRORE, p->ssid, t);
        e->codice = WIFID_ERR_IP;
        strcpy(e->perche, "collegata, ma senza indirizzo IP (DHCP)");
        return 0;     /* the radio is on that network all the same */
    }
    {
        char a[16];

        ip_testo(a, g_st.ip);
        snprintf(t, sizeof(t), "%s: indirizzo %s", p->ssid, a);
        evento(WIFID_EV_IP, p->ssid, t);
    }
    return 0;
}

/* The automatic choice: the default if heard (or hidden), else the strongest
 * saved network that is heard. */
static void scegli_e_collega(void)
{
    WifiRete reti[32];
    int      n, i, migliore = -1, dbm = -1000;
    Salvata *def = NULL, *s;
    WifiEsito e;

    if (g_n_salvate == 0 || !driver_c_e()) return;
    for (i = 0; i < g_n_salvate; i++) if (g_salvate[i].p.predefinita) def = &g_salvate[i];

    n = driver_cerca(reti, 32);
    if (def && def->p.nascosta) { collega(&def->p, def, &e); return; }
    for (i = 0; i < n; i++) {
        s = salvata(reti[i].ssid);
        if (!s) continue;
        if (s == def) { collega(&def->p, def, &e); return; }
        if (reti[i].potenza > dbm) { dbm = reti[i].potenza; migliore = i; }
    }
    if (migliore >= 0) {
        s = salvata(reti[migliore].ssid);
        if (s->p.auth < 0) s->p.auth = reti[migliore].auth;
        collega(&s->p, s, &e);
    }
}

static void controlla(void)
{
    WifiStato r;

    g_st.scheda = driver_c_e();
    if (!g_st.scheda) { g_st.radio.stato = WIFI_STATO_SPENTA; return; }
    if (driver_stato(&r) != 0) return;
    g_st.radio = r;
    if (g_collegata && r.stato != WIFI_STATO_CONNESSA) {
        char t[96];

        g_collegata = 0;
        g_st.ip_ok = 0;
        snprintf(t, sizeof(t), "%s: connessione persa", g_attiva.ssid);
        evento(WIFID_EV_CADUTA, g_attiva.ssid, t);
        /* the rule: out through the connection that is up - if there is an
         * Ethernet card, back to it until the radio comes back */
        if (g_st.uscita && ipc_lookup(NET_SERVIZIO_0) > 0) {
            g_st.uscita = 0;
            if (uscita_su(NET_SERVIZIO_0) == 0) evento(WIFID_EV_IP, NULL, "Si esce di nuovo dalla scheda Ethernet");
        }
    }
}

/* =============================================================================
 * Requests
 * ============================================================================= */
static int puo_cambiare(int pid)
{
    int uid = proc_uid(pid);

    return uid == 0 || uid == g_mio_uid;
}

static void esito_a(int pid, int codice, const char *perche)
{
    WifiEsito e;

    memset(&e, 0, sizeof(e));
    e.codice = codice;
    strncpy(e.perche, perche, sizeof(e.perche) - 1);
    ipc_send((unsigned int)pid, WIFID_MSG_ESITO, &e, sizeof(e));
}

static void salva(const WifidProfilo *p)
{
    Salvata *s = salvata(p->ssid);
    int      i;

    if (!s) {
        if (g_n_salvate >= WIFID_SALVATE_MAX) return;
        s = &g_salvate[g_n_salvate++];
        memset(s, 0, sizeof(*s));
    }
    {
        int ha = s->ha_segreto;
        unsigned char seg[32]; unsigned int n = s->segreto_n;

        memcpy(seg, s->segreto, 32);
        s->p = *p;
        memset(s->p.parola, 0, sizeof(s->p.parola));
        s->p.salva = 0;
        s->ha_segreto = ha; memcpy(s->segreto, seg, 32); s->segreto_n = n;
    }
    if (p->parola[0]) {
        if (p->auth == WIFI_AUTH_WEP) {
            s->segreto_n = (unsigned int)strlen(p->parola);
            if (s->segreto_n > 32) s->segreto_n = 32;
            memcpy(s->segreto, p->parola, s->segreto_n);
        } else {
            pbkdf2_sha1((const unsigned char *)p->parola, (unsigned int)strlen(p->parola),
                        (const unsigned char *)p->ssid, (unsigned int)strlen(p->ssid), 4096, s->segreto, 32);
            s->segreto_n = 32;
        }
        s->ha_segreto = 1;
    }
    if (p->predefinita)
        for (i = 0; i < g_n_salvate; i++) if (&g_salvate[i] != s) g_salvate[i].p.predefinita = 0;
    cfg_scrivi();
}

static void richiesta(const IpcMessage *meta, const unsigned char *buf)
{
    int pid = (int)meta->sender_pid, i;

    switch (meta->tipo) {
    case WIFID_MSG_STATO:
        g_st.avvio = g_avvio;
        g_st.sistema = (g_mio_uid == 0 && g_avvio);
        g_st.proprietario = (unsigned int)g_mio_uid;
        ipc_send((unsigned int)pid, WIFID_MSG_STATO_R, &g_st, sizeof(g_st));
        return;

    case WIFID_MSG_CERCA: {
        WifiRete reti[32];
        int      n = driver_cerca(reti, 32);

        for (i = 0; i < n; i++) {
            WifidRete r;
            Salvata  *s = salvata(reti[i].ssid);

            r.rete = reti[i];
            r.salvata = s != NULL;
            r.predefinita = s ? s->p.predefinita : 0;
            ipc_send((unsigned int)pid, WIFID_MSG_RETE, &r, sizeof(r));
        }
        ipc_send((unsigned int)pid, WIFID_MSG_FINE, NULL, 0);
        return;
    }

    case WIFID_MSG_SALVATE:
        for (i = 0; i < g_n_salvate; i++)
            ipc_send((unsigned int)pid, WIFID_MSG_PROFILO, &g_salvate[i].p, sizeof(WifidProfilo));
        ipc_send((unsigned int)pid, WIFID_MSG_FINE, NULL, 0);
        return;

    case WIFID_MSG_ASCOLTA:
        for (i = 0; i < ASCOLTATORI_MAX; i++) if (g_ascolta[i] == pid) break;
        if (i == ASCOLTATORI_MAX)
            for (i = 0; i < ASCOLTATORI_MAX; i++) if (g_ascolta[i] == 0) { g_ascolta[i] = pid; break; }
        esito_a(pid, 0, "in ascolto");
        return;
    }

    /* --- from here on, requests that CHANGE something ------------------------- */
    if (!puo_cambiare(pid)) {
        esito_a(pid, WIFID_ERR_PERMESSO, g_mio_uid == 0
                ? "e' la WiFi di sistema: la cambia solo root (sudo)"
                : "questo gestore WiFi e' di un altro utente");
        return;
    }

    switch (meta->tipo) {
    case WIFID_MSG_CONNETTI: {
        WifidProfilo p;
        WifiEsito    e;
        Salvata     *s;

        memset(&p, 0, sizeof(p));
        memcpy(&p, buf, meta->len < sizeof(p) ? meta->len : sizeof(p));
        p.ssid[WIFI_SSID_MAX - 1] = 0;
        s = salvata(p.ssid);
        if (p.auth < 0 && s) p.auth = s->p.auth;
        if (!p.parola[0] && s && p.ip_metodo == WIFID_IP_DHCP && s->p.ip_metodo == WIFID_IP_STATICO)
            p = s->p;                       /* a saved network keeps its own settings */
        collega(&p, s, &e);
        /* saved when the RADIO joined: an address that failed (WIFID_ERR_IP)
         * is the network's problem of the moment, not a wrong password */
        if ((e.codice == 0 || e.codice == WIFID_ERR_IP) && p.salva) salva(&p);
        ipc_send((unsigned int)pid, WIFID_MSG_ESITO, &e, sizeof(e));
        memset(&p, 0, sizeof(p));
        return;
    }
    case WIFID_MSG_STACCA: {
        WifiEsito e;

        driver_esito(WIFI_MSG_STACCA, NULL, 0, &e);
        g_collegata = 0;
        g_fermo = 1;
        g_st.ip_ok = 0;
        evento(WIFID_EV_STACCATA, g_attiva.ssid, "WiFi scollegata");
        esito_a(pid, 0, "scollegata");
        return;
    }
    case WIFID_MSG_SALVA: {
        WifidProfilo p;

        memset(&p, 0, sizeof(p));
        memcpy(&p, buf, meta->len < sizeof(p) ? meta->len : sizeof(p));
        p.ssid[WIFI_SSID_MAX - 1] = 0;
        if (!salvata(p.ssid) && g_n_salvate >= WIFID_SALVATE_MAX) { esito_a(pid, WIFID_ERR_PIENO, "troppe reti salvate"); return; }
        salva(&p);
        memset(&p, 0, sizeof(p));
        esito_a(pid, 0, "salvata");
        return;
    }
    case WIFID_MSG_DIMENTICA:
    case WIFID_MSG_PREDEFINITA: {
        char ssid[WIFI_SSID_MAX];
        int  trovata = 0;

        memset(ssid, 0, sizeof(ssid));
        memcpy(ssid, buf, meta->len < sizeof(ssid) - 1 ? meta->len : sizeof(ssid) - 1);
        for (i = 0; i < g_n_salvate; i++) {
            if (meta->tipo == WIFID_MSG_PREDEFINITA) {
                g_salvate[i].p.predefinita = !strcmp(g_salvate[i].p.ssid, ssid);
                trovata |= g_salvate[i].p.predefinita;
            } else if (!strcmp(g_salvate[i].p.ssid, ssid)) {
                memmove(&g_salvate[i], &g_salvate[i + 1], (size_t)(g_n_salvate - i - 1) * sizeof(Salvata));
                g_n_salvate--;
                trovata = 1;
                break;
            }
        }
        cfg_scrivi();
        if (meta->tipo == WIFID_MSG_PREDEFINITA && !ssid[0]) esito_a(pid, 0, "nessuna rete predefinita");
        else esito_a(pid, trovata ? 0 : WIFI_ERR_NON_TROVATA, trovata ? "fatto" : "non e' una rete salvata");
        return;
    }
    case WIFID_MSG_AVVIO: {
        int si = 0;

        if (proc_uid(pid) != 0 || g_mio_uid != 0) {
            esito_a(pid, WIFID_ERR_PERMESSO, "il collegamento all'avvio e' di root: usa root");
            return;
        }
        memcpy(&si, buf, meta->len < sizeof(si) ? meta->len : sizeof(si));
        g_avvio = si != 0;
        cfg_scrivi();
        esito_a(pid, 0, g_avvio ? "collegamento all'avvio acceso" : "collegamento all'avvio spento");
        return;
    }
    default:
        return;
    }
}

/* =============================================================================
 * main
 * ============================================================================= */
int main(int argc, char **argv)
{
    int auto_avvio = (argc > 1 && strcmp(argv[1], "-auto") == 0), giro = 0;

    if (argc > 1 && !auto_avvio) {
        printf("uso: wifid            il gestore del WiFi (il comando `wifi` e l'icona della\n");
        printf("                      scrivania lo avviano quando non c'e')\n");
        printf("     wifid -auto      all'avvio, da root: solo se il collegamento all'avvio e' acceso\n");
        return strcmp(argv[1], "-h") == 0 ? 0 : 2;
    }

    g_mio_uid = getuid();
    if (g_mio_uid == 0) {
        mkdir("/cfg", 0755);
        mkdir("/cfg/wifi", 0700);
        strcpy(g_cfg, "/cfg/wifi/root.cfg");
        strcpy(g_chiave_file, "/cfg/wifi/root.key");
    } else {
        const char *casa = getenv("HOME");
        char        d[200];

        if (!casa || !casa[0]) casa = "/";
        snprintf(d, sizeof(d), "%s/.exwin", casa);         mkdir(d, 0700);
        snprintf(d, sizeof(d), "%s/.exwin/config", casa);  mkdir(d, 0700);
        snprintf(g_cfg, sizeof(g_cfg), "%s/.exwin/config/wifi.cfg", casa);
        snprintf(g_chiave_file, sizeof(g_chiave_file), "%s/.exwin/config/wifi.key", casa);
    }
    if (chiave_prepara() != 0)
        printf("wifid: non posso leggere ne' creare %s: le reti si salvano senza chiavi\n", g_chiave_file);
    cfg_leggi();

    if (auto_avvio && !(g_mio_uid == 0 && g_avvio)) return 0;     /* not asked: silent */

    if (ipc_register(WIFID_SERVIZIO) < 0) {
        printf("wifid: c'e' gia' un altro gestore del WiFi\n");
        return 1;
    }
    printf("wifid: avviato con uid %d, %d reti salvate%s\n", g_mio_uid, g_n_salvate,
           (g_mio_uid == 0 && g_avvio) ? ", collegamento all'avvio acceso" : "");
    if (auto_avvio) {
        int i;

        for (i = 0; i < 60 && !driver_c_e(); i++) usleep(500000);  /* the driver may come later */
    }
    controlla();
    if (!g_collegata) scegli_e_collega();

    for (;;) {
        IpcMessage    meta;
        unsigned char buf[IPC_MSG_MAX_DATA];

        if (ipc_scegli(filtro_tutto, NULL, &meta, buf, sizeof(buf), GIRO_MS) >= 0) {
            if ((int)meta.sender_pid != g_pid_wifi) richiesta(&meta, buf);
            memset(buf, 0, sizeof(buf));                           /* a passphrase may have been in it */
            continue;
        }
        giro++;
        if (giro % CONTROLLO_GIRI == 0) controlla();
        if (!g_collegata && !g_fermo && giro % RITENTA_GIRI == 0) scegli_e_collega();
    }
}
