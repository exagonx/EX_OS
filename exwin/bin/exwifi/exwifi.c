/* =============================================================================
 * exwin/bin/exwifi/exwifi.c — the WiFi panel of the desktop (@WIFI)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Opened by a click on the WiFi icon of the bar (exwin/bin/pm), like the
 * volume panel; it places itself above the bar, at the right. What the user
 * asked for (2026-10-14):
 *   - the networks the radio hears, with signal, security, "saved", "default";
 *   - join one: password, save it, make it the default, DHCP or a static
 *     address;
 *   - a HIDDEN network: SSID, security and everything else typed by hand
 *     (the same form, with "Rete nascosta" ticked);
 *   - when connected, the state and "Disconnetti"; picking another network
 *     and "Collegati" moves to it;
 *   - the saved networks: make one the default, forget one;
 *   - root only: connect at start-up, before login.
 *
 * It is a face of wifid (bin/wifid, drivers/net/wifid_proto.h), exactly like
 * the `wifi` command: the same requests, the same permissions - wifid refuses
 * a change to root's system connection from a user, and the panel shows the
 * reason it gives.
 *
 * ! THE REQUESTS ARE SYNCHRONOUS, and a connection with DHCP can take some
 * seconds: the panel says "Collegamento in corso..." before asking, so a
 * window that does not move for a moment has said why.
 * ============================================================================= */
#include "libc.h"
#include "exwin.h"
#include "kbd_proto.h"
#include "wifi_proto.h"
#include "wifid_proto.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("exwifi", VERSIONE_APP);

#define FIN_W       420
#define FIN_H       456      /* + 26 for root's start-up line: fits 800x600 above the bar */
#define BARRA_H      28
#define RETI_MAX     32

enum {
    ID_LISTA = 10, ID_AGGIORNA, ID_COLLEGA, ID_STACCA, ID_DIMENTICA, ID_PREDEFINITA,
    ID_NASCOSTA, ID_SALVA, ID_DEFAULT, ID_DHCP, ID_STATICO, ID_AVVIO, ID_SICUREZZA
};

static ExWindow  g_f, g_stato1, g_stato2, g_stato3, g_lista, g_msg;
static ExWindow  g_ssid, g_sicurezza, g_parola, g_nascosta, g_salva, g_default;
static ExWindow  g_dhcp, g_statico, g_ip, g_masch, g_gw, g_dns, g_avvio, g_stacca;
static WifidRete g_reti[RETI_MAX];
static int       g_n = 0;
static int       g_pid = 0;
static WifidStato g_st;

/* the security list of the form, in this order */
static const int g_auth_voci[4] = { WIFI_AUTH_WPA2, WIFI_AUTH_WPA, WIFI_AUTH_WEP, WIFI_AUTH_APERTA };

/* =============================================================================
 * wifid
 * ============================================================================= */
static int wifid_trova(void)
{
    int i;

    g_pid = ipc_lookup(WIFID_SERVIZIO);
    if (g_pid > 0) return 0;
    {
        char *av[2] = { "/bin/wifid", 0 };

        spawn_ex(av[0], av, environ, 0, 0);
    }
    for (i = 0; i < 30 && (g_pid = ipc_lookup(WIFID_SERVIZIO)) <= 0; i++) usleep(100000);
    return g_pid > 0 ? 0 : -1;
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

static void messaggio(const char *t)
{
    ex_set_text(g_msg, t);
    ex_update(g_f);
}

/* the last answer of wifid, put back on the bottom line after the list is
 * refreshed (refreshing writes "Scegli una rete..." over it) */
static char g_esito[64];

static int chiedi(unsigned int tipo, const void *dati, unsigned int n, unsigned int ms)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    WifiEsito     e;

    if (g_pid <= 0 || ipc_send((unsigned int)g_pid, tipo, dati, n) < 0) {
        messaggio("Il gestore WiFi (wifid) non risponde.");
        return -1;
    }
    if (attendi(WIFID_MSG_ESITO, WIFID_MSG_ESITO, buf, NULL, NULL, ms) != 0) {
        messaggio("Nessuna risposta dal gestore WiFi.");
        return -1;
    }
    memcpy(&e, buf, sizeof(e));
    e.perche[sizeof(e.perche) - 1] = 0;
    messaggio(e.perche);
    strncpy(g_esito, e.perche, sizeof(g_esito) - 1);
    return e.codice;
}

/* =============================================================================
 * The state at the top
 * ============================================================================= */
static void stato_aggiorna(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    char          t[120];

    memset(&g_st, 0, sizeof(g_st));
    if (g_pid <= 0 || ipc_send((unsigned int)g_pid, WIFID_MSG_STATO, 0, 0) < 0 ||
        attendi(WIFID_MSG_STATO_R, WIFID_MSG_STATO_R, buf, NULL, NULL, 2000) != 0) {
        ex_set_text(g_stato1, "Il gestore WiFi non risponde.");
        ex_set_text(g_stato2, "");
        ex_set_text(g_stato3, "");
        return;
    }
    memcpy(&g_st, buf, sizeof(g_st));
    if (!g_st.scheda) {
        ex_set_text(g_stato1, "Nessuna scheda WiFi (nessun driver 'wifi0').");
        ex_set_text(g_stato2, "");
        ex_set_text(g_stato3, "");
    } else if (g_st.radio.stato == WIFI_STATO_CONNESSA) {
        snprintf(t, sizeof(t), "Connesso a %s", g_st.radio.ssid);
        ex_set_text(g_stato1, t);
        snprintf(t, sizeof(t), "Segnale %d dBm   velocita' %u Mbit/s   %s", g_st.radio.potenza,
                 g_st.radio.velocita, wifi_auth_nome(g_st.radio.auth));
        ex_set_text(g_stato2, t);
        if (g_st.ip_ok)
            snprintf(t, sizeof(t), "Indirizzo %u.%u.%u.%u (%s)", g_st.ip[0], g_st.ip[1], g_st.ip[2],
                     g_st.ip[3], g_st.ip_metodo == WIFID_IP_STATICO ? "statico" : "DHCP");
        else
            snprintf(t, sizeof(t), "Nessun indirizzo IP");
        ex_set_text(g_stato3, t);
    } else {
        snprintf(t, sizeof(t), "WiFi: %s", wifi_stato_nome(g_st.radio.stato));
        ex_set_text(g_stato1, t);
        ex_set_text(g_stato2, g_st.sistema ? "Connessione di sistema: si cambia solo da root." : "");
        ex_set_text(g_stato3, "");
    }
    ex_enable(g_stacca, g_st.radio.stato == WIFI_STATO_CONNESSA);
    if (g_avvio) ex_set_checked(g_avvio, g_st.avvio);
}

/* =============================================================================
 * The list
 * ============================================================================= */
static void lista_aggiorna(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len, quale;
    int           i;

    messaggio("Cerco le reti...");
    ex_list_clear(g_lista);
    g_n = 0;
    if (g_pid > 0 && ipc_send((unsigned int)g_pid, WIFID_MSG_CERCA, 0, 0) == 0)
        while (attendi(WIFID_MSG_RETE, WIFID_MSG_FINE, buf, &len, &quale, g_n ? 5000 : 20000) == 0 &&
               quale != WIFID_MSG_FINE)
            if (len >= sizeof(WifidRete) && g_n < RETI_MAX) memcpy(&g_reti[g_n++], buf, sizeof(WifidRete));
    for (i = 0; i < g_n; i++) {
        char r[96], barre[6];
        int  b = (g_reti[i].rete.potenza + 100) / 10, k;

        for (k = 0; k < 5; k++) barre[k] = k < b ? '|' : '.';
        barre[5] = 0;
        snprintf(r, sizeof(r), "%-22s %s %-6s %s%s", g_reti[i].rete.ssid, barre,
                 wifi_auth_nome(g_reti[i].rete.auth), g_reti[i].salvata ? "salvata" : "",
                 g_reti[i].predefinita ? " *" : "");
        ex_list_add(g_lista, r);
    }
    messaggio(g_n ? "Scegli una rete, poi Collegati." : "Nessuna rete si sente.");
}

/* a row picked: fill the form with what is known of that network */
static void rete_scelta(void)
{
    unsigned int i = ex_list_get_selected(g_lista), k;

    if (i >= (unsigned int)g_n) return;
    ex_set_text(g_ssid, g_reti[i].rete.ssid);
    for (k = 0; k < 4; k++) if (g_auth_voci[k] == g_reti[i].rete.auth) ex_item_select(g_sicurezza, k);
    ex_set_checked(g_nascosta, 0);
    ex_set_text(g_parola, "");
    messaggio(g_reti[i].salvata ? "Rete salvata: la password non serve."
                                : (g_reti[i].rete.auth == WIFI_AUTH_APERTA ? "Rete aperta: niente password."
                                                                         : "Scrivi la password."));
}

static int ip_da(ExWindow c, unsigned char *ip)
{
    unsigned int a, b, d, e;

    if (sscanf(ex_get_text(c), "%u.%u.%u.%u", &a, &b, &d, &e) != 4 || a > 255 || b > 255 || d > 255 || e > 255)
        return 0;
    ip[0] = (unsigned char)a; ip[1] = (unsigned char)b; ip[2] = (unsigned char)d; ip[3] = (unsigned char)e;
    return 1;
}

static void statico_abilita(void)
{
    int si = ex_is_checked(g_statico);

    ex_enable(g_ip, si); ex_enable(g_masch, si); ex_enable(g_gw, si); ex_enable(g_dns, si);
}

/* "Collegati": the form becomes a WifidProfilo */
static void collega(void)
{
    WifidProfilo p;
    unsigned int k = ex_item_get_selected(g_sicurezza);

    memset(&p, 0, sizeof(p));
    strncpy(p.ssid, ex_get_text(g_ssid), WIFI_SSID_MAX - 1);
    if (!p.ssid[0]) { messaggio("Scegli una rete dall'elenco, o scrivi il nome (SSID)."); return; }
    p.auth = k < 4 ? g_auth_voci[k] : -1;
    strncpy(p.parola, ex_get_text(g_parola), sizeof(p.parola) - 1);
    p.nascosta = ex_is_checked(g_nascosta);
    p.salva = ex_is_checked(g_salva) || ex_is_checked(g_default);
    p.predefinita = ex_is_checked(g_default);
    p.ip_metodo = ex_is_checked(g_statico) ? WIFID_IP_STATICO : WIFID_IP_DHCP;
    if (p.ip_metodo == WIFID_IP_STATICO &&
        !(ip_da(g_ip, p.ip) && ip_da(g_masch, p.maschera) && ip_da(g_gw, p.gateway) && ip_da(g_dns, p.dns))) {
        messaggio("Indirizzo statico: scrivi i quattro numeri in ogni campo.");
        return;
    }
    messaggio("Collegamento in corso...");
    g_esito[0] = 0;
    if (chiedi(WIFID_MSG_CONNETTI, &p, sizeof(p), 90000) == 0)
        snprintf(g_esito, sizeof(g_esito), "Collegata a %s.", p.ssid);
    memset(&p, 0, sizeof(p));
    ex_set_text(g_parola, "");
    stato_aggiorna();
    lista_aggiorna();
    if (g_esito[0]) messaggio(g_esito);
}

static void su_scelta(unsigned int tipo)
{
    unsigned int i = ex_list_get_selected(g_lista);

    if (i >= (unsigned int)g_n || !g_reti[i].salvata) { messaggio("Scegli una rete SALVATA dall'elenco."); return; }
    g_esito[0] = 0;
    chiedi(tipo, g_reti[i].rete.ssid, WIFI_SSID_MAX, 5000);
    lista_aggiorna();
    if (g_esito[0]) messaggio(g_esito);
}

/* =============================================================================
 * The window
 * ============================================================================= */
static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        ex_quit(0);
        return 0;
    case EXM_TIMER:
        stato_aggiorna();
        ex_update(g_f);
        return 0;
    case EXM_KEY:
        if ((wp & KBD_KEY_MASK) == 27) { ex_quit(0); return 0; }
        return ex_default_proc(f, msg, wp, lp);
    case EXM_COMMAND:
        switch (wp) {
        case ID_LISTA:       rete_scelta(); if (EX_IS_OPEN(lp)) collega(); break;
        case ID_AGGIORNA:    lista_aggiorna(); break;
        case ID_COLLEGA:     collega(); break;
        case ID_STACCA:      chiedi(WIFID_MSG_STACCA, 0, 0, 10000); stato_aggiorna(); break;
        case ID_DIMENTICA:   su_scelta(WIFID_MSG_DIMENTICA); break;
        case ID_PREDEFINITA: su_scelta(WIFID_MSG_PREDEFINITA); break;
        case ID_DHCP:
        case ID_STATICO:     statico_abilita(); break;
        case ID_NASCOSTA:
            if (ex_is_checked(g_nascosta)) {
                ex_set_text(g_ssid, "");
                messaggio("Rete nascosta: scrivi nome, sicurezza e password.");
            }
            break;
        case ID_AVVIO: {
            int v = ex_is_checked(g_avvio);

            if (chiedi(WIFID_MSG_AVVIO, &v, sizeof(v), 5000) != 0) ex_set_checked(g_avvio, !v);
            break;
        }
        default: break;
        }
        ex_update(g_f);
        return 0;
    }
    return ex_default_proc(f, msg, wp, lp);
}

static ExWindow etichetta(const char *t, int x, int y, int w)
{
    return ex_create("label", t, EX_CHILD, x, y, w, 18, g_f, 0, 0);
}

int main(void)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          y, h = FIN_H;

    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("exwifi: il server a finestre non risponde. Avvialo con:  exwin\n");
        return 1;
    }
    if (getuid() == 0) h += 26;
    g_f = ex_create("window", "WiFi", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    (int)sw - FIN_W - 6, (int)sh - BARRA_H - h - 6, FIN_W, h, 0, 0, proc);
    if (!g_f) return 1;

    y = 26;
    g_stato1 = etichetta("", 10, y, FIN_W - 20);      y += 18;
    g_stato2 = etichetta("", 10, y, FIN_W - 20);      y += 18;
    g_stato3 = etichetta("", 10, y, FIN_W - 20);      y += 22;
    g_stacca = ex_create("button", "Disconnetti", EX_CHILD, 10, y, 96, 22, g_f, ID_STACCA, 0);
    ex_create("button", "Aggiorna", EX_CHILD, 110, y, 96, 22, g_f, ID_AGGIORNA, 0);
    ex_create("button", "Predefinita", EX_CHILD, 210, y, 96, 22, g_f, ID_PREDEFINITA, 0);
    ex_create("button", "Dimentica", EX_CHILD, 310, y, 96, 22, g_f, ID_DIMENTICA, 0);
    y += 28;

    g_lista = ex_create("lista", "", EX_CHILD, 10, y, FIN_W - 20, 96, g_f, ID_LISTA, 0);
    y += 102;

    etichetta("Rete (SSID)", 10, y + 2, 90);
    g_ssid = ex_create("testo", "", EX_CHILD, 104, y, 180, 22, g_f, 0, 0);
    g_nascosta = ex_create("checkbox", "Nascosta", EX_CHILD, 292, y + 1, 110, 20, g_f, ID_NASCOSTA, 0);
    y += 28;
    etichetta("Sicurezza", 10, y + 2, 90);
    g_sicurezza = ex_create("combo", "", EX_CHILD, 104, y, 180, 22, g_f, ID_SICUREZZA, 0);
    ex_item_add(g_sicurezza, "WPA2");
    ex_item_add(g_sicurezza, "WPA");
    ex_item_add(g_sicurezza, "WEP");
    ex_item_add(g_sicurezza, "aperta");
    ex_item_select(g_sicurezza, 0);
    y += 28;
    etichetta("Password", 10, y + 2, 90);
    g_parola = ex_create("testo", "", EX_CHILD | EX_PASSWORD, 104, y, 180, 22, g_f, 0, 0);
    y += 28;
    g_salva = ex_create("checkbox", "Salva la rete", EX_CHILD, 10, y, 140, 20, g_f, ID_SALVA, 0);
    g_default = ex_create("checkbox", "Rete predefinita", EX_CHILD, 160, y, 160, 20, g_f, ID_DEFAULT, 0);
    ex_set_checked(g_salva, 1);
    y += 24;
    g_dhcp = ex_create("radio", "DHCP", EX_CHILD, 10, y, 80, 20, g_f, ID_DHCP, 0);
    g_statico = ex_create("radio", "IP statico", EX_CHILD, 100, y, 110, 20, g_f, ID_STATICO, 0);
    ex_set_checked(g_dhcp, 1);
    y += 24;
    etichetta("IP", 10, y + 2, 20);
    g_ip = ex_create("testo", "", EX_CHILD, 32, y, 110, 22, g_f, 0, 0);
    etichetta("Masch.", 150, y + 2, 50);
    g_masch = ex_create("testo", "255.255.255.0", EX_CHILD, 202, y, 110, 22, g_f, 0, 0);
    y += 26;
    etichetta("GW", 10, y + 2, 20);
    g_gw = ex_create("testo", "", EX_CHILD, 32, y, 110, 22, g_f, 0, 0);
    etichetta("DNS", 150, y + 2, 50);
    g_dns = ex_create("testo", "", EX_CHILD, 202, y, 110, 22, g_f, 0, 0);
    statico_abilita();
    y += 30;
    ex_create("button", "Collegati", EX_CHILD, 10, y, 110, 24, g_f, ID_COLLEGA, 0);
    y += 30;
    if (getuid() == 0) {
        g_avvio = ex_create("checkbox", "Collegati all'avvio, prima del login", EX_CHILD,
                            10, y, FIN_W - 20, 20, g_f, ID_AVVIO, 0);
        y += 26;
    }
    g_msg = etichetta("", 10, y, FIN_W - 20);

    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_update(g_f);

    if (wifid_trova() != 0) messaggio("Il gestore WiFi (wifid) non parte.");
    stato_aggiorna();
    lista_aggiorna();
    ex_update(g_f);
    ex_set_timer(g_f, 3000);

    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
