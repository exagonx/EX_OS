/* =============================================================================
 * drivers/nforce/nforce.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * /dev/nforce.drv — la scheda di rete integrata nei chipset NVIDIA nForce
 * (7 ottobre 2026)
 *
 * Nasce per la scheda madre con chipset MCP73 il cui referto sta in
 * sonda/mb_oem_000: Ethernet 10de:07dc, registri in memoria a BAR0.
 *
 * ! NVIDIA NON HA MAI PUBBLICATO QUESTI REGISTRI. La mappa qui sotto e' quella
 * che la comunita' ha ricostruito negli anni (il driver «forcedeth» di Linux
 * ne e' il deposito): nomi come «UnknownSetupReg5» non sono una dimenticanza,
 * sono quel che se ne sa. Dove un valore e' scritto senza spiegazione e'
 * perche' la spiegazione non esiste: e' il valore con cui la scheda funziona.
 *
 * ! QEMU NON LA EMULA. Questo driver compila, ma la prima volta che gira
 * davvero e' sulla macchina vera: per questo dice molto. `-v` racconta ogni
 * passo dell'accensione, `-d` stampa i registri senza toccare niente.
 *
 * COME LAVORA
 *
 *   - Due anelli di descrittori in memoria DMA, uno per ricevere e uno per
 *     trasmettere. Il formato e' il terzo dei tre che il chip conosce (16
 *     byte: indirizzo alto, indirizzo basso, VLAN, flag e lunghezza): e'
 *     quello che un MCP73 usa di suo. La meta' alta resta zero: siamo a 32 bit.
 *   - Niente interrupt: si guarda la scheda ogni PERIODO_MS e dopo ogni
 *     richiesta. L'IRQ di questa scheda (7, sulla macchina del referto) e'
 *     condiviso col controller USB, e un driver in ring 3 che si contende un
 *     IRQ alla prima uscita e' un guasto in piu' da distinguere dagli altri.
 *   - Il PHY non si resetta: si legge com'e'. Il BIOS lo lascia acceso e con
 *     la negoziazione fatta; se il link non c'e' la si fa ripartire, e basta.
 *     Su questi chipset puo' esserci un'unita' di gestione che usa lo stesso
 *     PHY, e resettarglielo sotto e' il modo sicuro di perdere il link.
 *   - Si riceve in modo promiscuo: il filtro lo fa ip.drv, che guarda
 *     l'indirizzo di ogni frame comunque. Un filtro sbagliato qui sarebbe un
 *     «non riceve niente» indistinguibile da dieci altre cause.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "net_proto.h"

/* +0.001 a ogni modifica: `nforce.drv -version` la stampa. */
EX_VERSIONE("nforce.drv", "0.001");

#define NV_VENDITORE      0x10DE
#define NV_MMIO_BYTE      0x1000u

/* -----------------------------------------------------------------------------
 * I registri (scostamenti da BAR0)
 * --------------------------------------------------------------------------- */
#define R_IRQ_STATO       0x000       /* cause dell'interruzione: 1 le azzera */
#define R_IRQ_MASCHERA    0x004
#define R_SETUP6          0x008
#define R_INTERVALLO      0x00C       /* ogni quanto la scheda raggruppa */
#define R_MAC_RESET       0x034
#define R_MISC1           0x080
#define R_TX_CONTROLLO    0x084
#define R_TX_STATO        0x088
#define R_FILTRO          0x08C
#define R_OFFLOAD         0x090       /* la misura dei buffer di ricezione */
#define R_RX_CONTROLLO    0x094
#define R_RX_STATO        0x098
#define R_SLOT            0x09C
#define R_TX_RINVIO       0x0A0
#define R_RX_RINVIO       0x0A4
#define R_MAC_A           0x0A8
#define R_MAC_B           0x0AC
#define R_MULTI_A         0x0B0
#define R_MULTI_B         0x0B4
#define R_MULTI_MASCH_A   0x0B8
#define R_MULTI_MASCH_B   0x0BC
#define R_PHY_INTERF      0x0C0
#define R_TX_ANELLO       0x100
#define R_RX_ANELLO       0x104
#define R_MISURE          0x108       /* (rx - 1) << 16 | (tx - 1) */
#define R_TX_POLL         0x10C
#define R_VELOCITA        0x110
#define R_SETUP5          0x130
#define R_TX_SOGLIA       0x13C
#define R_TXRX            0x144
#define R_TX_ANELLO_ALTO  0x148
#define R_RX_ANELLO_ALTO  0x14C
#define R_MII_STATO       0x180
#define R_MII_MASCHERA    0x184
#define R_ADATTATORE      0x188
#define R_MII_VELOCITA    0x18C
#define R_MII_CONTROLLO   0x190
#define R_MII_DATI        0x194
#define R_SVEGLIA         0x200
#define R_ALIMENTAZIONE   0x26C
#define R_VLAN            0x300
#define R_ALIMENTAZIONE2  0x600

#define IRQ_TUTTE         0x83FFu

#define MAC_RESET_SU      0x0F3u
#define MISC1_FORZA       0x3B0F3Cu
#define MISC1_MEZZO       0x02u       /* half duplex */
#define TXC_AVVIA         0x01u
#define TXC_GESTIONE      0x40000000u /* l'unita' di gestione e' attiva */
#define TXS_OCCUPATO      0x01u
#define FILTRO_SEMPRE     0x7F0000u
#define FILTRO_PROMISCUO  0x80u
#define RXC_AVVIA         0x01u
#define RXS_OCCUPATO      0x01u
#define SLOT_PREDEF       0x00007F00u
#define TX_RINVIO_PREDEF  0x15050Fu
#define TX_RINVIO_RG_100  0x16070Fu
#define TX_RINVIO_RG_1000 0x14050Fu
#define RX_RINVIO_PREDEF  0x16u
#define PHY_MEZZO         0x100u
#define PHY_100           0x1u
#define PHY_1000          0x2u
#define PHY_RGMII         0x10000000u
#define VEL_FORZA         0x10000u
#define VEL_10            1000u
#define VEL_100           100u
#define VEL_1000          50u
#define SETUP5_BIT31      0x80000000u
#define SOGLIA_PREDEF     0x1E08000u
#define SOGLIA_1000       0xFE08000u
#define TXRX_VIA          0x0001u     /* «guarda l'anello di trasmissione» */
#define TXRX_BIT1         0x0002u
#define TXRX_BIT2         0x0004u
#define TXRX_RESET        0x0010u
#define TXRX_DESC_3       0xC02200u   /* descrittori del terzo formato */
#define MII_ST_ERRORE     0x0001u
#define MII_ST_LETTURA    0x0007u
#define MII_ST_TUTTO      0x000Fu
#define ADATT_AVVIA       0x02u
#define ADATT_PHY_VALIDO  0x40000u
#define ADATT_IN_FUNZIONE 0x100000u
#define MII_VEL_BIT8      (1u << 8)
#define MII_C_IN_USO      0x08000u
#define MII_C_SCRIVI      0x00400u
#define ALIM_ACCESO       0x8000u
#define ALIM_VALIDO       0x0100u
#define ALIM2_ACCENDI     0x0F15u

/* I descrittori del terzo formato: quattro parole. */
#define D_ALTO            0
#define D_BASSO           4
#define D_VLAN            8
#define D_FLAG            12
#define DESC_BYTE         16

#define RX_LIBERO         (1u << 31)  /* e' della scheda */
#define RX_ERRORE         (1u << 30)
#define RX_VALIDO         (1u << 29)
#define RX_MENO_UNO       (1u << 25)
#define RX_ERR_QUALI      0x01FC0000u /* i sette bit che dicono che errore */
#define RX_ERR_TRAMA      (1u << 24)
#define RX_LUNGHEZZA      0x3FFFu
#define TX_DA_MANDARE     (1u << 31)  /* e' della scheda */
#define TX_ULTIMO         (1u << 29)

/* I registri del PHY che servono (sono quelli dello standard MII). */
#define MII_BMCR          0
#define MII_BMSR          1
#define MII_ID1           2
#define MII_ID2           3
#define MII_ANNUNCIO      4
#define MII_COMPAGNO      5
#define MII_1000_CTRL     9
#define MII_1000_STATO    10
#define BMCR_NEGOZIA      0x1000
#define BMCR_RIPARTI      0x0200
#define BMSR_LINK         0x0004

#define RX_N              32
#define TX_N              8
#define BUF_LEN           2048
#define RX_CHIEDI         1536        /* quanto diciamo alla scheda di accettare */

#define OFF_RX_ANELLO     0
#define OFF_TX_ANELLO     (OFF_RX_ANELLO + RX_N * DESC_BYTE)
#define OFF_RX_BUF        4096
#define OFF_TX_BUF        (OFF_RX_BUF + RX_N * BUF_LEN)
#define DMA_BYTE          (OFF_TX_BUF + TX_N * BUF_LEN)

#define PERIODO_MS        20
#define CODA_N            64

/* I modelli: tutti i numeri che la stessa famiglia di chipset usa per la sua
 * Ethernet. Provato (dal referto) c'e' solo 0x07DC; gli altri hanno gli
 * stessi registri, ma finche' qualcuno non li accende sono una speranza. */
static const struct { unsigned short dev; const char *nome; } g_modelli[] = {
    { 0x07DC, "NVIDIA nForce MCP73 Ethernet" },
    { 0x07DD, "NVIDIA nForce MCP73 Ethernet" },
    { 0x07DE, "NVIDIA nForce MCP73 Ethernet" },
    { 0x07DF, "NVIDIA nForce MCP73 Ethernet" },
    { 0x0760, "NVIDIA nForce MCP77 Ethernet" },
    { 0x0761, "NVIDIA nForce MCP77 Ethernet" },
    { 0x0762, "NVIDIA nForce MCP77 Ethernet" },
    { 0x0763, "NVIDIA nForce MCP77 Ethernet" },
    { 0x0AB0, "NVIDIA nForce MCP79 Ethernet" },
    { 0x0AB1, "NVIDIA nForce MCP79 Ethernet" },
    { 0x0AB2, "NVIDIA nForce MCP79 Ethernet" },
    { 0x0AB3, "NVIDIA nForce MCP79 Ethernet" },
    { 0x054C, "NVIDIA nForce MCP67 Ethernet" },
    { 0x054D, "NVIDIA nForce MCP67 Ethernet" },
    { 0x054E, "NVIDIA nForce MCP67 Ethernet" },
    { 0x054F, "NVIDIA nForce MCP67 Ethernet" },
};

/* -----------------------------------------------------------------------------
 * Stato
 * --------------------------------------------------------------------------- */
static unsigned int   g_base = 0, g_mmio_fis = 0, g_irq = 0;
static unsigned int   g_bus = 0xFFFFFFFF, g_slot = 0, g_funzione = 0;
static unsigned short g_dispositivo = 0;
static char           g_modello[48] = "NVIDIA nForce Ethernet";
static unsigned char  g_mac[6];

static unsigned int   g_dma_virt = 0, g_dma_fis = 0;
static unsigned int   g_rx_qui = 0;         /* il prossimo descrittore da guardare */
static unsigned int   g_tx_prossimo = 0;
static unsigned int   g_phy = 0;            /* l'indirizzo del PHY sul filo MII */
static int            g_phy_trovato = 0;
static unsigned int   g_vel = 10;           /* 10, 100, 1000 */
static int            g_pieno = 0;          /* full duplex */
static int            g_link = 0;
static unsigned int   g_battiti_link = 0;

static NetContatori   g_cont;
static int            g_verboso = 0;

static unsigned char  g_coda[CODA_N][NET_FRAME_MAX];
static unsigned int   g_coda_len[CODA_N];
static int            g_coda_testa = 0, g_coda_conta = 0;
static unsigned int   g_lettore_pid = 0;

static volatile unsigned char *g_reg = 0;

static unsigned int reg_leggi(unsigned int off)
{
    return *(volatile unsigned int *)(g_reg + off);
}

static void reg_scrivi(unsigned int off, unsigned int val)
{
    *(volatile unsigned int *)(g_reg + off) = val;
}

/* Dopo una scrittura che deve essere arrivata prima della prossima: una
 * lettura la spinge fuori dai tamponi del ponte. */
static void spingi(void) { (void)reg_leggi(R_IRQ_STATO); }

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

/* Aspetta che (registro & maschera) valga `atteso`. 0 se e' successo. */
static int aspetta_reg(unsigned int off, unsigned int maschera, unsigned int atteso,
                       unsigned int ms)
{
    unsigned int i;

    for (i = 0; i < ms * 10; i++) {
        if ((reg_leggi(off) & maschera) == atteso) return 0;
        usleep(100);
    }
    return -1;
}

/* -----------------------------------------------------------------------------
 * Il filo di gestione del PHY (MII)
 * --------------------------------------------------------------------------- */
static int mii(unsigned int phy, unsigned int reg, int scrivi, unsigned int valore)
{
    unsigned int c;

    reg_scrivi(R_MII_STATO, MII_ST_LETTURA);
    if (reg_leggi(R_MII_CONTROLLO) & MII_C_IN_USO) {
        reg_scrivi(R_MII_CONTROLLO, MII_C_IN_USO);
        usleep(100);
    }

    c = (phy << 5) | reg;
    if (scrivi) {
        reg_scrivi(R_MII_DATI, valore);
        c |= MII_C_SCRIVI;
    }
    reg_scrivi(R_MII_CONTROLLO, c);

    if (aspetta_reg(R_MII_CONTROLLO, MII_C_IN_USO, 0, 20) != 0) return -1;
    if (scrivi) return 0;
    if (reg_leggi(R_MII_STATO) & MII_ST_ERRORE) return -1;
    return (int)(reg_leggi(R_MII_DATI) & 0xFFFF);
}

static int phy_cerca(void)
{
    unsigned int i;

    for (i = 1; i <= 32; i++) {
        unsigned int a = i & 0x1F;
        int id1 = mii(a, MII_ID1, 0, 0), id2;

        if (id1 < 0 || id1 == 0xFFFF || id1 == 0) continue;
        id2 = mii(a, MII_ID2, 0, 0);
        if (id2 < 0 || id2 == 0xFFFF) continue;
        g_phy = a;
        g_phy_trovato = 1;
        if (g_verboso)
            printf("nforce: PHY all'indirizzo %u, identita' %04x:%04x\n", a, id1, id2);
        return 0;
    }
    return -1;
}

/* Legge dal PHY velocita' e duplex e li dice al MAC. Rende 1 se il link c'e'. */
static int link_aggiorna(void)
{
    int bmsr, annuncio, compagno, c1000, s1000;
    unsigned int vel = 10, phyreg, rinvio;
    int pieno = 0, link;

    if (!g_phy_trovato) return 0;

    (void)mii(g_phy, MII_BMSR, 0, 0);       /* il bit del link e' «a ritenuta» */
    bmsr = mii(g_phy, MII_BMSR, 0, 0);
    link = (bmsr >= 0 && (bmsr & BMSR_LINK)) ? 1 : 0;

    if (link) {
        annuncio = mii(g_phy, MII_ANNUNCIO, 0, 0);
        compagno = mii(g_phy, MII_COMPAGNO, 0, 0);
        c1000    = mii(g_phy, MII_1000_CTRL, 0, 0);
        s1000    = mii(g_phy, MII_1000_STATO, 0, 0);
        if (annuncio < 0) annuncio = 0;
        if (compagno < 0) compagno = 0;

        if (c1000 >= 0 && s1000 >= 0 && (c1000 & 0x0200) && (s1000 & 0x0800)) {
            vel = 1000; pieno = 1;
        } else if (annuncio & compagno & 0x0100) { vel = 100; pieno = 1; }
        else if (annuncio & compagno & 0x0080)   { vel = 100; pieno = 0; }
        else if (annuncio & compagno & 0x0040)   { vel = 10;  pieno = 1; }
        else                                     { vel = 10;  pieno = 0; }
    }

    if (link == g_link && vel == g_vel && pieno == g_pieno) return link;
    g_link = link; g_vel = vel; g_pieno = pieno;

    phyreg = reg_leggi(R_PHY_INTERF) & ~(PHY_MEZZO | PHY_100 | PHY_1000);
    if (!pieno)           phyreg |= PHY_MEZZO;
    if (vel == 100)       phyreg |= PHY_100;
    else if (vel == 1000) phyreg |= PHY_1000;
    reg_scrivi(R_PHY_INTERF, phyreg);

    if (phyreg & PHY_RGMII) rinvio = (vel == 1000) ? TX_RINVIO_RG_1000 : TX_RINVIO_RG_100;
    else                    rinvio = TX_RINVIO_PREDEF;
    reg_scrivi(R_TX_RINVIO, rinvio);
    reg_scrivi(R_TX_SOGLIA, vel == 1000 ? SOGLIA_1000 : SOGLIA_PREDEF);

    reg_scrivi(R_MISC1, MISC1_FORZA | (pieno ? 0 : MISC1_MEZZO));
    spingi();
    reg_scrivi(R_VELOCITA, VEL_FORZA | (vel == 1000 ? VEL_1000 : vel == 100 ? VEL_100 : VEL_10));
    spingi();

    printf("nforce: link %s", link ? "SU" : "GIU'");
    if (link) printf(", %u Mbit %s duplex", vel, pieno ? "full" : "half");
    printf("\n");
    return link;
}

/* -----------------------------------------------------------------------------
 * Accensione
 * --------------------------------------------------------------------------- */
static void leggi_mac(void)
{
    unsigned int a = reg_leggi(R_MAC_A), b = reg_leggi(R_MAC_B);

    g_mac[0] = (unsigned char)(a & 0xFF);
    g_mac[1] = (unsigned char)((a >> 8) & 0xFF);
    g_mac[2] = (unsigned char)((a >> 16) & 0xFF);
    g_mac[3] = (unsigned char)((a >> 24) & 0xFF);
    g_mac[4] = (unsigned char)(b & 0xFF);
    g_mac[5] = (unsigned char)((b >> 8) & 0xFF);

    /* ! SUI CHIPSET PIU' VECCHI IL BIOS LO LASCIA AL ROVESCIO. Un indirizzo
     * col bit «di gruppo» acceso nel primo byte non puo' essere quello di una
     * scheda: se letto dritto ce l'ha e rovesciato no, era al rovescio. */
    if ((g_mac[0] & 1) && !(g_mac[5] & 1)) {
        unsigned char t;
        int k;

        for (k = 0; k < 3; k++) { t = g_mac[k]; g_mac[k] = g_mac[5 - k]; g_mac[5 - k] = t; }
        if (g_verboso) printf("nforce: il MAC era scritto al rovescio, lo raddrizzo\n");
    }
}

static void ferma(void)
{
    reg_scrivi(R_RX_CONTROLLO, reg_leggi(R_RX_CONTROLLO) & ~RXC_AVVIA);
    if (aspetta_reg(R_RX_STATO, RXS_OCCUPATO, 0, 50) != 0 && g_verboso)
        printf("nforce: la ricezione non si ferma (stato %08x)\n", reg_leggi(R_RX_STATO));
    reg_scrivi(R_TX_CONTROLLO, reg_leggi(R_TX_CONTROLLO) & ~TXC_AVVIA);
    if (aspetta_reg(R_TX_STATO, TXS_OCCUPATO, 0, 50) != 0 && g_verboso)
        printf("nforce: la trasmissione non si ferma (stato %08x)\n", reg_leggi(R_TX_STATO));

    reg_scrivi(R_TXRX, TXRX_BIT2 | TXRX_RESET | TXRX_DESC_3);
    spingi();
    usleep(100);
    reg_scrivi(R_TXRX, TXRX_BIT2 | TXRX_DESC_3);
    spingi();
}

static void prepara_anelli(void)
{
    unsigned int i;

    for (i = 0; i < RX_N; i++) {
        volatile unsigned int *d = rx_desc(i);

        d[D_ALTO / 4]  = 0;
        d[D_BASSO / 4] = rx_buf_fis(i);
        d[D_VLAN / 4]  = 0;
        d[D_FLAG / 4]  = RX_CHIEDI | RX_LIBERO;
    }
    for (i = 0; i < TX_N; i++) {
        volatile unsigned int *d = tx_desc(i);

        d[D_ALTO / 4]  = 0;
        d[D_BASSO / 4] = tx_buf_fis(i);
        d[D_VLAN / 4]  = 0;
        d[D_FLAG / 4]  = 0;                 /* nostro: niente da mandare */
    }
    g_rx_qui = 0;
    g_tx_prossimo = 0;

    reg_scrivi(R_RX_ANELLO, g_dma_fis + OFF_RX_ANELLO);
    reg_scrivi(R_RX_ANELLO_ALTO, 0);
    reg_scrivi(R_TX_ANELLO, g_dma_fis + OFF_TX_ANELLO);
    reg_scrivi(R_TX_ANELLO_ALTO, 0);
    reg_scrivi(R_MISURE, ((RX_N - 1) << 16) | (TX_N - 1));
    spingi();
}

static int inizializza_scheda(void)
{
    unsigned int v;
    int gestione;

    /* L'alimentazione: su questi chipset il MAC puo' essere a riposo. */
    reg_scrivi(R_ALIMENTAZIONE2, reg_leggi(R_ALIMENTAZIONE2) & ~ALIM2_ACCENDI);
    spingi();

    gestione = (reg_leggi(R_TX_CONTROLLO) & TXC_GESTIONE) != 0;
    if (gestione)
        printf("nforce: c'e' un'unita' di gestione attiva sulla scheda: non "
               "resetto il MAC\n");

    leggi_mac();
    reg_scrivi(R_SVEGLIA, 0);
    ferma();

    if (!gestione) {
        /* Il reset del MAC si porta via l'indirizzo: lo si rimette. */
        unsigned int a = reg_leggi(R_MAC_A), b = reg_leggi(R_MAC_B);
        unsigned int tp = reg_leggi(R_TX_POLL);

        reg_scrivi(R_TXRX, TXRX_BIT2 | TXRX_RESET | TXRX_DESC_3);
        spingi();
        reg_scrivi(R_MAC_RESET, MAC_RESET_SU);
        spingi();
        usleep(100);
        reg_scrivi(R_MAC_RESET, 0);
        spingi();
        usleep(100);
        reg_scrivi(R_MAC_A, a);
        reg_scrivi(R_MAC_B, b);
        reg_scrivi(R_TX_POLL, tp);
        reg_scrivi(R_TXRX, TXRX_BIT2 | TXRX_DESC_3);
        spingi();
    }

    reg_scrivi(R_MULTI_A, 0xFFFFFFFFu);
    reg_scrivi(R_MULTI_B, 0xFFFFu);
    reg_scrivi(R_MULTI_MASCH_A, 0xFFFFFFFFu);
    reg_scrivi(R_MULTI_MASCH_B, 0xFFFFu);
    reg_scrivi(R_FILTRO, 0);
    reg_scrivi(R_TX_CONTROLLO, reg_leggi(R_TX_CONTROLLO) & TXC_GESTIONE);
    reg_scrivi(R_RX_CONTROLLO, 0);
    reg_scrivi(R_ADATTATORE, 0);

    prepara_anelli();

    reg_scrivi(R_VELOCITA, VEL_FORZA | VEL_10);
    reg_scrivi(R_TX_SOGLIA, SOGLIA_PREDEF);
    reg_scrivi(R_TXRX, TXRX_DESC_3);
    reg_scrivi(R_VLAN, 0);
    spingi();
    reg_scrivi(R_TXRX, TXRX_BIT1 | TXRX_DESC_3);
    if (aspetta_reg(R_SETUP5, SETUP5_BIT31, SETUP5_BIT31, 100) != 0 && g_verboso)
        printf("nforce: il MAC non dice 'pronto' (SETUP5 = %08x): proseguo\n",
               reg_leggi(R_SETUP5));

    reg_scrivi(R_MII_MASCHERA, 0);
    reg_scrivi(R_IRQ_MASCHERA, 0);          /* niente interrupt: si guarda */
    reg_scrivi(R_IRQ_STATO, IRQ_TUTTE);
    reg_scrivi(R_MII_STATO, MII_ST_TUTTO);

    reg_scrivi(R_MISC1, MISC1_FORZA | MISC1_MEZZO);
    reg_scrivi(R_TX_STATO, reg_leggi(R_TX_STATO));
    reg_scrivi(R_FILTRO, FILTRO_SEMPRE);
    reg_scrivi(R_OFFLOAD, RX_CHIEDI);
    reg_scrivi(R_RX_STATO, reg_leggi(R_RX_STATO));
    reg_scrivi(R_SLOT, SLOT_PREDEF | (uptime_ms() & 0xFF));
    reg_scrivi(R_TX_RINVIO, TX_RINVIO_PREDEF);
    reg_scrivi(R_RX_RINVIO, RX_RINVIO_PREDEF);
    reg_scrivi(R_INTERVALLO, 970);
    reg_scrivi(R_SETUP6, 3);

    reg_scrivi(R_MII_VELOCITA, MII_VEL_BIT8 | 5);
    spingi();

    if (phy_cerca() != 0) {
        printf("nforce: nessun PHY risponde sul filo di gestione: la scheda "
               "non puo' avere un link\n");
    } else {
        int bmsr;

        reg_scrivi(R_ADATTATORE, (g_phy << 24) | ADATT_PHY_VALIDO | ADATT_IN_FUNZIONE);
        (void)mii(g_phy, MII_BMSR, 0, 0);
        bmsr = mii(g_phy, MII_BMSR, 0, 0);
        if (bmsr >= 0 && !(bmsr & BMSR_LINK)) {
            int bmcr = mii(g_phy, MII_BMCR, 0, 0);

            /* Niente link: si fa ripartire la negoziazione (senza reset) e
             * le si da' il tempo che vuole, fino a quattro secondi. */
            if (bmcr >= 0) {
                int t;

                if (g_verboso) printf("nforce: niente link, rilancio la negoziazione\n");
                mii(g_phy, MII_BMCR, 1, (unsigned int)bmcr | BMCR_NEGOZIA | BMCR_RIPARTI);
                for (t = 0; t < 40; t++) {
                    usleep(100000);
                    (void)mii(g_phy, MII_BMSR, 0, 0);
                    bmsr = mii(g_phy, MII_BMSR, 0, 0);
                    if (bmsr >= 0 && (bmsr & BMSR_LINK)) break;
                }
            }
        }
    }

    v = reg_leggi(R_ALIMENTAZIONE);
    if (!(v & ALIM_ACCESO)) reg_scrivi(R_ALIMENTAZIONE, v | ALIM_ACCESO);
    spingi();
    usleep(100);
    reg_scrivi(R_ALIMENTAZIONE, reg_leggi(R_ALIMENTAZIONE) | ALIM_VALIDO);

    reg_scrivi(R_FILTRO, FILTRO_SEMPRE | FILTRO_PROMISCUO);

    g_link = -1;                            /* cosi' il primo aggiornamento scrive */
    link_aggiorna();

    reg_scrivi(R_RX_CONTROLLO, reg_leggi(R_RX_CONTROLLO) | RXC_AVVIA);
    spingi();
    reg_scrivi(R_TX_CONTROLLO, reg_leggi(R_TX_CONTROLLO) | TXC_AVVIA);
    spingi();
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
        unsigned int f = d[D_FLAG / 4], len;
        int buono = 0;

        if (f & RX_LIBERO) break;           /* ancora della scheda: niente altro */

        len = f & RX_LUNGHEZZA;
        if (f & RX_VALIDO) {
            if (!(f & RX_ERRORE)) {
                buono = 1;
            } else if ((f & RX_ERR_QUALI) == RX_ERR_TRAMA) {
                /* Un bit in piu' in coda: il frame e' buono, un byte piu' corto. */
                if ((f & RX_MENO_UNO) && len > 0) len--;
                buono = 1;
            }
        }
        if (buono && len >= 14 && len <= NET_FRAME_MAX) {
            accoda(rx_buf(g_rx_qui), len);
            g_cont.ricevuti++;
        } else {
            g_cont.errori_rx++;
        }

        d[D_BASSO / 4] = rx_buf_fis(g_rx_qui);
        d[D_ALTO / 4]  = 0;
        d[D_VLAN / 4]  = 0;
        d[D_FLAG / 4]  = RX_CHIEDI | RX_LIBERO;     /* di nuovo della scheda */
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
        if (!(d[D_FLAG / 4] & TX_DA_MANDARE)) break;
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

    d[D_ALTO / 4]  = 0;
    d[D_BASSO / 4] = tx_buf_fis(g_tx_prossimo);
    d[D_VLAN / 4]  = 0;
    d[D_FLAG / 4]  = (len - 1) | TX_ULTIMO | TX_DA_MANDARE;     /* per ultimo: e' il via */

    g_tx_prossimo = (g_tx_prossimo + 1) % TX_N;
    reg_scrivi(R_TXRX, TXRX_VIA | TXRX_DESC_3);
    g_cont.inviati++;
    return 0;
}

static void servi_scheda(void)
{
    unsigned int st = reg_leggi(R_IRQ_STATO);

    if (st) reg_scrivi(R_IRQ_STATO, st);    /* scriverci le azzera */
    svuota_rx();

    /* Il link si riguarda ogni secondo: un cavo staccato e riattaccato puo'
     * tornare a un'altra velocita', e il MAC la vuole sapere. */
    if (++g_battiti_link >= 1000 / PERIODO_MS) {
        g_battiti_link = 0;
        reg_scrivi(R_MII_STATO, MII_ST_TUTTO);
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
static int conosciuta(unsigned short dev)
{
    int i;

    for (i = 0; i < (int)(sizeof(g_modelli) / sizeof(g_modelli[0])); i++)
        if (g_modelli[i].dev == dev) return i;
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
        printf("nforce: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
        return -1;
    }

    for (ord = 0; ord < 16; ord++) {
        n = chiedi_pci(pid, ord, &d);
        if (n <= 0) break;
        if (d.venditore != NV_VENDITORE) continue;
        i = conosciuta(d.dispositivo);
        if (i < 0) {
            printf("nforce: Ethernet NVIDIA %04x:%04x non in elenco: la provo "
                   "lo stesso\n", d.venditore, d.dispositivo);
        }

        /* ! I REGISTRI SONO IL PRIMO BAR DI MEMORIA, che e' BAR0. Gli altri
         * due di memoria che questa scheda ha non sono suoi registri. */
        g_mmio_fis = 0;
        g_base     = 0;
        for (b = 0; b < 6; b++) {
            if (d.bar[b] == 0) continue;
            if (d.bar_io[b]) { if (g_base == 0)     g_base     = d.bar[b]; }
            else             { if (g_mmio_fis == 0) g_mmio_fis = d.bar[b]; }
        }
        if (g_mmio_fis == 0) {
            printf("nforce: trovata %04x:%04x ma senza BAR di memoria.\n",
                   d.venditore, d.dispositivo);
            return -1;
        }

        g_irq         = d.irq_linea;
        g_bus         = d.bus;
        g_slot        = d.slot;
        g_funzione    = d.funzione;
        g_dispositivo = d.dispositivo;
        if (i >= 0) strncpy(g_modello, g_modelli[i].nome, sizeof(g_modello) - 1);

        {
            PciAzione a;

            a.bus = d.bus; a.slot = d.slot; a.funzione = d.funzione;
            a.riservato = 0; a.offset = 0;
            a.bit = PCI_ABIL_IO | PCI_ABIL_MEMORIA | PCI_ABIL_BUSMASTER;
            ipc_send(pid, PCI_MSG_ABILITA, &a, sizeof(a));
        }
        return 0;
    }

    printf("nforce: nessuna Ethernet NVIDIA nForce sul bus PCI.\n");
    return -1;
}

/* I registri, senza toccare niente: e' la riga da mandare quando non va. */
static void stampa_registri(void)
{
    static const struct { unsigned int off; const char *nome; } r[] = {
        { R_IRQ_STATO, "irq stato" }, { R_MISC1, "misc1" },
        { R_TX_CONTROLLO, "tx controllo" }, { R_TX_STATO, "tx stato" },
        { R_FILTRO, "filtro" }, { R_RX_CONTROLLO, "rx controllo" },
        { R_RX_STATO, "rx stato" }, { R_MAC_A, "mac A" }, { R_MAC_B, "mac B" },
        { R_PHY_INTERF, "phy interf" }, { R_TX_ANELLO, "anello tx" },
        { R_RX_ANELLO, "anello rx" }, { R_MISURE, "misure" },
        { R_TX_POLL, "tx poll" }, { R_VELOCITA, "velocita'" },
        { R_SETUP5, "setup5" }, { R_TXRX, "txrx" }, { R_MII_STATO, "mii stato" },
        { R_ADATTATORE, "adattatore" }, { R_ALIMENTAZIONE, "alimentazione" },
        { R_ALIMENTAZIONE2, "alimentazione2" },
    };
    unsigned int i;

    for (i = 0; i < sizeof(r) / sizeof(r[0]); i++)
        printf("  %03x %-14s %08x\n", r[i].off, r[i].nome, reg_leggi(r[i].off));
}

static void stampa_stato(void)
{
    printf("nforce: %s (%04x:%04x)\n", g_modello, NV_VENDITORE, g_dispositivo);
    printf("        %02x:%02x.%u  registri 0x%08x  IRQ %u (non usato: si guarda ogni %d ms)\n",
           g_bus, g_slot, g_funzione, g_mmio_fis, g_irq, PERIODO_MS);
    printf("        MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("        DMA %u byte: virt 0x%x, fisico 0x%x; anelli %d RX, %d TX\n",
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
    printf("uso: /dev/nforce.drv [-i] [-d] [-v]\n");
    printf("  (nessuna opzione)  si aggancia alla scheda e resta in servizio\n");
    printf("  -i                 la sonda: dice se c'e' e basta\n");
    printf("  -d                 stampa i registri com'e' adesso, senza toccarli\n");
    printf("  -v                 racconta ogni passo dell'accensione\n");
}

int main(int argc, char **argv)
{
    int sonda = 0, registri = 0, i, rc;
    DmaZona z;
    MmioZona m;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)      sonda = 1;
        else if (strcmp(argv[i], "-d") == 0) registri = 1;
        else if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else { uso(); return 2; }
    }

    memset(&g_cont, 0, sizeof(g_cont));

    if (cerca_su_pci() != 0) return 1;

    if (sonda) {
        printf("nforce: %s in %02x:%02x.%u\n", g_modello, g_bus, g_slot, g_funzione);
        return 0;
    }

    m.fisico = g_mmio_fis;
    m.byte   = NV_MMIO_BYTE;
    rc = mmio_map(&m);
    if (rc < 0) {
        printf("nforce: mmio_map(0x%08x, %u) fallita (%d)\n", g_mmio_fis, NV_MMIO_BYTE, rc);
        return 1;
    }
    g_reg = (volatile unsigned char *)m.virt;

    if (registri) {
        printf("nforce: registri a 0x%08x, come li ha lasciati chi e' venuto prima\n",
               g_mmio_fis);
        stampa_registri();
        return 0;
    }

    {
        unsigned int a = reg_leggi(R_MAC_A);

        if (a == 0xFFFFFFFFu) {
            printf("nforce: i registri mappati non rispondono (MAC A = ffffffff):\n"
                   "        la finestra e' a 0x%08x; se e' quella giusta la scheda\n"
                   "        non sta decodificando.\n", g_mmio_fis);
            return 1;
        }
    }

    z.byte = DMA_BYTE;
    rc = dma_alloc(&z);
    if (rc < 0) {
        printf("nforce: dma_alloc(%u) fallita (%d)\n", DMA_BYTE, rc);
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;
    memset((void *)g_dma_virt, 0, DMA_BYTE);

    if (inizializza_scheda() != 0) return 1;

    stampa_stato();
    if (g_verboso) stampa_registri();

    if (ipc_register(NET_SERVIZIO_0) < 0) {
        printf("nforce: non riesco a registrare il servizio '%s'\n", NET_SERVIZIO_0);
        return 1;
    }
    printf("nforce: servizio '%s' attivo\n", NET_SERVIZIO_0);

    servi();
    return 0;
}
