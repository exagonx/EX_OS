/* =============================================================================
 * drivers/ar5k/ar5k_radio.c — the HP laptop's wireless switch, done by hand
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * On the user's HP laptop (Atheros card with PCI subsystem 103c:137b) the
 * radio is powered by the embedded controller, not by the card. Read from
 * the laptop's own DSDT on 15 October 2026 (sonda.drv -acpi):
 *
 *   - the WiFi button makes the EC raise query 0x10; _Q10 flips WLSW, bit 7
 *     of byte 0xF6 of the extended CMOS (ports 0x72/0x73), and then writes
 *     EC.WLON = WLSW (WLAS is 0 and WLSL is 1 from power-on);
 *   - EC.WLON is bit 0 of byte 0x71 of the EC's RAM, reached with the ACPI
 *     EC protocol on ports 0x62 (data) and 0x66 (command/status);
 *   - WLIN (bit 0 of CMOS 0xF6) says a WLAN card is fitted.
 *
 * EX-OS runs no AML and never asks the EC for its queries, so the button
 * does nothing. -radio only reads; -radio:on does what _Q10 does when the
 * switch goes to "on". Both refuse any other machine: these addresses are
 * this firmware's, and on another one they would mean something else.
 * ============================================================================= */
#include "libc.h"

#define EC_DATI     0x62
#define EC_COMANDO  0x66
#define   EC_OBF    0x01        /* output buffer full: a byte to read      */
#define   EC_IBF    0x02        /* input buffer full: the EC is still busy */
#define   EC_SCI    0x20        /* SCI_EVT: a query is waiting             */
#define EC_LEGGI    0x80
#define EC_SCRIVI   0x81

#define CMOS_IND    0x72
#define CMOS_DATI   0x73
#define CMOS_WLAN   0xF6
#define   WLIN      0x01
#define   WLWI      0x08
#define   WLSW      0x80

#define EC_WLON_BYTE 0x71
#define   EC_WLON    0x01
#define EC_LED_BYTE  0x0A       /* WLLD, bits 2-3 */

static int ec_attendi(int bit, int acceso)
{
    unsigned int t0 = uptime_ms();

    while (uptime_ms() - t0 < 200) {
        int s = ioport_in(EC_COMANDO);
        if (s < 0) return -1;
        if (((s & bit) != 0) == acceso) return 0;
    }
    return -1;
}

static int ec_leggi(unsigned int ind)
{
    if (ec_attendi(EC_IBF, 0)) return -1;
    ioport_out(EC_COMANDO, EC_LEGGI);
    if (ec_attendi(EC_IBF, 0)) return -1;
    ioport_out(EC_DATI, ind);
    if (ec_attendi(EC_OBF, 1)) return -1;
    return ioport_in(EC_DATI);
}

static int ec_scrivi(unsigned int ind, unsigned int val)
{
    if (ec_attendi(EC_IBF, 0)) return -1;
    ioport_out(EC_COMANDO, EC_SCRIVI);
    if (ec_attendi(EC_IBF, 0)) return -1;
    ioport_out(EC_DATI, ind);
    if (ec_attendi(EC_IBF, 0)) return -1;
    ioport_out(EC_DATI, val);
    return ec_attendi(EC_IBF, 0);
}

static int cmos_leggi(unsigned int ind)
{
    ioport_out(CMOS_IND, ind);
    return ioport_in(CMOS_DATI);
}

static void cmos_scrivi(unsigned int ind, unsigned int val)
{
    ioport_out(CMOS_IND, ind);
    ioport_out(CMOS_DATI, val);
}

static int stato(void)
{
    int c = cmos_leggi(CMOS_WLAN), w = ec_leggi(EC_WLON_BYTE), l = ec_leggi(EC_LED_BYTE);
    int s = ioport_in(EC_COMANDO);

    printf("ar5k: CMOS 0xF6 = 0x%02x: scheda %s, WLWI %d, interruttore (WLSW) %s\n",
           c & 0xff, c & WLIN ? "presente" : "ASSENTE", (c & WLWI) != 0, c & WLSW ? "acceso" : "spento");
    if (w < 0 || l < 0) {
        printf("ar5k: l'EC non risponde (stato 0x%02x)\n", s & 0xff);
        return -1;
    }
    printf("ar5k: EC 0x71 = 0x%02x: radio (WLON) %s;  EC 0x0A = 0x%02x: LED WLLD %d\n",
           w, w & EC_WLON ? "ACCESA" : "spenta", l, (l >> 2) & 3);
    printf("ar5k: stato EC 0x%02x%s\n", s & 0xff,
           s & EC_SCI ? " - ha eventi in coda (le pressioni del tasto: nessuno li chiede)" : "");
    return c;
}

int ar5k_radio(int accendi, unsigned int sotto_id)
{
    int c, w;

    if (sotto_id != 0x137b103cu) {
        printf("ar5k: -radio vale solo per il portatile HP con la scheda 103c:137b:\n");
        printf("      gli indirizzi di EC e CMOS sono quelli del suo firmware.\n");
        return 1;
    }
    if (ioport_bind(EC_DATI, 1) || ioport_bind(EC_COMANDO, 1) || ioport_bind(CMOS_IND, 2)) {
        printf("ar5k: ioport_bind rifiutata: serve root\n");
        return 1;
    }
    c = stato();
    if (c < 0 || !accendi) return c < 0;
    if (!(c & WLIN)) { printf("ar5k: il firmware dice che la scheda non c'e': non tocco niente\n"); return 1; }

    /* what _Q10 does when the switch goes on: WLSW = 1, then EC.WLON = WLSW */
    printf("ar5k: accendo come il tasto: WLSW = 1, poi EC.WLON = 1\n");
    cmos_scrivi(CMOS_WLAN, (unsigned int)(c | WLSW) & 0xff);
    w = ec_leggi(EC_WLON_BYTE);
    if (w < 0 || ec_scrivi(EC_WLON_BYTE, (unsigned int)(w | EC_WLON) & 0xff) != 0) {
        printf("ar5k: scrittura nell'EC non riuscita: rimetto la CMOS com'era\n");
        cmos_scrivi(CMOS_WLAN, (unsigned int)c & 0xff);
        return 1;
    }
    usleep(200000);                     /* _Q10 sleeps 200 ms after it */
    return stato() < 0;
}

/* =============================================================================
 * -tasto: the WiFi button, served for a minute as an ACPI system would
 *
 * When SCI_EVT is set the EC holds a query: QR_EC (0x84) on the command port
 * returns its number on the data port. 0x10 is the button: _Q10 flips WLSW
 * and copies it into WLON; 0x29 with EC byte 0x56 (WMID) = 5 calls _Q10 too.
 * The queries left from earlier presses are drained first WITHOUT acting on
 * them - a day of presses would toggle the radio over and over. Any other
 * query is printed and dropped: nothing in EX-OS handles them today either.
 * ============================================================================= */
#define EC_QUERY     0x84
#define EC_WMID_BYTE 0x56

static int ec_query(void)
{
    if (ec_attendi(EC_IBF, 0)) return -1;
    ioport_out(EC_COMANDO, EC_QUERY);
    if (ec_attendi(EC_OBF, 1)) return -1;
    return ioport_in(EC_DATI);
}

static void come_q10(void)
{
    int c = cmos_leggi(CMOS_WLAN), w = ec_leggi(EC_WLON_BYTE), sw;

    if (c < 0 || w < 0) return;
    sw = (c & WLSW) ? 0 : 1;
    cmos_scrivi(CMOS_WLAN, (unsigned int)(sw ? (c | WLSW) : (c & ~WLSW)) & 0xff);
    ec_scrivi(EC_WLON_BYTE, (unsigned int)(sw ? (w | EC_WLON) : (w & ~EC_WLON)) & 0xff);
    printf("ar5k: tasto WiFi -> radio %s\n", sw ? "ACCESA" : "spenta");
}

int ar5k_tasto(unsigned int secondi, unsigned int sotto_id)
{
    unsigned int t0;
    int q, n = 0;

    if (sotto_id != 0x137b103cu) {
        printf("ar5k: -tasto vale solo per il portatile HP con la scheda 103c:137b\n");
        return 1;
    }
    if (ioport_bind(EC_DATI, 1) || ioport_bind(EC_COMANDO, 1) || ioport_bind(CMOS_IND, 2)) {
        printf("ar5k: ioport_bind rifiutata: serve root\n");
        return 1;
    }
    while ((ioport_in(EC_COMANDO) & EC_SCI) && n < 64) {
        q = ec_query();
        if (q < 0) break;
        if (q == 0) break;                      /* 0: no query after all */
        n++;
    }
    printf("ar5k: %d eventi vecchi dell'EC tolti dalla coda senza eseguirli\n", n);
    stato();
    printf("ar5k: per %u secondi premo io il tasto per il firmware: prova a premerlo\n", secondi);

    t0 = uptime_ms();
    while (uptime_ms() - t0 < secondi * 1000u) {
        if (!(ioport_in(EC_COMANDO) & EC_SCI)) { usleep(50000); continue; }
        q = ec_query();
        if (q <= 0) { usleep(50000); continue; }
        if (q == 0x10) { come_q10(); continue; }
        if (q == 0x29) {
            int wm = ec_leggi(EC_WMID_BYTE);
            if (wm == 5) { come_q10(); continue; }
            printf("ar5k: evento EC 0x29, WMID %d: ignorato\n", wm);
            continue;
        }
        printf("ar5k: evento EC 0x%02x: ignorato\n", q);
    }
    stato();
    return 0;
}

/* --- for the wifi0 service (ar5k_servizio.c) ------------------------------------- */

/* 1 when this is the HP laptop and the EC/CMOS ports were granted */
int ar5k_ec_prendi(unsigned int sotto_id)
{
    if (sotto_id != 0x137b103cu) return 0;
    return ioport_bind(EC_DATI, 1) == 0 && ioport_bind(EC_COMANDO, 1) == 0 &&
           ioport_bind(CMOS_IND, 2) == 0;
}

int ar5k_ec_radio_accesa(void)
{
    int w = ec_leggi(EC_WLON_BYTE);
    return w >= 0 && (w & EC_WLON);
}

/* what -radio:on does, quietly: WLSW = 1, then WLON = 1 */
int ar5k_ec_accendi(void)
{
    int c = cmos_leggi(CMOS_WLAN), w;

    if (c < 0 || !(c & WLIN)) return -1;
    cmos_scrivi(CMOS_WLAN, (unsigned int)(c | WLSW) & 0xff);
    w = ec_leggi(EC_WLON_BYTE);
    if (w < 0 || ec_scrivi(EC_WLON_BYTE, (unsigned int)(w | EC_WLON) & 0xff) != 0) {
        cmos_scrivi(CMOS_WLAN, (unsigned int)c & 0xff);
        return -1;
    }
    return 0;
}

/* the queries queued before the service started: dropped, not acted on */
int ar5k_ec_svuota(void)
{
    int q, n = 0;

    while ((ioport_in(EC_COMANDO) & EC_SCI) && n < 64) {
        q = ec_query();
        if (q <= 0) break;
        n++;
    }
    return n;
}

/* one look at the EC: 1 when the button switched the radio */
int ar5k_ec_servi(void)
{
    int q;

    if (!(ioport_in(EC_COMANDO) & EC_SCI)) return 0;
    q = ec_query();
    if (q == 0x10 || (q == 0x29 && ec_leggi(EC_WMID_BYTE) == 5)) { come_q10(); return 1; }
    return 0;
}

/* WLLD, bits 2-3 of EC byte 0x0A: declared by the firmware, never written
 * by it. Read and written only by the interactive LED test. */
int ar5k_ec_led_leggi(void)
{
    int l = ec_leggi(EC_LED_BYTE);
    return l < 0 ? -1 : (l >> 2) & 3;
}

int ar5k_ec_led_scrivi(int v)
{
    int l = ec_leggi(EC_LED_BYTE);

    if (l < 0) return -1;
    return ec_scrivi(EC_LED_BYTE, (unsigned int)((l & ~0x0c) | ((v & 3) << 2)) & 0xff);
}
