/* =============================================================================
 * kernel/block/ahci.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * AHCI — i dischi SATA come li parla un controller di oggi (9 ottobre 2026)
 *
 * Nasce per il PC di prova (NVIDIA MCP73): li' il disco si raggiungeva solo
 * dai registri IDE «di cortesia» che il controller offre accanto a quelli veri,
 * e solo in PIO — il bus master IDE di quel chipset dice «finito» senza aver
 * mosso un byte. I registri veri sono in memoria, a BAR5, e sono AHCI.
 *
 * COME LAVORA
 *
 *   - I registri dell'HBA si mappano a un indirizzo virtuale FISSO e BASSO
 *     (AHCI_VIRT), nella tabella delle pagine che ogni processo condivide col
 *     kernel: un disco si legge anche da dentro una chiamata di sistema, con
 *     le pagine di un processo qualunque. E' la stessa scelta dell'APIC.
 *   - Per ogni disco: l'elenco dei comandi (32 intestazioni), l'area dove il
 *     disco scrive le sue risposte (FIS), e UNA tabella di comando. Si usa
 *     sempre e solo lo slot 0: un comando per volta.
 *   - Niente interrupt: si lancia il comando e si guarda il registro finche'
 *     lo slot non si libera. Come nel resto del sottosistema dei dischi, che
 *     il kernel usa prima che la catena degli interrupt sia completa.
 *   - I dati passano da un buffer di rimbalzo, come nel DMA dell'IDE: chi
 *     chiama puo' passare memoria che non e' contigua.
 *
 * ! NON SI RESETTA L'HBA. Il BIOS lo lascia configurato, col disco gia'
 * negoziato; un reset qui sotto vorrebbe dire rinegoziare il link del disco da
 * cui il kernel e' appena stato caricato, per non guadagnare niente.
 *
 * Solo dischi ATA: un lettore ottico su una porta AHCI si riconosce dalla
 * firma e si lascia stare.
 * ============================================================================= */

#include "kernel.h"
#include "paging.h"
#include "sched.h"      /* g_ticks */
#include "ahci.h"

/* Due pagine: 0x100 di registri generali piu' 0x80 per porta, fino a 32. */
#define AHCI_VIRT           0x000DD000u

/* --- registri generali --------------------------------------------------- */
#define H_CAP               0x00
#define H_GHC               0x04
#define H_IS                0x08
#define H_PI                0x0C
#define H_VS                0x10
#define H_CAP2              0x24
#define H_BOHC              0x28

#define GHC_AE              0x80000000u     /* «parlo AHCI» */
#define GHC_IE              0x00000002u     /* interrupt accesi */

/* --- registri di una porta (da 0x100 + n * 0x80) ------------------------- */
#define P_CLB               0x00
#define P_CLBU              0x04
#define P_FB                0x08
#define P_FBU               0x0C
#define P_IS                0x10
#define P_IE                0x14
#define P_CMD               0x18
#define P_TFD               0x20
#define P_SIG               0x24
#define P_SSTS              0x28
#define P_SCTL              0x2C
#define P_SERR              0x30
#define P_CI                0x38

#define CMD_ST              0x00000001u     /* la porta esegue comandi */
#define CMD_SUD             0x00000002u
#define CMD_POD             0x00000004u
#define CMD_FRE             0x00000010u     /* la porta riceve le risposte */
#define CMD_FR              0x00004000u
#define CMD_CR              0x00008000u
#define IS_TFES             0x40000000u     /* il disco ha detto «errore» */
#define FIRMA_DISCO         0x00000101u

#define ATA_IDENTIFY        0xEC
#define ATA_READ_DMA_EXT    0x25
#define ATA_WRITE_DMA_EXT   0x35
#define ATA_READ_DMA        0xC8
#define ATA_WRITE_DMA       0xCA
#define ATA_FLUSH           0xE7
#define ATA_FLUSH_EXT       0xEA

#define RIMBALZO_BYTE       65536u
#define SETT_MAX            (RIMBALZO_BYTE / 512u)

#define TMO_CMD_MS          8000u

/* Per le prove: 1 rifa' il reset del link su ogni porta, anche dove non
 * serve. In QEMU e' l'unico modo di percorrere quella strada. */
#ifndef FORZA_RESET_LINK
#define FORZA_RESET_LINK    0
#endif

typedef struct {
    volatile uint8_t *reg;      /* i registri di QUESTA porta */
    uint8_t   lba48;
    uint64_t  settori;
    uint16_t  id[256];
    /* La memoria che il controller legge e scrive: allineata come vuole. */
    uint32_t  elenco[32 * 8] __attribute__((aligned(1024)));
    uint8_t   fis[256]       __attribute__((aligned(256)));
    uint8_t   tabella[256]   __attribute__((aligned(128)));
} AhciDisco;

static AhciDisco g_disco[AHCI_MAX_DISCHI];
static int       g_n = 0;
static uint8_t   g_rimbalzo[RIMBALZO_BYTE] __attribute__((aligned(4096)));

/* I controller gia' presi: bus, slot, funzione in un numero solo. */
static uint32_t  g_preso[4];
static int       g_n_presi = 0;

static volatile uint8_t *g_hba = 0;

void *memset(void *dst, int c, uint32_t n);

static uint32_t rl(volatile uint8_t *b, uint32_t off) { return *(volatile uint32_t *)(b + off); }
static void     wl(volatile uint8_t *b, uint32_t off, uint32_t v) { *(volatile uint32_t *)(b + off) = v; }

/* -----------------------------------------------------------------------------
 * Lo spazio di configurazione PCI (meccanismo 1), per trovare il controller
 * --------------------------------------------------------------------------- */
static uint32_t pci32(uint32_t bus, uint32_t slot, uint32_t fn, uint32_t off)
{
    port_outl(0xCF8, 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (off & 0xFC));
    return port_inl(0xCFC);
}

static void pci32_scrivi(uint32_t bus, uint32_t slot, uint32_t fn, uint32_t off, uint32_t v)
{
    port_outl(0xCF8, 0x80000000u | (bus << 16) | (slot << 11) | (fn << 8) | (off & 0xFC));
    port_outl(0xCFC, v);
}

/* Aspetta che (registro & maschera) valga `atteso`. 0 se e' successo. */
static int aspetta(volatile uint8_t *b, uint32_t off, uint32_t maschera,
                   uint32_t atteso, uint32_t ms)
{
    uint32_t fine = g_ticks + (ms + 9u) / 10u + 1u;

    for (;;) {
        if ((rl(b, off) & maschera) == atteso) return 0;
        if (g_ticks >= fine) return -1;
        __asm__ volatile ("pause");
    }
}

/* -----------------------------------------------------------------------------
 * Un comando, sullo slot 0. Rende 0, o -1.
 * --------------------------------------------------------------------------- */
static int comando(AhciDisco *d, uint8_t cmd, uint64_t lba, uint32_t n,
                   uint32_t byte, int scrivi)
{
    volatile uint8_t *p = d->reg;
    uint8_t  *t = d->tabella;
    uint32_t *prd = (uint32_t *)(d->tabella + 0x80);

    if (aspetta(p, P_TFD, 0x88u, 0, 1000) != 0) {
        klog(LOG_WARN, "AHCI: il disco resta occupato (TFD %08x)", rl(p, P_TFD));
        return -1;
    }

    memset(t, 0, 0x80);
    t[0]  = 0x27;                       /* dal sistema al disco */
    t[1]  = 0x80;                       /* e' un comando, non un controllo */
    t[2]  = cmd;
    t[4]  = (uint8_t)lba;
    t[5]  = (uint8_t)(lba >> 8);
    t[6]  = (uint8_t)(lba >> 16);
    t[7]  = 0x40;                       /* indirizzi LBA */
    t[8]  = (uint8_t)(lba >> 24);
    t[9]  = (uint8_t)(lba >> 32);
    t[10] = (uint8_t)(lba >> 40);
    t[12] = (uint8_t)n;
    t[13] = (uint8_t)(n >> 8);
    if (cmd == ATA_READ_DMA || cmd == ATA_WRITE_DMA)
        t[7] = (uint8_t)(0xE0 | ((lba >> 24) & 0x0F));  /* 28 bit: il resto sta qui */

    /* Una voce sola: tutto il trasferimento sta nel buffer di rimbalzo. */
    if (byte) {
        prd[0] = (uint32_t)(uintptr_t)g_rimbalzo;
        prd[1] = 0;
        prd[2] = 0;
        prd[3] = byte - 1u;
    }

    /* L'intestazione dello slot 0: cinque parole di comando, il verso, quante
     * voci di dati, e dov'e' la tabella. */
    d->elenco[0] = 5u | (scrivi ? (1u << 6) : 0u) | ((byte ? 1u : 0u) << 16);
    d->elenco[1] = 0;
    d->elenco[2] = (uint32_t)(uintptr_t)d->tabella;
    d->elenco[3] = 0;

    wl(p, P_IS, 0xFFFFFFFFu);
    wl(p, P_CI, 1u);

    {
        uint32_t fine = g_ticks + (TMO_CMD_MS + 9u) / 10u + 1u;

        for (;;) {
            if (rl(p, P_IS) & IS_TFES) {
                klog(LOG_ERROR, "AHCI: il disco rifiuta il comando %02x a lba=%u "
                     "(TFD %08x, SERR %08x)", cmd, (uint32_t)lba,
                     rl(p, P_TFD), rl(p, P_SERR));
                /* Dopo un errore la porta si ferma: la si fa ripartire. */
                wl(p, P_CMD, rl(p, P_CMD) & ~CMD_ST);
                (void)aspetta(p, P_CMD, CMD_CR, 0, 500);
                wl(p, P_SERR, 0xFFFFFFFFu);
                wl(p, P_IS, 0xFFFFFFFFu);
                wl(p, P_CMD, rl(p, P_CMD) | CMD_ST);
                return -1;
            }
            if (!(rl(p, P_CI) & 1u)) break;
            if (g_ticks >= fine) {
                klog(LOG_ERROR, "AHCI: comando %02x a lba=%u senza risposta dopo %u ms "
                     "(CI %08x, IS %08x, TFD %08x)", cmd, (uint32_t)lba, TMO_CMD_MS,
                     rl(p, P_CI), rl(p, P_IS), rl(p, P_TFD));
                return -1;
            }
            __asm__ volatile ("pause");
        }
    }

    if (rl(p, P_TFD) & 0x01u) return -1;    /* ERR nello stato */
    return 0;
}

/* -----------------------------------------------------------------------------
 * Una porta: fermarla, darle la sua memoria, farla ripartire
 * --------------------------------------------------------------------------- */
static int porta_prepara(AhciDisco *d, volatile uint8_t *p)
{
    uint32_t ssts;

    wl(p, P_CMD, rl(p, P_CMD) & ~CMD_ST);
    if (aspetta(p, P_CMD, CMD_CR, 0, 500) != 0) return -1;
    wl(p, P_CMD, rl(p, P_CMD) & ~CMD_FRE);
    if (aspetta(p, P_CMD, CMD_FR, 0, 500) != 0) return -1;

    memset(d->elenco, 0, sizeof(d->elenco));
    memset(d->fis, 0, sizeof(d->fis));

    wl(p, P_CLB,  (uint32_t)(uintptr_t)d->elenco);
    wl(p, P_CLBU, 0);
    wl(p, P_FB,   (uint32_t)(uintptr_t)d->fis);
    wl(p, P_FBU,  0);
    wl(p, P_IE,   0);
    wl(p, P_SERR, 0xFFFFFFFFu);
    wl(p, P_IS,   0xFFFFFFFFu);

    wl(p, P_CMD, rl(p, P_CMD) | CMD_FRE | CMD_SUD | CMD_POD);

    ssts = rl(p, P_SSTS);

    /* ! SE LA PORTA NON E' MAI STATA USATA IN AHCI, LA FIRMA NON C'E' ANCORA
     * (9 ottobre 2026). Letto sul PC di prova, senza toccare niente: link su
     * (SSTS 123), ma firma ffffffff e stato «occupato» (TFD 80). Il BIOS quel
     * controller l'ha sempre usato dai registri IDE; in AHCI il disco si
     * presenta con un messaggio che manda dopo un reset del link, e che la
     * porta puo' ricevere solo da quando e' accesa la ricezione (FRE), cioe'
     * da adesso. Si rifa' il reset del link e si aspetta quel messaggio. */
    if (FORZA_RESET_LINK || (ssts & 0x0Fu) != 3u ||
        rl(p, P_SIG) == 0xFFFFFFFFu || (rl(p, P_TFD) & 0x88u)) {
        uint32_t fine;

        wl(p, P_SCTL, (rl(p, P_SCTL) & ~0x0Fu) | 1u);       /* DET = 1: reset */
        fine = g_ticks + 2u;
        while (g_ticks < fine) __asm__ volatile ("pause");  /* almeno 1 ms */
        wl(p, P_SCTL, rl(p, P_SCTL) & ~0x0Fu);              /* DET = 0: via */

        /* Il link torna su: un disco che deve anche rimettersi a girare ci
         * mette di piu' di uno gia' sveglio. */
        if (aspetta(p, P_SSTS, 0x0Fu, 3u, 5000) != 0) return -1;
        wl(p, P_SERR, 0xFFFFFFFFu);     /* senza, il messaggio del disco non entra */
        wl(p, P_IS,   0xFFFFFFFFu);
    }

    if (aspetta(p, P_TFD, 0x88u, 0, 10000) != 0) return -1;

    wl(p, P_CMD, rl(p, P_CMD) | CMD_ST);
    d->reg = p;
    return 0;
}

static int disco_identifica(AhciDisco *d)
{
    uint32_t i;

    if (comando(d, ATA_IDENTIFY, 0, 0, 512, 0) != 0) return -1;
    for (i = 0; i < 256; i++)
        d->id[i] = (uint16_t)(g_rimbalzo[i * 2] | (g_rimbalzo[i * 2 + 1] << 8));

    d->lba48 = (d->id[83] & (1 << 10)) ? 1 : 0;
    d->settori = 0;
    if (d->lba48)
        d->settori = (uint64_t)d->id[100] | ((uint64_t)d->id[101] << 16) |
                     ((uint64_t)d->id[102] << 32) | ((uint64_t)d->id[103] << 48);
    if (d->settori == 0)
        d->settori = (uint64_t)d->id[60] | ((uint64_t)d->id[61] << 16);
    return d->settori ? 0 : -1;
}

/* Una porta com'era prima che la si toccasse: ferma, poi i suoi registri. */
static void porta_rimetti(volatile uint8_t *p, const uint32_t *prima)
{
    wl(p, P_CMD, rl(p, P_CMD) & ~CMD_ST);
    (void)aspetta(p, P_CMD, CMD_CR, 0, 500);
    wl(p, P_CMD, rl(p, P_CMD) & ~CMD_FRE);
    (void)aspetta(p, P_CMD, CMD_FR, 0, 500);
    wl(p, P_CLB, prima[1]);
    wl(p, P_FB,  prima[2]);
    wl(p, P_IE,  prima[3]);
    wl(p, P_CMD, prima[0]);
}

/* -----------------------------------------------------------------------------
 * Un controller: accenderlo in AHCI e guardare le sue porte
 * --------------------------------------------------------------------------- */
static int controller(uint32_t bus, uint32_t slot, uint32_t fn, uint32_t abar)
{
    volatile uint8_t *h;
    uint32_t pi, vs, i, cmd;
    uint32_t ghc_prima, salvati[32][4], link_prima[32];
    int trovati = 0;

    if (g_hba != 0) {
        klog(LOG_WARN, "AHCI: un secondo controller (PCI %u:%u.%u) resta fuori", bus, slot, fn);
        return 0;
    }

    /* Memoria e bus master accesi: senza il secondo il controller non puo'
     * leggere ne' scrivere i dati. */
    cmd = pci32(bus, slot, fn, 0x04);
    if ((cmd & 0x06u) != 0x06u) pci32_scrivi(bus, slot, fn, 0x04, cmd | 0x06u);

    h = (volatile uint8_t *)paging_mappa_mmio_basso(AHCI_VIRT, abar, 2);

    /* ! QUEL CHE SI TOCCA SI RICORDA, E SE NON SI PRENDE NIENTE SI RIMETTE
     * (9 ottobre 2026). Sul PC di prova il passaggio all'AHCI non e' riuscito
     * e ha lasciato il controller a meta': modo AHCI acceso, porta fermata,
     * disco non adottato. Da quell'istante anche i registri IDE restituivano
     * dati sbagliati — i programmi caricati dal disco morivano di page fault
     * alla prima istruzione — e la macchina e' rimasta da spegnere a mano. Un
     * tentativo fallito deve lasciare tutto com'era. */
    ghc_prima = rl(h, H_GHC);
    for (i = 0; i < 32; i++) {
        volatile uint8_t *p = h + 0x100 + i * 0x80;

        salvati[i][0] = rl(p, P_CMD);
        salvati[i][1] = rl(p, P_CLB);
        salvati[i][2] = rl(p, P_FB);
        salvati[i][3] = rl(p, P_IE);
        link_prima[i] = rl(p, P_SSTS);  /* dove c'era un disco PRIMA di toccare */
    }

    /* «Parlo AHCI», e niente interrupt. Poi si guarda se dall'altra parte
     * c'e' davvero un AHCI: la versione e le porte dichiarate. */
    wl(h, H_GHC, rl(h, H_GHC) | GHC_AE);
    wl(h, H_GHC, rl(h, H_GHC) & ~GHC_IE);
    vs = rl(h, H_VS);
    pi = rl(h, H_PI);
    if (!(rl(h, H_GHC) & GHC_AE) || vs == 0 || vs == 0xFFFFFFFFu || pi == 0) {
        klog(LOG_WARN, "AHCI: PCI %u:%u.%u a 0x%08x non risponde come un AHCI "
             "(versione %08x, porte %08x, GHC %08x)", bus, slot, fn, abar, vs, pi,
             rl(h, H_GHC));
        wl(h, H_GHC, ghc_prima);
        return 0;
    }

    /* Se il BIOS dice di tenerlo lui, glielo si chiede. */
    if (rl(h, H_CAP2) & 1u) {
        wl(h, H_BOHC, rl(h, H_BOHC) | 2u);
        (void)aspetta(h, H_BOHC, 1u, 0, 200);
    }

    g_hba = h;
    klog(LOG_INFO, "AHCI: controller in PCI %u:%u.%u, registri a 0x%08x, versione "
         "%u.%u, porte %08x", bus, slot, fn, abar, (vs >> 16) & 0xFFFF,
         (vs >> 8) & 0xFF, pi);

    for (i = 0; i < 32 && g_n < AHCI_MAX_DISCHI; i++) {
        volatile uint8_t *p = h + 0x100 + i * 0x80;
        AhciDisco *d = &g_disco[g_n];
        uint32_t sig;

        if (!(pi & (1u << i))) continue;
        klog(LOG_INFO, "AHCI: porta %u prima: CMD %08x SSTS %08x SIG %08x TFD %08x "
             "CLB %08x", i, rl(p, P_CMD), rl(p, P_SSTS), rl(p, P_SIG), rl(p, P_TFD),
             rl(p, P_CLB));
        /* ! «NIENTE ATTACCATO» SI DECIDE CON QUEL CHE C'ERA PRIMA (9 ottobre
         * 2026). Sul PC di prova la porta 0 ha il disco e il link su finche'
         * il controller e' in modo IDE; nell'istante in cui si accende il
         * modo AHCI il link CADE (SSTS da 123 a 100) e va ristabilito. Chi
         * guarda il link solo adesso conclude che non c'e' niente. */
        if ((rl(p, P_SSTS) & 0x0Fu) != 3u && (link_prima[i] & 0x0Fu) != 3u) continue;

        /* Una firma gia' letta che non e' di un disco (un lettore ottico)
         * basta a lasciar stare la porta. Una firma che non c'e' ancora no:
         * la si guarda dopo aver preparato la porta. */
        sig = rl(p, P_SIG);
        if (sig != FIRMA_DISCO && sig != 0xFFFFFFFFu && !FORZA_RESET_LINK) {
            klog(LOG_INFO, "AHCI: porta %u: non e' un disco (firma %08x): la lascio",
                 i, sig);
            continue;
        }
        if (porta_prepara(d, p) != 0) {
            klog(LOG_WARN, "AHCI: porta %u: non si prepara (CMD %08x, SSTS %08x, "
                 "TFD %08x, SERR %08x)", i, rl(p, P_CMD), rl(p, P_SSTS),
                 rl(p, P_TFD), rl(p, P_SERR));
            porta_rimetti(p, salvati[i]);
            continue;
        }
        sig = rl(p, P_SIG);
        if (sig != FIRMA_DISCO) {
            klog(LOG_INFO, "AHCI: porta %u: non e' un disco (firma %08x): la lascio",
                 i, sig);
            porta_rimetti(p, salvati[i]);
            continue;
        }
        if (disco_identifica(d) != 0) {
            klog(LOG_WARN, "AHCI: porta %u: il disco non si presenta", i);
            porta_rimetti(p, salvati[i]);
            continue;
        }
        klog(LOG_INFO, "AHCI: porta %u: disco di %u MB%s", i,
             (uint32_t)(d->settori / 2048), d->lba48 ? " LBA48" : " LBA28");
        g_n++;
        trovati++;
    }

    /* Nessun disco adottato: il controller torna com'era, porta per porta, e
     * non lo si considera preso — i registri IDE restano la sua strada. */
    if (trovati == 0) {
        for (i = 0; i < 32; i++) {
            volatile uint8_t *p = h + 0x100 + i * 0x80;

            if (!(pi & (1u << i))) continue;
            porta_rimetti(p, salvati[i]);
        }
        wl(h, H_GHC, ghc_prima);
        g_hba = 0;
        klog(LOG_INFO, "AHCI: nessun disco adottato in PCI %u:%u.%u: ho rimesso i "
             "registri com'erano", bus, slot, fn);
        return 0;
    }

    if (g_n_presi < 4) g_preso[g_n_presi++] = (bus << 16) | (slot << 8) | fn;
    return trovati;
}

/* =============================================================================
 * Verso il resto del kernel
 * ============================================================================= */

/* Cerca i controller. Con `anche_emulati` a zero prende solo quelli che si
 * dichiarano AHCI (classe 01.06, interfaccia 01); a uno anche quelli che il
 * BIOS presenta come IDE o RAID ma hanno i registri AHCI a BAR5. Rende quanti
 * dischi ci sono in tutto dopo la ricerca. */
int ahci_cerca(int anche_emulati)
{
    uint32_t bus, slot, fn;

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (fn = 0; fn < 8; fn++) {
                uint32_t id = pci32(bus, slot, fn, 0x00), classe, abar;
                uint32_t sotto, interf;

                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                classe = pci32(bus, slot, fn, 0x08);
                if ((classe >> 24) != 0x01u) continue;
                sotto  = (classe >> 16) & 0xFF;
                interf = (classe >> 8) & 0xFF;

                if (ahci_controller_preso(bus, slot, fn)) continue;
                if (sotto == 0x06 && interf == 0x01) {
                    /* un AHCI dichiarato */
                } else if (anche_emulati && (sotto == 0x01 || sotto == 0x04 || sotto == 0x06)) {
                    /* forse: lo dira' lui */
                } else {
                    continue;
                }

                abar = pci32(bus, slot, fn, 0x24);
                if ((abar & 1u) || (abar & 0xFFFFF000u) == 0) continue;     /* non e' memoria */
                (void)controller(bus, slot, fn, abar & 0xFFFFF000u);
            }
        }
    }
    return g_n;
}

/* =============================================================================
 * ahci_guarda — i registri di ogni controller che POTREBBE essere AHCI, nel log
 *
 * ! SOLA LETTURA, E VIENE PRIMA DI OGNI TENTATIVO (9 ottobre 2026). Il primo
 * passaggio all'AHCI sul PC di prova e' stato fatto alla cieca ed e' andato
 * male; quel che serviva sapere prima — se il controller e' gia' in modo AHCI,
 * quali porte ha, se il disco c'e' e come si presenta — stava in questi
 * registri, che si possono leggere senza cambiare niente. Non si scrive
 * nemmeno il registro di comando PCI. Rende quanti controller ha guardato.
 * ============================================================================= */
int ahci_guarda(void)
{
    uint32_t bus, slot, fn, i;
    int visti = 0;

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (fn = 0; fn < 8; fn++) {
                uint32_t id = pci32(bus, slot, fn, 0x00), classe, abar, pi;
                volatile uint8_t *h;

                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                classe = pci32(bus, slot, fn, 0x08);
                if ((classe >> 24) != 0x01u) continue;
                abar = pci32(bus, slot, fn, 0x24);
                if ((abar & 1u) || (abar & 0xFFFFF000u) == 0) continue;
                if (g_hba != 0 && ahci_controller_preso(bus, slot, fn)) {
                    klog(LOG_INFO, "AHCI: PCI %u:%u.%u e' gia' in uso da questo driver",
                         bus, slot, fn);
                    visti++;
                    continue;
                }
                if (g_hba != 0) continue;       /* la finestra e' di un altro */

                h = (volatile uint8_t *)paging_mappa_mmio_basso(AHCI_VIRT,
                                                                abar & 0xFFFFF000u, 2);
                pi = rl(h, H_PI);
                klog(LOG_INFO, "AHCI: PCI %u:%u.%u %04x:%04x classe %06x comando %04x "
                     "BAR5 %08x", bus, slot, fn, id & 0xFFFF, id >> 16, classe >> 8,
                     pci32(bus, slot, fn, 0x04) & 0xFFFF, abar);
                klog(LOG_INFO, "AHCI:   CAP %08x GHC %08x IS %08x PI %08x VS %08x "
                     "CAP2 %08x BOHC %08x", rl(h, H_CAP), rl(h, H_GHC), rl(h, H_IS), pi,
                     rl(h, H_VS), rl(h, H_CAP2), rl(h, H_BOHC));
                for (i = 0; i < 32; i++) {
                    volatile uint8_t *p = h + 0x100 + i * 0x80;

                    if (pi != 0xFFFFFFFFu && !(pi & (1u << i))) continue;
                    if (pi == 0xFFFFFFFFu && i > 7) break;
                    klog(LOG_INFO, "AHCI:   porta %u: CMD %08x SSTS %08x SIG %08x "
                         "TFD %08x IS %08x SERR %08x CLB %08x FB %08x", i,
                         rl(p, P_CMD), rl(p, P_SSTS), rl(p, P_SIG), rl(p, P_TFD),
                         rl(p, P_IS), rl(p, P_SERR), rl(p, P_CLB), rl(p, P_FB));
                }
                visti++;
            }
        }
    }
    if (visti == 0) klog(LOG_INFO, "AHCI: nessun controller di dischi con registri a BAR5");
    return visti;
}

/* C'e' un controller che il BIOS presenta come IDE o RAID, ma che ha i
 * registri AHCI a BAR5 e un disco attaccato? Sola lettura: e' la domanda che
 * fa l'installatore prima di proporre il passaggio. 1 si', 0 no. */
int ahci_candidati(void)
{
    uint32_t bus, slot, fn, i;

    if (g_hba != 0) return 0;           /* la finestra dei registri e' occupata */

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (fn = 0; fn < 8; fn++) {
                uint32_t id = pci32(bus, slot, fn, 0x00), classe, abar, pi, vs;
                uint32_t sotto, interf;
                volatile uint8_t *h;

                if ((id & 0xFFFFu) == 0xFFFFu) continue;
                classe = pci32(bus, slot, fn, 0x08);
                if ((classe >> 24) != 0x01u) continue;
                sotto  = (classe >> 16) & 0xFF;
                interf = (classe >> 8) & 0xFF;
                if (sotto == 0x06 && interf == 0x01) continue;  /* quello si usa gia' */
                if (sotto != 0x01 && sotto != 0x04 && sotto != 0x06) continue;
                abar = pci32(bus, slot, fn, 0x24);
                if ((abar & 1u) || (abar & 0xFFFFF000u) == 0) continue;

                h  = (volatile uint8_t *)paging_mappa_mmio_basso(AHCI_VIRT,
                                                                 abar & 0xFFFFF000u, 2);
                vs = rl(h, H_VS);
                pi = rl(h, H_PI);
                if (vs == 0 || vs == 0xFFFFFFFFu || pi == 0 || pi == 0xFFFFFFFFu) continue;
                if ((vs >> 16) == 0 || (vs >> 16) > 3) continue;    /* AHCI 1.x .. 3.x */
                for (i = 0; i < 32; i++)
                    if ((pi & (1u << i)) && (rl(h + 0x100 + i * 0x80, P_SSTS) & 0x0Fu) == 3u)
                        return 1;
            }
        }
    }
    return 0;
}

int ahci_controller_preso(uint32_t bus, uint32_t slot, uint32_t fn)
{
    int i;

    for (i = 0; i < g_n_presi; i++)
        if (g_preso[i] == ((bus << 16) | (slot << 8) | fn)) return 1;
    return 0;
}

int ahci_dischi(void) { return g_n; }

const uint16_t *ahci_identify(int k)
{
    return (k >= 0 && k < g_n) ? g_disco[k].id : 0;
}

uint64_t ahci_settori(int k)
{
    return (k >= 0 && k < g_n) ? g_disco[k].settori : 0;
}

int ahci_rw(int k, uint64_t lba, uint32_t n, void *buf, int scrivi)
{
    AhciDisco *d;
    uint8_t   *p = (uint8_t *)buf;

    if (k < 0 || k >= g_n || n == 0) return -1;
    d = &g_disco[k];
    if (lba + n > d->settori || lba + n < lba) return -1;

    while (n > 0) {
        uint32_t ora = n > SETT_MAX ? SETT_MAX : n, i, byte = ora * 512u;
        uint8_t  cmd;
        int      r;

        if (d->lba48) cmd = scrivi ? ATA_WRITE_DMA_EXT : ATA_READ_DMA_EXT;
        else          cmd = scrivi ? ATA_WRITE_DMA : ATA_READ_DMA;
        if (!d->lba48 && ora > 255) { ora = 255; byte = ora * 512u; }

        /* Un comando per volta: il buffer di rimbalzo e lo slot 0 sono uno
         * solo. Gli interrupt restano accesi, come nel driver IDE: le
         * scadenze si misurano con g_ticks, che cammina col timer. */
        if (scrivi) for (i = 0; i < byte; i++) g_rimbalzo[i] = p[i];
        r = comando(d, cmd, lba, ora, byte, scrivi);
        if (r == 0 && !scrivi) for (i = 0; i < byte; i++) p[i] = g_rimbalzo[i];
        if (r != 0) return -1;

        lba += ora; n -= ora; p += byte;
    }
    return 0;
}

int ahci_flush(int k)
{
    if (k < 0 || k >= g_n) return -1;
    return comando(&g_disco[k], g_disco[k].lba48 ? ATA_FLUSH_EXT : ATA_FLUSH, 0, 0, 0, 0);
}
