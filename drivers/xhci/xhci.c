/* =============================================================================
 * drivers/xhci/xhci.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * USB 3: il controller xHCI — la meta' «controller» dello stack
 *
 *     /dev/xhci.drv            trova il controller, lo avvia, dice cosa vede
 *     /dev/xhci.drv -v         dice tutto quello che vede
 *     /dev/xhci.drv -i         la sonda: dice se c'e' un xHCI, esce
 *
 * ! PERCHE' ESISTE, VISTO CHE C'E' GIA' uhci.drv. Perche' una scheda madre
 * davvero senza legacy non ha un UHCI: ha un xHCI e basta. Su quella macchina
 * uhci.drv non trova niente da guidare, e senza questo EX-OS non ha input.
 *
 * -----------------------------------------------------------------------------
 * ! COSA CAMBIA E COSA NO RISPETTO A UHCI
 *
 * Lo stack USB si divide in due meta' di dimensione molto diversa, ed e' il
 * motivo per cui UHCI e' venuto prima:
 *
 *   il controller     UHCI: porte I/O, una lista di frame, descrittori da 32
 *                     byte. xHCI: MMIO, anelli di comando e di evento,
 *                     contesti dei dispositivi. Mondi diversi — ed e' QUESTO
 *                     file.
 *   tutto il resto    enumerazione, indirizzi, descrittori, configurazioni,
 *                     classe HID, hub. IDENTICO su qualunque controller, gia'
 *                     scritto e provato in drivers/uhci/uhci.c.
 *
 * ! LA DIFFERENZA CHE SORPRENDE: L'INDIRIZZO NON LO ASSEGNA IL DRIVER.
 * Su UHCI si manda SET_ADDRESS e si tiene il conto a mano. Qui si chiede al
 * controller un «alloggiamento» (Enable Slot) e poi gli si dice «indirizza
 * quello che c'e' sulla porta N» (Address Device): l'indirizzo USB lo mette
 * lui e il driver non lo vede quasi mai. Da fuori il dispositivo si nomina
 * con il numero di alloggiamento, non con l'indirizzo.
 *
 * -----------------------------------------------------------------------------
 * ! I TRE SPAZI DI REGISTRI, e non sono a spiazzamenti fissi
 *
 * Una finestra MMIO sola, divisa in tre pezzi le cui posizioni si LEGGONO:
 *
 *   capacita'   all'inizio. Dice, fra l'altro, dove stanno gli altri due.
 *   operativi   a base + CAPLENGTH. Comando, stato, porte.
 *   runtime     a base + RTSOFF. Gli interrupter, cioe' gli anelli di evento.
 *   campanelli  a base + DBOFF. Si scrive li' per dire «ho messo qualcosa
 *               in un anello».
 *
 * ! SCRIVERE A SPIAZZAMENTI FISSI E' SBAGLIATO ANCHE QUANDO INDOVINA.
 * CAPLENGTH vale 0x40 sul qemu-xhci, 0x20 su altri, 0x80 su certi ferri: e'
 * un numero da LEGGERE, e la stessa cosa vale per RTSOFF e DBOFF. Un driver
 * che li da' per noti funziona sulla macchina su cui e' stato scritto.
 *
 * -----------------------------------------------------------------------------
 * ! IL DMA: ANELLI E CONTESTI STANNO TUTTI IN dma_alloc()
 *
 * L'xHCI e' un bus master come l'UHCI: legge gli anelli, i contesti e i
 * buffer agli indirizzi FISICI che gli si danno, senza passare dalla MMU.
 * In piu' pretende allineamenti a 64 byte per quasi tutto: dma_alloc rende
 * memoria allineata a pagina, quindi disponendo i pezzi a spiazzamenti
 * multipli di 4096 l'allineamento e' gratis.
 *
 * ! E VUOLE INDIRIZZI A 64 BIT. I registri dei puntatori sono larghi 64 bit
 * anche su una macchina a 32: si scrive la meta' bassa e SI AZZERA LA META'
 * ALTA. Lasciarla com'e' vuol dire dare al controller un indirizzo con
 * dentro i resti di quello che c'era prima — cioe' DMA in un punto a caso
 * della memoria fisica.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "kbd_proto.h"
#include "usb_comune.h"

/* +0.001 a ogni modifica: `xhci.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("xhci.drv", "0.001");

/* --- Registri di CAPACITA', dall'inizio della finestra -------------------- */
#define C_CAPLENGTH     0x00    /* 8 bit: quanto sono lunghi questi registri */
#define C_HCIVERSION    0x02    /* 16 bit */
#define C_HCSPARAMS1    0x04
#define C_HCSPARAMS2    0x08
#define C_HCCPARAMS1    0x10
#define C_DBOFF         0x14
#define C_RTSOFF        0x18

/* HCSPARAMS1 */
#define HCS1_MAXSLOTS(v)    ((v) & 0xFF)
#define HCS1_MAXPORTS(v)    (((v) >> 24) & 0xFF)
/* HCSPARAMS2: i buffer «di appoggio» che il controller pretende in prestito */
#define HCS2_SPB(v)         ((((v) >> 21) & 0x1F) | ((((v) >> 27) & 0x1F) << 5))
/* HCCPARAMS1 */
#define HCC1_CSZ            0x00000004u     /* contesti da 64 byte, non 32 */
#define HCC1_XECP(v)        (((v) >> 16) & 0xFFFF)   /* in DWORD dalla base */

/* --- Registri OPERATIVI, da base + CAPLENGTH ----------------------------- */
#define O_USBCMD        0x00
#define O_USBSTS        0x04
#define O_PAGESIZE      0x08
#define O_DNCTRL        0x14
#define O_CRCR          0x18    /* 64 bit */
#define O_DCBAAP        0x30    /* 64 bit */
#define O_CONFIG        0x38
#define O_PORTSC(p)     (0x400 + ((p) - 1) * 0x10)

#define CMD_RS          0x00000001u     /* corri */
#define CMD_HCRST       0x00000002u     /* azzera tutto il controller */
#define CMD_INTE        0x00000004u

#define STS_HCH         0x00000001u     /* fermo */
#define STS_HSE         0x00000004u     /* errore di sistema: DMA rifiutato */
#define STS_EINT        0x00000008u
#define STS_PCD         0x00000010u     /* una porta e' cambiata */
#define STS_CNR         0x00000800u     /* «non pronto»: non toccarmi */

/* PORTSC */
#define PSC_CCS         0x00000001u     /* c'e' qualcosa attaccato */
#define PSC_PED         0x00000002u     /* porta abilitata */
#define PSC_OCA         0x00000008u
#define PSC_PR          0x00000010u     /* azzera la porta */
#define PSC_PP          0x00000200u     /* alimentazione */
#define PSC_SPEED(v)    (((v) >> 10) & 0x0F)
#define PSC_CSC         0x00020000u
#define PSC_PEC         0x00040000u
#define PSC_WRC         0x00080000u
#define PSC_OCC         0x00100000u
#define PSC_PRC         0x00200000u
#define PSC_PLC         0x00400000u
#define PSC_CEC         0x00800000u
/* ! I BIT DI CAMBIAMENTO SI AZZERANO SCRIVENDOCI 1, e stanno nello stesso
 * registro dei comandi. Scrivere PORTSC senza mascherarli vuol dire azzerare
 * per sbaglio dei cambiamenti che non si sono ancora guardati. */
#define PSC_CAMBI       (PSC_CSC | PSC_PEC | PSC_WRC | PSC_OCC | \
                         PSC_PRC | PSC_PLC | PSC_CEC)
/* I bit che scrivendoci 1 fanno qualcosa di irreversibile o indesiderato */
#define PSC_RW_MASK     0x0E01C3E0u

/* --- Registri di RUNTIME, da base + RTSOFF ------------------------------- */
#define R_IMAN(i)       (0x20 + (i) * 32 + 0x00)
#define R_IMOD(i)       (0x20 + (i) * 32 + 0x04)
#define R_ERSTSZ(i)     (0x20 + (i) * 32 + 0x08)
#define R_ERSTBA(i)     (0x20 + (i) * 32 + 0x10)   /* 64 bit */
#define R_ERDP(i)       (0x20 + (i) * 32 + 0x18)   /* 64 bit */

#define ERDP_EHB        0x00000008u     /* «ho svuotato»: si scrive 1 */

/* --- TRB: il mattone di tutti gli anelli, 16 byte ------------------------ */
#define TRB_C           0x00000001u     /* «ciclo»: dice di chi e' il TRB */
#define TRB_TC          0x00000002u     /* solo nel Link: gira il ciclo */
#define TRB_TIPO(t)     (((t) & 0x3F) << 10)
#define TRB_TIPO_DI(v)  (((v) >> 10) & 0x3F)

#define TRB_NORMALE     1
#define TRB_SETUP       2
#define TRB_DATI        3
#define TRB_STATO       4
#define TRB_LINK        6
#define TRB_NOOP_CMD    23
#define TRB_ENABLE_SLOT 9
#define TRB_ADDR_DEV    11
#define TRB_CONFIG_EP   12
#define TRB_EVAL_CTX    13
#define TRB_EV_TRASF    32
#define TRB_EV_COMANDO  33
#define TRB_EV_PORTA    34

#define TRB_IOC         0x00000020u     /* «avvisami quando hai finito» */
#define TRB_IDT         0x00000040u     /* i dati stanno NEL TRB */
#define TRB_DIR_IN      0x00010000u

#define TRB_COD(v)      (((v) >> 24) & 0xFF)    /* codice di completamento */
#define TRB_SLOT(v)     (((v) >> 24) & 0xFF)
#define COD_OK          1
#define COD_CORTO       13      /* «pacchetto corto»: NON e' un errore */

/* Le richieste USB, i descrittori e le classi stanno in usb_comune.h: sono
 * dello standard, non di questo controller. */

#define ANELLO_TRB      256     /* TRB per anello: 256 * 16 = una pagina */

/* -----------------------------------------------------------------------------
 * Stato
 * --------------------------------------------------------------------------- */
static volatile unsigned char *g_mmio = 0;  /* la finestra, base */
static volatile unsigned char *g_op   = 0;  /* + CAPLENGTH */
static volatile unsigned char *g_rt   = 0;  /* + RTSOFF */
static volatile unsigned char *g_db   = 0;  /* + DBOFF */

static unsigned int g_verboso  = 0;
static unsigned int g_porte    = 0;
static unsigned int g_slot_max = 0;
static unsigned int g_ctx64    = 0;     /* contesti da 64 byte invece di 32 */

static unsigned int g_dma_virt = 0, g_dma_fis = 0;
#define DMA_BYTE    (32u * 4096u)

/* Disposizione dentro la zona DMA. Ogni pezzo a inizio pagina, cosi'
 * l'allineamento a 64 che l'xHCI pretende non e' un problema da risolvere. */
#define OFF_DCBAA   0x0000      /* (slot+1) * 8 byte */
#define OFF_CMD     0x1000      /* anello dei comandi */
#define OFF_EVT     0x2000      /* anello degli eventi, un segmento solo */
#define OFF_ERST    0x3000      /* la tabella dei segmenti: una riga */
#define OFF_SPB     0x3800      /* l'elenco dei buffer di appoggio */
#define OFF_IN_CTX  0x4000      /* contesto d'ingresso: cosa chiediamo */
#define OFF_BUF     0x5000      /* buffer dei trasferimenti di controllo */
#define OFF_BUFI    0x6000      /* dove arrivano i rapporti HID */
#define OFF_EPI     0x7000      /* anello dell'endpoint di interruzione */

/* ! UN CONTESTO E UN ANELLO PER ALLOGGIAMENTO, e con gli hub non e' piu' un
 * lusso: un mouse dietro un hub sono DUE dispositivi indirizzati insieme —
 * l'hub, a cui si continua a parlare per interrogare le sue porte, e il mouse.
 * Con un contesto solo il secondo indirizzamento cancellava il primo, e il
 * sintomo sarebbe stato un hub che smette di rispondere appena si trova
 * qualcosa attaccato. */
#define SLOT_MAX    4
#define OFF_DEV_CTX(s)  (0x8000u + ((s) - 1u) * 0x1000u)   /* 0x8000..0xB000 */
#define OFF_EP0(s)      (0xC000u + ((s) - 1u) * 0x1000u)   /* 0xC000..0xF000 */

#define OFF_SPB0    0x10000     /* i buffer di appoggio, una pagina l'uno */
#define SPB_MAX     15

#define VIRT(off)   ((volatile unsigned int *)(g_dma_virt + (off)))
#define FIS(off)    (g_dma_fis + (off))

/* Gli anelli, lato nostro */
static unsigned int g_cmd_i = 0, g_cmd_ciclo = 1;
static unsigned int g_evt_i = 0, g_evt_ciclo = 1;
/* Lo stato dell'anello dell'endpoint 0, uno per alloggiamento. L'indice 0 non
 * si usa: gli alloggiamenti partono da 1, come li numera il controller. */
static unsigned int g_ep0_i[SLOT_MAX + 1];
static unsigned int g_ep0_ciclo[SLOT_MAX + 1];
static unsigned int g_epi_i = 0, g_epi_ciclo = 1;

/* Il dispositivo indirizzato */
static unsigned int g_slot = 0;
static unsigned int g_maxp0 = 8;
static unsigned int g_dci   = 0;    /* l'endpoint di interruzione, numerato
                                     * come lo numera l'xHCI */
static UsbDispositivo g_dev;        /* cio' che la meta' comune ha letto */
static int g_kbd_pid = -1;

/* Stato mouse consegnato ai client (stesso protocollo del PS/2) */
static int          g_dx = 0, g_dy = 0;
static unsigned int g_bottoni = 0, g_novita = 0, g_attesa_pid = 0;

/* -----------------------------------------------------------------------------
 * Accesso ai registri
 *
 * ! `volatile` OVUNQUE E NIENTE VARIABILI D'APPOGGIO. Un registro cambia
 * senza che nessuno scriva, e un ciclo che aspetta un bit letto una volta
 * sola non finisce mai.
 * --------------------------------------------------------------------------- */
static unsigned int rd32(volatile unsigned char *b, unsigned int off)
{
    return *(volatile unsigned int *)(b + off);
}

static void wr32(volatile unsigned char *b, unsigned int off, unsigned int v)
{
    *(volatile unsigned int *)(b + off) = v;
}

/* ! LA META' ALTA SI AZZERA SEMPRE, ANCHE SE «TANTO SIAMO A 32 BIT». E'
 * proprio perche' siamo a 32 bit che nessuno la scriverebbe mai per caso: se
 * il BIOS o un reset ci ha lasciato dentro qualcosa, il controller legge un
 * indirizzo a 64 bit che non esiste e fa DMA nel vuoto — o peggio, non nel
 * vuoto. Si scrive prima la parte bassa e poi l'alta, perche' certi
 * controller campionano il registro alla scrittura della seconda meta'. */
static void wr64(volatile unsigned char *b, unsigned int off, unsigned int basso)
{
    *(volatile unsigned int *)(b + off)     = basso;
    *(volatile unsigned int *)(b + off + 4) = 0;
}

/* -----------------------------------------------------------------------------
 * Il TRB
 * --------------------------------------------------------------------------- */
static void trb_scrivi(volatile unsigned int *t, unsigned int p0, unsigned int p1,
                       unsigned int stato, unsigned int controllo)
{
    t[0] = p0;
    t[1] = p1;
    t[2] = stato;
    /* ! IL DWORD DI CONTROLLO PER ULTIMO, e non e' pedanteria: contiene il
     * bit di ciclo, cioe' il momento in cui il TRB passa di proprieta' al
     * controller. Scriverlo per primo vuol dire consegnargli un TRB di cui
     * non ha ancora letto il resto. */
    t[3] = controllo;
}

/* -----------------------------------------------------------------------------
 * Prendere il controller al BIOS
 *
 * ! SE NON SI FA, IL CONTROLLER E' DI QUALCUN ALTRO. Il firmware lo usa per
 * far funzionare tastiera e mouse USB prima che ci sia un sistema operativo,
 * e continua a usarlo finche' non gli si dice di smettere. Due padroni sullo
 * stesso hardware vuol dire registri che cambiano sotto le mani, e il sintomo
 * non somiglia a «il BIOS e' ancora attaccato»: somiglia a un controller che
 * a volte risponde e a volte no.
 *
 * ! E SI TROVA SCORRENDO UNA CATENA, non a uno spiazzamento fisso: le
 * capacita' estese sono una lista concatenata, ognuna dice quanto e' lontana
 * la prossima in DWORD. Un «prossimo» a zero e' la fine — e senza quel
 * controllo si gira per sempre sullo stesso elemento.
 * --------------------------------------------------------------------------- */
static void bios_via(void)
{
    unsigned int hcc = rd32(g_mmio, C_HCCPARAMS1);
    unsigned int off = HCC1_XECP(hcc) * 4;
    unsigned int giri;

    if (off == 0) return;   /* nessuna capacita' estesa: niente da togliere */

    for (giri = 0; giri < 64 && off; giri++) {
        unsigned int cap = rd32(g_mmio, off);
        unsigned int id  = cap & 0xFF;
        unsigned int nxt = (cap >> 8) & 0xFF;

        if (id == 1) {              /* USB Legacy Support */
            unsigned int t;

            if ((cap & 0x00010000u) == 0) {
                if (g_verboso) printf("xhci: il BIOS non lo teneva\n");
                return;
            }
            /* Si chiede: si accende il bit «e' del sistema operativo» e si
             * aspetta che il firmware spenga il proprio. */
            wr32(g_mmio, off, cap | 0x01000000u);
            for (t = 0; t < 200; t++) {
                cap = rd32(g_mmio, off);
                if ((cap & 0x00010000u) == 0) break;
                usleep(5000);
            }
            if (cap & 0x00010000u)
                printf("xhci: !  il BIOS non ha mollato il controller\n");
            else if (g_verboso)
                printf("xhci: controller tolto al BIOS\n");

            /* ! E SI ZITTISCONO I SUOI INTERRUPT DI EMULAZIONE. Restano
             * armati anche dopo il passaggio di consegne, e sono interrupt
             * che nessuno raccoglie piu'. */
            wr32(g_mmio, off + 4, 0);
            return;
        }
        if (nxt == 0) return;
        off += nxt * 4;
    }
}

/* -----------------------------------------------------------------------------
 * Fermare e azzerare
 *
 * ! PRIMA SI FERMA, POI SI AZZERA. Un HCRST dato mentre il controller corre
 * e' fuori specifica: si ottiene un reset «riuscito» e uno stato interno a
 * meta', che si manifesta molto dopo come un anello che non avanza.
 * --------------------------------------------------------------------------- */
static int hc_reset(void)
{
    unsigned int t, v;

    v = rd32(g_op, O_USBCMD);
    wr32(g_op, O_USBCMD, v & ~CMD_RS);

    for (t = 0; t < 200; t++) {
        if (rd32(g_op, O_USBSTS) & STS_HCH) break;
        usleep(1000);
    }
    if (!(rd32(g_op, O_USBSTS) & STS_HCH)) {
        printf("xhci: non si ferma\n");
        return 0;
    }

    wr32(g_op, O_USBCMD, CMD_HCRST);

    /* ! DUE ATTESE, NON UNA. Prima che HCRST si spenga da solo, e POI che
     * CNR — «controller non pronto» — si spenga. Fermarsi alla prima
     * significa scrivere DCBAAP e CRCR mentre il controller sta ancora
     * rimettendo a posto le proprie strutture: le scritture si perdono
     * senza dare errore. */
    for (t = 0; t < 1000; t++) {
        if (!(rd32(g_op, O_USBCMD) & CMD_HCRST)) break;
        usleep(1000);
    }
    if (rd32(g_op, O_USBCMD) & CMD_HCRST) {
        printf("xhci: il reset non finisce\n");
        return 0;
    }
    for (t = 0; t < 1000; t++) {
        if (!(rd32(g_op, O_USBSTS) & STS_CNR)) break;
        usleep(1000);
    }
    if (rd32(g_op, O_USBSTS) & STS_CNR) {
        printf("xhci: resta 'non pronto' dopo il reset\n");
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * I buffer di appoggio
 *
 * ! NON SONO NOSTRI E NON SI TOCCANO: sono memoria che il controller chiede
 * IN PRESTITO per i propri comodi interni. Il numero lo dice lui in
 * HCSPARAMS2, e se se ne dichiarano meno di quanti ne chiede si comporta in
 * modi che non somigliano a «mi manca memoria».
 *
 * Su QEMU sono zero e questa funzione non fa niente — motivo in piu' per
 * scriverla adesso invece che quando un ferro vero ne chiedera' otto.
 * --------------------------------------------------------------------------- */
static void appoggio_prepara(void)
{
    unsigned int n = HCS2_SPB(rd32(g_mmio, C_HCSPARAMS2));
    volatile unsigned int *elenco = VIRT(OFF_SPB);
    unsigned int i;

    if (n == 0) {
        VIRT(OFF_DCBAA)[0] = 0;
        VIRT(OFF_DCBAA)[1] = 0;
        return;
    }
    if (n > SPB_MAX) {
        /* ! SI DICE, NON SI TACE. Dichiararne meno di quanti ne chiede e' un
         * guasto che si manifesta molto dopo e in modi che non somigliano a
         * «memoria insufficiente»: meglio una riga che lo dica adesso. */
        printf("xhci: !  il controller chiede %u buffer di appoggio, ne diamo %u\n",
               n, (unsigned int)SPB_MAX);
        n = SPB_MAX;
    }

    for (i = 0; i < n; i++) {
        elenco[i * 2]     = FIS(OFF_SPB0 + i * 4096);
        elenco[i * 2 + 1] = 0;
        memset((void *)(g_dma_virt + OFF_SPB0 + i * 4096), 0, 4096);
    }

    /* ! LA VOCE 0 DEL DCBAA NON E' UNO SLOT: e' il puntatore a questo
     * elenco. E' l'unico posto dove va, ed e' anche il motivo per cui gli
     * slot cominciano da 1. */
    VIRT(OFF_DCBAA)[0] = FIS(OFF_SPB);
    VIRT(OFF_DCBAA)[1] = 0;

    if (g_verboso) printf("xhci: %u buffer di appoggio\n", n);
}

/* -----------------------------------------------------------------------------
 * Gli anelli
 *
 * ! UN ANELLO NON E' UN CERCHIO: e' un pezzo di memoria dritto con in fondo
 * un TRB di tipo Link che rimanda all'inizio. E quel Link porta il bit
 * «gira il ciclo», che e' cio' che distingue i TRB di questo giro da quelli
 * del giro precedente — senza, al secondo giro il controller rieseguirebbe i
 * comandi vecchi credendoli nuovi.
 * --------------------------------------------------------------------------- */
static void anelli_prepara(void)
{
    volatile unsigned int *cmd = VIRT(OFF_CMD);
    volatile unsigned int *erst = VIRT(OFF_ERST);

    memset((void *)(g_dma_virt + OFF_CMD), 0, 4096);
    memset((void *)(g_dma_virt + OFF_EVT), 0, 4096);

    /* Il Link in fondo all'anello dei comandi */
    trb_scrivi(&cmd[(ANELLO_TRB - 1) * 4], FIS(OFF_CMD), 0, 0,
               TRB_TIPO(TRB_LINK) | TRB_TC);
    g_cmd_i = 0; g_cmd_ciclo = 1;

    /* ! L'ANELLO DEGLI EVENTI NON HA IL LINK, e non e' una dimenticanza: e'
     * il controller a scriverci, e sa dov'e' la fine perche' gliela dice la
     * tabella dei segmenti. Metterci un Link vorrebbe dire togliergli un
     * posto e non dirglielo. */
    g_evt_i = 0; g_evt_ciclo = 1;

    erst[0] = FIS(OFF_EVT);     /* base del segmento */
    erst[1] = 0;
    erst[2] = ANELLO_TRB;       /* quanti TRB ci stanno */
    erst[3] = 0;
}

/* Mette un TRB nell'anello dei comandi e suona il campanello 0.
 *
 * ! IL CAMPANELLO SI SUONA DOPO, SEMPRE. E' la scrittura che dice al
 * controller «vai a guardare»: farlo prima vuol dire mandarlo a leggere un
 * TRB che non c'e' ancora. */
static void comando(unsigned int p0, unsigned int p1, unsigned int stato,
                    unsigned int tipo, unsigned int extra)
{
    volatile unsigned int *t = VIRT(OFF_CMD + g_cmd_i * 16);

    /* `extra` sono i bit alti del dword di controllo: per quasi tutti i
     * comandi che riguardano un dispositivo, il numero di alloggiamento sta
     * in 31:24. Un Address Device senza quel campo si rivolge allo slot 0,
     * che non esiste. */
    trb_scrivi(t, p0, p1, stato,
               TRB_TIPO(tipo) | extra | (g_cmd_ciclo ? TRB_C : 0));

    g_cmd_i++;
    if (g_cmd_i == ANELLO_TRB - 1) {    /* siamo arrivati al Link */
        volatile unsigned int *l = VIRT(OFF_CMD + (ANELLO_TRB - 1) * 16);
        l[3] = TRB_TIPO(TRB_LINK) | TRB_TC | (g_cmd_ciclo ? TRB_C : 0);
        g_cmd_i = 0;
        g_cmd_ciclo ^= 1;
    }

    wr32(g_db, 0, 0);
}

/* Aspetta un evento e lo rende. 0 = niente entro il tempo dato.
 *
 * ! SI RICONOSCE DAL BIT DI CICLO, NON DA UN CONTATORE. Il controller non
 * dice «ce ne sono tre»: scrive i TRB in fila e li marca con il ciclo
 * corrente. Un TRB il cui ciclo non combacia e' un TRB del giro scorso, cioe'
 * spazzatura che sembra un evento — ed e' il modo piu' facile di leggere
 * eventi che non sono mai successi. */
static int evento(unsigned int *out, unsigned int ms)
{
    unsigned int t;

    for (t = 0; t < ms; t++) {
        volatile unsigned int *e = VIRT(OFF_EVT + g_evt_i * 16);
        unsigned int c = e[3];

        if ((c & TRB_C) == (g_evt_ciclo ? TRB_C : 0)) {
            out[0] = e[0]; out[1] = e[1]; out[2] = e[2]; out[3] = c;

            g_evt_i++;
            if (g_evt_i == ANELLO_TRB) { g_evt_i = 0; g_evt_ciclo ^= 1; }

            /* ! SI DICE FIN DOVE SI E' LETTO, o l'anello si riempie e il
             * controller smette di consegnare. EHB va scritto a 1 per
             * spegnerlo: e' un bit che si azzera scrivendoci 1, come i
             * cambiamenti delle porte. */
            wr64(g_rt, R_ERDP(0), FIS(OFF_EVT + g_evt_i * 16) | ERDP_EHB);
            return 1;
        }
        usleep(1000);
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * Avvio
 * --------------------------------------------------------------------------- */
static int hc_init(void)
{
    unsigned int hcs1 = rd32(g_mmio, C_HCSPARAMS1);
    unsigned int hcc1 = rd32(g_mmio, C_HCCPARAMS1);

    g_porte    = HCS1_MAXPORTS(hcs1);
    g_slot_max = HCS1_MAXSLOTS(hcs1);
    g_ctx64    = (hcc1 & HCC1_CSZ) ? 1 : 0;

    if (g_porte == 0 || g_porte > 255) {
        printf("xhci: HCSPARAMS1 dice %u porte: non ha senso\n", g_porte);
        return 0;
    }

    bios_via();
    if (!hc_reset()) return 0;

    memset((void *)g_dma_virt, 0, DMA_BYTE);
    appoggio_prepara();
    anelli_prepara();

    /* ! QUANTI SLOT SI USANO VA DICHIARATO, e il massimo non e' il valore
     * giusto: il controller alloca in base a questo numero. Ne chiediamo
     * quanti ne ha, che e' semplice e non costa niente su un emulatore; su
     * un ferro con 64 slot varra' la pena chiederne meno. */
    wr32(g_op, O_CONFIG, g_slot_max);

    wr64(g_op, O_DCBAAP, FIS(OFF_DCBAA));

    /* ! IL BIT 0 DI CRCR E' IL CICLO, NON UN PEZZO DELL'INDIRIZZO. L'anello
     * e' allineato a 64 byte, quindi i sei bit bassi sono liberi per i flag:
     * scriverci l'indirizzo nudo lascia il ciclo a 0 e il controller aspetta
     * dei comandi che, per lui, non sono ancora suoi. */
    wr64(g_op, O_CRCR, FIS(OFF_CMD) | 1);

    /* L'interrupter 0: la tabella dei segmenti, dove siamo arrivati a
     * leggere, e quante righe ha la tabella.
     *
     * ! ERSTSZ SI SCRIVE PRIMA DI ERSTBA. Scrivere la base con la dimensione
     * ancora a zero vuol dire dichiarare un anello di eventi lungo NIENTE, e
     * il controller non ha dove scrivere cio' che succede. */
    wr32(g_rt, R_ERSTSZ(0), 1);
    wr64(g_rt, R_ERDP(0), FIS(OFF_EVT));
    wr64(g_rt, R_ERSTBA(0), FIS(OFF_ERST));

    /* Si parte. */
    wr32(g_op, O_USBCMD, CMD_RS);

    {
        unsigned int t;
        for (t = 0; t < 200; t++) {
            if (!(rd32(g_op, O_USBSTS) & STS_HCH)) break;
            usleep(1000);
        }
        if (rd32(g_op, O_USBSTS) & STS_HCH) {
            printf("xhci: non parte (USBSTS 0x%x)\n", rd32(g_op, O_USBSTS));
            return 0;
        }
    }

    /* ! HSE VA GUARDATO SUBITO. «Host System Error» vuol dire che una
     * lettura DMA del controller e' stata rifiutata — cioe' che gli abbiamo
     * dato un indirizzo che non puo' leggere. E' l'errore che dice «hai
     * sbagliato un puntatore fisico», e senza guardarlo si passa un'ora a
     * cercarlo negli anelli. */
    if (rd32(g_op, O_USBSTS) & STS_HSE) {
        printf("xhci: HSE: il controller non riesce a leggere la nostra memoria\n");
        return 0;
    }

    return 1;
}

/* -----------------------------------------------------------------------------
 * La prova che gli anelli funzionano: un comando che non fa niente
 *
 * ! UN «NO OP» NON E' UNA PROVA INUTILE, E' L'UNICA PROVA PULITA. Fa il giro
 * completo — noi scriviamo un TRB, suoniamo il campanello, il controller lo
 * legge, esegue e scrive un evento che noi rileggiamo — senza che ci sia di
 * mezzo un dispositivo che possa essere lui quello rotto. Se questo passa,
 * anello dei comandi, campanello, anello degli eventi e bit di ciclo sono
 * tutti a posto; se non passa, non ha senso guardare piu' in la'.
 * --------------------------------------------------------------------------- */
static int prova_anelli(void)
{
    unsigned int e[4];

    comando(0, 0, 0, TRB_NOOP_CMD, 0);

    if (!evento(e, 1000)) {
        printf("xhci: nessun evento dopo un comando nullo: gli anelli non girano\n");
        return 0;
    }
    if (TRB_TIPO_DI(e[3]) != TRB_EV_COMANDO) {
        printf("xhci: atteso un evento di comando, arrivato il tipo %u\n",
               TRB_TIPO_DI(e[3]));
        return 0;
    }
    if (TRB_COD(e[2]) != COD_OK) {
        printf("xhci: comando nullo fallito, codice %u\n", TRB_COD(e[2]));
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Un comando, e la sua risposta
 *
 * ! GLI EVENTI DI PORTA ARRIVANO IN MEZZO, e vanno saltati invece che presi
 * per la risposta. Il controller li scrive quando una porta cambia stato —
 * cosa che succede proprio mentre si sta indirizzando un dispositivo — e chi
 * legge il primo evento che trova crede che il comando sia fallito con un
 * codice che non c'entra niente.
 * --------------------------------------------------------------------------- */
static int comando_risposta(unsigned int p0, unsigned int p1, unsigned int stato,
                            unsigned int tipo, unsigned int extra, unsigned int *e)
{
    unsigned int giri;

    comando(p0, p1, stato, tipo, extra);

    for (giri = 0; giri < 8; giri++) {
        if (!evento(e, 1000)) return 0;
        if (TRB_TIPO_DI(e[3]) == TRB_EV_COMANDO) return 1;
        if (g_verboso && TRB_TIPO_DI(e[3]) == TRB_EV_PORTA)
            printf("xhci: (evento di porta durante un comando)\n");
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * Azzerare una porta
 *
 * ! SERVE SOLO ALLE PORTE USB 2, e va fatto lo stesso. Su USB 3 il
 * collegamento si addestra da solo e la porta si presenta gia' abilitata; su
 * USB 2 finche' non le si da' un reset resta «c'e' qualcosa attaccato» e
 * niente piu'. Chiedere un alloggiamento per un dispositivo su una porta non
 * abilitata fallisce con un codice che parla di contesti, non di porte.
 * --------------------------------------------------------------------------- */
static int porta_reset(unsigned int p)
{
    unsigned int v = rd32(g_op, O_PORTSC(p));
    unsigned int t;

    if (v & PSC_PED) return 1;      /* gia' pronta: USB 3 */

    wr32(g_op, O_PORTSC(p), (v & PSC_RW_MASK) | PSC_PR);

    /* ! SI ASPETTA PRC, NON PED. Il bit «reset finito» e' quello che dice che
     * l'hardware ha davvero concluso; PED si accende un istante dopo, e chi
     * guarda solo quello a volte esce troppo presto. */
    for (t = 0; t < 500; t++) {
        v = rd32(g_op, O_PORTSC(p));
        if (v & PSC_PRC) break;
        usleep(1000);
    }
    if (!(v & PSC_PRC)) {
        printf("xhci: porta %u: il reset non finisce\n", p);
        return 0;
    }
    wr32(g_op, O_PORTSC(p), (v & PSC_RW_MASK) | PSC_PRC);

    v = rd32(g_op, O_PORTSC(p));
    if (!(v & PSC_PED)) {
        printf("xhci: porta %u: resettata ma non abilitata\n", p);
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Indirizzare il dispositivo
 *
 * ! QUI STA LA DIFFERENZA VERA CON UHCI. Su UHCI si manda SET_ADDRESS e ci si
 * tiene il conto. Qui si compila un «contesto d'ingresso» — una scheda che
 * descrive il dispositivo e il suo endpoint 0 — e si chiede al controller di
 * indirizzarlo lui. L'indirizzo USB non lo scegliamo e quasi non lo vediamo:
 * da qui in poi il dispositivo si nomina con il numero di alloggiamento.
 * --------------------------------------------------------------------------- */
#define CTX_B   (g_ctx64 ? 64u : 32u)

static unsigned int maxp_da_velocita(unsigned int s)
{
    switch (s) {
    case 4: case 5: return 512;     /* super speed */
    case 3:         return 64;      /* high speed */
    default:        return 8;       /* full e low: si parte da 8 per specifica */
    }
}

/* Dove sta un dispositivo. Sulla radice il percorso e' 0; dietro un hub e' il
 * numero di porta dell'hub, e la porta della RADICE resta quella dell'hub.
 *
 * ! LA «STRINGA DI PERCORSO» E' LA COSA CHE MANCAVA PER GLI HUB. Su UHCI un
 * dispositivo dietro un hub si indirizzava esattamente come uno diretto: era
 * il driver a tenere il conto. Qui e' il controller a instradare i pacchetti,
 * e per farlo deve sapere da quale porta di quale hub si arriva — quattro bit
 * per livello, fino a cinque livelli. Senza, il comando riesce e i
 * trasferimenti finiscono nel vuoto. */
typedef struct {
    unsigned int radice;        /* porta del controller */
    unsigned int percorso;      /* 0 = attaccato alla radice */
    unsigned int vel;           /* codice di velocita' dell'xHCI */
    unsigned int tt_slot;       /* alloggiamento dell'hub traduttore, 0 = nessuno */
    unsigned int tt_porta;
} XhciPosto;

static int indirizza(const XhciPosto *dove)
{
    volatile unsigned int *ic  = VIRT(OFF_IN_CTX);              /* controllo */
    volatile unsigned int *sc  = VIRT(OFF_IN_CTX + CTX_B);      /* slot */
    volatile unsigned int *ep0 = VIRT(OFF_IN_CTX + CTX_B * 2);  /* endpoint 0 */
    unsigned int e[4];

    memset((void *)(g_dma_virt + OFF_IN_CTX), 0, 4096);
    memset((void *)(g_dma_virt + OFF_DEV_CTX(g_slot)), 0, 4096);
    memset((void *)(g_dma_virt + OFF_EP0(g_slot)), 0, 4096);

    /* L'anello di trasferimento dell'endpoint 0, con il suo Link in fondo. */
    trb_scrivi(VIRT(OFF_EP0(g_slot) + (ANELLO_TRB - 1) * 4),
               FIS(OFF_EP0(g_slot)), 0, 0, TRB_TIPO(TRB_LINK) | TRB_TC);
    g_ep0_i[g_slot] = 0; g_ep0_ciclo[g_slot] = 1;

    /* ! SI DICHIARA COSA SI AGGIUNGE, E LO SLOT CONTA COME UNO. Il bit 0 e'
     * il contesto dello slot, il bit 1 l'endpoint 0. Dimenticare il bit 0
     * fa fallire il comando dicendo «parametro non valido», che non aiuta a
     * capire quale. */
    ic[1] = 0x3;

    /* Contesto dello slot: quanto e' lungo (una voce sola: l'endpoint 0), a
     * che velocita' va, da quale porta della radice si arriva, e — se sta
     * dietro un hub — per quale strada. */
    sc[0] = (1u << 27) | ((dove->vel & 0xF) << 20) | (dove->percorso & 0xFFFFF);
    sc[1] = (dove->radice & 0xFF) << 16;

    /* ! IL TRADUTTORE SERVE SOLO A CHI VA PIANO DIETRO UN HUB VELOCE, e va
     * dichiarato: un mouse full speed attaccato a un hub high speed parla
     * attraverso il «transaction translator» dell'hub, e il controller deve
     * sapere quale, o programma la banda per la velocita' sbagliata. */
    if (dove->tt_slot)
        sc[2] = (dove->tt_slot & 0xFF) | ((dove->tt_porta & 0xFF) << 8);

    /* Contesto dell'endpoint 0: e' di CONTROLLO (tipo 4), tre tentativi, e
     * il puntatore all'anello con il bit di ciclo acceso.
     *
     * ! IL BIT 0 DEL PUNTATORE E' IL CICLO, come in CRCR. Scriverci
     * l'indirizzo nudo vuol dire dire al controller che l'anello e' vuoto. */
    g_maxp0 = maxp_da_velocita(dove->vel);
    ep0[1] = (3u << 1) | (4u << 3) | (g_maxp0 << 16);
    ep0[2] = FIS(OFF_EP0(g_slot)) | 1;
    ep0[3] = 0;
    ep0[4] = 8;     /* lunghezza media di un TRB: 8 per il controllo */

    /* Il posto del contesto del dispositivo, che riempira' il controller. */
    VIRT(OFF_DCBAA)[g_slot * 2]     = FIS(OFF_DEV_CTX(g_slot));
    VIRT(OFF_DCBAA)[g_slot * 2 + 1] = 0;

    if (!comando_risposta(FIS(OFF_IN_CTX), 0, 0,
                          TRB_ADDR_DEV, g_slot << 24, e)) {
        printf("xhci: nessuna risposta all'indirizzamento\n");
        return 0;
    }
    if (TRB_COD(e[2]) != COD_OK) {
        printf("xhci: indirizzamento fallito, codice %u\n", TRB_COD(e[2]));
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Evaluate Context: cambiare cio' che il controller SA, senza rifare tutto
 *
 * ! SERVE A FULL SPEED, E NON E' UN CASO RARO. La specifica dice di partire da
 * un endpoint 0 da 8 byte, leggere i primi byte del descrittore e poi
 * CORREGGERE: solo a high speed il valore e' noto in anticipo. Senza questo
 * comando, ogni trasferimento piu' lungo di 8 byte verso un dispositivo full
 * speed si spezza in modo che non somiglia a un errore di dimensione.
 * --------------------------------------------------------------------------- */
static int valuta_maxp0(unsigned int maxp)
{
    volatile unsigned int *ic   = VIRT(OFF_IN_CTX);
    volatile unsigned int *ep0  = VIRT(OFF_IN_CTX + CTX_B * 2);
    volatile unsigned int *dep0 = VIRT(OFF_DEV_CTX(g_slot) + CTX_B);
    unsigned int e[4], k;

    memset((void *)(g_dma_virt + OFF_IN_CTX), 0, 4096);

    /* ! SOLO L'ENDPOINT 0, e lo slot NO. Di questo comando il controller
     * guarda un campo solo — la dimensione del pacchetto — e accendere anche
     * il bit dello slot vorrebbe dire chiedergli di rivalutare cose che non
     * abbiamo motivo di toccare. */
    ic[1] = 0x2;

    /* Si parte da cio' che il controller ha gia' scritto, cambiando un campo:
     * ricostruirlo da zero vuol dire indovinare gli altri. */
    for (k = 0; k < 5; k++) ep0[k] = dep0[k];
    ep0[1] = (ep0[1] & 0x0000FFFFu) | (maxp << 16);

    if (!comando_risposta(FIS(OFF_IN_CTX), 0, 0,
                          TRB_EVAL_CTX, g_slot << 24, e)) {
        printf("xhci: nessuna risposta a Evaluate Context\n");
        return 0;
    }
    if (TRB_COD(e[2]) != COD_OK) {
        printf("xhci: Evaluate Context fallito, codice %u\n", TRB_COD(e[2]));
        return 0;
    }
    g_maxp0 = maxp;
    return 1;
}

/* -----------------------------------------------------------------------------
 * Dire al controller che quel dispositivo e' un hub
 *
 * ! SE NON GLIELO SI DICE, RIFIUTA DI INDIRIZZARE CIO' CHE STA DIETRO. Per
 * lui e' un dispositivo come un altro finche' non gli si accende il bit
 * «hub» e non gli si dice quante porte ha: la stringa di percorso di un
 * dispositivo dietro un hub che lui non sa essere un hub non porta da nessuna
 * parte.
 * --------------------------------------------------------------------------- */
static int hub_dichiara(unsigned int porte)
{
    volatile unsigned int *ic  = VIRT(OFF_IN_CTX);
    volatile unsigned int *sc  = VIRT(OFF_IN_CTX + CTX_B);
    volatile unsigned int *dsc = VIRT(OFF_DEV_CTX(g_slot));
    unsigned int e[4];

    memset((void *)(g_dma_virt + OFF_IN_CTX), 0, 4096);

    ic[1] = 0x1;                    /* solo il contesto dello slot */

    sc[0] = dsc[0] | (1u << 26);    /* il bit «sono un hub» */
    sc[1] = (dsc[1] & 0x00FFFFFFu) | ((porte & 0xFF) << 24);
    sc[2] = dsc[2];
    sc[3] = dsc[3];

    if (!comando_risposta(FIS(OFF_IN_CTX), 0, 0,
                          TRB_EVAL_CTX, g_slot << 24, e)) {
        printf("xhci: nessuna risposta dichiarando l'hub\n");
        return 0;
    }
    if (TRB_COD(e[2]) != COD_OK) {
        printf("xhci: l'hub non e' stato accettato, codice %u\n", TRB_COD(e[2]));
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Un trasferimento di controllo
 *
 * ! TRE TRB PER UNA RICHIESTA SOLA: setup, dati, stato. E' la stessa
 * tripletta dell'UHCI — dove erano tre TD — solo scritta diversamente. Il
 * primo porta gli otto byte della richiesta DENTRO il TRB (bit IDT), quindi
 * non serve un buffer per loro.
 * --------------------------------------------------------------------------- */
/* ! L'ANELLO E' QUELLO DELL'ALLOGGIAMENTO, non «quello». Da quando i
 * dispositivi indirizzati possono essere due — un hub e cio' che ci sta
 * dietro — mettere i TRB sempre nello stesso anello vorrebbe dire mandare
 * all'uno le richieste destinate all'altro. */
static void ep0_metti(unsigned int slot, unsigned int p0, unsigned int p1,
                      unsigned int stato, unsigned int controllo)
{
    volatile unsigned int *t = VIRT(OFF_EP0(slot) + g_ep0_i[slot] * 16);

    trb_scrivi(t, p0, p1, stato,
               controllo | (g_ep0_ciclo[slot] ? TRB_C : 0));

    g_ep0_i[slot]++;
    if (g_ep0_i[slot] == ANELLO_TRB - 1) {
        volatile unsigned int *l = VIRT(OFF_EP0(slot) + (ANELLO_TRB - 1) * 16);
        l[3] = TRB_TIPO(TRB_LINK) | TRB_TC | (g_ep0_ciclo[slot] ? TRB_C : 0);
        g_ep0_i[slot] = 0;
        g_ep0_ciclo[slot] ^= 1;
    }
}

/* ! LA FIRMA E' QUELLA DI UsbControllo, e non e' un caso: e' la cucitura con
 * cui la meta' comune dello stack chiede un trasferimento senza sapere quale
 * controller ha sotto. `dev` qui e' il numero di ALLOGGIAMENTO; su UHCI la
 * stessa funzione riceve un indirizzo USB. Vedi drivers/usb/usb_comune.h. */
static int controllo(unsigned int dev, unsigned int rt, unsigned int req,
                     unsigned int val, unsigned int idx,
                     void *dati, unsigned int len, int in)
{
    unsigned int e[4], giri;

    if (!in && len && dati)
        memcpy((void *)(g_dma_virt + OFF_BUF), dati, len);

    /* Setup: gli otto byte della richiesta stanno nei due primi dword.
     * TRT (bit 17:16) dice se ci sara' una fase dati e in che verso: 0 =
     * nessuna, 2 = IN, 3 = OUT. Sbagliarlo da' un trasferimento che si
     * ferma senza spiegazioni. */
    ep0_metti(dev, rt | (req << 8) | (val << 16),
              idx | (len << 16),
              8,
              TRB_TIPO(TRB_SETUP) | TRB_IDT |
              (len ? (in ? (2u << 16) : (3u << 16)) : 0));

    if (len)
        ep0_metti(dev, FIS(OFF_BUF), 0, len,
                  TRB_TIPO(TRB_DATI) | (in ? TRB_DIR_IN : 0));

    /* ! LO STATO VA NEL VERSO OPPOSTO AI DATI, ed e' la trappola che si
     * scrive due volte prima di ricordarla: dopo una lettura lo stato e'
     * una scrittura, e viceversa. Solo su di lui si chiede l'avviso (IOC),
     * perche' e' lui a dire che l'intera richiesta e' finita. */
    ep0_metti(dev, 0, 0, 0,
              TRB_TIPO(TRB_STATO) | TRB_IOC | (in ? 0 : TRB_DIR_IN));

    /* ! IL CAMPANELLO DELLO SLOT, CON DENTRO L'ENDPOINT. Il campanello 0 e'
     * quello dei comandi; quello del dispositivo e' il numero di
     * alloggiamento, e il valore scritto dice quale endpoint si e' rifornito:
     * 1 e' l'endpoint 0 di controllo. */
    wr32(g_db, dev * 4, 1);

    for (giri = 0; giri < 8; giri++) {
        if (!evento(e, 1000)) {
            printf("xhci: il trasferimento di controllo non risponde\n");
            return -1;
        }
        if (TRB_TIPO_DI(e[3]) != TRB_EV_TRASF) continue;

        /* ! «PACCHETTO CORTO» NON E' UN ERRORE, ed e' anzi il caso normale
         * quando si chiedono piu' byte di quanti il dispositivo ne abbia:
         * chiedendone 255 ne arriverebbero comunque 18, e il codice sarebbe
         * 13. La meta' comune conta sul fatto che questo non sia un guasto. */
        if (TRB_COD(e[2]) != COD_OK && TRB_COD(e[2]) != COD_CORTO) {
            printf("xhci: trasferimento fallito, codice %u\n", TRB_COD(e[2]));
            return -1;
        }
        {
            /* Il campo «residuo» dice quanti byte NON sono arrivati. */
            unsigned int resto = e[2] & 0x00FFFFFF;
            unsigned int avuti = (resto <= len) ? (len - resto) : 0;

            if (in && dati && avuti)
                memcpy(dati, (void *)(g_dma_virt + OFF_BUF), avuti);
            return 0;
        }
    }
    printf("xhci: nessun evento di trasferimento\n");
    return -1;
}

/* -----------------------------------------------------------------------------
 * L'endpoint di interruzione: dichiararlo, e poi ascoltarlo
 *
 * ! UN ENDPOINT SI DICHIARA, NON SI USA E BASTA. Su UHCI bastava mettere un
 * TD nella lista dei frame; qui il controller deve prima SAPERE che
 * quell'endpoint esiste, con che pacchetto e ogni quanto — glielo si dice con
 * un Configure Endpoint, e senza, il campanello suona nel vuoto.
 * --------------------------------------------------------------------------- */

/* L'xHCI numera gli endpoint a modo suo: l'endpoint N in ingresso e' 2N+1.
 * Non e' una complicazione gratuita — cosi' ingresso e uscita dello stesso
 * numero hanno due contesti distinti, che e' cio' che sono davvero. */
#define DCI_IN(n)   ((n) * 2u + 1u)

/* ! L'INTERVALLO NON E' IL bInterval DEL DESCRITTORE, e le due scale non si
 * assomigliano. L'xHCI vuole il LOGARITMO in base 2 dei microframe; il
 * descrittore, per full e low speed, lo dice in MILLISECONDI. Passarlo
 * pari pari da' un endpoint interrogato migliaia di volte piu' spesso o piu'
 * di rado del dovuto: nel primo caso si vede come bus occupato, nel secondo
 * come un mouse che scatta. */
static unsigned int intervallo_xhci(unsigned int vel, unsigned int b)
{
    if (vel >= 3) {                 /* high, super: gia' log2 di microframe */
        if (b == 0) b = 1;
        if (b > 16) b = 16;
        return b - 1;
    }
    {                               /* full, low: millisecondi -> log2(uframe) */
        unsigned int e = 3, v = b ? b : 1;
        while (v > 1) { v >>= 1; e++; }
        if (e > 15) e = 15;
        return e;
    }
}

static int configura_endpoint(unsigned int ep, unsigned int maxp,
                              unsigned int bintervallo, unsigned int vel)
{
    volatile unsigned int *ic;
    volatile unsigned int *sc;
    volatile unsigned int *ec;
    unsigned int e[4];

    g_dci = DCI_IN(ep);

    memset((void *)(g_dma_virt + OFF_IN_CTX), 0, 4096);
    memset((void *)(g_dma_virt + OFF_EPI), 0, 4096);

    /* L'anello dell'endpoint, con il suo Link in fondo. */
    trb_scrivi(VIRT(OFF_EPI + (ANELLO_TRB - 1) * 4), FIS(OFF_EPI), 0, 0,
               TRB_TIPO(TRB_LINK) | TRB_TC);
    g_epi_i = 0; g_epi_ciclo = 1;

    ic = VIRT(OFF_IN_CTX);
    sc = VIRT(OFF_IN_CTX + CTX_B);
    ec = VIRT(OFF_IN_CTX + CTX_B * (g_dci + 1));

    /* ! SI AGGIUNGE L'ENDPOINT E SI RITOCCA LO SLOT, quindi il bit 0 va acceso
     * anche qui: il contesto dello slot deve dire quante voci ha adesso, e
     * dimenticarlo lascia il controller convinto che oltre l'endpoint 0 non
     * ci sia niente. */
    ic[1] = 1u | (1u << g_dci);

    /* Il contesto dello slot si ricopia da quello che il controller ha
     * scritto dopo l'indirizzamento, cambiando solo il numero di voci: e'
     * l'unico modo di non disfare cio' che lui sa gia' del dispositivo. */
    {
        volatile unsigned int *dsc = VIRT(OFF_DEV_CTX(g_slot));
        sc[0] = (dsc[0] & 0x07FFFFFFu) | (g_dci << 27);
        sc[1] = dsc[1];
        sc[2] = dsc[2];
        sc[3] = dsc[3];
    }

    /* Interruzione IN = tipo 7, tre tentativi. */
    ec[0] = intervallo_xhci(vel, bintervallo) << 16;
    ec[1] = (3u << 1) | (7u << 3) | (maxp << 16);
    ec[2] = FIS(OFF_EPI) | 1;       /* bit 0 = ciclo, come sempre */
    ec[3] = 0;
    ec[4] = maxp;                   /* lunghezza media di un TRB */

    if (!comando_risposta(FIS(OFF_IN_CTX), 0, 0,
                          TRB_CONFIG_EP, g_slot << 24, e)) {
        printf("xhci: nessuna risposta a Configure Endpoint\n");
        return 0;
    }
    if (TRB_COD(e[2]) != COD_OK) {
        printf("xhci: Configure Endpoint fallito, codice %u\n", TRB_COD(e[2]));
        return 0;
    }
    return 1;
}

/* Mette in coda una lettura e suona il campanello dell'endpoint.
 *
 * ! QUI NON SI PUO' FARE COME SU UHCI, dove il TD restava nella lista dei
 * frame per sempre e il controller lo ritentava da solo. Un TRB dell'xHCI si
 * consuma: appena il dispositivo risponde, quella lettura e' finita e va
 * rimessa. Il ciclo di servizio ne tiene sempre una in coda, che e'
 * l'equivalente di «non lasciare mai la coda vuota». */
static void int_arma(void)
{
    volatile unsigned int *t = VIRT(OFF_EPI + g_epi_i * 16);

    trb_scrivi(t, FIS(OFF_BUFI), 0, g_dev.ep_maxp,
               TRB_TIPO(TRB_NORMALE) | TRB_IOC | (g_epi_ciclo ? TRB_C : 0));

    g_epi_i++;
    if (g_epi_i == ANELLO_TRB - 1) {
        volatile unsigned int *l = VIRT(OFF_EPI + (ANELLO_TRB - 1) * 16);
        l[3] = TRB_TIPO(TRB_LINK) | TRB_TC | (g_epi_ciclo ? TRB_C : 0);
        g_epi_i = 0;
        g_epi_ciclo ^= 1;
    }

    wr32(g_db, g_slot * 4, g_dci);
}

/* Rende i byte del rapporto, 0 se il dispositivo non ha ancora detto niente. */
static int int_leggi(unsigned char *out, unsigned int max, unsigned int ms)
{
    unsigned int e[4];

    if (!evento(e, ms)) return 0;
    if (TRB_TIPO_DI(e[3]) != TRB_EV_TRASF) return 0;

    if (TRB_COD(e[2]) != COD_OK && TRB_COD(e[2]) != COD_CORTO) {
        /* ! UN ERRORE NON DEVE FERMARE L'ASCOLTO. Se si smette di rimettere
         * la lettura, il dispositivo tace per sempre e nessuno lo dice. */
        int_arma();
        return 0;
    }

    {
        unsigned int resto = e[2] & 0x00FFFFFF;
        unsigned int avuti = (resto <= g_dev.ep_maxp) ? (g_dev.ep_maxp - resto) : 0;

        if (avuti > max) avuti = max;
        if (avuti) memcpy(out, (void *)(g_dma_virt + OFF_BUFI), avuti);

        int_arma();                 /* subito pronti per il prossimo */
        return (int)avuti;
    }
}

/* -----------------------------------------------------------------------------
 * Le porte
 *
 * ! L'ALIMENTAZIONE NON E' DATA PER SCONTATA. Dopo un reset del controller
 * PP puo' essere spento, e una porta senza corrente dice «non c'e' niente
 * attaccato» qualunque cosa ci sia. E' lo stesso inganno degli hub USB, dove
 * bisognava alimentare e poi ASPETTARE.
 * --------------------------------------------------------------------------- */
static const char *velocita_nome(unsigned int s)
{
    switch (s) {
    case 1:  return "full  (12 Mbit)";
    case 2:  return "low   (1,5 Mbit)";
    case 3:  return "high  (480 Mbit)";
    case 4:  return "super (5 Gbit)";
    case 5:  return "super+ (10 Gbit)";
    default: return "sconosciuta";
    }
}

static unsigned int porte_guarda(void)
{
    unsigned int p, trovati = 0;

    for (p = 1; p <= g_porte; p++) {
        unsigned int v = rd32(g_op, O_PORTSC(p));

        if (!(v & PSC_PP)) {
            /* Accendere la corrente, senza toccare i bit di cambiamento. */
            wr32(g_op, O_PORTSC(p), (v & PSC_RW_MASK) | PSC_PP);
            usleep(20000);
            v = rd32(g_op, O_PORTSC(p));
        }

        if (v & PSC_CCS) {
            trovati++;
            printf("xhci: porta %u  %s%s\n", p, velocita_nome(PSC_SPEED(v)),
                   (v & PSC_PED) ? "  abilitata" : "");
        } else if (g_verboso) {
            printf("xhci: porta %u  niente attaccato\n", p);
        }

        /* I cambiamenti visti si azzerano, o restano a dire per sempre che
         * qualcosa e' successo. */
        if (v & PSC_CAMBI)
            wr32(g_op, O_PORTSC(p), (v & PSC_RW_MASK) | (v & PSC_CAMBI));
    }
    return trovati;
}

/* Rende il numero della prima porta con qualcosa attaccato, 0 se nessuna. */
static unsigned int porta_con_dispositivo(unsigned int *vel)
{
    unsigned int p;

    for (p = 1; p <= g_porte; p++) {
        unsigned int v = rd32(g_op, O_PORTSC(p));
        if (v & PSC_CCS) { *vel = PSC_SPEED(v); return p; }
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * Chiedere un alloggiamento
 *
 * ! IL NUMERO LO DA' IL CONTROLLER, e sta nell'EVENTO, non nel comando. E'
 * la stessa inversione dell'indirizzo: qui non si sceglie, si riceve.
 * --------------------------------------------------------------------------- */
static int enable_slot(void)
{
    unsigned int e[4];

    if (!comando_risposta(0, 0, 0, TRB_ENABLE_SLOT, 0, e)) {
        printf("xhci: nessuna risposta alla richiesta di alloggiamento\n");
        return 0;
    }
    if (TRB_COD(e[2]) != COD_OK) {
        printf("xhci: alloggiamento negato, codice %u\n", TRB_COD(e[2]));
        return 0;
    }
    g_slot = TRB_SLOT(e[3]);
    if (g_slot == 0 || g_slot > g_slot_max) {
        printf("xhci: alloggiamento %u fuori intervallo\n", g_slot);
        return 0;
    }
    /* ! IL LIMITE E' NOSTRO, NON DEL CONTROLLER, e va detto invece di
     * scrivere fuori dalla zona DMA: i contesti e gli anelli sono
     * preparati per SLOT_MAX dispositivi. */
    if (g_slot > SLOT_MAX) {
        printf("xhci: alloggiamento %u: ne reggiamo %u per volta\n",
               g_slot, (unsigned int)SLOT_MAX);
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Interrogare il dispositivo: da qui in poi non e' piu' roba da controller
 *
 * ! TUTTO QUESTO STA IN drivers/usb/usb_comune.c, e queste funzioni sono solo
 * la colla. Descrittori, catena della configurazione, HID «boot», richieste di
 * classe agli hub: identici su UHCI, e infatti sono le stesse righe. Se un
 * giorno una di queste chiamate avesse bisogno di sapere che sotto c'e' un
 * xHCI, vorrebbe dire che la cucitura e' nel posto sbagliato.
 * --------------------------------------------------------------------------- */

/* Le velocita' comuni, nei numeri dell'xHCI. La traduzione sta QUI e non in
 * usb_comune.c, che non deve conoscere i codici di nessun controller. */
static unsigned int vel_xhci(unsigned int v)
{
    switch (v) {
    case USB_VEL_LOW:  return 2;
    case USB_VEL_HIGH: return 3;
    default:           return 1;    /* full */
    }
}

/* Legge i descrittori del dispositivo appena indirizzato e, se l'endpoint 0
 * non e' quello che avevamo dichiarato a scatola chiusa, lo corregge. */
static int conosci(void)
{
    if (!usb_desc_corto(controllo, g_slot, &g_dev)) {
        printf("xhci: il dispositivo non risponde al primo descrittore\n");
        return 0;
    }

    /* ! A FULL SPEED IL maxPacketSize NON SI PUO' SAPERE PRIMA, e questa non
     * e' una correzione di comodo: la specifica prescrive proprio di partire
     * da 8, leggere, e poi rivalutare il contesto. Senza, ogni trasferimento
     * piu' lungo di 8 byte verso un dispositivo full speed si spezza in modo
     * che non somiglia a un errore di dimensione. */
    if (g_dev.maxp0 != g_maxp0) {
        if (g_verboso)
            printf("xhci: l'endpoint 0 vuole %u byte invece di %u: lo correggo\n",
                   g_dev.maxp0, g_maxp0);
        if (!valuta_maxp0(g_dev.maxp0)) return 0;
    }

    if (!usb_desc_lungo(controllo, g_slot, &g_dev)) {
        printf("xhci: descrittore di dispositivo non credibile\n");
        return 0;
    }

    printf("xhci: dispositivo  USB %x.%02x  venditore %04x prodotto %04x  maxp0 %u\n",
           (g_dev.versione >> 8) & 0xFF, g_dev.versione & 0xFF,
           g_dev.venditore, g_dev.prodotto, g_dev.maxp0);
    return 1;
}

/* Configura l'interfaccia HID «boot» e dichiara il suo endpoint. */
static int hid_prepara(unsigned int vel)
{
    if (!usb_configura_hid(controllo, g_slot, &g_dev, g_verboso)) {
        printf("xhci: non e' un HID 'boot'\n");
        return 0;
    }

    printf("xhci: %s USB 'boot', endpoint %u, pacchetto %u\n",
           g_dev.proto == USB_PROTO_MOUSE ? "mouse" : "tastiera",
           g_dev.ep, g_dev.ep_maxp);

    return configura_endpoint(g_dev.ep, g_dev.ep_maxp, g_dev.ep_intervallo, vel);
}

/* -----------------------------------------------------------------------------
 * Dietro un hub
 *
 * ! SENZA QUESTO SI VEDE MENO DI META' DEL BUS, ed e' la stessa ragione per
 * cui gli hub sono serviti su UHCI: su una macchina vera almeno una porta
 * finisce quasi sempre in un hub — quello del pannello frontale, quello dentro
 * un monitor, quello di una tastiera con le prese.
 *
 * ! MA QUI SERVE UNA COSA IN PIU' CHE SU UHCI NON SERVIVA. La', un dispositivo
 * dietro un hub si indirizzava come uno diretto: era il driver a tenere il
 * conto. Qui e' il CONTROLLER a instradare i pacchetti, e per farlo gli
 * servono due cose che non ha modo di indovinare: che quel dispositivo sia un
 * hub, e da quale sua porta si arriva.
 * --------------------------------------------------------------------------- */
static int dietro_hub(unsigned int radice, unsigned int vel_hub)
{
    unsigned int porte = 0, attesa = 0, i;
    unsigned int slot_hub = g_slot;

    /* Le richieste di classe vogliono l'hub configurato, come su UHCI. */
    if (controllo(slot_hub, 0x00, USB_REQ_SET_CONF, 1, 0, 0, 0, 0) != 0) {
        printf("xhci: l'hub non accetta la configurazione\n");
        return 0;
    }

    if (!usb_hub_descrittore(controllo, slot_hub, &porte, &attesa)) {
        printf("xhci: l'hub non dice quante porte ha\n");
        return 0;
    }
    printf("xhci: hub con %u porte\n", porte);

    /* Prima di instradare qualunque cosa, il controller deve SAPERE che e' un
     * hub: vedi hub_dichiara(). */
    if (!hub_dichiara(porte)) return 0;

    usb_hub_accendi(controllo, slot_hub, porte, attesa);

    for (i = 1; i <= porte; i++) {
        unsigned int velp = USB_VEL_FULL;
        XhciPosto dove;

        if (!usb_hub_porta_pronta(controllo, slot_hub, i, &velp)) continue;

        if (!enable_slot()) return 0;

        dove.radice   = radice;
        dove.percorso = i;              /* un livello solo: un nibble basta */
        dove.vel      = vel_xhci(velp);
        dove.tt_slot  = 0;
        dove.tt_porta = 0;

        /* ! IL TRADUTTORE SERVE SOLO A CHI VA PIANO DIETRO UN HUB VELOCE.
         * Un mouse full speed attaccato a un hub high speed parla attraverso
         * il «transaction translator» dell'hub: senza dirlo al controller, la
         * banda viene programmata per la velocita' sbagliata. */
        if (velp != USB_VEL_HIGH && vel_hub == 3) {
            dove.tt_slot  = slot_hub;
            dove.tt_porta = i;
        }

        if (!indirizza(&dove)) continue;
        if (!conosci()) continue;

        /* ! UN SOLO LIVELLO. Gli hub si incatenano fino a cinque, e la
         * stringa di percorso avrebbe posto per tutti — quattro bit a
         * livello. Ma ogni livello in piu' e' ricorsione con lo stato del
         * dispositivo in variabili globali, ed e' lo stesso limite che ha
         * gia' uhci.drv. */
        if (g_dev.classe == USB_CLASSE_HUB) {
            if (g_verboso) printf("xhci: porta %u dell'hub: un altro hub, saltato\n", i);
            continue;
        }

        if (hid_prepara(vel_xhci(velp))) return 1;
    }

    printf("xhci: nessun HID 'boot' dietro l'hub\n");
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il mouse, verso i client — stesso protocollo del PS/2 e del seriale
 * --------------------------------------------------------------------------- */
static void rispondi(unsigned int pid)
{
    MouseStato s;

    s.dx = g_dx; s.dy = g_dy;
    s.bottoni = g_bottoni;
    s.presente = 1;
    s.persi = 0;

    if (ipc_send(pid, MOUSE_MSG_STATO, &s, sizeof(s)) < 0) return;
    g_dx = 0; g_dy = 0; g_novita = 0;
}

/* Il ciclo di servizio: si ascolta l'endpoint e si risponde ai client. */
static void servi(void)
{
    for (;;) {
        IpcMessage    meta;
        unsigned char payload[64];
        unsigned char rap[8];
        int n;

        /* ! SI ASPETTA POCO, NON A LUNGO. L'evento arriva quando il
         * dispositivo ha qualcosa da dire; restare fermi dentro l'attesa
         * vuol dire non rispondere ai client nel frattempo. */
        n = int_leggi(rap, sizeof(rap), 10);

        if (g_dev.proto == USB_PROTO_TASTIERA) {
            if (n >= 8) usb_tastiera_rapporto(rap, g_kbd_pid);
            continue;
        }

        if (n >= 3 && usb_mouse_rapporto(rap, (unsigned int)n,
                                         &g_dx, &g_dy, &g_bottoni)) {
            g_novita = 1;
            if (g_attesa_pid) {
                unsigned int p = g_attesa_pid;
                g_attesa_pid = 0;
                rispondi(p);
            }
        }

        if (ipc_recv_timeout(&meta, payload, sizeof(payload), 10) < 0) continue;

        if (meta.tipo == MOUSE_MSG_LEGGI) {
            unsigned int attendi = 0;

            if (meta.len >= sizeof(unsigned int))
                memcpy(&attendi, payload, sizeof(unsigned int));

            if (!attendi || g_novita) rispondi(meta.sender_pid);
            else                      g_attesa_pid = meta.sender_pid;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Trovare il controller sul PCI
 *
 * ! LA CLASSE E' LA STESSA DELL'UHCI: cambia l'INTERFACCIA. 0x0C/0x03 vuol
 * dire «un controller USB», e il terzo byte dice quale: 0x00 UHCI, 0x10 OHCI,
 * 0x20 EHCI, 0x30 xHCI. Cercare per classe e prendere il primo che capita
 * vuol dire guidare un UHCI credendolo un xHCI.
 * --------------------------------------------------------------------------- */
static int cerca_xhci(unsigned int *irq, unsigned int *bar)
{
    PciRichiesta   r;
    PciDispositivo d;
    IpcMessage     meta;
    unsigned char  buf[IPC_MSG_MAX_DATA];
    int pid = -1, ord;
    unsigned int attesa;

    /* Si aspetta il servizio PCI invece di pretenderlo pronto: questo driver
     * deve poter partire come modulo all'avvio, e i moduli partono tutti
     * insieme. Stessa ragione scritta in uhci.c. */
    for (attesa = 0; attesa < 50; attesa++) {
        pid = ipc_lookup(PCI_SERVIZIO);
        if (pid >= 0) break;
        usleep(100000);
    }
    if (pid < 0) {
        printf("xhci: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
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

        if (d.interfaccia != 0x30) continue;     /* 0x30 = xHCI */

        /* ! I REGISTRI STANNO IN MEMORIA, NON NELLE PORTE, ed e' tutta la
         * differenza con l'UHCI: si cerca un BAR di MEMORIA (bar_io == 0).
         * Un xHCI non ha nessun BAR di I/O, quindi cercare come fa uhci.c
         * non troverebbe niente e sembrerebbe «controller assente». */
        if (d.bar[0] == 0 || d.bar_io[0]) {
            printf("xhci: BAR0 non e' memoria: non so dove sono i registri\n");
            return 0;
        }

        *bar = d.bar[0];
        *irq = d.irq_linea;
        printf("xhci: controller in %02x:%02x.%u, registri a 0x%x, IRQ %u\n",
               d.bus, d.slot, d.funzione, d.bar[0], d.irq_linea);
        return 1;
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    unsigned int irq = 0, bar = 0, solo_sonda = 0;
    MmioZona m;
    DmaZona  z;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-i") == 0) solo_sonda = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("uso: xhci.drv [-v] [-i]\n");
            printf("  -v  dice tutto quello che vede\n");
            printf("  -i  dice se c'e' un xHCI ed esce\n");
            return 0;
        }
    }

    if (!cerca_xhci(&irq, &bar)) {
        printf("xhci: nessun controller xHCI su questa macchina\n");
        return 1;
    }
    if (solo_sonda) return 0;

    /* ! LA FINESTRA SI CHIEDE GRANDE QUANTO SERVE, e «quanto serve» non si
     * sa prima di averla mappata: le porte stanno a 0x400 + n*0x10 nei
     * registri operativi, che cominciano dopo CAPLENGTH. 64 KB coprono
     * qualunque xHCI reale con margine, e mapparne di piu' non costa niente
     * perche' non e' RAM. */
    m.fisico = bar;
    m.byte   = 0x10000;
    if (mmio_map(&m) != 0) {
        printf("xhci: mmio_map rifiutata. Il file dev'essere un .drv\n");
        return 1;
    }
    g_mmio = (volatile unsigned char *)m.virt;

    {
        unsigned int caplen = rd32(g_mmio, C_CAPLENGTH) & 0xFF;
        unsigned int vers   = (rd32(g_mmio, C_CAPLENGTH) >> 16) & 0xFFFF;

        /* ! SE CAPLENGTH E' 0 O 0xFF LA FINESTRA NON E' QUELLA GIUSTA, e
         * conviene dirlo qui invece di leggere registri che non esistono:
         * 0xFF..FF e' quello che si legge da un indirizzo che nessuno
         * decodifica, e da li' in poi ogni valore sembrerebbe plausibile. */
        if (caplen == 0 || caplen == 0xFF) {
            printf("xhci: CAPLENGTH vale 0x%x: la finestra non risponde\n", caplen);
            return 1;
        }
        g_op = g_mmio + caplen;
        g_rt = g_mmio + (rd32(g_mmio, C_RTSOFF) & ~0x1Fu);
        g_db = g_mmio + (rd32(g_mmio, C_DBOFF)  & ~0x03u);

        printf("xhci: versione %u.%u%u, registri operativi a +0x%x\n",
               (vers >> 8) & 0xFF, (vers >> 4) & 0xF, vers & 0xF, caplen);
    }

    z.byte = DMA_BYTE;
    if (dma_alloc(&z) != 0) {
        printf("xhci: dma_alloc rifiutata\n");
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;

    if (!hc_init()) return 1;

    printf("xhci: %u porte, %u alloggiamenti, contesti da %u byte\n",
           g_porte, g_slot_max, g_ctx64 ? 64 : 32);

    if (!prova_anelli()) return 1;
    printf("xhci: anelli di comando e di evento a posto\n");

    {
        unsigned int n = porte_guarda();
        if (n == 0) {
            printf("xhci: nessun dispositivo attaccato\n");
            return 0;
        }
        printf("xhci: %u dispositiv%s attaccat%s\n",
               n, (n == 1) ? "o" : "i", (n == 1) ? "o" : "i");
    }

    /* ! UN DISPOSITIVO PER VOLTA, e va detto invece di lasciarlo credere.
     * Il ciclo su tutte le porte c'e', ma ci si ferma al primo HID che
     * risponde: reggerne due insieme vuole uno stato per dispositivo invece
     * che globale, ed e' lo stesso limite che ha uhci.drv. */
    {
        unsigned int vel = 0;
        unsigned int p = porta_con_dispositivo(&vel);

        XhciPosto dove;

        if (!porta_reset(p)) return 1;
        if (!enable_slot()) return 1;
        if (g_verboso) printf("xhci: alloggiamento %u per la porta %u\n", g_slot, p);

        dove.radice   = p;
        dove.percorso = 0;              /* attaccato alla radice */
        dove.vel      = vel;
        dove.tt_slot  = 0;
        dove.tt_porta = 0;

        if (!indirizza(&dove)) return 1;
        if (!conosci()) return 1;

        if (g_dev.classe == USB_CLASSE_HUB) {
            if (!dietro_hub(p, vel)) return 1;
        } else {
            if (!hid_prepara(vel)) return 1;
        }
    }

    if (g_dev.proto == USB_PROTO_TASTIERA) {
        /* ! NON SI REGISTRA IL SERVIZIO "kbd": si mandano SCANCODE a chi lo
         * serve gia'. La ragione per esteso sta in usb_comune.c — rifare qui
         * mappe, editing di riga, modo raw ed eco vorrebbe dire riscrivere la
         * meta' sottile di kbd.c. */
        g_kbd_pid = ipc_lookup(KBD_SERVICE_NAME);
        if (g_kbd_pid < 0) {
            printf("xhci: il servizio '%s' non c'e': a chi consegno i tasti?\n",
                   KBD_SERVICE_NAME);
            return 1;
        }
        printf("xhci: tastiera USB -> scancode al servizio '%s' (PID %d)\n",
               KBD_SERVICE_NAME, g_kbd_pid);
    } else {
        if (ipc_register(MOUSE_SERVICE_NAME) < 0) {
            printf("xhci: ipc_register('%s') fallita - c'e' gia' un mouse?\n",
                   MOUSE_SERVICE_NAME);
            return 1;
        }
        printf("xhci: servizio '%s' attivo (mouse USB)\n", MOUSE_SERVICE_NAME);
    }

    /* La prima lettura si mette in coda adesso: da qui in poi ce n'e' sempre
     * una in attesa, e il ciclo la rimette appena ne consuma una. */
    int_arma();
    servi();
    return 0;
}
