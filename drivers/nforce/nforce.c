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
EX_VERSIONE("nforce.drv", "0.009");

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
#define TXC_SINCRO         0x000F0000u /* a che punto e' l'unita' di gestione */
#define TXC_SINCRO_PHY     0x00040000u /* ...ha gia' preparato lei il PHY */
#define TXC_OSPITE         0x00004000u /* «il driver del sistema e' caricato» */
#define TXS_OCCUPATO      0x01u
#define FILTRO_SEMPRE     0x7F0000u
#define FILTRO_PROMISCUO  0x80u
#define RXC_AVVIA         0x01u
#define RXS_OCCUPATO      0x01u
#define SLOT_PREDEF       0x00007F00u
#define SLOT_MASCHERA      0x0003FF00u
#define SLOT_1000          0x0003FF00u /* a un gigabit il tempo di slot e' un altro */
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
#define BMCR_SPENTO        0x0800      /* il PHY e' a riposo */
#define BMCR_ISOLATO       0x0400      /* staccato dal MAC */
#define BMCR_RIPARTI      0x0200
#define BMSR_LINK         0x0004

/* ! L'ANELLO DI TRASMISSIONE NON PUO' ESSERE PICCOLO (8 ottobre 2026). Con 8
 * descrittori la scheda riceveva e non trasmetteva MAI: i descrittori
 * restavano «da mandare» e dhcp non aveva risposta. Dichiarandone 64, sul PC
 * di prova (MCP73), partono. 64 e' anche il minimo che il driver di
 * riferimento accetta; quello di ricezione, a 32, funziona. */
#define RX_N              32
#define TX_N              64
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
static unsigned int   g_per_noi = 0;        /* frame arrivati al nostro indirizzo */
static int            g_phy_realtek = 0;    /* -r */
static int            g_senza_reset = 0;    /* -n */

/* -w e -y: scritture in piu', fatte a scheda pronta e prima di avviarla.
 * Servono a provare un valore da lontano senza rifare il driver. */
#define IN_PIU_N 8
static unsigned int   g_w_reg[IN_PIU_N], g_w_val[IN_PIU_N], g_w_n = 0;
static unsigned int   g_y_reg[IN_PIU_N], g_y_val[IN_PIU_N], g_y_n = 0;

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

/* ! LE ATTESE BREVI NON SI FANNO CON usleep() (8 ottobre 2026). usleep(100)
 * non dura cento microsecondi: il sistema dorme a battiti da dieci
 * millisecondi, e un ciclo «fino a 20 ms, a passi di 100 us» ne durava
 * duemila. L'accensione della scheda prendeva piu' di dieci secondi, e ip.drv
 * ne aspetta dieci: la scheda in prova da sola andava, e all'avvio dhcp non
 * trovava nessuno. Una pausa breve e' qualche lettura di un registro; una
 * scadenza si misura con l'orologio. */
static void attimo(void)
{
    volatile unsigned int i;

    for (i = 0; i < 200; i++) (void)reg_leggi(R_IRQ_STATO);
}

/* Aspetta che (registro & maschera) valga `atteso`. 0 se e' successo. */
static int aspetta_reg(unsigned int off, unsigned int maschera, unsigned int atteso,
                       unsigned int ms)
{
    unsigned int t0 = uptime_ms();

    for (;;) {
        if ((reg_leggi(off) & maschera) == atteso) return 0;
        if (uptime_ms() - t0 > ms + 10u) return -1;     /* +10: un battito intero */
        attimo();
    }
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
        attimo();
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

    /* ! A UN GIGABIT IL TEMPO DI SLOT CAMBIA (8 ottobre 2026). Sul PC di prova
     * il PHY negozia 1000 Mbit pieni e il link e' su, ma dhcp non riceve
     * niente: questo registro restava al valore di 10 e 100. */
    reg_scrivi(R_SLOT, (reg_leggi(R_SLOT) & ~SLOT_MASCHERA) |
                       (vel == 1000 ? SLOT_1000 : SLOT_PREDEF));
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
    attimo();
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

    /* ! IL BIT 30 DA SOLO NON BASTA (8 ottobre 2026). Sul PC di prova il BIOS
     * lascia 40000000: l'unita' di gestione esiste, ma non ha preso lei il
     * PHY (i bit di sincronia sono a zero) e quindi non sta usando il MAC.
     * Prenderlo per «in uso» faceva saltare il reset del MAC. */
    v = reg_leggi(R_TX_CONTROLLO);
    gestione = (v & TXC_GESTIONE) && (v & TXC_SINCRO) == TXC_SINCRO_PHY;
    if (g_senza_reset) gestione = 1;
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
        attimo();
        reg_scrivi(R_MAC_RESET, 0);
        spingi();
        attimo();
        reg_scrivi(R_MAC_A, a);
        reg_scrivi(R_MAC_B, b);
        reg_scrivi(R_TX_POLL, tp);
        reg_scrivi(R_TXRX, TXRX_BIT2 | TXRX_DESC_3);
        spingi();
    }

    /* ! IL FILTRO PER INDIRIZZO VALE PER TUTTI I FRAME, ANCHE IN PROMISCUO
     * (8 ottobre 2026). Qui c'erano indirizzo e maschera a tutti uno: «passa
     * solo chi e' diretto a ff:ff:ff:ff:ff:ff». La scheda riceveva, e nella
     * prova i conteggi salivano, ma erano solo i broadcast della rete: la
     * risposta del server DHCP, che arriva indirizzata ALLA SCHEDA, veniva
     * scartata, e dhcp non andava avanti. Misurato sul PC di prova cambiando
     * questi quattro registri con -w: da una trentina di frame in cinque
     * secondi a una cinquantina. Maschera a zero = nessun bit si confronta;
     * il bit basso dell'indirizzo e' quello che il chip vuole sempre acceso. */
    reg_scrivi(R_MULTI_A, 0x00000001u);
    reg_scrivi(R_MULTI_B, 0);
    reg_scrivi(R_MULTI_MASCH_A, 0);
    reg_scrivi(R_MULTI_MASCH_B, 0);
    reg_scrivi(R_FILTRO, 0);
    /* ! «IL DRIVER E' CARICATO»: senza questo bit l'unita' di gestione non
     * lascia il percorso dei dati al sistema. */
    reg_scrivi(R_TX_CONTROLLO, (reg_leggi(R_TX_CONTROLLO) & TXC_GESTIONE) | TXC_OSPITE);
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
        if (g_phy_realtek) {
            /* La preparazione che il driver di riferimento fa a un Realtek
             * RTL8211B collegato in RGMII. Qui a richiesta (-r): i valori
             * sono scritti a memoria e vanno confermati dalla scheda. */
            static const unsigned short passi[][2] = {
                { 0x1F, 0x0000 }, { 0x19, 0x8E00 }, { 0x1F, 0x0001 }, { 0x13, 0xAD17 },
                { 0x14, 0xFB54 }, { 0x18, 0xF5C7 }, { 0x1F, 0x0000 },
            };
            unsigned int k;

            for (k = 0; k < sizeof(passi) / sizeof(passi[0]); k++)
                mii(g_phy, passi[k][0], 1, passi[k][1]);
            mii(g_phy, MII_BMCR, 1, BMCR_NEGOZIA | BMCR_RIPARTI);
            printf("nforce: PHY Realtek preparato (-r), rinegozio\n");
            usleep(300000);
        }
        {
            unsigned int k;

            for (k = 0; k < g_y_n; k++) {
                mii(g_phy, g_y_reg[k], 1, g_y_val[k]);
                printf("nforce: PHY registro %u <- %04x\n", g_y_reg[k], g_y_val[k]);
            }
        }
        (void)mii(g_phy, MII_BMSR, 0, 0);
        bmsr = mii(g_phy, MII_BMSR, 0, 0);
        if (bmsr >= 0 && !(bmsr & BMSR_LINK)) {
            int bmcr = mii(g_phy, MII_BMCR, 0, 0);

            /* Niente link: si fa ripartire la negoziazione (senza reset) e
             * le si da' il tempo che vuole, fino a quattro secondi. */
            if (bmcr >= 0) {
                int t;

                /* ! UN PHY A RIPOSO NON NEGOZIA (8 ottobre 2026). Un BIOS che
                 * non avvia dalla rete lo lascia spesso spento o isolato: i
                 * due bit vanno tolti, o «rilancia» non rilancia niente. E se
                 * non annuncia nessuna velocita', gli si dicono le quattro di
                 * base (10 e 100, mezzo e pieno). */
                int ann = mii(g_phy, MII_ANNUNCIO, 0, 0);

                printf("nforce: niente link (BMCR %04x): sveglio il PHY e rilancio la "
                       "negoziazione\n", bmcr);
                if (ann >= 0 && (ann & 0x01E0) == 0)
                    mii(g_phy, MII_ANNUNCIO, 1, (unsigned int)ann | 0x01E1);
                bmcr &= ~(BMCR_SPENTO | BMCR_ISOLATO);
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
    attimo();
    reg_scrivi(R_ALIMENTAZIONE, reg_leggi(R_ALIMENTAZIONE) | ALIM_VALIDO);

    reg_scrivi(R_FILTRO, FILTRO_SEMPRE | FILTRO_PROMISCUO);

    g_link = -1;                            /* cosi' il primo aggiornamento scrive */
    link_aggiorna();
    {
        unsigned int k;

        for (k = 0; k < g_w_n; k++) {
            reg_scrivi(g_w_reg[k], g_w_val[k]);
            printf("nforce: registro %03x <- %08x\n", g_w_reg[k], g_w_val[k]);
        }
    }

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
            if (memcmp(rx_buf(g_rx_qui), g_mac, 6) == 0) g_per_noi++;
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
    i = uptime_ms();
    while (d[D_FLAG / 4] & TX_DA_MANDARE) { /* fino a 200 ms */
        if (uptime_ms() - i > 200u) {
            g_cont.errori_tx++;
            return -1;
        }
        attimo();
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
        { R_FILTRO, "filtro" }, { R_OFFLOAD, "rx misura" }, { R_RX_CONTROLLO, "rx controllo" },
        { R_SLOT, "slot" }, { R_TX_RINVIO, "tx rinvio" }, { R_TX_SOGLIA, "tx soglia" },
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

/* Il PHY, letto dal filo di gestione: e' li' che sta scritto se il cavo c'e'.
 * BMSR si legge due volte perche' il bit del link e' «a ritenuta». */
static void stampa_phy(void)
{
    int bmcr, bmsr;

    if (phy_cerca() != 0) {
        printf("  PHY: nessuno risponde sul filo di gestione\n");
        return;
    }
    bmcr = mii(g_phy, MII_BMCR, 0, 0);
    (void)mii(g_phy, MII_BMSR, 0, 0);
    bmsr = mii(g_phy, MII_BMSR, 0, 0);
    printf("  PHY %u  identita' %04x:%04x\n", g_phy,
           mii(g_phy, MII_ID1, 0, 0) & 0xFFFF, mii(g_phy, MII_ID2, 0, 0) & 0xFFFF);
    printf("  PHY controllo %04x  stato %04x  annuncio %04x  compagno %04x  1000: %04x %04x\n",
           bmcr & 0xFFFF, bmsr & 0xFFFF,
           mii(g_phy, MII_ANNUNCIO, 0, 0) & 0xFFFF, mii(g_phy, MII_COMPAGNO, 0, 0) & 0xFFFF,
           mii(g_phy, MII_1000_CTRL, 0, 0) & 0xFFFF, mii(g_phy, MII_1000_STATO, 0, 0) & 0xFFFF);
    printf("  link: %s%s%s\n", (bmsr >= 0 && (bmsr & BMSR_LINK)) ? "SU" : "GIU' (cavo? switch?)",
           (bmcr >= 0 && (bmcr & BMCR_SPENTO)) ? ", PHY A RIPOSO" : "",
           (bmcr >= 0 && (bmcr & BMCR_ISOLATO)) ? ", PHY ISOLATO" : "");
}

/* I conteggi del driver che sta girando, se ce n'e' uno: dicono se i frame
 * escono (inviati), se ne arrivano (ricevuti), e dove si perdono. */
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
    /* ! DI CHI SONO, PRIMA DI TUTTO. Con due schede il driver in servizio
     * puo' essere quello dell'altra, e i suoi conteggi non dicono niente di
     * questa. */
    if (ipc_send(pid, NET_MSG_INFO, 0, 0) >= 0) {
        for (t = 0; t < 8; t++) {
            NetStato st;

            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) break;
            if ((int)meta.sender_pid != pid || meta.tipo != NET_MSG_STATO) continue;
            if (meta.len < sizeof(st)) break;
            memcpy(&st, buf, sizeof(st));
            st.modello[sizeof(st.modello) - 1] = 0;
            printf("  in servizio c'e': %s%s\n", st.modello,
                   strstr(st.modello, "nForce") ? "" : "  (NON QUESTA SCHEDA)");
            break;
        }
    }
    if (ipc_send(pid, NET_MSG_CONTATORI, 0, 0) < 0) return;
    for (t = 0; t < 8; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) break;
        if ((int)meta.sender_pid != pid || meta.tipo != NET_MSG_CONTEGGI) continue;
        if (meta.len < sizeof(c)) break;
        memcpy(&c, buf, sizeof(c));
        printf("  conteggi: inviati %u (errori %u), ricevuti %u (errori %u), "
               "persi in coda %u, giri %u\n", c.inviati, c.errori_tx, c.ricevuti,
               c.errori_rx, c.persi_coda, c.battiti);
        return;
    }
    printf("  conteggi: il driver in servizio non risponde\n");
}

/* -p: la prova da sola. Accende la scheda SENZA mettersi in servizio, per
 * cinque secondi manda una domanda ARP a tutti ogni mezzo secondo e conta
 * quel che arriva. Si puo' lanciare mentre un'altra scheda tiene la rete: e'
 * cosi' che la si prova da lontano. */
static void prova_da_sola(void)
{
    unsigned char f[60];
    unsigned int  i, giri;

    memset(f, 0, sizeof(f));
    memset(f, 0xFF, 6);                         /* a tutti */
    memcpy(f + 6, g_mac, 6);
    f[12] = 0x08; f[13] = 0x06;                 /* ARP */
    f[15] = 0x01; f[16] = 0x08; f[18] = 6; f[19] = 4; f[21] = 0x01;
    memcpy(f + 22, g_mac, 6);
    f[38] = 192; f[39] = 168; f[40] = 0; f[41] = 1;

    printf("nforce: prova di 5 secondi (una domanda ARP ogni mezzo secondo)\n");
    for (giri = 0; giri < 250; giri++) {
        if (giri % 25 == 0) {
            int r = trasmetti(f, sizeof(f));

            if (r != 0) printf("nforce: la trasmissione %u non parte\n", giri / 25);
        }
        usleep(PERIODO_MS * 1000);
        g_cont.battiti++;
        servi_scheda();
        g_coda_conta = 0;                       /* nessuno li legge: si contano e basta */
    }

    printf("nforce: inviati %u (errori %u), ricevuti %u (errori %u), di cui %u "
           "diretti a questa scheda\n", g_cont.inviati, g_cont.errori_tx,
           g_cont.ricevuti, g_cont.errori_rx, g_per_noi);
    for (i = 0; i < 8; i++)
        printf("%s%08x", i ? " " : "  tx flag: ", tx_desc(i)[D_FLAG / 4]);
    printf("\n");
    for (i = 0; i < 8; i++)
        printf("%s%08x", i ? " " : "  rx flag: ", rx_desc(i)[D_FLAG / 4]);
    printf("\n");
    stampa_registri();
    printf("nforce: %s\n", (tx_desc(0)[D_FLAG / 4] & TX_DA_MANDARE)
           ? "NON TRASMETTE: il primo descrittore e' ancora da mandare."
           : "TRASMETTE.");
    printf("nforce: %s\n",
           g_cont.ricevuti ? "RICEVE." :
           "NON RICEVE NIENTE (su una rete viva in 5 secondi qualcosa passa).");
    if (g_cont.ricevuti && g_per_noi == 0)
        printf("nforce: MA NIENTE DI DIRETTO A LEI: se il router ha risposto alle\n"
               "        domande ARP, il filtro per indirizzo lo sta scartando.\n");
    ferma();
    reg_scrivi(R_TX_CONTROLLO, reg_leggi(R_TX_CONTROLLO) & ~TXC_OSPITE);
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
    printf("  -d                 registri, PHY e conteggi com'e' adesso: e' la\n");
    printf("                     schermata da mandare quando la rete non va\n");
    printf("  -v                 racconta ogni passo dell'accensione\n");
    printf("  -l                 dice se il cavo c'e' (esce con 0) o no (1), e basta:\n");
    printf("                     lo usa netdetect per scegliere fra due schede\n");
    printf("  -p                 la prova da sola: accende, trasmette e conta per\n");
    printf("                     5 secondi senza mettersi in servizio\n");
    printf("  -r                 prepara il PHY Realtek RTL8211B prima di negoziare\n");
    printf("  -n                 non resetta il MAC\n");
    printf("  -w REG=VAL         scrive un registro (esadecimali) prima di avviare\n");
    printf("  -y REG=VAL         scrive un registro del PHY (esadecimali)\n");
}

int main(int argc, char **argv)
{
    int sonda = 0, registri = 0, prova = 0, solo_link = 0, i, rc;
    DmaZona z;
    MmioZona m;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)      sonda = 1;
        else if (strcmp(argv[i], "-d") == 0) registri = 1;
        else if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-p") == 0) prova = 1;
        else if (strcmp(argv[i], "-l") == 0) solo_link = 1;
        else if (strcmp(argv[i], "-r") == 0) g_phy_realtek = 1;
        else if (strcmp(argv[i], "-n") == 0) g_senza_reset = 1;
        else if ((strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "-y") == 0) && i + 1 < argc) {
            int   phy = argv[i][1] == 'y';
            char *u = strchr(argv[i + 1], '=');
            unsigned int r, val;

            if (u == NULL) { uso(); return 2; }
            r   = (unsigned int)strtoul(argv[i + 1], NULL, 16);
            val = (unsigned int)strtoul(u + 1, NULL, 16);
            if (phy) {
                if (g_y_n == IN_PIU_N || r > 31) { uso(); return 2; }
                g_y_reg[g_y_n] = r; g_y_val[g_y_n++] = val & 0xFFFF;
            } else {
                if (g_w_n == IN_PIU_N || r >= NV_MMIO_BYTE || (r & 3)) { uso(); return 2; }
                g_w_reg[g_w_n] = r; g_w_val[g_w_n++] = val;
            }
            i++;
        }
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

    if (solo_link) {
        int bmsr = -1;

        if (phy_cerca() == 0) {
            (void)mii(g_phy, MII_BMSR, 0, 0);
            bmsr = mii(g_phy, MII_BMSR, 0, 0);
        }
        rc = (bmsr >= 0 && (bmsr & BMSR_LINK)) ? 0 : 1;
        printf("nforce: link %s\n", rc == 0 ? "SU" : "GIU'");
        return rc;
    }

    if (registri) {
        printf("nforce: %s, registri a 0x%08x, cosi' come sono adesso\n",
               g_modello, g_mmio_fis);
        stampa_registri();
        stampa_phy();
        stampa_conteggi();
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

    {
        unsigned int t0 = uptime_ms();

        if (inizializza_scheda() != 0) return 1;
        printf("nforce: scheda accesa in %u ms\n", uptime_ms() - t0);
    }

    stampa_stato();
    if (g_verboso) stampa_registri();

    if (prova) {
        prova_da_sola();
        return 0;
    }

    if (ipc_register(NET_SERVIZIO_0) < 0) {
        printf("nforce: non riesco a registrare il servizio '%s'\n", NET_SERVIZIO_0);
        return 1;
    }
    printf("nforce: servizio '%s' attivo\n", NET_SERVIZIO_0);

    servi();
    return 0;
}
