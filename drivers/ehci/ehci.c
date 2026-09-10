/* =============================================================================
 * drivers/ehci/ehci.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * USB 2.0: il controller EHCI — le chiavette sulle macchine vere
 *
 *     /dev/ehci.drv            trova il controller, enumera, serve il disco
 *     /dev/ehci.drv -v         dice tutto quello che vede
 *     /dev/ehci.drv -i         la sonda: dice se c'e' un EHCI, esce
 *
 * ! PERCHE' ESISTE, VISTO CHE C'E' GIA' xhci.drv. Perche' l'xHCI e' del 2010
 * in poi. Il portatile per cui e' nato tutto questo (@USB-DISCO, @SIS) ha un
 * SiS 964: tre OHCI per l'USB 1.1 e UN EHCI per il 2.0, cioe' nessuno dei due
 * controller che EX-OS sapeva guidare. Su quella macchina questo file e'
 * l'unica strada per leggere una chiavetta.
 *
 * -----------------------------------------------------------------------------
 * ! DOV'E' DIVERSO DA xHCI, E DOVE E' UGUALE
 *
 * Uguale: descrittori, configurazioni, classe di memoria di massa, comandi
 * SCSI. Sono usb_comune.c e usb_massa.c, e questo file non li tocca — li
 * chiama attraverso i due puntatori a funzione, come fa xhci.drv.
 *
 * Diverso, e in un modo che conta:
 *
 *   L'INDIRIZZO LO ASSEGNA IL DRIVER. Come su UHCI: SET_ADDRESS e il conto se
 *   lo tiene lui. L'xHCI faceva il contrario (Enable Slot / Address Device), e
 *   chi viene da quel file se lo aspetta.
 *
 *   NON C'E' NESSUN CAMPANELLO. Sull'xHCI si mette un TRB nell'anello e si
 *   suona; qui si appende una catena di qTD a una QH che sta gia' nella lista
 *   asincrona, e il controller la trova da solo al giro dopo. Non si «manda»
 *   niente: si mette li' e si guarda quando il bit Attivo si spegne.
 *
 *   LE PORTE POSSONO NON ESSERE SUE. Un EHCI convive con dei controller
 *   COMPAGNI (OHCI o UHCI) che servono l'USB 1.1 sulle STESSE prese fisiche.
 *   Chi possiede una porta lo dice un bit, e un dispositivo full o low speed
 *   va CEDUTO al compagno: tenerselo vuol dire una presa in cui non funziona
 *   piu' niente finche' non si riavvia.
 *
 * -----------------------------------------------------------------------------
 * ! IL BIOS SE LO TIENE FINCHE' NON GLIELO SI CHIEDE
 *
 * Su una macchina vera il BIOS usa l'EHCI per la tastiera USB prima che parta
 * il sistema, e non lo lascia: c'e' una stretta di mano — la struttura
 * «legacy support» nello spazio di configurazione PCI — in cui si alza un bit
 * e si aspetta che lui abbassi il suo. Saltarla vuol dire due padroni sullo
 * stesso controller, e i sintomi non somigliano a niente di riconoscibile:
 * trasferimenti che spariscono, interruzioni che arrivano a nessuno.
 *
 * ! E PER FARLA SERVE SCRIVERE NELLO SPAZIO DI CONFIGURAZIONE, che il servizio
 * pci.drv non offre — sa leggere e accendere i bit del registro comando, non
 * scrivere un registro qualunque. Quindi qui si aprono 0xCF8/0xCFC per conto
 * proprio. E' l'unica cosa che questo driver fa alle spalle del servizio PCI,
 * ed e' scritto qui perche' non sembri una dimenticanza.
 *
 * -----------------------------------------------------------------------------
 * ! COSA NON FA, DETTO SUBITO
 *
 *   - i dispositivi FULL e LOW SPEED li cede al compagno e non li guarda. Per
 *     servirli servirebbero le «split transaction» attraverso il transaction
 *     translator di un hub, che sono un capitolo a se';
 *   - un dispositivo per volta, come uhci.drv e xhci.drv;
 *   - niente interruzioni: si guarda il bit Attivo del qTD. Per un disco che
 *     si legge a blocchi va bene; per un mouse sarebbe uno spreco di CPU.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "usb_comune.h"
#include "usb_massa.h"

/* +0.001 a ogni modifica: `ehci.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("ehci.drv", "0.001");

/* --- Registri di capacita' (all'inizio della finestra) --------------------- */
#define C_CAPLENGTH   0x00      /* byte 0 = lunghezza, byte 2-3 = versione */
#define C_HCSPARAMS   0x04      /* bit 0-3: quante porte */
#define C_HCCPARAMS   0x08      /* bit 8-15: dove sta la stretta di mano */

/* --- Registri operativi (a base + CAPLENGTH) ------------------------------ */
#define O_USBCMD      0x00
#define O_USBSTS      0x04
#define O_USBINTR     0x08
#define O_FRINDEX     0x0C
#define O_CTRLDSSEG   0x10
#define O_PERIODICBASE 0x14
#define O_ASYNCLIST   0x18
#define O_CONFIGFLAG  0x40
#define O_PORTSC(n)   (0x44 + (n) * 4)

#define CMD_RS        0x00000001u   /* corri */
#define CMD_HCRESET   0x00000002u
#define CMD_ASE       0x00000020u   /* lista asincrona accesa */
#define CMD_IAAD      0x00000040u   /* «ho tolto una QH, avvisami» */

#define STS_HALTED    0x00001000u
#define STS_ASS       0x00008000u   /* la lista asincrona sta girando */
#define STS_IAA       0x00000020u

/* PORTSC. ! I BIT 1, 3 E 5 SI AZZERANO SCRIVENDOCI 1: una scrittura distratta
 * che li ricopia com'erano cancella un cambiamento che nessuno ha ancora
 * letto. Ogni scrittura passa da porta_scrivi(). */
#define P_CONNESSO    0x00000001u
#define P_CONN_CAMB   0x00000002u
#define P_ABILITATA   0x00000004u
#define P_ABIL_CAMB   0x00000008u
#define P_OC_CAMB     0x00000020u
#define P_RESET       0x00000100u
#define P_LINEA       0x00000C00u   /* 01 = K state = low speed */
#define P_POTENZA     0x00001000u
#define P_PADRONE     0x00002000u   /* 1 = la porta e' del compagno */
#define P_RWC         (P_CONN_CAMB | P_ABIL_CAMB | P_OC_CAMB)

/* --- La memoria condivisa col controller ---------------------------------- */
#define DMA_BYTE      (8u * 4096u)

#define OFF_QH_TESTA  0x0000    /* la QH finta che chiude il cerchio */
#define OFF_QH_CTRL   0x0100    /* endpoint 0 */
#define OFF_QH_IN     0x0200    /* bulk IN */
#define OFF_QH_OUT    0x0300    /* bulk OUT */
#define OFF_QTD       0x1000    /* otto qTD da 32 byte */
#define OFF_SETUP     0x2000    /* gli 8 byte di una richiesta di controllo */
#define OFF_BUF       0x3000    /* i dati: 4 KB */
#define BUF_MAX       4096

#define QTD_N         8

static volatile unsigned char *g_mmio = 0;
static volatile unsigned char *g_op   = 0;
static unsigned int g_dma_virt = 0, g_dma_fis = 0;
static unsigned int g_porte = 0;
static unsigned int g_verboso = 0;

/* -avvio: lanciato da /boot/avvio.sh, dove una riga di lamentela per ogni
 * macchina che quel controller non ce l'ha e' rumore a ogni accensione. */
static unsigned int g_avvio = 0;
static unsigned int g_indirizzo = 0;    /* quello assegnato al dispositivo */
static UsbDispositivo g_dev;

/* Il dispositivo PCI, per la stretta di mano col BIOS. */
static unsigned int g_bus = 0, g_slot = 0, g_fn = 0;

#define VIRT(o)   ((volatile unsigned int *)(g_dma_virt + (o)))
#define FIS(o)    (g_dma_fis + (o))

static unsigned int rd32(volatile unsigned char *b, unsigned int off)
{
    return *(volatile unsigned int *)(b + off);
}

static void wr32(volatile unsigned char *b, unsigned int off, unsigned int v)
{
    *(volatile unsigned int *)(b + off) = v;
}

/* ! OGNI SCRITTURA SU PORTSC PASSA DA QUI, per non cancellare i tre bit che si
 * azzerano scrivendo 1. Vedi P_RWC. */
static void porta_scrivi(unsigned int p, unsigned int v)
{
    wr32(g_op, O_PORTSC(p), (v & ~P_RWC));
}

/* -----------------------------------------------------------------------------
 * Lo spazio di configurazione PCI, a mano
 *
 * Serve solo alla stretta di mano col BIOS: il servizio pci.drv non sa
 * scrivere un registro qualunque. Vedi il commento in testa al file.
 * --------------------------------------------------------------------------- */
static unsigned int cfg_leggi(unsigned int off)
{
    unsigned int v = 0xFFFFFFFF;
    unsigned int ind = 0x80000000u | (g_bus << 16) | (g_slot << 11) |
                       (g_fn << 8) | (off & 0xFC);

    if (ioport_out32(0xCF8, ind) != 0) return 0xFFFFFFFF;
    if (ioport_in32(0xCFC, &v) != 0)   return 0xFFFFFFFF;
    return v;
}

static void cfg_scrivi(unsigned int off, unsigned int val)
{
    unsigned int ind = 0x80000000u | (g_bus << 16) | (g_slot << 11) |
                       (g_fn << 8) | (off & 0xFC);

    if (ioport_out32(0xCF8, ind) != 0) return;
    (void)ioport_out32(0xCFC, val);
}

/* La stretta di mano. Rende 1 se il controller e' nostro. */
static int prendi_dal_bios(void)
{
    unsigned int hcc = rd32(g_mmio, C_HCCPARAMS);
    unsigned int eecp = (hcc >> 8) & 0xFF;
    unsigned int leg, giri;

    /* Meno di 0x40 vuol dire «non c'e' nessuna struttura»: il controller e'
     * gia' libero, come in QEMU. */
    if (eecp < 0x40) {
        if (g_verboso) printf("ehci: nessuna stretta di mano da fare\n");
        return 1;
    }

    leg = cfg_leggi(eecp);
    if ((leg & 0x00010000u) == 0) {          /* bit 16: se l'e' preso il BIOS */
        if (g_verboso) printf("ehci: il BIOS non lo tiene\n");
        return 1;
    }

    printf("ehci: il controller e' del BIOS, glielo chiedo\n");
    cfg_scrivi(eecp, leg | 0x01000000u);     /* bit 24: lo vuole il sistema */

    for (giri = 0; giri < 100; giri++) {
        leg = cfg_leggi(eecp);
        if ((leg & 0x00010000u) == 0) {
            printf("ehci: il BIOS l'ha lasciato\n");
            return 1;
        }
        usleep(10000);
    }

    /* ! NON E' UN MOTIVO PER RINUNCIARE, ed e' meglio dirlo che tacere: certi
     * BIOS non abbassano mai quel bit pur avendo smesso di usarlo. Si va
     * avanti, e se qualcosa non funziona questa riga e' il primo posto dove
     * guardare. */
    printf("ehci: il BIOS non risponde alla stretta di mano - vado avanti\n");
    return 1;
}

/* -----------------------------------------------------------------------------
 * Le code e i descrittori
 *
 * ! LA QH E' SIA LA DESCRIZIONE DELL'ENDPOINT SIA IL POSTO DOVE IL CONTROLLER
 * LAVORA. I byte da 0x10 in poi sono la «sovrapposizione»: il controller ci
 * copia dentro il qTD che sta eseguendo e ci aggiorna lo stato. Percio' prima
 * di ogni trasferimento vanno azzerati a mano — un bit «fermato» rimasto li'
 * da un errore di prima blocca il trasferimento nuovo senza che nessuno lo
 * abbia chiesto.
 * --------------------------------------------------------------------------- */
#define QTD_ATTIVO    0x80
#define QTD_FERMATO   0x40
#define QTD_ERR_DATI  0x20
#define QTD_BABBLE    0x10
#define QTD_XACT      0x08

#define PID_OUT       0
#define PID_IN        1
#define PID_SETUP     2

static unsigned int qtd_fis(unsigned int i) { return FIS(OFF_QTD + i * 32); }
static volatile unsigned int *qtd(unsigned int i)
{
    return VIRT(OFF_QTD + i * 32);
}

/* Prepara un qTD. `prossimo` e' l'indice del successivo, o -1 per «ultimo». */
static void qtd_prepara(unsigned int i, int prossimo, unsigned int pid,
                        unsigned int dt, unsigned int buf_fis, unsigned int len,
                        unsigned int ioc)
{
    volatile unsigned int *t = qtd(i);
    unsigned int k;

    t[0] = (prossimo < 0) ? 1u : qtd_fis((unsigned int)prossimo);
    t[1] = 1u;                                  /* alternativo: nessuno */
    t[2] = (dt << 31) | (len << 16) | (ioc << 15) | (3u << 10) |
           (pid << 8) | QTD_ATTIVO;

    /* I puntatori sono a PAGINE: il primo porta anche lo scostamento dentro
     * la pagina, gli altri no. Con al massimo 4 KB ne bastano due. */
    for (k = 3; k < 8; k++) t[k] = 0;
    t[3] = buf_fis;
    t[4] = (buf_fis + 0x1000u) & ~0xFFFu;
}

/* Prepara una QH. `testa` = e' quella che chiude il cerchio. */
static void qh_prepara(unsigned int off, unsigned int prossima_fis,
                       unsigned int indirizzo, unsigned int ep,
                       unsigned int maxp, unsigned int controllo_ep,
                       unsigned int testa)
{
    volatile unsigned int *q = VIRT(off);
    unsigned int k;

    for (k = 0; k < 16; k++) q[k] = 0;

    q[0] = prossima_fis | (1u << 1);            /* tipo 1 = QH */

    /* Velocita' alta (2), toggle dal qTD (bit 14), e per l'endpoint 0 anche
     * il bit di «endpoint di controllo». */
    (void)controllo_ep;         /* il bit C serve solo a full e low speed */
    q[1] = (indirizzo & 0x7F) | (ep << 8) | (2u << 12) | (1u << 14) |
           (testa ? (1u << 15) : 0u) | (maxp << 16) | (15u << 28);

    /* Mult = 1: una transazione per microframe. Zero vorrebbe dire «nessuna»,
     * ed e' l'errore che fa sembrare il controller morto. */
    q[2] = (1u << 30);

    q[4] = 1u;                                  /* nessun qTD in coda */
    q[5] = 1u;
}

/* Attacca una catena di qTD a una QH e aspetta che finisca.
 * Rende 0, USB_MASSA_STALLO se l'endpoint si e' fermato, -1 altrimenti.
 *
 * ! SI GUARDA L'ULTIMO qTD, NON LA CODA, e questa e' costata mezz'ora. La
 * prima stesura aspettava che si spegnesse il bit Attivo dentro la
 * SOVRAPPOSIZIONE della QH — che pero' e' azzerata da noi un attimo prima. Se
 * il controller non tocca quella coda — non e' partito, non l'ha in lista,
 * l'indirizzo e' sbagliato — la sovrapposizione resta com'era: zero, cioe'
 * «non attivo», cioe' FINITO BENE. Ogni trasferimento riusciva
 * istantaneamente senza trasferire niente, e il primo a lamentarsi era il
 * descrittore, tre chiamate piu' in la'.
 *
 * I qTD invece nascono ATTIVI, li accendiamo noi: se restano accesi vuol dire
 * che nessuno li ha eseguiti, e questa e' una domanda a cui si puo' rispondere
 * con la verita'. */
static int esegui(unsigned int qh_off, unsigned int primo, unsigned int ultimo,
                  unsigned int ms)
{
    volatile unsigned int *q = VIRT(qh_off);
    unsigned int giri, stato, i;

    /* La sovrapposizione si azzera PRIMA: vedi il commento qui sopra. */
    q[3] = 0;
    q[4] = qtd_fis(primo);
    q[5] = 1u;
    q[6] = 0;                                   /* token: nessuno stato vecchio */

    for (giri = 0; giri < ms * 10; giri++) {
        /* Un errore ferma la catena: puo' essersi fermata su uno qualunque
         * dei qTD, non solo sull'ultimo. */
        for (i = primo; i <= ultimo; i++) {
            stato = qtd(i)[2] & 0xFF;
            if (stato & (QTD_FERMATO | QTD_BABBLE | QTD_ERR_DATI)) {
                if (g_verboso)
                    printf("ehci: qTD %u fermato (stato %02x)\n", i, stato);
                return (stato & QTD_FERMATO) ? USB_MASSA_STALLO : -1;
            }
        }

        if (!(qtd(ultimo)[2] & QTD_ATTIVO)) return 0;

        usleep(100);
    }

    printf("ehci: trasferimento senza risposta (qTD ancora attivo)\n");
    return -1;
}

/* Quanti byte NON sono stati trasferiti: il controller li lascia scritti nel
 * token del qTD, dove il conto scende man mano. */
static unsigned int resto_qtd(unsigned int i)
{
    return (qtd(i)[2] >> 16) & 0x7FFF;
}

/* -----------------------------------------------------------------------------
 * Il trasferimento di controllo — la cucitura con usb_comune.c
 * --------------------------------------------------------------------------- */
static int controllo(unsigned int dev, unsigned int rt, unsigned int req,
                     unsigned int val, unsigned int idx,
                     void *dati, unsigned int len, int in)
{
    volatile unsigned char *s = (volatile unsigned char *)(g_dma_virt + OFF_SETUP);
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

    /* SETUP, poi i dati (se ci sono), poi lo stato nel verso opposto. */
    qtd_prepara(0, len ? 1 : 2, PID_SETUP, 0, FIS(OFF_SETUP), 8, 0);

    if (len > 0)
        qtd_prepara(1, 2, in ? PID_IN : PID_OUT, 1, FIS(OFF_BUF), len, 0);

    /* ! LO STATO VA NEL VERSO CONTRARIO AI DATI, sempre. Un GET_DESCRIPTOR
     * (dati IN) si chiude con un pacchetto OUT vuoto; una SET_ADDRESS (senza
     * dati) si chiude con un IN. Sbagliarlo da' un trasferimento che sembra
     * riuscito e lascia il dispositivo a meta'. */
    qtd_prepara(2, -1, (len && in) ? PID_OUT : PID_IN, 1, 0, 0, 1);

    r = esegui(OFF_QH_CTRL, 0, 2, 1000);
    if (r != 0) return -1;

    if (in && len > 0) {
        unsigned int avuti = len - resto_qtd(1);
        if (avuti > len) avuti = len;
        memcpy(dati, (void *)(g_dma_virt + OFF_BUF), avuti);
    }

    return 0;
}

/* -----------------------------------------------------------------------------
 * Il trasferimento bulk — la cucitura con usb_massa.c
 * --------------------------------------------------------------------------- */
static unsigned int g_toggle_in = 0, g_toggle_out = 0;

static int bulk_ehci(unsigned int dev, unsigned int ep, void *dati,
                     unsigned int len, int in)
{
    unsigned int qh = in ? OFF_QH_IN : OFF_QH_OUT;
    unsigned int *tog = in ? &g_toggle_in : &g_toggle_out;
    unsigned int avuti;
    int r;

    (void)dev; (void)ep;

    if (len > BUF_MAX) return -1;
    if (!in && len > 0) memcpy((void *)(g_dma_virt + OFF_BUF), dati, len);

    /* ! IL TOGGLE SE LO TIENE LA QH, e per questo non si azzera qui. Il bit 14
     * di q[1] dice «prendi il toggle dal qTD», quindi lo teniamo noi: un
     * toggle sbagliato non da' errore, da' un pacchetto ignorato dal
     * dispositivo e un trasferimento che non finisce mai. */
    qtd_prepara(0, -1, in ? PID_IN : PID_OUT, *tog, FIS(OFF_BUF), len, 1);

    r = esegui(qh, 0, 0, 5000);
    if (r == USB_MASSA_STALLO) return USB_MASSA_STALLO;
    if (r != 0) return -1;

    /* Il toggle avanza di un pacchetto per ogni pacchetto intero mandato. Con
     * un solo qTD per trasferimento basta guardare quanti ne sono serviti. */
    {
        unsigned int maxp = in ? g_dev.ep_in_maxp : g_dev.ep_out_maxp;
        unsigned int pacchetti;

        avuti = len - resto_qtd(0);
        if (avuti > len) avuti = len;

        pacchetti = (maxp > 0) ? ((avuti + maxp - 1) / maxp) : 1;
        if (pacchetti == 0) pacchetti = 1;
        *tog ^= (pacchetti & 1);
    }

    if (in && avuti > 0) memcpy(dati, (void *)(g_dma_virt + OFF_BUF), avuti);

    return (int)avuti;
}

/* Il trasporto bulk azzera il toggle quando l'endpoint viene sbloccato: una
 * CLEAR_FEATURE(ENDPOINT_HALT) rimette a zero il conto anche dalla parte del
 * dispositivo, e non accorgersene lascia i due lati sfasati per sempre. */
static void toggle_azzera(unsigned int dev, unsigned int ep, int in)
{
    (void)dev; (void)ep;
    if (in) g_toggle_in = 0; else g_toggle_out = 0;
}

/* -----------------------------------------------------------------------------
 * Accensione
 * --------------------------------------------------------------------------- */
static int hc_reset(void)
{
    unsigned int giri;

    /* Prima si ferma, poi si resetta: un HCRESET mentre corre e' fuori
     * specifica e su certi controller lascia i registri a meta'. */
    wr32(g_op, O_USBCMD, rd32(g_op, O_USBCMD) & ~CMD_RS);
    for (giri = 0; giri < 100; giri++) {
        if (rd32(g_op, O_USBSTS) & STS_HALTED) break;
        usleep(10000);
    }

    wr32(g_op, O_USBCMD, CMD_HCRESET);
    for (giri = 0; giri < 100; giri++) {
        if (!(rd32(g_op, O_USBCMD) & CMD_HCRESET)) return 1;
        usleep(10000);
    }

    printf("ehci: il controller non finisce il reset\n");
    return 0;
}

static int hc_avvia(void)
{
    unsigned int giri;

    /* Il cerchio delle code: testa -> controllo -> IN -> OUT -> testa. */
    qh_prepara(OFF_QH_TESTA, FIS(OFF_QH_CTRL), 0, 0, 64, 0, 1);
    qh_prepara(OFF_QH_CTRL,  FIS(OFF_QH_IN),   0, 0, 64, 1, 0);
    qh_prepara(OFF_QH_IN,    FIS(OFF_QH_OUT),  0, 0, 512, 0, 0);
    qh_prepara(OFF_QH_OUT,   FIS(OFF_QH_TESTA), 0, 0, 512, 0, 0);

    wr32(g_op, O_CTRLDSSEG, 0);
    wr32(g_op, O_ASYNCLIST, FIS(OFF_QH_TESTA));
    wr32(g_op, O_USBINTR, 0);           /* niente interruzioni: si guarda */

    wr32(g_op, O_USBCMD, (8u << 16) | CMD_ASE | CMD_RS);

    for (giri = 0; giri < 100; giri++) {
        if (rd32(g_op, O_USBSTS) & STS_ASS) break;
        usleep(10000);
    }
    if (!(rd32(g_op, O_USBSTS) & STS_ASS)) {
        printf("ehci: la lista asincrona non parte\n");
        return 0;
    }

    /* ! CONFIGFLAG E' L'INTERRUTTORE CHE DA' LE PORTE A NOI. Finche' vale 0
     * ogni presa appartiene al controller compagno, e un EHCI perfettamente
     * inizializzato non vede NIENTE attaccato. */
    wr32(g_op, O_CONFIGFLAG, 1);
    usleep(20000);

    return 1;
}

/* Rende 1 se sulla porta c'e' un dispositivo ad alta velocita' pronto. */
/* Solo la domanda «c'e' qualcosa?», senza toccare niente: la usa l'attesa. */
static int porta_collegata(unsigned int p)
{
    return (rd32(g_op, O_PORTSC(p)) & P_CONNESSO) ? 1 : 0;
}

static int porta_prepara(unsigned int p)
{
    unsigned int v = rd32(g_op, O_PORTSC(p));
    unsigned int giri;

    if (!(v & P_CONNESSO)) return 0;

    /* ! UN LOW SPEED SI RICONOSCE PRIMA DEL RESET, dalle linee: resettarlo per
     * poi cederlo sarebbe un reset inutile su un dispositivo di un altro
     * controller. */
    if ((v & P_LINEA) == 0x0400u) {
        printf("ehci: porta %u: dispositivo low speed, la cedo al compagno\n",
               p + 1);
        porta_scrivi(p, v | P_PADRONE);
        return 0;
    }

    if (!(v & P_POTENZA)) {
        porta_scrivi(p, v | P_POTENZA);
        usleep(50000);
        v = rd32(g_op, O_PORTSC(p));
    }

    /* Reset: si alza il bit, si aspetta, si abbassa, e si aspetta che il
     * controller confermi di averlo abbassato. */
    porta_scrivi(p, (v & ~P_ABILITATA) | P_RESET);
    usleep(60000);
    porta_scrivi(p, rd32(g_op, O_PORTSC(p)) & ~P_RESET);

    for (giri = 0; giri < 50; giri++) {
        v = rd32(g_op, O_PORTSC(p));
        if (!(v & P_RESET)) break;
        usleep(2000);
    }

    /* ! DOPO IL RESET SI ASPETTA, e non e' prudenza generica: e' il caso di
     * una chiavetta GIA' INFILATA all'accensione.
     *
     * Infilata a macchina accesa, il driver la trova al giro d'attesa dopo —
     * un secondo pieno — e a quel punto e' sveglia da un pezzo. Trovata subito
     * all'avvio, invece, le si chiede il primo descrittore un attimo dopo il
     * reset, e lei non risponde: «il dispositivo non risponde al primo
     * descrittore» su una chiavetta che due minuti dopo funziona benissimo.
     *
     * Visto su un Acer Aspire 3000 il 10 settembre 2026, e non e' un caso
     * strano: e' il caso NORMALE, perche' la gente accende la macchina con la
     * chiavetta gia' dentro. La specifica concede 10 ms dopo il reset; cento
     * costano niente e coprono anche i dispositivi lenti. */
    usleep(100000);

    v = rd32(g_op, O_PORTSC(p));

    /* ! DOPO IL RESET, «ABILITATA» VUOL DIRE ALTA VELOCITA'. E' cosi' che
     * l'EHCI risponde alla domanda «che velocita' ha?»: se dopo un reset la
     * porta non si e' abilitata, il dispositivo non e' suo. Si cede, e la
     * presa continua a funzionare con l'altro controller. */
    if (!(v & P_ABILITATA)) {
        printf("ehci: porta %u: non e' alta velocita', la cedo al compagno\n",
               p + 1);
        porta_scrivi(p, v | P_PADRONE);
        return 0;
    }

    printf("ehci: porta %u: dispositivo ad alta velocita'\n", p + 1);
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il dispositivo
 * --------------------------------------------------------------------------- */
static void qh_indirizzo(unsigned int off, unsigned int indirizzo,
                         unsigned int ep, unsigned int maxp)
{
    volatile unsigned int *q = VIRT(off);

    /* Si azzerano indirizzo (0-6), endpoint (8-11) e pacchetto massimo
     * (16-26) prima di rimetterli: lasciare i vecchi bit e fare OR vuol dire
     * un endpoint che e' la somma di due numeri. */
    q[1] = (q[1] & ~0x07FF0F7Fu) | (indirizzo & 0x7F) | (ep << 8) |
           (maxp << 16);
}

static int conosci(void)
{
    int giro;

    /* ! TRE TENTATIVI, per la stessa ragione dell'attesa qui sopra: il primo
     * descrittore e' la prima parola che si scambia con un dispositivo appena
     * resettato, e se arriva troppo presto quello tace. Rinunciare al primo no
     * vuol dire dichiarare assente una chiavetta che c'e'. */
    for (giro = 0; giro < 3; giro++) {
        if (usb_desc_corto(controllo, 0, &g_dev)) break;
        if (giro == 2) {
            printf("ehci: il dispositivo non risponde al primo descrittore\n");
            return 0;
        }
        usleep(100000);
    }

    /* Ad alta velocita' l'endpoint 0 e' sempre da 64 byte; si rilegge lo
     * stesso, perche' un dispositivo che dice altro va creduto. */
    qh_indirizzo(OFF_QH_CTRL, 0, 0, g_dev.maxp0);

    /* ! L'INDIRIZZO SI ASSEGNA QUI, e da questo momento il dispositivo non
     * risponde piu' allo zero: se la QH non lo segue, tutto il resto
     * dell'enumerazione parla a nessuno. */
    g_indirizzo = 1;
    if (controllo(0, 0x00, USB_REQ_SET_ADDR, g_indirizzo, 0, 0, 0, 0) != 0) {
        printf("ehci: SET_ADDRESS rifiutata\n");
        return 0;
    }
    usleep(10000);
    qh_indirizzo(OFF_QH_CTRL, g_indirizzo, 0, g_dev.maxp0);

    if (!usb_desc_lungo(controllo, g_indirizzo, &g_dev)) {
        printf("ehci: descrittore di dispositivo non credibile\n");
        return 0;
    }

    printf("ehci: dispositivo  USB %x.%02x  venditore %04x prodotto %04x\n",
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

    qh_indirizzo(OFF_QH_IN,  g_indirizzo, g_dev.ep_in,  g_dev.ep_in_maxp);
    qh_indirizzo(OFF_QH_OUT, g_indirizzo, g_dev.ep_out, g_dev.ep_out_maxp);
    g_toggle_in = 0;
    g_toggle_out = 0;

    memset(&m, 0, sizeof(m));
    m.ctl           = controllo;
    m.bulk          = bulk_ehci;
    m.toggle_azzera = toggle_azzera;
    m.dev         = g_indirizzo;
    m.ep_in       = g_dev.ep_in;
    m.ep_out      = g_dev.ep_out;
    m.interfaccia = g_dev.interfaccia;
    m.verboso     = g_verboso;

    if (!usb_massa_pronta(&m)) return 0;

    if (usb_massa_nome(&m, nome, sizeof(nome)) && nome[0])
        printf("ehci: %s\n", nome);

    if (!usb_massa_capacita(&m)) {
        printf("ehci: il supporto non dice quanto e' grande\n");
        return 0;
    }

    printf("ehci: %u blocchi da %u byte\n", m.blocchi, m.byte_blocco);

    usb_massa_servi(&m, "usb0");        /* non torna finche' c'e' */
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il controller sul bus
 * --------------------------------------------------------------------------- */
static int cerca_ehci(unsigned int *bar)
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
        printf("ehci: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
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

        if (d.interfaccia != 0x20) continue;     /* 0x20 = EHCI */

        if (d.bar[0] == 0 || d.bar_io[0]) {
            printf("ehci: BAR0 non e' memoria: non so dove sono i registri\n");
            return 0;
        }

        g_bus  = d.bus;
        g_slot = d.slot;
        g_fn   = d.funzione;
        *bar   = d.bar[0];

        printf("ehci: controller in %02x:%02x.%u, registri a 0x%x, IRQ %u\n",
               d.bus, d.slot, d.funzione, d.bar[0], d.irq_linea);

        /* Memoria e bus master: senza il secondo il controller non puo'
         * leggere le code, e senza il primo non risponde ai registri. */
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
    unsigned int bar = 0, solo_sonda = 0, caplen;
    MmioZona m;
    DmaZona  z;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-i") == 0) solo_sonda = 1;
        else if (strcmp(argv[i], "-avvio") == 0) g_avvio = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("uso: ehci.drv [-v] [-i] [-avvio]\n");
            printf("  -v      dice tutto quello che vede\n");
            printf("  -i      dice se c'e' un EHCI ed esce\n");
            printf("  -avvio  se non c'e' niente da fare, esce in silenzio:\n");
            printf("          e' la forma che serve a /boot/avvio.sh\n");
            return 0;
        }
    }

    if (!cerca_ehci(&bar)) {
        if (!g_avvio)
            printf("ehci: nessun controller EHCI su questa macchina\n");
        return 1;
    }
    if (solo_sonda) return 0;

    /* Le porte di configurazione servono alla stretta di mano col BIOS. */
    if (ioport_bind(0xCF8, 8) != 0) {
        printf("ehci: ioport_bind(0xCF8) rifiutata: niente stretta di mano\n");
    }

    m.fisico = bar;
    m.byte   = 0x1000;
    if (mmio_map(&m) != 0) {
        printf("ehci: mmio_map rifiutata. Il file dev'essere un .drv\n");
        return 1;
    }
    g_mmio = (volatile unsigned char *)m.virt;

    caplen = rd32(g_mmio, C_CAPLENGTH) & 0xFF;
    if (caplen == 0 || caplen == 0xFF) {
        printf("ehci: CAPLENGTH vale 0x%x: la finestra non risponde\n", caplen);
        return 1;
    }
    g_op    = g_mmio + caplen;
    g_porte = rd32(g_mmio, C_HCSPARAMS) & 0x0F;

    printf("ehci: versione %x, %u porte, registri operativi a +0x%x\n",
           (rd32(g_mmio, C_CAPLENGTH) >> 16) & 0xFFFF, g_porte, caplen);

    if (!prendi_dal_bios()) return 1;

    z.byte = DMA_BYTE;
    if (dma_alloc(&z) != 0) {
        printf("ehci: dma_alloc rifiutata\n");
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;
    memset((void *)g_dma_virt, 0, DMA_BYTE);

    if (!hc_reset()) return 1;
    if (!hc_avvia()) return 1;

    printf("ehci: controller acceso, cerco un dispositivo\n");

    return aspetta_e_servi("ehci");
}
