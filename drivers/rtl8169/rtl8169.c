/* =============================================================================
 * drivers/rtl8169/rtl8169.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * /dev/rtl8169.drv — le schede Gigabit PCI di Realtek: RTL8169, RTL8169S,
 * RTL8110S, RTL8169SB e RTL8169SC (8 ottobre 2026)
 *
 * Nasce per una scheda PCI con sopra un RTL8169SC, messa nel PC di prova
 * perche' l'Ethernet integrata (nforce.drv) non prendeva l'indirizzo: questi
 * registri, a differenza di quelli, Realtek li ha pubblicati.
 *
 * ! QEMU NON LA EMULA (ha la 8139, che e' un'altra scheda): si prova solo
 * sulla macchina. Li' funziona dall'8 ottobre 2026, con un RTL8169SC: dhcp
 * prende l'indirizzo e ping risponde. `-v` racconta l'accensione, `-d` stampa
 * registri, PHY e conteggi.
 *
 * ! NON E' PER LE RTL8168/8111, quelle su PCI Express: i registri si
 * somigliano, l'accensione no. Qui non sono in elenco.
 *
 * COME LAVORA (lo stesso disegno di nforce.drv)
 *
 *   - Due anelli di descrittori da 16 byte in memoria DMA: comando e
 *     lunghezza, VLAN, indirizzo basso, indirizzo alto. L'ultimo di ogni
 *     anello porta il bit «fine anello».
 *   - Niente interrupt: si guarda la scheda ogni PERIODO_MS e dopo ogni
 *     richiesta. La maschera delle interruzioni resta a zero.
 *   - Il PHY e' dentro il chip e si parla attraverso un registro del MAC.
 *     Velocita' e duplex il MAC li segue da solo: qui si leggono per dirli.
 *   - Si riceve in modo promiscuo: il filtro lo fa ip.drv.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "net_proto.h"

/* +0.001 a ogni modifica: `rtl8169.drv -version` la stampa. */
EX_VERSIONE("rtl8169.drv", "0.002");

#define RTL_MMIO_BYTE     0x100u

/* -----------------------------------------------------------------------------
 * I registri (scostamenti dal BAR di memoria)
 * --------------------------------------------------------------------------- */
#define R_MAC             0x00        /* sei byte */
#define R_MULTI           0x08        /* otto byte: il filtro dei gruppi */
#define R_TX_ANELLO       0x20
#define R_TX_ANELLO_ALTO  0x24
#define R_COMANDO         0x37        /* 8 bit */
#define R_TX_POLL         0x38        /* 8 bit */
#define R_IRQ_MASCHERA    0x3C        /* 16 bit */
#define R_IRQ_STATO       0x3E        /* 16 bit: scriverci 1 azzera */
#define R_TX_CONFIG       0x40
#define R_RX_CONFIG       0x44
#define R_PERSI           0x4C
#define R_9346            0x50        /* 8 bit: la serratura dei registri */
#define R_PHY             0x60        /* la porta verso il PHY */
#define R_PHY_STATO       0x6C        /* 8 bit */
#define R_RX_MASSIMO      0xDA        /* 16 bit */
#define R_CPLUS           0xE0        /* 16 bit */
#define R_MITIGA          0xE2        /* 16 bit */
#define R_RX_ANELLO       0xE4
#define R_RX_ANELLO_ALTO  0xE8
#define R_TX_SOGLIA       0xEC        /* 8 bit */

#define CMD_RESET         0x10
#define CMD_RX            0x08
#define CMD_TX            0x04
#define POLL_NORMALE      0x40        /* «guarda l'anello di trasmissione» */
#define SERR_APRI         0xC0
#define SERR_CHIUDI       0x00
#define TXC_IFG           0x03000000u /* la pausa fra i frame dello standard */
#define TXC_DMA           0x00000700u /* raffica senza limite */
#define TXC_VERSIONE      0x7C800000u /* quale chip e' */
#define RXC_SOGLIA        0x0000E000u /* consegna a frame intero */
#define RXC_DMA           0x00000700u
#define RXC_TUTTI         0x0000000Fu /* promiscuo, nostri, gruppi, broadcast */
#define CPLUS_MULRW       0x0008
#define CPLUS_RX_SOMMA    0x0020
#define CPLUS_RX_VLAN     0x0040
#define IRQ_RX_VUOTO      0x0010      /* descrittori di ricezione finiti */
#define IRQ_RX_FIFO       0x0040
#define PHYS_PIENO        0x01
#define PHYS_LINK         0x02
#define PHYS_10           0x04
#define PHYS_100          0x08
#define PHYS_1000         0x10
#define PHY_FLAG          0x80000000u

/* I descrittori: quattro parole. */
#define D_CMD             0
#define D_VLAN            1
#define D_BASSO           2
#define D_ALTO            3
#define DESC_BYTE         16

#define D_SCHEDA          (1u << 31)  /* e' della scheda */
#define D_FINE_ANELLO     (1u << 30)
#define D_PRIMO           (1u << 29)
#define D_ULTIMO          (1u << 28)
#define RX_ERRORE         (1u << 21)
#define RX_LUNGHEZZA      0x3FFFu     /* col CRC, che non ci serve */

/* I registri del PHY che servono (sono quelli dello standard MII). */
#define MII_BMCR          0
#define MII_BMSR          1
#define MII_ID1           2
#define MII_ID2           3
#define MII_ANNUNCIO      4
#define MII_COMPAGNO      5
#define MII_1000_CTRL     9
#define MII_1000_STATO    10
#define BMCR_RESET        0x8000
#define BMCR_NEGOZIA      0x1000
#define BMCR_SPENTO       0x0800
#define BMCR_ISOLATO      0x0400
#define BMCR_RIPARTI      0x0200
#define BMSR_LINK         0x0004

#define RX_N              32
#define TX_N              8
#define BUF_LEN           2048
#define RX_CHIEDI         1536

/* ! GLI ANELLI VANNO ALLINEATI A 256 BYTE: il chip ignora gli otto bit bassi
 * dell'indirizzo. La zona DMA parte da una pagina, e 1024 e' multiplo di 256. */
#define OFF_RX_ANELLO     0
#define OFF_TX_ANELLO     1024
#define OFF_RX_BUF        4096
#define OFF_TX_BUF        (OFF_RX_BUF + RX_N * BUF_LEN)
#define DMA_BYTE          (OFF_TX_BUF + TX_N * BUF_LEN)

#define PERIODO_MS        20
#define CODA_N            64

/* Le schede con questo chip. Provata c'e' solo la RTL8169SC. */
static const struct { unsigned short ven, dev; const char *nome; } g_modelli[] = {
    { 0x10EC, 0x8169, "Realtek RTL8169/8110 Gigabit" },
    { 0x10EC, 0x8167, "Realtek RTL8169SC/8110SC Gigabit" },
    { 0x1186, 0x4300, "D-Link DGE-528T (RTL8169)" },
    { 0x1259, 0xC107, "Allied Telesyn AT-2500TX (RTL8169)" },
    { 0x16EC, 0x0116, "U.S. Robotics USR997902 (RTL8169)" },
    { 0x1737, 0x1032, "Linksys EG1032 (RTL8169)" },
};

/* -----------------------------------------------------------------------------
 * Stato
 * --------------------------------------------------------------------------- */
static unsigned int   g_base = 0, g_mmio_fis = 0, g_irq = 0;
static unsigned int   g_bus = 0xFFFFFFFF, g_slot = 0, g_funzione = 0;
static unsigned short g_venditore = 0, g_dispositivo = 0;
static char           g_modello[48] = "Realtek RTL8169";
static unsigned char  g_mac[6];

static unsigned int   g_dma_virt = 0, g_dma_fis = 0;
static unsigned int   g_rx_qui = 0;
static unsigned int   g_tx_prossimo = 0;
static int            g_stato_link = -1;    /* l'ultimo R_PHY_STATO detto */
static unsigned int   g_battiti_link = 0;

static NetContatori   g_cont;
static int            g_verboso = 0;

static unsigned char  g_coda[CODA_N][NET_FRAME_MAX];
static unsigned int   g_coda_len[CODA_N];
static int            g_coda_testa = 0, g_coda_conta = 0;
static unsigned int   g_lettore_pid = 0;

static volatile unsigned char *g_reg = 0;

static unsigned int  r32(unsigned int off) { return *(volatile unsigned int *)(g_reg + off); }
static unsigned int  r16(unsigned int off) { return *(volatile unsigned short *)(g_reg + off); }
static unsigned int  r8(unsigned int off)  { return *(volatile unsigned char *)(g_reg + off); }
static void w32(unsigned int off, unsigned int v) { *(volatile unsigned int *)(g_reg + off) = v; }
static void w16(unsigned int off, unsigned int v) { *(volatile unsigned short *)(g_reg + off) = (unsigned short)v; }
static void w8(unsigned int off, unsigned int v)  { *(volatile unsigned char *)(g_reg + off) = (unsigned char)v; }

static volatile unsigned int *rx_desc(unsigned int i)
{
    return (volatile unsigned int *)(g_dma_virt + OFF_RX_ANELLO + i * DESC_BYTE);
}
static volatile unsigned int *tx_desc(unsigned int i)
{
    return (volatile unsigned int *)(g_dma_virt + OFF_TX_ANELLO + i * DESC_BYTE);
}
static unsigned char *rx_buf(unsigned int i) { return (unsigned char *)(g_dma_virt + OFF_RX_BUF + i * BUF_LEN); }
static unsigned char *tx_buf(unsigned int i) { return (unsigned char *)(g_dma_virt + OFF_TX_BUF + i * BUF_LEN); }
static unsigned int rx_buf_fis(unsigned int i) { return g_dma_fis + OFF_RX_BUF + i * BUF_LEN; }
static unsigned int tx_buf_fis(unsigned int i) { return g_dma_fis + OFF_TX_BUF + i * BUF_LEN; }

/* -----------------------------------------------------------------------------
 * Il PHY, attraverso il registro R_PHY: il bit 31 dice «fatto», al contrario
 * nei due versi (in lettura si accende, in scrittura si spegne).
 * --------------------------------------------------------------------------- */
static int phy_leggi(unsigned int reg)
{
    int i;

    w32(R_PHY, (reg & 0x1F) << 16);
    for (i = 0; i < 100; i++) {
        unsigned int v;

        usleep(100);
        v = r32(R_PHY);
        if (v & PHY_FLAG) return (int)(v & 0xFFFF);
    }
    return -1;
}

static int phy_scrivi(unsigned int reg, unsigned int valore)
{
    int i;

    w32(R_PHY, PHY_FLAG | ((reg & 0x1F) << 16) | (valore & 0xFFFF));
    for (i = 0; i < 100; i++) {
        usleep(100);
        if (!(r32(R_PHY) & PHY_FLAG)) return 0;
    }
    return -1;
}

/* Dice il link quando cambia. Rende 1 se c'e'. */
static int link_aggiorna(void)
{
    int s = (int)r8(R_PHY_STATO) & (PHYS_PIENO | PHYS_LINK | PHYS_10 | PHYS_100 | PHYS_1000);

    if (s == g_stato_link) return (s & PHYS_LINK) ? 1 : 0;
    g_stato_link = s;

    printf("rtl8169: link %s", (s & PHYS_LINK) ? "SU" : "GIU'");
    if (s & PHYS_LINK)
        printf(", %u Mbit %s duplex", (s & PHYS_1000) ? 1000u : (s & PHYS_100) ? 100u : 10u,
               (s & PHYS_PIENO) ? "full" : "half");
    printf("\n");
    return (s & PHYS_LINK) ? 1 : 0;
}

/* -----------------------------------------------------------------------------
 * Accensione
 * --------------------------------------------------------------------------- */
static void prepara_anelli(void)
{
    unsigned int i;

    for (i = 0; i < RX_N; i++) {
        volatile unsigned int *d = rx_desc(i);

        d[D_VLAN]  = 0;
        d[D_BASSO] = rx_buf_fis(i);
        d[D_ALTO]  = 0;
        d[D_CMD]   = D_SCHEDA | (i == RX_N - 1 ? D_FINE_ANELLO : 0) | RX_CHIEDI;
    }
    for (i = 0; i < TX_N; i++) {
        volatile unsigned int *d = tx_desc(i);

        d[D_VLAN]  = 0;
        d[D_BASSO] = tx_buf_fis(i);
        d[D_ALTO]  = 0;
        d[D_CMD]   = (i == TX_N - 1 ? D_FINE_ANELLO : 0);   /* nostro */
    }
    g_rx_qui = 0;
    g_tx_prossimo = 0;
}

static void scrivi_anelli(void)
{
    w32(R_TX_ANELLO_ALTO, 0);
    w32(R_TX_ANELLO, g_dma_fis + OFF_TX_ANELLO);
    w32(R_RX_ANELLO_ALTO, 0);
    w32(R_RX_ANELLO, g_dma_fis + OFF_RX_ANELLO);
}

static void accendi_phy(void)
{
    int bmcr, t, ann, c1000;

    /* La pagina 0 dei registri del PHY, e fuori dal riposo. */
    phy_scrivi(0x1F, 0x0000);
    phy_scrivi(0x0E, 0x0000);

    bmcr = phy_leggi(MII_BMCR);
    if (bmcr < 0) {
        printf("rtl8169: il PHY non risponde: la scheda non puo' avere un link\n");
        return;
    }
    if (g_verboso)
        printf("rtl8169: PHY %04x:%04x, controllo %04x\n",
               phy_leggi(MII_ID1) & 0xFFFF, phy_leggi(MII_ID2) & 0xFFFF, bmcr);

    phy_scrivi(MII_BMCR, BMCR_RESET);
    for (t = 0; t < 50; t++) {
        usleep(10000);
        bmcr = phy_leggi(MII_BMCR);
        if (bmcr >= 0 && !(bmcr & BMCR_RESET)) break;
    }
    if (t == 50 && g_verboso) printf("rtl8169: il reset del PHY non finisce: proseguo\n");

    /* Si annuncia tutto quel che lo standard prevede: 10 e 100 mezzo e
     * pieno, 1000 mezzo e pieno. Chi sta dall'altra parte sceglie. */
    ann = phy_leggi(MII_ANNUNCIO);
    if (ann < 0) ann = 0;
    phy_scrivi(MII_ANNUNCIO, ((unsigned int)ann & 0x001F) | 0x01E0 | 0x0001);
    c1000 = phy_leggi(MII_1000_CTRL);
    if (c1000 < 0) c1000 = 0;
    phy_scrivi(MII_1000_CTRL, ((unsigned int)c1000 & ~0x0300u) | 0x0300);
    phy_scrivi(MII_BMCR, BMCR_NEGOZIA | BMCR_RIPARTI);

    /* La negoziazione a un gigabit vuole i suoi secondi: fino a cinque. */
    for (t = 0; t < 50; t++) {
        if (r8(R_PHY_STATO) & PHYS_LINK) break;
        usleep(100000);
    }
}

static int inizializza_scheda(void)
{
    unsigned int v, versione;
    int i, vecchio;

    w16(R_IRQ_MASCHERA, 0);
    w8(R_COMANDO, CMD_RESET);
    for (i = 0; i < 1000; i++) {
        usleep(100);
        if (!(r8(R_COMANDO) & CMD_RESET)) break;
    }
    if (i == 1000) {
        printf("rtl8169: il reset non finisce (comando %02x)\n", r8(R_COMANDO));
        return -1;
    }

    /* Il reset ricarica l'indirizzo dalla memoria della scheda. */
    for (i = 0; i < 6; i++) g_mac[i] = (unsigned char)r8(R_MAC + (unsigned int)i);

    versione = r32(R_TX_CONFIG) & TXC_VERSIONE;
    /* ! I PRIMI TRE CHIP vogliono trasmissione e ricezione accese PRIMA di
     * accettare la configurazione; dal 8110SB in poi prima vengono gli
     * anelli. La famiglia si legge dal registro di trasmissione. */
    vecchio = (versione == 0x00000000u || versione == 0x00800000u ||
               versione == 0x04000000u);
    if (g_verboso)
        printf("rtl8169: chip %08x (%s)\n", versione,
               versione == 0x18000000u ? "RTL8169SC/8110SC" :
               versione == 0x98000000u ? "RTL8169SC/8110SC rev. E" :
               versione == 0x10000000u ? "RTL8169SB/8110SB" :
               vecchio ? "RTL8169/8169S/8110S" : "non in elenco");

    accendi_phy();
    prepara_anelli();

    w8(R_9346, SERR_APRI);
    if (vecchio) w8(R_COMANDO, CMD_TX | CMD_RX);

    w8(R_TX_SOGLIA, 0x3F);                  /* nessuna trasmissione anticipata */
    w16(R_RX_MASSIMO, RX_CHIEDI);
    v = r16(R_CPLUS);
    w16(R_CPLUS, (v | CPLUS_MULRW) & ~(unsigned int)(CPLUS_RX_SOMMA | CPLUS_RX_VLAN));
    w16(R_MITIGA, 0);
    scrivi_anelli();

    if (!vecchio) w8(R_COMANDO, CMD_TX | CMD_RX);
    w32(R_RX_CONFIG, (r32(R_RX_CONFIG) & 0xFF7E1880u) | RXC_SOGLIA | RXC_DMA | RXC_TUTTI);
    w32(R_TX_CONFIG, TXC_IFG | TXC_DMA);
    w8(R_9346, SERR_CHIUDI);

    w32(R_PERSI, 0);
    w32(R_MULTI, 0xFFFFFFFFu);
    w32(R_MULTI + 4, 0xFFFFFFFFu);
    w16(R_IRQ_MASCHERA, 0);                 /* niente interrupt: si guarda */
    w16(R_IRQ_STATO, 0xFFFF);

    if ((r8(R_COMANDO) & (CMD_TX | CMD_RX)) != (CMD_TX | CMD_RX))
        printf("rtl8169: trasmissione e ricezione non restano accese (comando %02x)\n",
               r8(R_COMANDO));

    g_stato_link = -1;
    link_aggiorna();
    return 0;
}

/* -----------------------------------------------------------------------------
 * Ricevere e trasmettere
 * --------------------------------------------------------------------------- */
static void accoda(const unsigned char *f, unsigned int len)
{
    int slot;

    if (len > NET_FRAME_MAX) len = NET_FRAME_MAX;
    if (g_coda_conta == CODA_N) {
        g_coda_testa = (g_coda_testa + 1) % CODA_N;
        g_coda_conta--;
        g_cont.persi_coda++;
    }
    slot = (g_coda_testa + g_coda_conta) % CODA_N;
    memcpy(g_coda[slot], f, len);
    g_coda_len[slot] = len;
    g_coda_conta++;
}

static void svuota_rx(void)
{
    unsigned int giri;

    for (giri = 0; giri < RX_N; giri++) {
        volatile unsigned int *d = rx_desc(g_rx_qui);
        unsigned int f = d[D_CMD], len;

        if (f & D_SCHEDA) break;            /* ancora della scheda: niente altro */

        len = f & RX_LUNGHEZZA;
        if (len >= 4) len -= 4;             /* il CRC in coda */
        if (!(f & RX_ERRORE) && (f & D_PRIMO) && (f & D_ULTIMO) &&
            len >= 14 && len <= NET_FRAME_MAX) {
            accoda(rx_buf(g_rx_qui), len);
            g_cont.ricevuti++;
        } else {
            g_cont.errori_rx++;
        }

        d[D_VLAN]  = 0;
        d[D_BASSO] = rx_buf_fis(g_rx_qui);
        d[D_ALTO]  = 0;
        d[D_CMD]   = D_SCHEDA | (g_rx_qui == RX_N - 1 ? D_FINE_ANELLO : 0) | RX_CHIEDI;
        g_rx_qui = (g_rx_qui + 1) % RX_N;
    }
}

static int trasmetti(const unsigned char *f, unsigned int len)
{
    volatile unsigned int *d;
    unsigned int i;

    if (len > NET_MTU + 14) return -1;

    d = tx_desc(g_tx_prossimo);
    for (i = 0; i < 2000; i++) {            /* fino a 200 ms */
        if (!(d[D_CMD] & D_SCHEDA)) break;
        usleep(100);
    }
    if (i == 2000) {
        g_cont.errori_tx++;
        return -1;
    }

    memcpy(tx_buf(g_tx_prossimo), f, len);
    if (len < NET_FRAME_MIN) {
        memset(tx_buf(g_tx_prossimo) + len, 0, NET_FRAME_MIN - len);
        len = NET_FRAME_MIN;
    }

    d[D_VLAN]  = 0;
    d[D_BASSO] = tx_buf_fis(g_tx_prossimo);
    d[D_ALTO]  = 0;
    d[D_CMD]   = D_SCHEDA | D_PRIMO | D_ULTIMO |
                 (g_tx_prossimo == TX_N - 1 ? D_FINE_ANELLO : 0) | len;     /* per ultimo: e' il via */

    g_tx_prossimo = (g_tx_prossimo + 1) % TX_N;
    w8(R_TX_POLL, POLL_NORMALE);
    g_cont.inviati++;
    return 0;
}

static void servi_scheda(void)
{
    unsigned int st = r16(R_IRQ_STATO);

    if (st) w16(R_IRQ_STATO, st);           /* scriverci le azzera */
    if (st & (IRQ_RX_VUOTO | IRQ_RX_FIFO)) g_cont.overflow++;
    svuota_rx();

    if (++g_battiti_link >= 1000 / PERIODO_MS) {
        g_battiti_link = 0;
        link_aggiorna();
    }
}

static void consegna(void)
{
    if (g_lettore_pid == 0 || g_coda_conta == 0) return;

    ipc_send(g_lettore_pid, NET_MSG_FRAME,
             g_coda[g_coda_testa], g_coda_len[g_coda_testa]);
    g_coda_testa = (g_coda_testa + 1) % CODA_N;
    g_coda_conta--;
    g_lettore_pid = 0;
}

/* -----------------------------------------------------------------------------
 * Trovare la scheda
 * --------------------------------------------------------------------------- */
static int conosciuta(unsigned short ven, unsigned short dev)
{
    int i;

    for (i = 0; i < (int)(sizeof(g_modelli) / sizeof(g_modelli[0])); i++)
        if (g_modelli[i].ven == ven && g_modelli[i].dev == dev) return i;
    return -1;
}

static int chiedi_pci(int pid, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    r.ordinale    = ordinale;
    r.classe      = PCI_CLASSE_RETE;
    r.sottoclasse = PCI_SOTTO_ETHERNET;
    r.venditore   = PCI_QUALUNQUE;
    r.dispositivo = PCI_QUALUNQUE;

    if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;

    for (t = 0; t < 8; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo == PCI_MSG_FINE) return 0;
        if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(*out)) {
            memcpy(out, buf, sizeof(*out));
            return 1;
        }
        return -1;
    }
    return -1;
}

static int cerca_su_pci(void)
{
    PciDispositivo d;
    unsigned int   ord;
    int            pid, n, i, b;

    pid = ipc_attendi(PCI_SERVIZIO, 5000);
    if (pid <= 0) {
        printf("rtl8169: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
        return -1;
    }

    for (ord = 0; ord < 16; ord++) {
        n = chiedi_pci(pid, ord, &d);
        if (n <= 0) break;
        i = conosciuta(d.venditore, d.dispositivo);
        if (i < 0) continue;

        /* ! IL PRIMO BAR E' DI PORTE, IL SECONDO DI MEMORIA: gli stessi
         * registri visti in due modi. Qui si usa la memoria. */
        g_mmio_fis = 0;
        g_base     = 0;
        for (b = 0; b < 6; b++) {
            if (d.bar[b] == 0) continue;
            if (d.bar_io[b]) { if (g_base == 0)     g_base     = d.bar[b]; }
            else             { if (g_mmio_fis == 0) g_mmio_fis = d.bar[b]; }
        }
        if (g_mmio_fis == 0) {
            printf("rtl8169: trovata %04x:%04x ma senza BAR di memoria.\n",
                   d.venditore, d.dispositivo);
            return -1;
        }

        g_irq         = d.irq_linea;
        g_bus         = d.bus;
        g_slot        = d.slot;
        g_funzione    = d.funzione;
        g_venditore   = d.venditore;
        g_dispositivo = d.dispositivo;
        strncpy(g_modello, g_modelli[i].nome, sizeof(g_modello) - 1);

        {
            PciAzione a;

            a.bus = d.bus; a.slot = d.slot; a.funzione = d.funzione;
            a.riservato = 0; a.offset = 0;
            a.bit = PCI_ABIL_IO | PCI_ABIL_MEMORIA | PCI_ABIL_BUSMASTER;
            ipc_send(pid, PCI_MSG_ABILITA, &a, sizeof(a));
        }
        return 0;
    }

    printf("rtl8169: nessuna scheda Realtek RTL8169 sul bus PCI.\n");
    return -1;
}

/* I registri, senza toccare niente: e' la schermata da mandare quando non va. */
static void stampa_registri(void)
{
    printf("  comando %02x  irq stato %04x maschera %04x  serratura %02x  c+ %04x\n",
           r8(R_COMANDO), r16(R_IRQ_STATO), r16(R_IRQ_MASCHERA), r8(R_9346), r16(R_CPLUS));
    printf("  tx config %08x  rx config %08x  rx massimo %u  persi %u\n",
           r32(R_TX_CONFIG), r32(R_RX_CONFIG), r16(R_RX_MASSIMO), r32(R_PERSI) & 0xFFFFFFu);
    printf("  anello tx %08x  anello rx %08x  stato phy %02x\n",
           r32(R_TX_ANELLO), r32(R_RX_ANELLO), r8(R_PHY_STATO));
    printf("  MAC %02x:%02x:%02x:%02x:%02x:%02x\n", r8(R_MAC), r8(R_MAC + 1),
           r8(R_MAC + 2), r8(R_MAC + 3), r8(R_MAC + 4), r8(R_MAC + 5));
}

static void stampa_phy(void)
{
    int bmcr = phy_leggi(MII_BMCR), bmsr;
    unsigned int s = r8(R_PHY_STATO);

    if (bmcr < 0) {
        printf("  PHY: non risponde\n");
        return;
    }
    (void)phy_leggi(MII_BMSR);              /* il bit del link e' «a ritenuta» */
    bmsr = phy_leggi(MII_BMSR);
    printf("  PHY identita' %04x:%04x\n", phy_leggi(MII_ID1) & 0xFFFF, phy_leggi(MII_ID2) & 0xFFFF);
    printf("  PHY controllo %04x  stato %04x  annuncio %04x  compagno %04x  1000: %04x %04x\n",
           bmcr & 0xFFFF, bmsr & 0xFFFF,
           phy_leggi(MII_ANNUNCIO) & 0xFFFF, phy_leggi(MII_COMPAGNO) & 0xFFFF,
           phy_leggi(MII_1000_CTRL) & 0xFFFF, phy_leggi(MII_1000_STATO) & 0xFFFF);
    printf("  link: %s", (s & PHYS_LINK) ? "SU" : "GIU' (cavo? switch?)");
    if (s & PHYS_LINK)
        printf(", %u Mbit %s duplex", (s & PHYS_1000) ? 1000u : (s & PHYS_100) ? 100u : 10u,
               (s & PHYS_PIENO) ? "full" : "half");
    printf("%s%s\n", (bmcr & BMCR_SPENTO) ? ", PHY A RIPOSO" : "",
           (bmcr & BMCR_ISOLATO) ? ", PHY ISOLATO" : "");
}

/* I conteggi del driver che sta girando, se ce n'e' uno. */
static void stampa_conteggi(void)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    NetContatori  c;
    int           pid = ipc_lookup(NET_SERVIZIO_0), t;

    if (pid <= 0) {
        printf("  conteggi: nessun driver di rete in servizio adesso\n");
        return;
    }
    if (ipc_send(pid, NET_MSG_CONTATORI, 0, 0) < 0) return;
    for (t = 0; t < 8; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) break;
        if ((int)meta.sender_pid != pid || meta.tipo != NET_MSG_CONTEGGI) continue;
        if (meta.len < sizeof(c)) break;
        memcpy(&c, buf, sizeof(c));
        printf("  conteggi: inviati %u (errori %u), ricevuti %u (errori %u), "
               "persi in coda %u, anello pieno %u, giri %u\n", c.inviati, c.errori_tx,
               c.ricevuti, c.errori_rx, c.persi_coda, c.overflow, c.battiti);
        return;
    }
    printf("  conteggi: il driver in servizio non risponde\n");
}

static void stampa_stato(void)
{
    printf("rtl8169: %s (%04x:%04x)\n", g_modello, g_venditore, g_dispositivo);
    printf("         %02x:%02x.%u  registri 0x%08x  IRQ %u (non usato: si guarda ogni %d ms)\n",
           g_bus, g_slot, g_funzione, g_mmio_fis, g_irq, PERIODO_MS);
    printf("         MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("         DMA %u byte: virt 0x%x, fisico 0x%x; anelli %d RX, %d TX\n",
           DMA_BYTE, g_dma_virt, g_dma_fis, RX_N, TX_N);
}

/* -----------------------------------------------------------------------------
 * Il ciclo di servizio
 * --------------------------------------------------------------------------- */
static void servi(void)
{
    IpcMessage    meta;
    unsigned char payload[IPC_MSG_MAX_DATA];

    for (;;) {
        int r = ipc_recv_timeout(&meta, payload, sizeof(payload), PERIODO_MS);

        if (r < 0) {
            g_cont.battiti++;
            servi_scheda();
            consegna();
            continue;
        }

        switch (meta.tipo) {
        case NET_MSG_INFO: {
            NetStato s;
            int i;

            memset(&s, 0, sizeof(s));
            for (i = 0; i < 6; i++) s.mac[i] = g_mac[i];
            s.mtu        = NET_MTU;
            s.porta_base = g_base;
            s.irq        = g_irq;
            s.bus        = g_bus;
            s.slot       = g_slot;
            strncpy(s.modello, g_modello, sizeof(s.modello) - 1);
            ipc_send(meta.sender_pid, NET_MSG_STATO, &s, sizeof(s));
            break;
        }

        case NET_MSG_INVIA: {
            NetEsito e;

            e.codice = trasmetti(payload, meta.len);
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }

        case NET_MSG_RICEVI:
            g_lettore_pid = meta.sender_pid;
            servi_scheda();
            consegna();
            break;

        case NET_MSG_CONTATORI:
            ipc_send(meta.sender_pid, NET_MSG_CONTEGGI, &g_cont, sizeof(g_cont));
            break;

        default:
            break;
        }
    }
}

static void uso(void)
{
    printf("uso: /dev/rtl8169.drv [-i] [-d] [-v]\n");
    printf("  (nessuna opzione)  si aggancia alla scheda e resta in servizio\n");
    printf("  -i                 la sonda: dice se c'e' e basta\n");
    printf("  -d                 registri, PHY e conteggi com'e' adesso: e' la\n");
    printf("                     schermata da mandare quando la rete non va\n");
    printf("  -v                 racconta ogni passo dell'accensione\n");
    printf("  -l                 dice se il cavo c'e' (esce con 0) o no (1), e basta:\n");
    printf("                     lo usa netdetect per scegliere fra due schede\n");
}

int main(int argc, char **argv)
{
    int sonda = 0, registri = 0, solo_link = 0, i, rc;
    DmaZona z;
    MmioZona m;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)      sonda = 1;
        else if (strcmp(argv[i], "-d") == 0) registri = 1;
        else if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-l") == 0) solo_link = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else { uso(); return 2; }
    }

    memset(&g_cont, 0, sizeof(g_cont));

    if (cerca_su_pci() != 0) return 1;

    if (sonda) {
        printf("rtl8169: %s in %02x:%02x.%u\n", g_modello, g_bus, g_slot, g_funzione);
        return 0;
    }

    m.fisico = g_mmio_fis;
    m.byte   = RTL_MMIO_BYTE;
    rc = mmio_map(&m);
    if (rc < 0) {
        printf("rtl8169: mmio_map(0x%08x, %u) fallita (%d)\n", g_mmio_fis, RTL_MMIO_BYTE, rc);
        return 1;
    }
    g_reg = (volatile unsigned char *)m.virt;

    if (solo_link) {
        rc = (r8(R_PHY_STATO) & PHYS_LINK) ? 0 : 1;
        printf("rtl8169: link %s\n", rc == 0 ? "SU" : "GIU'");
        return rc;
    }

    if (registri) {
        printf("rtl8169: %s, registri a 0x%08x, cosi' come sono adesso\n",
               g_modello, g_mmio_fis);
        stampa_registri();
        stampa_phy();
        stampa_conteggi();
        return 0;
    }

    if (r32(R_TX_CONFIG) == 0xFFFFFFFFu) {
        printf("rtl8169: i registri mappati non rispondono (tutti a ff):\n"
               "         la finestra e' a 0x%08x; la scheda non sta decodificando.\n",
               g_mmio_fis);
        return 1;
    }

    z.byte = DMA_BYTE;
    rc = dma_alloc(&z);
    if (rc < 0) {
        printf("rtl8169: dma_alloc(%u) fallita (%d)\n", DMA_BYTE, rc);
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;
    memset((void *)g_dma_virt, 0, DMA_BYTE);

    if (inizializza_scheda() != 0) return 1;

    stampa_stato();
    if (g_verboso) { stampa_registri(); stampa_phy(); }

    if (ipc_register(NET_SERVIZIO_0) < 0) {
        printf("rtl8169: non riesco a registrare il servizio '%s'\n", NET_SERVIZIO_0);
        return 1;
    }
    printf("rtl8169: servizio '%s' attivo\n", NET_SERVIZIO_0);

    servi();
    return 0;
}
