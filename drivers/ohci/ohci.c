/* =============================================================================
 * drivers/ohci/ohci.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * USB 1.1: il controller OHCI — l'altra meta' delle macchine vere
 *
 *     /dev/ohci.drv            trova il controller, enumera, serve il disco
 *     /dev/ohci.drv -v         dice tutto quello che vede
 *     /dev/ohci.drv -i         la sonda: dice se c'e' un OHCI, esce
 *
 * ! PERCHE' SERVE, VISTO CHE C'E' GIA' L'EHCI. Perche' un EHCI da solo non
 * serve nessuno: le porte USB 1.1 sono fisicamente le STESSE prese, e chi le
 * guida e' il controller compagno. ehci.drv cede al compagno tutto cio' che
 * non e' ad alta velocita' — e se quel compagno non ha un driver, cedere
 * significa buttare via. Sul SiS 964 dell'Acer i compagni sono tre OHCI.
 *
 * E c'e' il caso che ha fatto nascere tutto: un lettore di dischetti USB e'
 * full speed. Dietro l'EHCI non ci arriva mai.
 *
 * ! UHCI E OHCI FANNO LA STESSA COSA IN DUE MODI DIVERSI, e non e' una
 * ripetizione: sono due standard concorrenti per lo stesso bus. Intel e VIA
 * hanno messo UHCI, tutti gli altri — SiS, ALi, NVIDIA, ATI, Apple — OHCI.
 * Una macchina ha l'uno o l'altro, mai tutti e due.
 *
 * -----------------------------------------------------------------------------
 * ! COM'E' FATTO, IN UNA FRASE PER PEZZO
 *
 *   HCCA        una pagina condivisa dove il controller scrive cosa ha finito.
 *   ED          la descrizione di un endpoint: indirizzo, numero, verso,
 *               pacchetto massimo. Ne teniamo tre — controllo, bulk IN, bulk
 *               OUT — e stanno in due liste che il controller percorre.
 *   TD          un pezzo di trasferimento: da dove a dove, quanti byte.
 *
 * Si appendono i TD all'ED, si dice al controller «la lista e' piena», e si
 * guarda il CODICE DI CONDIZIONE del TD: vale 0xF finche' nessuno l'ha
 * toccato, e diventa 0 quando e' andata bene. E' la stessa forma dell'attesa
 * dell'EHCI, ed e' il posto dove si sbaglia allo stesso modo — vedi il
 * commento su esegui().
 *
 * ! IL CONTO DEI PACCHETTI STA NELL'ED, non nel TD: bit 1 di HeadP. Percio'
 * riscrivendo HeadP quel bit va CONSERVATO, e azzerato solo quando lo azzera
 * anche il dispositivo — cioe' dopo una CLEAR_FEATURE(ENDPOINT_HALT).
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "usb_comune.h"
#include "usb_massa.h"

/* +0.001 a ogni modifica: `ohci.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("ohci.drv", "0.001");

/* --- I registri, a spiazzamenti fissi (qui non si legge nessun CAPLENGTH) -- */
#define R_REVISION      0x00
#define R_CONTROL       0x04
#define R_CMDSTATUS     0x08
#define R_INTSTATUS     0x0C
#define R_INTENABLE     0x10
#define R_INTDISABLE    0x14
#define R_HCCA          0x18
#define R_CTRLHEADED    0x20
#define R_CTRLCURRED    0x24
#define R_BULKHEADED    0x28
#define R_BULKCURRED    0x2C
#define R_DONEHEAD      0x30
#define R_FMINTERVAL    0x34
#define R_PERIODICSTART 0x40
#define R_RHDESCA       0x48
#define R_RHSTATUS      0x50
#define R_RHPORT(n)     (0x54 + (n) * 4)

#define CTRL_CLE        0x00000010u     /* lista di controllo accesa */
#define CTRL_BLE        0x00000020u     /* lista bulk accesa */
#define CTRL_HCFS       0x000000C0u     /* in che stato e' */
#define CTRL_OPERATIVO  0x00000080u     /* HCFS = 2 */
#define CTRL_IR         0x00000100u     /* le interruzioni vanno alla SMM */

#define CMD_HCR         0x00000001u     /* reset */
#define CMD_CLF         0x00000002u     /* «ho messo roba nella lista di controllo» */
#define CMD_BLF         0x00000004u     /* idem, bulk */
#define CMD_OCR         0x00000008u     /* «il controller lo voglio io» */

/* Le porte del root hub */
#define RH_CCS          0x00000001u     /* c'e' qualcosa attaccato */
#define RH_PES          0x00000002u     /* porta abilitata */
#define RH_PRS          0x00000010u     /* reset in corso */
#define RH_PPS          0x00000100u     /* alimentata */
#define RH_LSDA         0x00000200u     /* e' un dispositivo low speed */
#define RH_CSC          0x00010000u
#define RH_PRSC         0x00100000u
#define RH_LPSC         0x00010000u     /* in HcRhStatus: accendi tutte */

/* --- La memoria condivisa col controller ----------------------------------
 * ! GLI ALLINEAMENTI NON SONO UN DETTAGLIO: l'HCCA vuole 256 byte, ED e TD ne
 * vogliono 16. La zona DMA nasce allineata a pagina, quindi basta scegliere
 * spiazzamenti multipli di 256 e non ci si pensa piu'. */
#define DMA_BYTE        (8u * 4096u)

#define OFF_HCCA        0x0000      /* 256 byte */
#define OFF_ED_CTRL     0x0100
#define OFF_ED_IN       0x0110
#define OFF_ED_OUT      0x0120
#define OFF_TD          0x0200      /* otto TD da 16 byte */
#define OFF_SETUP       0x0300
#define OFF_BUF         0x1000
#define BUF_MAX         4096

#define TD_N            8
#define TD_CODA         7           /* il TD finto che chiude ogni lista */

static volatile unsigned char *g_reg = 0;
static unsigned int g_dma_virt = 0, g_dma_fis = 0;
static unsigned int g_porte = 0;
static unsigned int g_verboso = 0;

/* -avvio: lanciato da /boot/avvio.sh, dove una riga di lamentela per ogni
 * macchina che quel controller non ce l'ha e' rumore a ogni accensione. */
static unsigned int g_avvio = 0;
static unsigned int g_indirizzo = 0;
static unsigned int g_low = 0;          /* il dispositivo e' low speed */
static UsbDispositivo g_dev;

#define VIRT(o)   ((volatile unsigned int *)(g_dma_virt + (o)))
#define FIS(o)    (g_dma_fis + (o))

static unsigned int rd(unsigned int off)
{
    return *(volatile unsigned int *)(g_reg + off);
}

static void wr(unsigned int off, unsigned int v)
{
    *(volatile unsigned int *)(g_reg + off) = v;
}

/* -----------------------------------------------------------------------------
 * TD ed ED
 * --------------------------------------------------------------------------- */
#define DP_SETUP    0u
#define DP_OUT      1u
#define DP_IN       2u

#define CC_DI(v)    (((v) >> 28) & 0xF)
#define CC_NIENTE   0xF     /* nessuno l'ha ancora toccato */
#define CC_OK       0x0
#define CC_STALLO   0x4
#define CC_MANCA    0x9     /* meno byte del previsto: non e' un guasto */

static unsigned int td_fis(unsigned int i) { return FIS(OFF_TD + i * 16); }
static volatile unsigned int *td(unsigned int i) { return VIRT(OFF_TD + i * 16); }

/* Prepara un TD. `toggle`: 0 = quello dell'ED, 2 = DATA0, 3 = DATA1. */
static void td_prepara(unsigned int i, int prossimo, unsigned int dp,
                       unsigned int toggle, unsigned int buf_fis,
                       unsigned int len)
{
    volatile unsigned int *t = td(i);

    /* ! «BUFFER ROUNDING» ACCESO (bit 18): dice che un pacchetto piu' corto
     * del previsto NON e' un errore. Senza, ogni risposta corta — che nel
     * protocollo delle chiavette e' la norma — fermerebbe l'endpoint. */
    t[0] = (1u << 18) | (dp << 19) | (7u << 21) | (toggle << 24) |
           (CC_NIENTE << 28);
    t[1] = len ? buf_fis : 0;
    t[2] = (prossimo < 0) ? 0u : td_fis((unsigned int)prossimo);
    t[3] = len ? (buf_fis + len - 1) : 0;
}

/* Quanti byte sono passati davvero. ! SE CBP E' ZERO SONO PASSATI TUTTI: e'
 * la convenzione dell'OHCI, e presa alla rovescia da' zero byte proprio nei
 * trasferimenti riusciti per intero. */
static unsigned int td_avuti(unsigned int i, unsigned int buf_fis,
                             unsigned int len)
{
    unsigned int cbp = td(i)[1];

    if (cbp == 0) return len;
    if (cbp < buf_fis) return 0;
    return cbp - buf_fis;
}

static void ed_prepara(unsigned int off, unsigned int indirizzo,
                       unsigned int ep, unsigned int dir, unsigned int maxp)
{
    volatile unsigned int *e = VIRT(off);

    e[0] = (indirizzo & 0x7F) | (ep << 7) | (dir << 11) |
           (g_low ? (1u << 13) : 0u) | (maxp << 16);
    e[1] = td_fis(TD_CODA);         /* coda */
    e[2] = td_fis(TD_CODA);         /* testa: lista vuota */
    e[3] = 0;
}

/* Cambia indirizzo, endpoint e pacchetto massimo senza toccare il resto. */
static void ed_indirizzo(unsigned int off, unsigned int indirizzo,
                         unsigned int ep, unsigned int dir, unsigned int maxp)
{
    volatile unsigned int *e = VIRT(off);

    e[0] = (e[0] & ~0x07FF07FFu) | (indirizzo & 0x7F) | (ep << 7) |
           (dir << 11) | (g_low ? (1u << 13) : 0u) | (maxp << 16);
}

/* Appende una catena di TD a un ED e aspetta.
 *
 * ! SI GUARDA IL CODICE DI CONDIZIONE DEI TD, NON L'ED, per la stessa ragione
 * per cui sull'EHCI si guardano i qTD e non la coda: l'ED lo prepariamo noi, e
 * un ED che il controller non ha mai visto ha l'aria di uno che ha finito. Il
 * codice di condizione invece nasce a 0xF — «nessuno l'ha toccato» — e quel
 * valore lo scriviamo apposta: se resta li', la risposta e' «non e' successo
 * niente», che e' la verita'.
 *
 * Rende 0, USB_MASSA_STALLO, oppure -1. */
static int esegui(unsigned int ed_off, unsigned int primo, unsigned int ultimo,
                  unsigned int quale_lista, unsigned int ms)
{
    volatile unsigned int *e = VIRT(ed_off);
    unsigned int giri, i, cc;

    /* ! IL BIT 1 DI HeadP E' IL CONTO DEI PACCHETTI, e va conservato: e' li'
     * che l'OHCI tiene il toggle fra un trasferimento e l'altro. Il bit 0
     * invece e' «fermato», e si azzera apposta. */
    e[2] = td_fis(primo) | (e[2] & 0x2u);
    e[1] = td_fis(TD_CODA);

    /* «C'e' roba da fare»: senza questo il controller puo' non riguardare la
     * lista fino al prossimo giro. */
    wr(R_CMDSTATUS, quale_lista);

    for (giri = 0; giri < ms * 10; giri++) {
        int finito = 1;

        for (i = primo; i <= ultimo; i++) {
            cc = CC_DI(td(i)[0]);
            if (cc == CC_NIENTE) { finito = 0; break; }
            if (cc == CC_STALLO) {
                if (g_verboso) printf("ohci: endpoint fermato\n");
                return USB_MASSA_STALLO;
            }
            if (cc != CC_OK && cc != CC_MANCA) {
                if (g_verboso) printf("ohci: TD %u, condizione %x\n", i, cc);
                return -1;
            }
        }
        if (finito) return 0;

        usleep(100);
    }

    printf("ohci: trasferimento senza risposta\n");
    return -1;
}

/* -----------------------------------------------------------------------------
 * Il trasferimento di controllo — la cucitura con usb_comune.c
 * --------------------------------------------------------------------------- */
static int controllo(unsigned int dev, unsigned int rt, unsigned int req,
                     unsigned int val, unsigned int idx,
                     void *dati, unsigned int len, int in)
{
    volatile unsigned char *s = (volatile unsigned char *)(g_dma_virt + OFF_SETUP);
    unsigned int ultimo;
    int r;

    (void)dev;
    if (len > BUF_MAX) return -1;

    s[0] = (unsigned char)rt;
    s[1] = (unsigned char)req;
    s[2] = (unsigned char)(val & 0xFF);
    s[3] = (unsigned char)(val >> 8);
    s[4] = (unsigned char)(idx & 0xFF);
    s[5] = (unsigned char)(idx >> 8);
    s[6] = (unsigned char)(len & 0xFF);
    s[7] = (unsigned char)(len >> 8);

    if (!in && len > 0)
        memcpy((void *)(g_dma_virt + OFF_BUF), dati, len);

    /* SETUP e' sempre DATA0; i dati cominciano da DATA1; lo stato e' DATA1 e
     * va nel verso opposto ai dati. */
    if (len > 0) {
        td_prepara(0, 1, DP_SETUP, 2, FIS(OFF_SETUP), 8);
        td_prepara(1, 2, in ? DP_IN : DP_OUT, 3, FIS(OFF_BUF), len);
        td_prepara(2, TD_CODA, in ? DP_OUT : DP_IN, 3, 0, 0);
        ultimo = 2;
    } else {
        td_prepara(0, 1, DP_SETUP, 2, FIS(OFF_SETUP), 8);
        td_prepara(1, TD_CODA, DP_IN, 3, 0, 0);
        ultimo = 1;
    }

    r = esegui(OFF_ED_CTRL, 0, ultimo, CMD_CLF, 1000);
    if (r != 0) return -1;

    if (in && len > 0) {
        unsigned int avuti = td_avuti(1, FIS(OFF_BUF), len);
        if (avuti > len) avuti = len;
        memcpy(dati, (void *)(g_dma_virt + OFF_BUF), avuti);
    }

    return 0;
}

/* -----------------------------------------------------------------------------
 * Il trasferimento bulk — la cucitura con usb_massa.c
 * --------------------------------------------------------------------------- */
static int bulk_ohci(unsigned int dev, unsigned int ep, void *dati,
                     unsigned int len, int in)
{
    unsigned int ed = in ? OFF_ED_IN : OFF_ED_OUT;
    unsigned int avuti;
    int r;

    (void)dev; (void)ep;

    if (len > BUF_MAX) return -1;
    if (!in && len > 0) memcpy((void *)(g_dma_virt + OFF_BUF), dati, len);

    /* Toggle 0 = «quello dell'ED»: il conto se lo tiene il controller, come
     * vuole il protocollo per gli endpoint bulk. */
    td_prepara(0, TD_CODA, in ? DP_IN : DP_OUT, 0, FIS(OFF_BUF), len);

    r = esegui(ed, 0, 0, CMD_BLF, 5000);
    if (r == USB_MASSA_STALLO) return USB_MASSA_STALLO;
    if (r != 0) return -1;

    avuti = td_avuti(0, FIS(OFF_BUF), len);
    if (avuti > len) avuti = len;

    if (in && avuti > 0) memcpy(dati, (void *)(g_dma_virt + OFF_BUF), avuti);

    return (int)avuti;
}

/* Dopo una CLEAR_FEATURE(ENDPOINT_HALT) il dispositivo riparte da DATA0: qui
 * si azzera il bit 1 di HeadP, che e' dove l'OHCI tiene lo stesso conto. E si
 * azzera anche il bit 0, «fermato», che quella richiesta ha appena tolto dalla
 * parte del dispositivo ma non da questa. */
static void toggle_azzera(unsigned int dev, unsigned int ep, int in)
{
    volatile unsigned int *e = VIRT(in ? OFF_ED_IN : OFF_ED_OUT);

    (void)dev; (void)ep;
    e[2] = e[2] & ~0x3u;
}

/* -----------------------------------------------------------------------------
 * Accensione
 * --------------------------------------------------------------------------- */

/* ! ANCHE QUI IL CONTROLLER PUO' NON ESSERE NOSTRO, ma la stretta di mano e'
 * diversa da quella dell'EHCI: non si passa dallo spazio di configurazione, si
 * chiede al controller stesso. Se le interruzioni sono instradate alla SMM
 * (bit IR), c'e' un driver di sistema nascosto che lo sta usando: si alza OCR
 * e si aspetta che IR si spenga. */
static int prendi_dal_bios(void)
{
    unsigned int giri;

    if (!(rd(R_CONTROL) & CTRL_IR)) return 1;

    printf("ohci: il controller e' della SMM, glielo chiedo\n");
    wr(R_CMDSTATUS, CMD_OCR);

    for (giri = 0; giri < 100; giri++) {
        if (!(rd(R_CONTROL) & CTRL_IR)) {
            printf("ohci: l'ha lasciato\n");
            return 1;
        }
        usleep(10000);
    }

    printf("ohci: la SMM non risponde - vado avanti\n");
    return 1;
}

static int hc_avvia(void)
{
    unsigned int fminterval, giri;

    /* L'intervallo di trama va salvato e rimesso: il reset lo azzera, e un
     * controller con FrameInterval a zero non genera nessun frame — cioe' non
     * fa niente, in un modo che somiglia a un guasto. */
    fminterval = rd(R_FMINTERVAL);

    wr(R_CMDSTATUS, CMD_HCR);
    for (giri = 0; giri < 100; giri++) {
        if (!(rd(R_CMDSTATUS) & CMD_HCR)) break;
        usleep(1000);
    }
    if (rd(R_CMDSTATUS) & CMD_HCR) {
        printf("ohci: il controller non finisce il reset\n");
        return 0;
    }

    if ((fminterval & 0x3FFF) == 0) fminterval = 0x2EDF;    /* 11999 */
    wr(R_FMINTERVAL, fminterval | (0x2778u << 16));
    wr(R_PERIODICSTART, (fminterval & 0x3FFF) * 9 / 10);

    memset((void *)(g_dma_virt + OFF_HCCA), 0, 256);

    ed_prepara(OFF_ED_CTRL, 0, 0, 0, 8);
    ed_prepara(OFF_ED_IN,   0, 0, 2, 64);
    ed_prepara(OFF_ED_OUT,  0, 0, 1, 64);

    wr(R_HCCA, FIS(OFF_HCCA));
    wr(R_CTRLHEADED, FIS(OFF_ED_CTRL));
    wr(R_BULKHEADED, FIS(OFF_ED_IN));
    VIRT(OFF_ED_IN)[3] = FIS(OFF_ED_OUT);   /* le due bulk in fila */
    wr(R_CTRLCURRED, 0);
    wr(R_BULKCURRED, 0);
    wr(R_INTDISABLE, 0xFFFFFFFFu);          /* niente interruzioni: si guarda */

    wr(R_CONTROL, CTRL_OPERATIVO | CTRL_CLE | CTRL_BLE);

    /* Accende tutte le porte e aspetta: un dispositivo su una porta appena
     * alimentata non risponde subito. */
    wr(R_RHSTATUS, RH_LPSC);
    usleep(100000);

    return 1;
}

/* Solo la domanda «c'e' qualcosa?», senza toccare niente: la usa l'attesa. */
static int porta_collegata(unsigned int p)
{
    return (rd(R_RHPORT(p)) & RH_CCS) ? 1 : 0;
}

static int porta_prepara(unsigned int p)
{
    unsigned int v = rd(R_RHPORT(p));
    unsigned int giri;

    if (!(v & RH_CCS)) return 0;

    if (!(v & RH_PPS)) {
        wr(R_RHPORT(p), RH_PPS);
        usleep(50000);
    }

    wr(R_RHPORT(p), RH_PRS);

    for (giri = 0; giri < 100; giri++) {
        v = rd(R_RHPORT(p));
        if (v & RH_PRSC) break;
        usleep(2000);
    }
    wr(R_RHPORT(p), RH_PRSC);       /* il cambiamento si azzera scrivendoci 1 */
    usleep(20000);

    v = rd(R_RHPORT(p));
    if (!(v & RH_PES)) {
        printf("ohci: porta %u: non si abilita\n", p + 1);
        return 0;
    }

    g_low = (v & RH_LSDA) ? 1 : 0;
    printf("ohci: porta %u: dispositivo %s speed\n",
           p + 1, g_low ? "low" : "full");
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il dispositivo
 * --------------------------------------------------------------------------- */
static int conosci(void)
{
    if (!usb_desc_corto(controllo, 0, &g_dev)) {
        printf("ohci: il dispositivo non risponde al primo descrittore\n");
        return 0;
    }

    ed_indirizzo(OFF_ED_CTRL, 0, 0, 0, g_dev.maxp0);

    g_indirizzo = 1;
    if (controllo(0, 0x00, USB_REQ_SET_ADDR, g_indirizzo, 0, 0, 0, 0) != 0) {
        printf("ohci: SET_ADDRESS rifiutata\n");
        return 0;
    }
    usleep(10000);
    ed_indirizzo(OFF_ED_CTRL, g_indirizzo, 0, 0, g_dev.maxp0);

    if (!usb_desc_lungo(controllo, g_indirizzo, &g_dev)) {
        printf("ohci: descrittore di dispositivo non credibile\n");
        return 0;
    }

    printf("ohci: dispositivo  USB %x.%02x  venditore %04x prodotto %04x\n",
           (g_dev.versione >> 8) & 0xFF, g_dev.versione & 0xFF,
           g_dev.venditore, g_dev.prodotto);
    return 1;
}

static int massa_prepara(void)
{
    UsbMassa m;
    char     nome[40];

    if (!usb_configura_massa(controllo, g_indirizzo, &g_dev, g_verboso))
        return 0;

    ed_indirizzo(OFF_ED_IN,  g_indirizzo, g_dev.ep_in,  2, g_dev.ep_in_maxp);
    ed_indirizzo(OFF_ED_OUT, g_indirizzo, g_dev.ep_out, 1, g_dev.ep_out_maxp);
    VIRT(OFF_ED_IN)[2]  = td_fis(TD_CODA);
    VIRT(OFF_ED_OUT)[2] = td_fis(TD_CODA);

    memset(&m, 0, sizeof(m));
    m.ctl           = controllo;
    m.bulk          = bulk_ohci;
    m.toggle_azzera = toggle_azzera;
    m.dev           = g_indirizzo;
    m.ep_in         = g_dev.ep_in;
    m.ep_out        = g_dev.ep_out;
    m.interfaccia   = g_dev.interfaccia;
    m.verboso       = g_verboso;

    if (!usb_massa_pronta(&m)) return 0;

    if (usb_massa_nome(&m, nome, sizeof(nome)) && nome[0])
        printf("ohci: %s\n", nome);

    if (!usb_massa_capacita(&m)) {
        printf("ohci: il supporto non dice quanto e' grande\n");
        return 0;
    }

    printf("ohci: %u blocchi da %u byte\n", m.blocchi, m.byte_blocco);

    usb_massa_servi(&m, "usb0");        /* non torna finche' c'e' */
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il controller sul bus
 * --------------------------------------------------------------------------- */
static int cerca_ohci(unsigned int *bar)
{
    PciRichiesta   r;
    PciDispositivo d;
    IpcMessage     meta;
    unsigned char  buf[IPC_MSG_MAX_DATA];
    int pid = -1, ord;
    unsigned int attesa;

    for (attesa = 0; attesa < 50; attesa++) {
        pid = ipc_lookup(PCI_SERVIZIO);
        if (pid >= 0) break;
        usleep(100000);
    }
    if (pid < 0) {
        printf("ohci: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
        return 0;
    }

    for (ord = 0; ord < 16; ord++) {
        int t, avuto = 0;

        r.ordinale    = (unsigned int)ord;
        r.classe      = 0x0C;
        r.sottoclasse = 0x03;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send((unsigned int)pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return 0;

        for (t = 0; t < 8; t++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return 0;
            if ((int)meta.sender_pid != pid) continue;
            if (meta.tipo == PCI_MSG_FINE) return 0;
            if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(d)) {
                memcpy(&d, buf, sizeof(d));
                avuto = 1;
            }
            break;
        }
        if (!avuto) return 0;

        if (d.interfaccia != 0x10) continue;     /* 0x10 = OHCI */

        if (d.bar[0] == 0 || d.bar_io[0]) {
            printf("ohci: BAR0 non e' memoria: non so dove sono i registri\n");
            return 0;
        }

        *bar = d.bar[0];
        printf("ohci: controller in %02x:%02x.%u, registri a 0x%x, IRQ %u\n",
               d.bus, d.slot, d.funzione, d.bar[0], d.irq_linea);

        {
            PciAzione a;
            a.bus = d.bus; a.slot = d.slot; a.funzione = d.funzione;
            a.riservato = 0; a.offset = 0;
            a.bit = PCI_ABIL_MEMORIA | PCI_ABIL_BUSMASTER;
            ipc_send((unsigned int)pid, PCI_MSG_ABILITA, &a, sizeof(a));
            (void)ipc_recv_timeout(&meta, buf, sizeof(buf), 2000);
        }
        return 1;
    }
    return 0;
}


/* =============================================================================
 * ASPETTARE CHE LA CHIAVETTA ARRIVI
 *
 * ! SENZA QUESTO, LA RIGA IN /boot/avvio.sh NON SERVE A NIENTE. All'accensione
 * la chiavetta non c'e' quasi mai: si accende la macchina e POI si infila. Un
 * driver che guarda le porte una volta sola e se ne va lascia il sorvegliante
 * senza niente da montare, per sempre — e l'unico rimedio sarebbe ricordarsi
 * di lanciarlo a mano dopo, che e' esattamente cio' che il montaggio
 * automatico doveva togliere di mezzo.
 *
 * ! UNA PORTA GIA' PROVATA NON SI RIPROVA, o un dispositivo che non e' una
 * memoria di massa — un mouse, un hub — verrebbe enumerato ogni secondo per
 * sempre. Il segno si cancella quando quella porta torna vuota: cosi' la
 * stessa presa, staccata e riattaccata, viene riguardata.
 * ============================================================================= */
static int aspetta_e_servi(const char *chi)
{
    unsigned char provata[16];
    unsigned int  p;
    int           detto = 0;

    for (p = 0; p < 16; p++) provata[p] = 0;

    for (;;) {
        for (p = 0; p < g_porte && p < 16; p++) {
            if (!porta_collegata(p)) { provata[p] = 0; continue; }
            if (provata[p]) continue;

            provata[p] = 1;

            if (!porta_prepara(p)) continue;
            if (!conosci()) continue;

            if (massa_prepara()) return 0;  /* servita: di qui non si torna */

            printf("%s: sulla porta %u non c'e' una memoria di massa.\n",
                   chi, p + 1);
            printf("      Questo driver serve solo quelle: per mouse e\n");
            printf("      tastiere c'e' uhci/xhci.\n");
        }

        if (!detto) {
            printf("%s: aspetto una chiavetta.\n", chi);
            detto = 1;
        }
        usleep(1000000);
    }
}

int main(int argc, char **argv)
{
    unsigned int bar = 0, solo_sonda = 0;
    MmioZona m;
    DmaZona  z;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-i") == 0) solo_sonda = 1;
        else if (strcmp(argv[i], "-avvio") == 0) g_avvio = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("uso: ohci.drv [-v] [-i] [-avvio]\n");
            printf("  -v      dice tutto quello che vede\n");
            printf("  -i      dice se c'e' un OHCI ed esce\n");
            printf("  -avvio  se non c'e' niente da fare, esce in silenzio:\n");
            printf("          e' la forma che serve a /boot/avvio.sh\n");
            return 0;
        }
    }

    if (!cerca_ohci(&bar)) {
        if (!g_avvio)
            printf("ohci: nessun controller OHCI su questa macchina\n");
        return 1;
    }
    if (solo_sonda) return 0;

    m.fisico = bar;
    m.byte   = 0x1000;
    if (mmio_map(&m) != 0) {
        printf("ohci: mmio_map rifiutata. Il file dev'essere un .drv\n");
        return 1;
    }
    g_reg = (volatile unsigned char *)m.virt;

    printf("ohci: revisione %u.%u\n", (rd(R_REVISION) >> 4) & 0xF,
           rd(R_REVISION) & 0xF);

    if (!prendi_dal_bios()) return 1;

    z.byte = DMA_BYTE;
    if (dma_alloc(&z) != 0) {
        printf("ohci: dma_alloc rifiutata\n");
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;
    memset((void *)g_dma_virt, 0, DMA_BYTE);

    if (!hc_avvia()) return 1;

    g_porte = rd(R_RHDESCA) & 0xFF;
    if (g_porte == 0 || g_porte > 15) g_porte = 2;
    printf("ohci: controller acceso, %u porte\n", g_porte);

    return aspetta_e_servi("ohci");
}
