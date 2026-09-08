/* =============================================================================
 * bin/wifi/wifi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * wifi — le reti senza fili: quali si sentono, a quale attaccarsi, come va.
 *
 *     wifi -showlist            l'elenco delle reti disponibili
 *     wifi -connect:<SSID>      si aggancia a quella rete
 *     wifi -status              come va la connessione di adesso
 *     wifi -stacca              si stacca
 *     wifi -h                   questo elenco
 *
 * ! QUESTO COMANDO NON SA CHE SCHEDA HA SOTTO, e non deve saperlo. Parla il
 * protocollo di drivers/net/wifi_proto.h con il servizio «wifi0», e chi
 * risponde puo' essere qualunque driver wireless — oggi uno, domani un altro.
 * E' la stessa divisione che c'e' fra ip.drv e le schede Ethernet, ed e' il
 * motivo per cui il comando si scrive una volta sola.
 *
 * ! LA PASSWORD LA CHIEDE QUESTO COMANDO, NON IL DRIVER, e non e' un
 * dettaglio: il driver non ha un terminale — gira come servizio, e in un
 * servizio non c'e' nessuno che legga una riga. Chi ha il terminale e' chi ha
 * scritto il comando, ed e' qui.
 * ============================================================================= */

#include "libc.h"
#include "exuser.h"
#include "wifi_proto.h"

/* +0.001 a ogni modifica: `wifi -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("wifi", "0.001");

static int pid_wifi = 0;

static void uso(void)
{
    printf("uso: wifi -showlist            le reti che si sentono\n");
    printf("     wifi -connect:<SSID>      si aggancia a quella rete\n");
    printf("     wifi -status              come va la connessione di adesso\n");
    printf("     wifi -stacca              si stacca dalla rete\n");
    printf("     wifi -h                   questo elenco\n");
    printf("\n");
    printf("La difesa della rete (WEP, WPA, WPA2, WPA3) NON si dice: la\n");
    printf("riconosce il driver dal beacon. La password si', quella la\n");
    printf("chiede -connect quando serve.\n");
}

/* Aspetta una risposta di un tipo preciso dal driver. Come in nettest: si
 * scartano i messaggi di altri e di altro tipo invece di confonderli con
 * quello atteso. */
static int attendi(unsigned int tipo, unsigned char *buf,
                   unsigned int *len, unsigned int ms)
{
    IpcMessage meta;
    int        giri;

    for (giri = 0; giri < 64; giri++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_wifi) continue;
        if (meta.tipo != tipo) continue;
        if (len) *len = meta.len;
        return 0;
    }
    return -1;
}

/* Come sopra, ma accetta due tipi: serve all'elenco, che finisce con FINE. */
static int attendi_due(unsigned int a, unsigned int b, unsigned char *buf,
                       unsigned int *len, unsigned int ms, unsigned int *quale)
{
    IpcMessage meta;
    int        giri;

    for (giri = 0; giri < 64; giri++) {
        if (ipc_recv_timeout(&meta, buf, IPC_MSG_MAX_DATA, ms) < 0) return -1;
        if ((int)meta.sender_pid != pid_wifi) continue;
        if (meta.tipo != a && meta.tipo != b) continue;
        if (len)   *len = meta.len;
        if (quale) *quale = meta.tipo;
        return 0;
    }
    return -1;
}

static void stampa_mac(const unsigned char *m)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* ! LA POTENZA IN dBm NON DICE NIENTE A CHI GUARDA, e sono i numeri che
 * arrivano dalla radio: -30 e' ottimo, -90 e' quasi niente. Accanto al numero
 * si scrive quanto vale, perche' chi cerca dove mettere il portatile vuole
 * sapere «forte o debole», non un decibel. */
static const char *quanto_forte(int dbm)
{
    if (dbm >= -50) return "ottima";
    if (dbm >= -60) return "buona";
    if (dbm >= -70) return "discreta";
    if (dbm >= -80) return "debole";
    return "quasi niente";
}

static int comando_showlist(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len, tipo;
    int           quante = 0;

    if (ipc_send(pid_wifi, WIFI_MSG_CERCA, NULL, 0) < 0) {
        printf("wifi: non riesco a chiedere la ricerca.\n");
        return 1;
    }

    printf("Cerco le reti...\n\n");
    printf("  %-32s %-6s %-9s %-5s %s\n", "rete", "canale", "difesa",
           "dBm", "segnale");

    /* ! LA PRIMA ATTESA E' LUNGA, LE ALTRE NO. Una ricerca vera passa tutti i
     * canali e ci mette qualche secondo; una volta cominciata, le reti
     * arrivano una dietro l'altra. Un'unica attesa lunga per tutte
     * renderebbe questo comando immobile per un minuto sul primo guasto. */
    for (;;) {
        WifiRete r;

        if (attendi_due(WIFI_MSG_RETI, WIFI_MSG_FINE, buf, &len,
                        quante == 0 ? 10000 : 2000, &tipo) != 0) {
            printf("\nwifi: il driver ha smesso di rispondere.\n");
            return 1;
        }
        if (tipo == WIFI_MSG_FINE) break;
        if (len < sizeof(r)) continue;

        memcpy(&r, buf, sizeof(r));
        r.ssid[WIFI_SSID_MAX - 1] = '\0';

        /* Una rete puo' non dire il proprio nome: e' legittimo, e stampare
         * una riga vuota farebbe pensare a un difetto di questo comando. */
        printf("  %-32s %-6d %-9s %-5d %s\n",
               r.ssid[0] ? r.ssid : "(nome nascosto)",
               r.canale, wifi_auth_nome(r.auth), r.potenza,
               quanto_forte(r.potenza));
        quante++;
    }

    printf("\n");
    if (quante == 0) {
        printf("Non si sente niente. La radio e' accesa? `wifi -status`.\n");
        return 1;
    }
    printf("%d ret%s. Per attaccarsi:  wifi -connect:<nome>\n",
           quante, quante == 1 ? "e" : "i");
    return 0;
}

static int comando_status(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    WifiStato     s;

    if (ipc_send(pid_wifi, WIFI_MSG_STATO, NULL, 0) < 0) return 1;
    if (attendi(WIFI_MSG_STATO_R, buf, &len, 3000) != 0 || len < sizeof(s)) {
        printf("wifi: il driver non risponde.\n");
        return 1;
    }
    memcpy(&s, buf, sizeof(s));
    s.ssid[WIFI_SSID_MAX - 1] = '\0';
    s.scheda[sizeof(s.scheda) - 1] = '\0';

    printf("scheda      %s\n", s.scheda[0] ? s.scheda : "(senza nome)");
    printf("stato       %s\n", wifi_stato_nome(s.stato));

    if (s.stato != WIFI_STATO_CONNESSA) {
        /* ! QUANDO NON C'E' CONNESSIONE NON SI STAMPANO ZERI. Un elenco di
         * zeri sembra una connessione andata male; «non c'e'» e' un'altra
         * cosa e si legge in un colpo d'occhio. */
        printf("\nNessuna connessione. Per farne una:  wifi -showlist\n");
        return 0;
    }

    printf("connessa a  %s  (", s.ssid[0] ? s.ssid : "(nome nascosto)");
    stampa_mac(s.bssid);
    printf(")\n");
    printf("difesa      %s\n", wifi_auth_nome(s.auth));
    printf("canale      %d\n", s.canale);
    printf("segnale     %d dBm (%s)\n", s.potenza, quanto_forte(s.potenza));
    printf("velocita'   %u Mbit/s\n", s.velocita);
    printf("inviati     %u byte\n", s.byte_inviati);
    printf("ricevuti    %u byte\n", s.byte_ricevuti);
    return 0;
}

static int comando_connect(const char *ssid)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    WifiConnetti  c;
    WifiEsito     e;
    WifiRete      trovata;
    unsigned int  tipo;
    int           vista = 0;

    if (ssid == NULL || ssid[0] == '\0') {
        printf("uso: wifi -connect:<nome della rete>\n");
        return 1;
    }

    memset(&c, 0, sizeof(c));
    strncpy(c.ssid, ssid, sizeof(c.ssid) - 1);

    /* --- prima si guarda se quella rete si sente, e come si difende -------
     *
     * ! SI CERCA PRIMA DI CHIEDERE LA PASSWORD, e l'ordine conta: chiederla
     * per una rete che non c'e' vuol dire far scrivere una password a vuoto,
     * e per una rete aperta vuol dire chiedere una cosa che non serve. La
     * difesa la dice il beacon, ed e' li' che si legge. */
    if (ipc_send(pid_wifi, WIFI_MSG_CERCA, NULL, 0) < 0) return 1;

    printf("Cerco «%s»...\n", ssid);
    memset(&trovata, 0, sizeof(trovata));
    for (;;) {
        WifiRete r;

        if (attendi_due(WIFI_MSG_RETI, WIFI_MSG_FINE, buf, &len,
                        vista ? 2000 : 10000, &tipo) != 0) break;
        if (tipo == WIFI_MSG_FINE) break;
        if (len < sizeof(r)) continue;

        memcpy(&r, buf, sizeof(r));
        r.ssid[WIFI_SSID_MAX - 1] = '\0';
        if (strcmp(r.ssid, ssid) == 0) { trovata = r; vista = 1; }
    }

    if (!vista) {
        printf("\nwifi: «%s» non si sente.\n", ssid);
        printf("      L'elenco di quelle che si sentono:  wifi -showlist\n");
        return 1;
    }

    printf("  trovata: canale %d, %s, %d dBm (%s)\n",
           trovata.canale, wifi_auth_nome(trovata.auth),
           trovata.potenza, quanto_forte(trovata.potenza));

    if (trovata.auth != WIFI_AUTH_APERTA) {
        printf("\n  password (non si vede mentre la scrivi): ");
        fflush(stdout);
        /* ! LA PASSWORD NON SI RISTAMPA MAI, nemmeno per confermare quel che
         * si e' scritto. Uno schermo e' un posto pubblico piu' spesso di
         * quanto sembri, e una password letta da dietro le spalle e' persa
         * come una password scritta in un registro. */
        /* ! LA CONSOLE VA PRESA PRIMA DI LEGGERE I TASTI, e se il modo
         * grezzo non si prende exuser NON ripiega su una lettura normale:
         * mostrare una password perche' non si e' potuto nasconderla e'
         * peggio che rifiutarsi. Qui si rifiuta, e si dice. */
        exuser_prendi_console();
        if (exuser_leggi_password(c.parola, sizeof(c.parola)) < 0) {
            printf("\nwifi: non riesco a chiedere la password senza\n");
            printf("      mostrarla a schermo. Non ho letto niente.\n");
            return 1;
        }
        printf("\n");
    }

    if (ipc_send(pid_wifi, WIFI_MSG_CONNETTI, &c, sizeof(c)) < 0) {
        printf("wifi: non riesco a chiedere la connessione.\n");
        return 1;
    }

    /* Trenta secondi: l'aggancio, l'autenticazione e lo scambio delle chiavi
     * con un access point lento ci stanno dentro; oltre, e' fermo. */
    if (attendi(WIFI_MSG_ESITO, buf, &len, 30000) != 0 || len < sizeof(e)) {
        printf("wifi: nessuna risposta dal driver.\n");
        return 1;
    }
    memcpy(&e, buf, sizeof(e));
    e.perche[sizeof(e.perche) - 1] = '\0';

    if (e.codice != 0) {
        printf("wifi: non connessa: %s\n",
               e.perche[0] ? e.perche : wifi_perche(e.codice));
        return 1;
    }

    printf("Connessa a «%s».\n", ssid);
    printf("Adesso serve un indirizzo:  dhcp\n");
    return 0;
}

static int comando_stacca(void)
{
    unsigned char buf[IPC_MSG_MAX_DATA];
    unsigned int  len;
    WifiEsito     e;

    if (ipc_send(pid_wifi, WIFI_MSG_STACCA, NULL, 0) < 0) return 1;
    if (attendi(WIFI_MSG_ESITO, buf, &len, 5000) != 0 || len < sizeof(e)) {
        printf("wifi: il driver non risponde.\n");
        return 1;
    }
    memcpy(&e, buf, sizeof(e));
    printf("%s\n", e.codice == 0 ? "Staccata." : wifi_perche(e.codice));
    return e.codice == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "--help") == 0) {
        uso();
        return argc < 2 ? 1 : 0;
    }

    /* ! IL SERVIZIO SI CERCA UNA VOLTA SOLA, E SE NON C'E' SI DICE COSA
     * MANCA. «wifi0 non risponde» da solo manda a cercare un guasto nella
     * scheda; il piu' delle volte il driver non e' stato avviato, ed e' una
     * riga da scrivere, non un guasto. */
    pid_wifi = ipc_lookup(WIFI_SERVIZIO_0);
    if (pid_wifi <= 0) {
        printf("wifi: il servizio '%s' non e' attivo.\n\n", WIFI_SERVIZIO_0);
        printf("  Serve un driver wireless avviato. Se la scheda c'e',\n");
        printf("  `netdetect` dice quale driver vuole e se e' presente.\n");
        return 1;
    }

    if (strcmp(argv[1], "-showlist") == 0) return comando_showlist();
    if (strcmp(argv[1], "-status") == 0)   return comando_status();
    if (strcmp(argv[1], "-stacca") == 0)   return comando_stacca();
    if (strncmp(argv[1], "-connect:", 9) == 0)
        return comando_connect(argv[1] + 9);

    /* ! «-connect» SENZA I DUE PUNTI E' UN ERRORE FREQUENTE, e merita una
     * riga sua invece dell'elenco intero: chi lo scrive sa gia' cosa vuole
     * fare, gli manca solo la forma. */
    if (strcmp(argv[1], "-connect") == 0) {
        printf("uso: wifi -connect:<SSID>   (col nome attaccato ai due punti)\n");
        return 1;
    }

    printf("wifi: «%s» non e' un'opzione.\n\n", argv[1]);
    uso();
    return 1;
}
