/* =============================================================================
 * drivers/ar5k/ar5k_scan.c — stage 2: radio up at 2.4 GHz, a passive scan
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Uses the hardware code of Linux's ath5k (drivers/ar5k/ath5k, ISC) as it is:
 * ath5k_hw_init reads the EEPROM and the capabilities, ath5k_hw_reset brings
 * MAC, baseband and radio up on a channel, and the receive descriptors are
 * ath5k's. The bus functions below (EEPROM read, MAC, cache line) are
 * ath5k's pci.c.
 *
 * NOTHING IS TRANSMITTED: the scan only listens for beacons and probe
 * responses, 150 ms per channel, channels 1 to 13. At the end the chip is
 * put back in reset and asleep, as the BIOS left it.
 * ============================================================================= */
#include "ar5k_hw.h"
#include "wifi_proto.h"
#include "ath5k/reg.h"

#define RX_N        32
#define RX_BUF      2400
#define RETI_MAX    48

typedef struct {
    u8  bssid[6];
    char ssid[33];
    int canale;
    int dbm;
    int privacy;        /* capability bit: WEP or better */
    int wpa, rsn;       /* WPA (vendor IE) and WPA2 (RSN) elements */
} Rete;

static Rete g_reti[RETI_MAX];
static int  g_n;

/* the wifi0 service scans on every request: there it says nothing */
static int  g_zitto;
#define DIRE(...) do { if (!g_zitto) printf(__VA_ARGS__); } while (0)

/* the LED: Linux's ath5k led.c lists, by PCI subsystem, which GPIO drives the
 * laptop's WiFi light; the user's HP (103c:137b, "HP Compaq C700") has it on
 * pin 3, lit at level 0. It stays orange until a driver lights it. */
static int g_led_pin = -1, g_led_on;

/* ath5k_rfkill_hw_start (Linux's rfkill.c, ISC): the EEPROM's radio-switch
 * pin is driven to the level that ALLOWS the radio; when the EEPROM says a
 * real switch is wired to it, the pin goes back to input and is read. Then
 * the LED. Called after the first reset, as base.c does (reset keeps GPIOs). */
void ar5k_radio_e_led(struct ath5k_hw *ah)
{
    struct ath5k_eeprom_info *ee = &ah->ah_capabilities.cap_eeprom;
    u32 pin = ee->ee_rfkill_pin, pol = ee->ee_rfkill_pol ? 1 : 0;

    ath5k_hw_set_gpio_output(ah, pin);
    ath5k_hw_set_gpio(ah, pin, !pol);
    if (AR5K_EEPROM_HDR_RFKILL(ee->ee_header)) {
        ath5k_hw_set_gpio_input(ah, pin);
        DIRE("ar5k: interruttore radio sul GPIO %u (polarita' %u): %s\n", pin, pol,
               ath5k_hw_get_gpio(ah, pin) == pol ? "radio BLOCCATA dall'interruttore" : "radio permessa");
    } else {
        DIRE("ar5k: nessun interruttore radio nell'EEPROM: GPIO %u tenuto a %u (radio permessa)\n",
               pin, !pol);
    }
    if (g_led_pin >= 0) {
        ath5k_hw_set_gpio_output(ah, (u32)g_led_pin);
        ath5k_hw_set_gpio(ah, (u32)g_led_pin, (u32)g_led_on);
        DIRE("ar5k: LED acceso (GPIO %d a %d)\n", g_led_pin, g_led_on);
    }
}

/* --- ath5k's pci.c (ISC) -------------------------------------------------------- */
static bool pci_eeprom_read(struct ath_common *common, u32 offset, u16 *data)
{
    struct ath5k_hw *ah = (struct ath5k_hw *)common->ah;
    u32 status, timeout;

    ath5k_hw_reg_write(ah, offset, AR5K_EEPROM_BASE);
    AR5K_REG_ENABLE_BITS(ah, AR5K_EEPROM_CMD, AR5K_EEPROM_CMD_READ);
    for (timeout = AR5K_TUNE_REGISTER_TIMEOUT; timeout > 0; timeout--) {
        status = ath5k_hw_reg_read(ah, AR5K_EEPROM_STATUS);
        if (status & AR5K_EEPROM_STAT_RDDONE) {
            if (status & AR5K_EEPROM_STAT_RDERR) return false;
            *data = (u16)(ath5k_hw_reg_read(ah, AR5K_EEPROM_DATA) & 0xffff);
            return true;
        }
        usleep_range(15, 20);
    }
    return false;
}

static int pci_eeprom_read_mac(struct ath5k_hw *ah, u8 *mac)
{
    struct ath_common *common = ath5k_hw_common(ah);
    u8  mac_d[ETH_ALEN] = { 0 };
    u32 total = 0;
    int offset, octet = 0;
    u16 data;

    for (offset = 0x1f; offset >= 0x1d; offset--) {
        if (!pci_eeprom_read(common, (u32)offset, &data)) return -EIO;
        total += data;
        mac_d[octet + 1] = data & 0xff;
        mac_d[octet] = data >> 8;
        octet += 2;
    }
    if (!total || total == 3 * 0xffff) return -EINVAL;
    memcpy(mac, mac_d, ETH_ALEN);
    return 0;
}

static void pci_read_cachesize(struct ath_common *common, int *csz)
{
    (void)common;
    *csz = 64 >> 2;                     /* 64-byte lines, as on PCI Express */
}

static const struct ath_bus_ops g_bus = {
    .ath_bus_type    = ATH_PCI,
    .read_cachesize  = pci_read_cachesize,
    .eeprom_read     = pci_eeprom_read,
    .eeprom_read_mac = pci_eeprom_read_mac,
};

/* --- the frames ---------------------------------------------------------------- */
static void annota(const u8 *f, int n, int canale_rx, int dbm)
{
    int   sotto, i, canale = canale_rx;
    const u8 *ie, *fine;
    Rete  r;

    if (n < 24 + 12) return;
    if ((f[0] & 0x0c) != 0x00) return;              /* not management */
    sotto = f[0] >> 4;
    if (sotto != 8 && sotto != 5) return;           /* beacon, probe response */

    memset(&r, 0, sizeof(r));
    memcpy(r.bssid, f + 16, 6);
    r.privacy = (f[24 + 10] & 0x10) != 0;           /* capability: privacy */
    ie = f + 36;
    fine = f + n - 4;                               /* the FCS is at the end */
    while (ie + 2 <= fine && ie + 2 + ie[1] <= fine) {
        if (ie[0] == 0 && ie[1] <= 32) { memcpy(r.ssid, ie + 2, ie[1]); r.ssid[ie[1]] = 0; }
        if (ie[0] == 3 && ie[1] == 1) canale = ie[2];
        if (ie[0] == 48) r.rsn = 1;
        if (ie[0] == 221 && ie[1] >= 4 && ie[2] == 0x00 && ie[3] == 0x50 && ie[4] == 0xf2 && ie[5] == 1) r.wpa = 1;
        ie += 2 + ie[1];
    }
    r.canale = canale;
    r.dbm = dbm;

    for (i = 0; i < g_n; i++)
        if (memcmp(g_reti[i].bssid, r.bssid, 6) == 0) {
            if (dbm > g_reti[i].dbm) g_reti[i].dbm = dbm;
            return;
        }
    if (g_n < RETI_MAX) g_reti[g_n++] = r;
}

/* one channel: reset onto it, listen, read what arrived */
static int ascolta(struct ath5k_hw *ah, DmaZona *z, int canale, int ms, int *frame)
{
    struct ieee80211_channel ch;
    struct ath5k_desc *d = (struct ath5k_desc *)(uintptr_t)z->virt;
    u32   d_fis = z->fisico;
    u8   *buf = (u8 *)(uintptr_t)z->virt + 4096;
    u32   buf_fis = z->fisico + 4096;
    int   i, r, letto = 0, t;

    ar5k_canale_g(&ch, canale);
    ah->ah_current_channel = &ch;

    r = ath5k_hw_reset(ah, NL80211_IFTYPE_STATION, &ch, false, false);
    if (r != 0) { printf("ar5k: canale %d: ath5k_hw_reset = %d\n", canale, r); return r; }
    if (canale == 1) ar5k_radio_e_led(ah);

    for (i = 0; i < RX_N; i++) {
        memset(&d[i], 0, sizeof(d[i]));
        d[i].ds_link = i + 1 < RX_N ? d_fis + (u32)((i + 1) * sizeof(d[0])) : 0;
        d[i].ds_data = buf_fis + (u32)(i * RX_BUF);
        ath5k_hw_setup_rx_desc(ah, &d[i], RX_BUF, 0);
    }
    ath5k_hw_set_rxdp(ah, d_fis);
    ath5k_hw_start_rx_dma(ah);
    /* promiscuous: only listening, and any frame at all says the radio and
     * the DMA work; annota() keeps the beacons and probe responses */
    ath5k_hw_set_rx_filter(ah, AR5K_RX_FILTER_UCAST | AR5K_RX_FILTER_MCAST | AR5K_RX_FILTER_BCAST |
                               AR5K_RX_FILTER_BEACON | AR5K_RX_FILTER_PROBEREQ | AR5K_RX_FILTER_PROM);
    ath5k_hw_set_mcast_filter(ah, 0xffffffff, 0xffffffff);
    /* the profile counters (cleared by writing 0, as ani.c does): cycles,
     * cycles spent receiving, and cycles with the medium busy - they say
     * whether the radio hears anything at all, frames or not */
    ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_TX);
    ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_RX);
    ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_RXCLR);
    ath5k_hw_reg_write(ah, 0, AR5K_PROFCNT_CYCLE);
    ath5k_hw_start_rx_pcu(ah);
    if (canale == 1)
        DIRE("ar5k:   CR 0x%08x RXDP 0x%08x (scritto 0x%08x) filtro 0x%08x DIAG 0x%08x\n",
               ath5k_hw_reg_read(ah, AR5K_CR), ath5k_hw_reg_read(ah, AR5K_RXDP), d_fis,
               ath5k_hw_reg_read(ah, AR5K_RX_FILTER), ath5k_hw_reg_read(ah, AR5K_DIAG_SW));

    /* the window is measured, not counted: usleep(1000) is a 10 ms tick */
    for (t = (int)uptime_ms(); (int)(uptime_ms() - (unsigned)t) < ms && letto < RX_N; ) {
        struct ath5k_rx_status rs;

        memset(&rs, 0, sizeof(rs));
        r = ah->ah_proc_rx_desc(ah, &d[letto], &rs);
        if (r == -EINPROGRESS) { sched_yield(); continue; }   /* others run meanwhile */
        if (r == 0 && rs.rs_status == 0 && rs.rs_datalen > 0) {
            annota(buf + letto * RX_BUF, rs.rs_datalen, canale, rs.rs_rssi + ah->ah_noise_floor);
            (*frame)++;
        }
        letto++;
    }
    {
        u32 cic = ath5k_hw_reg_read(ah, AR5K_PROFCNT_CYCLE);
        u32 rx  = ath5k_hw_reg_read(ah, AR5K_PROFCNT_RX);
        u32 occ = ath5k_hw_reg_read(ah, AR5K_PROFCNT_RXCLR);

        DIRE("ar5k:   canale %2d: %d descr.  cicli %u rx %u occupato %u  RXDP 0x%08x"
               " st0 0x%08x st1 0x%08x PISR 0x%08x\n",
               canale, letto, cic, rx, occ, ath5k_hw_reg_read(ah, AR5K_RXDP),
               d[0].ud.ds_rx.rx_stat.rx_status_0, d[0].ud.ds_rx.rx_stat.rx_status_1,
               ath5k_hw_reg_read(ah, AR5K_PISR));
    }
    ath5k_hw_stop_rx_pcu(ah);
    ath5k_hw_dma_stop(ah);
    return 0;
}

void ar5k_canale_g(struct ieee80211_channel *ch, int n)
{
    memset(ch, 0, sizeof(*ch));
    ch->band        = NL80211_BAND_2GHZ;
    ch->center_freq = n == 14 ? 2484 : 2407 + 5 * n;
    ch->hw_value    = AR5K_MODE_11G;
}

int ar5k_apri(Ar5kChip *k, volatile void *registri, unsigned int sotto_id)
{
    static struct pci_dev pdev = { 1 };
    struct ath5k_hw *ah = calloc(1, sizeof(*ah));
    int r;

    memset(k, 0, sizeof(*k));
    if (!ah) return -ENOMEM;
    k->ah               = ah;
    ah->iobase          = (void __iomem *)registri;
    ah->pdev            = &pdev;
    ah->devid           = 0x001c;
    ah->opmode          = NL80211_IFTYPE_STATION;
    ah->common.ah       = ah;
    ah->common.bus_ops  = &g_bus;
    memset(ah->common.bssidmask, 0xff, ETH_ALEN);       /* as ath5k's init */
    if (sotto_id == 0x137b103cu) { g_led_pin = 3; g_led_on = 0; }    /* led.c: HP C700 */
    if (sotto_id == 0x137a103cu) { g_led_pin = 3; g_led_on = 1; }    /* led.c: HP CQ60  */

    k->reset0  = ath5k_hw_reg_read(ah, AR5K_RESET_CTL);
    k->sonno0  = ath5k_hw_reg_read(ah, AR5K_SLEEP_CTL);
    k->gpiocr0 = ath5k_hw_reg_read(ah, AR5K_GPIOCR);
    k->gpiodo0 = ath5k_hw_reg_read(ah, AR5K_GPIODO);
    DIRE("ar5k: sottosistema %04x:%04x, GPIO trovati: modo 0x%08x uscite 0x%08x ingressi 0x%08x\n",
           sotto_id & 0xffff, sotto_id >> 16, k->gpiocr0, k->gpiodo0, ath5k_hw_reg_read(ah, AR5K_GPIODI));

    r = ath5k_hw_init(ah);
    if (r != 0) { printf("ar5k: ath5k_hw_init = %d\n", r); return r; }
    if (pci_eeprom_read_mac(ah, k->mac) == 0) {
        memcpy(ah->common.macaddr, k->mac, ETH_ALEN);
        ath5k_hw_set_lladdr(ah, k->mac);
    }
    DIRE("ar5k: %s, radio 0x%02x, EEPROM %x.%x, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           ah->ah_radio == AR5K_RF2425 ? "AR2425/RF2425" : "AR5212",
           ah->ah_radio_5ghz_revision,
           ah->ah_capabilities.cap_eeprom.ee_version >> 12,
           ah->ah_capabilities.cap_eeprom.ee_version & 0xfff,
           k->mac[0], k->mac[1], k->mac[2], k->mac[3], k->mac[4], k->mac[5]);
    return 0;
}

void ar5k_chiudi(Ar5kChip *k)
{
    struct ath5k_hw *ah = k->ah;

    if (!ah) return;
    ath5k_hw_reg_write(ah, k->gpiocr0, AR5K_GPIOCR);    /* LED and switch pin as found */
    ath5k_hw_reg_write(ah, k->gpiodo0, AR5K_GPIODO);
    ath5k_hw_reg_write(ah, k->reset0, AR5K_RESET_CTL);
    usleep(20);
    ath5k_hw_reg_write(ah, k->sonno0, AR5K_SLEEP_CTL);
    DIRE("ar5k: chip rimesso in reset e a riposo (reset 0x%x)\n", ath5k_hw_reg_read(ah, AR5K_RESET_CTL));
}

int ar5k_scansiona(volatile void *registri, unsigned int sotto_id)
{
    Ar5kChip k;
    struct ath5k_hw *ah;
    DmaZona z;
    int     c, frame = 0, i;

    if (ar5k_apri(&k, registri, sotto_id) != 0) goto rimetti;
    ah = k.ah;

    z.byte = 4096 + RX_N * RX_BUF;
    z.virt = 0; z.fisico = 0;
    if (dma_alloc(&z) != 0) { printf("ar5k: dma_alloc(%u) rifiutata\n", z.byte); goto rimetti; }

    printf("ar5k: ascolto i canali 1-13, 150 ms ciascuno (nessuna trasmissione)\n");
    ath5k_hw_set_ledstate(ah, AR5K_LED_SCAN);
    for (c = 1; c <= 13; c++)
        if (ascolta(ah, &z, c, 150, &frame) != 0) break;

    printf("ar5k: %d frame utili, %d reti\n", frame, g_n);
    printf("   SSID                              BSSID              can   dBm  sicurezza\n");
    for (i = 0; i < g_n; i++) {
        Rete *n = &g_reti[i];
        printf("   %-33s %02x:%02x:%02x:%02x:%02x:%02x  %3d  %4d  %s\n",
               n->ssid[0] ? n->ssid : "(nascosta)",
               n->bssid[0], n->bssid[1], n->bssid[2], n->bssid[3], n->bssid[4], n->bssid[5],
               n->canale, n->dbm,
               n->rsn ? "WPA2" : n->wpa ? "WPA" : n->privacy ? "WEP" : "aperta");
    }

    /* the light: three seconds in the "associated" state, to see whether
     * the laptop's WiFi light is the card's LED pin */
    ath5k_hw_set_ledstate(ah, AR5K_LED_ASSOC);
    printf("ar5k: LED della scheda nello stato \"associata\" per 3 secondi (PCICFG 0x%08x)\n",
           ath5k_hw_reg_read(ah, AR5K_PCICFG));
    usleep(3000000);

rimetti:
    ar5k_chiudi(&k);
    return 0;
}

/* =============================================================================
 * -led: which output the laptop's WiFi light follows (15 October 2026)
 *
 * The light stayed orange with the radio on, with GPIO 3 driven low (Linux's
 * led.c entry for 103c:137b) and with PCICFG in the "associated" LED state;
 * the firmware never writes it. Four seconds a step were too fast to watch
 * (the user): each candidate now stays until the person at the console
 * answers whether the light changed. Candidates: GPIO 1 to 5 low and high
 * (GPIO 0 is the radio switch pin, left alone), the three PCICFG LED states
 * and, on the HP laptop, the four values of the EC's WLLD. All put back as
 * found; the answers go to the screen and to /root/ar5k-led.txt.
 * ============================================================================= */
int ar5k_ec_prendi(unsigned int sotto_id);
int ar5k_ec_led_leggi(void);
int ar5k_ec_led_scrivi(int v);

#define LED_PASSI 24
static char g_led_nome[LED_PASSI][48];
static char g_led_risp[LED_PASSI];
static int  g_led_n;

/* 's', 'n' or 'q' from the console */
static int chiedi(const char *nome)
{
    char c = 0, x;
    int  i = g_led_n;

    if (i >= LED_PASSI) return 'q';
    snprintf(g_led_nome[i], sizeof(g_led_nome[i]), "%s", nome);
    for (;;) {
        printf("  passo %2d: %-34s la spia e' cambiata? (s/n, q = basta) ", i + 1, nome);
        c = 0;
        while (read(0, &x, 1) == 1 && x != '\n' && x != '\r') if (!c && x != ' ') c = x;
        if (c == 'S') c = 's';
        if (c == 'N') c = 'n';
        if (c == 'Q') c = 'q';
        if (c == 's' || c == 'n' || c == 'q') break;
    }
    if (c != 'q') { g_led_risp[i] = c; g_led_n++; }
    return c;
}

int ar5k_prova_led(volatile void *registri, unsigned int sotto_id)
{
    static const struct { u32 stato; const char *nome; } STATI[] = {
        { AR5K_LED_INIT,  "scheda: LED nessuno stato" },
        { AR5K_LED_SCAN,  "scheda: LED in scansione" },
        { AR5K_LED_ASSOC, "scheda: LED associata" },
    };
    struct ieee80211_channel ch;
    Ar5kChip k;
    struct ath5k_hw *ah;
    u32 pin, liv, cr, dout;
    int i, r, ec, wlld0 = -1, fermo = 0;
    char nome[48];
    FILE *f;

    g_led_n = 0;
    ec = ar5k_ec_prendi(sotto_id);
    if (ar5k_apri(&k, registri, sotto_id) != 0) goto fine;
    ah = k.ah;
    ar5k_canale_g(&ch, 1);
    ah->ah_current_channel = &ch;
    r = ath5k_hw_reset(ah, NL80211_IFTYPE_STATION, &ch, false, false);
    if (r != 0) { printf("ar5k: ath5k_hw_reset = %d\n", r); goto fine; }
    ar5k_radio_e_led(ah);
    cr   = ath5k_hw_reg_read(ah, AR5K_GPIOCR);
    dout = ath5k_hw_reg_read(ah, AR5K_GPIODO);

    printf("ar5k: guarda la spia del WiFi. Ogni passo resta finche' non rispondi.\n");
    for (pin = 1; pin < AR5K_NUM_GPIO && !fermo; pin++)
        for (liv = 0; liv <= 1 && !fermo; liv++) {
            ath5k_hw_reg_write(ah, cr, AR5K_GPIOCR);
            ath5k_hw_reg_write(ah, dout, AR5K_GPIODO);
            ath5k_hw_set_gpio_output(ah, pin);
            ath5k_hw_set_gpio(ah, pin, liv);
            snprintf(nome, sizeof(nome), "scheda: GPIO %u a %u", pin, liv);
            if (chiedi(nome) == 'q') fermo = 1;
        }
    ath5k_hw_reg_write(ah, cr, AR5K_GPIOCR);
    ath5k_hw_reg_write(ah, dout, AR5K_GPIODO);
    for (i = 0; i < 3 && !fermo; i++) {
        ath5k_hw_set_ledstate(ah, STATI[i].stato);
        if (chiedi(STATI[i].nome) == 'q') fermo = 1;
    }
    if (ec && !fermo) {
        wlld0 = ar5k_ec_led_leggi();
        for (i = 0; i < 4 && !fermo && wlld0 >= 0; i++) {
            ar5k_ec_led_scrivi(i);
            snprintf(nome, sizeof(nome), "EC: WLLD = %d (era %d)", i, wlld0);
            if (chiedi(nome) == 'q') fermo = 1;
        }
        if (wlld0 >= 0) ar5k_ec_led_scrivi(wlld0);
    }

    printf("\nar5k: riepilogo della prova del LED\n");
    f = fopen("/root/ar5k-led.txt", "w");
    for (i = 0; i < g_led_n; i++) {
        printf("  passo %2d: %-34s %s\n", i + 1, g_led_nome[i],
               g_led_risp[i] == 's' ? "CAMBIATA" : "niente");
        if (f) fprintf(f, "passo %2d: %-34s %s\n", i + 1, g_led_nome[i],
                       g_led_risp[i] == 's' ? "CAMBIATA" : "niente");
    }
    if (f) { fclose(f); printf("ar5k: scritto anche in /root/ar5k-led.txt\n"); }
fine:
    ar5k_chiudi(&k);
    return 0;
}

/* =============================================================================
 * ar5k_cerca: one quiet scan for the wifi0 service (ar5k_servizio.c)
 *
 * The chip is opened, every channel listened to for 120 ms, and the chip put
 * back in reset and asleep - between two requests no DMA is running, so the
 * driver may be stopped at any moment. z is the service's receive area.
 * ============================================================================= */
int ar5k_cerca(volatile void *registri, unsigned int sotto_id, DmaZona *z,
               WifiRete *out, int max)
{
    Ar5kChip k;
    int c, frame = 0, i, n = 0;

    g_zitto = 1;
    g_n = 0;
    if (ar5k_apri(&k, registri, sotto_id) == 0) {
        ath5k_hw_set_ledstate(k.ah, AR5K_LED_SCAN);
        for (c = 1; c <= 13; c++)
            if (ascolta(k.ah, z, c, 120, &frame) != 0) break;
    }
    ar5k_chiudi(&k);
    if (k.ah) free(k.ah);
    g_zitto = 0;

    for (i = 0; i < g_n && n < max; i++) {
        if (!g_reti[i].ssid[0]) continue;               /* hidden: not listed */
        memset(&out[n], 0, sizeof(out[n]));
        strncpy(out[n].ssid, g_reti[i].ssid, WIFI_SSID_MAX - 1);
        memcpy(out[n].bssid, g_reti[i].bssid, 6);
        out[n].canale  = g_reti[i].canale;
        out[n].potenza = g_reti[i].dbm;
        out[n].auth    = g_reti[i].rsn ? WIFI_AUTH_WPA2 : g_reti[i].wpa ? WIFI_AUTH_WPA :
                         g_reti[i].privacy ? WIFI_AUTH_WEP : WIFI_AUTH_APERTA;
        out[n].velocita_max = 54;                       /* 802.11g */
        n++;
    }
    return n;
}

/* the MAC from the EEPROM, the chip put back at once (for the service) */
int ar5k_leggi_mac(volatile void *registri, unsigned int sotto_id, u8 mac[ETH_ALEN])
{
    Ar5kChip k;
    int r;

    g_zitto = 1;
    r = ar5k_apri(&k, registri, sotto_id);
    if (r == 0) memcpy(mac, k.mac, ETH_ALEN);
    ar5k_chiudi(&k);
    if (k.ah) free(k.ah);
    g_zitto = 0;
    return r;
}

/* =============================================================================
 * ar5k_spia: the laptop's WiFi light, for the wifi0 service
 *
 * Found with `ar5k.drv -led` on 15 October 2026: the light is the card's
 * GPIO 3, blue when driven low and orange when high or left as input -
 * exactly Linux's led.c entry for 103c:137b. The GPIO block keeps its state
 * while the chip is in reset and asleep, so the service sets it after each
 * scan (ar5k_chiudi puts the BIOS's GPIOs back) and on every button press.
 * Only the LED pin is touched; plain MMIO, no ath5k state needed.
 * ============================================================================= */
void ar5k_spia(volatile void *registri, unsigned int sotto_id, int accesa)
{
    volatile u32 *r = (volatile u32 *)registri;
    u32 pin, on;

    if (sotto_id == 0x137b103cu)      { pin = 3; on = 0; }
    else if (sotto_id == 0x137a103cu) { pin = 3; on = 1; }
    else return;
    r[AR5K_GPIOCR / 4] = (r[AR5K_GPIOCR / 4] & ~AR5K_GPIOCR_OUT(pin)) | AR5K_GPIOCR_OUT(pin);
    r[AR5K_GPIODO / 4] = (r[AR5K_GPIODO / 4] & ~(1u << pin)) | ((accesa ? on : !on) << pin);
}

/* =============================================================================
 * -led:ec: the light's colour for every combination of its two drivers
 *
 * The user wants the light to follow the scheme of other devices: off = radio
 * off, orange = on and not connected, blinking blue = connecting, steady blue
 * = connected. The light is the card's GPIO 3 together with the EC's WLLD;
 * each of the 8 combinations is held until the person at the console says
 * what they see. Everything is put back as found; the answers also go to
 * /root/ar5k-led.txt.
 * ============================================================================= */
int ar5k_prova_led_ec(volatile void *registri, unsigned int sotto_id)
{
    static const char *NOMI[] = { "spenta", "arancione", "blu fisso", "blu lampeggiante" };
    volatile u32 *r = (volatile u32 *)registri;
    u32 cr0, do0;
    int wlld0, g, w, n = 0, fermo = 0;
    char risposte[8];
    FILE *f;

    if (sotto_id != 0x137b103cu) { printf("ar5k: -led:ec vale solo per il portatile HP 103c:137b\n"); return 1; }
    if (!ar5k_ec_prendi(sotto_id)) { printf("ar5k: le porte dell'EC non si prendono: serve root\n"); return 1; }
    wlld0 = ar5k_ec_led_leggi();
    if (wlld0 < 0) { printf("ar5k: l'EC non risponde\n"); return 1; }
    cr0 = r[AR5K_GPIOCR / 4];
    do0 = r[AR5K_GPIODO / 4];

    printf("ar5k: guarda la spia. Per ogni passo scrivi che cosa vedi:\n");
    printf("      s = spenta, a = arancione, b = blu fisso, l = blu lampeggiante, q = basta\n");
    for (g = 0; g <= 1 && !fermo; g++)
        for (w = 0; w <= 3 && !fermo; w++) {
            char c = 0, x;

            ar5k_spia(registri, sotto_id, g == 0);        /* g = GPIO 3's level */
            ar5k_ec_led_scrivi(w);
            for (;;) {
                printf("  passo %d: GPIO 3 a %d, EC WLLD = %d - che cosa vedi? ", n + 1, g, w);
                c = 0;
                while (read(0, &x, 1) == 1 && x != '\n' && x != '\r') if (!c && x != ' ') c = x;
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (c == 's' || c == 'a' || c == 'b' || c == 'l' || c == 'q') break;
            }
            if (c == 'q') { fermo = 1; break; }
            risposte[n++] = c;
        }
    ar5k_ec_led_scrivi(wlld0);
    r[AR5K_GPIOCR / 4] = cr0;
    r[AR5K_GPIODO / 4] = do0;

    printf("\nar5k: riepilogo (WLLD era %d; tutto rimesso com'era)\n", wlld0);
    f = fopen("/root/ar5k-led.txt", "w");
    for (g = 0; g < n; g++) {
        int k = risposte[g] == 's' ? 0 : risposte[g] == 'a' ? 1 : risposte[g] == 'b' ? 2 : 3;
        printf("  GPIO 3 a %d, WLLD = %d: %s\n", g / 4, g % 4, NOMI[k]);
        if (f) fprintf(f, "GPIO 3 a %d, WLLD = %d: %s\n", g / 4, g % 4, NOMI[k]);
    }
    if (f) { fclose(f); printf("ar5k: scritto anche in /root/ar5k-led.txt\n"); }
    return 0;
}
