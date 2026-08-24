/* =============================================================================
 * drivers/uhci/uhci.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * USB: controller UHCI + enumerazione + classe HID «boot»
 *
 *     /dev/uhci.drv            cerca il controller, enumera, serve il mouse
 *     /dev/uhci.drv -v         dice tutto quello che vede
 *     /dev/uhci.drv -i         la sonda: dice se c'e' un UHCI, esce
 *
 * ! A COSA SERVE DAVVERO. Una scheda madre senza supporto legacy non ha
 * l'8042: tastiera e mouse sono USB e basta. Senza uno stack USB, su quella
 * macchina EX-OS non ha input — non «input scomodo», proprio nessuno.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' UHCI PER PRIMO, VISTO CHE LE SCHEDE MODERNE HANNO xHCI
 *
 * Perche' lo stack USB si divide in due meta' di dimensione molto diversa:
 *
 *   il controller     UHCI: porte I/O, una lista di frame, descrittori da
 *                     32 byte. xHCI: MMIO, anelli di comando e di evento,
 *                     contesti dei dispositivi. Sono mondi diversi.
 *   tutto il resto    enumerazione, indirizzi, descrittori, configurazioni,
 *                     classe HID. IDENTICO su qualunque controller.
 *
 * «Tutto il resto» e' il grosso, ed e' anche la parte piu' facile da
 * sbagliare in modi silenziosi. Farlo nascere contro il controller PIU'
 * SEMPLICE e' il modo di avere una sola incognita per volta. Quando arrivera'
 * xHCI cambiera' solo la meta' piccola.
 *
 * -----------------------------------------------------------------------------
 * ! IL DMA: TUTTO CIO' CHE IL CONTROLLER LEGGE STA IN dma_alloc()
 *
 * L'UHCI e' un bus master: legge la lista dei frame, i descrittori e i buffer
 * agli indirizzi FISICI che gli si danno, senza passare dalla MMU. Un blocco
 * di malloc ha un indirizzo virtuale e pagine sparse: darglielo vorrebbe dire
 * farlo leggere in un punto a caso della memoria fisica.
 *
 * La lista dei frame vuole anche l'allineamento a 4096, e i descrittori a 16:
 * dma_alloc rende memoria allineata a pagina, quindi il primo e' gratis e il
 * secondo si ottiene disponendo con cura dentro la pagina.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "kbd_proto.h"
#include "usb_comune.h"

/* +0.001 a ogni modifica: `uhci.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("uhci.drv", "0.001");

/* --- Registri UHCI, spiazzamenti dalla base I/O --------------------------- */
#define R_CMD       0x00    /* USBCMD  (16 bit) */
#define R_STS       0x02    /* USBSTS  (16 bit) */
#define R_INTR      0x04    /* USBINTR (16 bit) */
#define R_FRNUM     0x06    /* (16 bit) */
#define R_FLBASE    0x08    /* (32 bit) fisico della lista dei frame */
#define R_SOF       0x0C    /* (8 bit)  */
#define R_PORT1     0x10    /* PORTSC1 (16 bit) */

#define CMD_RUN     0x0001
#define CMD_HCRESET 0x0002
#define CMD_GRESET  0x0004
#define CMD_CF      0x0040  /* «configurato»: nessun effetto sull'hardware */
#define CMD_MAXP    0x0080  /* pacchetti fino a 64 byte */

#define STS_HALTED  0x0020

#define P_CCS       0x0001  /* c'e' qualcosa attaccato */
#define P_CSC       0x0002  /* la presenza e' CAMBIATA (si azzera scrivendo 1) */
#define P_PE        0x0004  /* porta abilitata */
#define P_PEDC      0x0008
#define P_LS        0x0100  /* dispositivo a bassa velocita' */
#define P_RESET     0x0200

/* --- Descrittori del controller ------------------------------------------ */
#define TD_TERM     0x00000001u
#define TD_QH       0x00000002u
#define TD_VF       0x00000004u     /* «prima in profondita'» */

#define TD_ACTIVE   0x00800000u
#define TD_STALLED  0x00400000u
#define TD_NAK      0x00080000u
#define TD_CERR1    0x08000000u     /* un solo tentativo */
#define TD_LS       0x04000000u

#define PID_SETUP   0x2D
#define PID_IN      0x69
#define PID_OUT     0xE1

/* Le richieste USB, i descrittori e le classi stanno in usb_comune.h: sono
 * dello standard, non di questo controller. */

#define PORTE_N         32      /* lo spazio I/O di un UHCI */

/* -----------------------------------------------------------------------------
 * Stato
 * --------------------------------------------------------------------------- */
static unsigned int g_base = 0;
static unsigned int g_verboso = 0;

static unsigned int g_dma_virt = 0, g_dma_fis = 0;
#define DMA_BYTE    (8u * 4096u)

/* Disposizione dentro la zona DMA. La lista dei frame per prima perche' vuole
 * i 4096 di allineamento, che qui sono gratis. */
#define OFF_FRAME   0x0000      /* 1024 puntatori = 4096 byte */
#define OFF_QH      0x1000      /* una QH, 16 byte allineati */
#define OFF_TD      0x1100      /* fino a 8 TD da 32 byte */
#define OFF_BUF     0x2000      /* buffer dei trasferimenti di controllo */
/* La coda PERIODICA: una QH e un TD che restano nella lista dei frame e che
 * il controller ripercorre da solo, per sempre. Vedi int_arma(). */
#define OFF_QHI     0x1080
#define OFF_TDI     0x1200
#define OFF_BUFI    0x3000

#define VIRT(off)   ((volatile unsigned int *)(g_dma_virt + (off)))
#define FIS(off)    (g_dma_fis + (off))

/* Il dispositivo HID trovato */
static unsigned int g_dev_addr   = 0;
static unsigned int g_dev_ls     = 0;
static unsigned int g_dev_ep     = 0;   /* endpoint di interruzione IN */
static unsigned int g_dev_maxp   = 8;
static unsigned int g_dev_proto  = 0;   /* USB_PROTO_MOUSE o _TASTIERA */
static UsbDispositivo g_dev;            /* cio' che la meta' comune ha letto */
static unsigned int g_ep_toggle  = 0;

/* Stato mouse consegnato ai client (stesso protocollo del PS/2) */
static int          g_dx = 0, g_dy = 0;
static unsigned int g_bottoni = 0, g_novita = 0, g_attesa_pid = 0;

/* -----------------------------------------------------------------------------
 * Accesso ai registri
 * --------------------------------------------------------------------------- */
static void w16(unsigned int off, unsigned int v) { ioport_out16(g_base + off, v); }
static unsigned int r16(unsigned int off)
{
    /* ioport_in16 rende il valore: 0..65535 sta tutto nei positivi, quindi
     * non ha l'ambiguita' di ioport_in32 (vedi libc.h). */
    int v = ioport_in16(g_base + off);
    return (v < 0) ? 0xFFFFu : (unsigned int)v;
}
static void w32(unsigned int off, unsigned int v) { ioport_out32(g_base + off, v); }

/* -----------------------------------------------------------------------------
 * Un trasferimento, costruito a mano
 *
 * ! SI ASPETTA CHE IL BIT «ATTIVO» SI SPENGA, non che arrivi un interrupt. Un
 * trasferimento di controllo dura microsecondi e serve solo durante
 * l'enumerazione: aspettarlo attivamente costa meno di quanto costerebbe
 * gestire l'interrupt, e soprattutto non ha uno stato da mantenere fra un
 * frame e l'altro.
 * --------------------------------------------------------------------------- */
static void td_scrivi(unsigned int i, unsigned int prossimo_off, unsigned int stato,
                      unsigned int token, unsigned int buf_fis)
{
    volatile unsigned int *td = VIRT(OFF_TD + i * 32);

    td[0] = prossimo_off ? (FIS(prossimo_off) | TD_VF) : TD_TERM;
    td[1] = stato;
    td[2] = token;
    td[3] = buf_fis;
}

static unsigned int token(unsigned int pid, unsigned int addr, unsigned int ep,
                          unsigned int toggle, unsigned int len)
{
    /* ! LA LUNGHEZZA E' «UNO IN MENO», e zero byte si scrive 0x7FF. E' la
     * codifica dell'hardware, non una stranezza da nascondere: scriverci la
     * lunghezza vera darebbe un byte in piu' a ogni trasferimento — e per lo
     * stato di un controllo, che e' lungo zero, darebbe un byte invece di
     * niente. */
    unsigned int ml = (len == 0) ? 0x7FF : ((len - 1) & 0x7FF);

    return (pid & 0xFF) | ((addr & 0x7F) << 8) | ((ep & 0x0F) << 15) |
           ((toggle & 1) << 19) | (ml << 21);
}

/* Mette la catena di TD in coda e aspetta al massimo `ms`.
 *
 * Rende 0 se tutto e' finito bene, -1 se il dispositivo ha rifiutato, e
 * -2 se allo scadere il TD e' ancora ATTIVO.
 *
 * ! -2 NON E' UN ERRORE PER UN ENDPOINT DI INTERRUZIONE, ed e' la cosa che
 * ho sbagliato la prima volta. Per specifica un NAK **non** consuma il
 * contatore dei tentativi: il TD di una lettura a cui il mouse non ha niente
 * da rispondere resta attivo e viene ritentato ogni frame, cioe' si comporta
 * esattamente come deve. Trattarlo come un guasto, e per giunta dopo
 * un'attesa di due secondi, faceva perdere un movimento su due — il ciclo
 * restava fermo dentro l'attesa mentre il mouse si muoveva. */
static int esegui(unsigned int n_td, unsigned int ms)
{
    volatile unsigned int *qh = VIRT(OFF_QH);
    volatile unsigned int *fl = VIRT(OFF_FRAME);
    unsigned int i, giri;

    qh[0] = TD_TERM;                    /* nessuna QH dopo di noi */
    qh[1] = FIS(OFF_TD);                /* il primo TD */

    /* ! LA QH VA IN TUTTI I 1024 FRAME, non in quello «corrente». Il numero
     * di frame avanza da solo mille volte al secondo: metterla in uno solo
     * vorrebbe dire sperare di indovinare quale sara' fra un istante. */
    for (i = 0; i < 1024; i++) fl[i] = FIS(OFF_QH) | TD_QH;

    for (giri = 0; giri < ms; giri++) {
        volatile unsigned int *ultimo = VIRT(OFF_TD + (n_td - 1) * 32);

        if (!(ultimo[1] & TD_ACTIVE)) break;
        usleep(1000);
    }

    /* Si toglie la QH dalla lista PRIMA di guardare l'esito: se si lasciasse
     * li', il controller ripercorrerebbe TD gia' consumati a ogni frame. */
    for (i = 0; i < 1024; i++) fl[i] = TD_TERM;

    for (i = 0; i < n_td; i++) {
        volatile unsigned int *td = VIRT(OFF_TD + i * 32);
        if (td[1] & TD_ACTIVE)  return -2;      /* ancora in attesa: vedi sopra */
        if (td[1] & TD_STALLED) return -1;      /* il dispositivo ha detto no */
    }
    return 0;
}

/* Trasferimento di controllo. `in` = i dati vanno dal dispositivo a noi. */
static int controllo(unsigned int addr, unsigned int rt, unsigned int req,
                     unsigned int val, unsigned int idx,
                     void *dati, unsigned int len, int in)
{
    volatile unsigned char *setup = (volatile unsigned char *)(g_dma_virt + OFF_BUF);
    volatile unsigned char *buf   = setup + 8;
    unsigned int stato_base = TD_ACTIVE | TD_CERR1 | (g_dev_ls ? TD_LS : 0);
    unsigned int n = 0, toggle = 1, resta = len, off = 0;

    setup[0] = (unsigned char)rt;
    setup[1] = (unsigned char)req;
    setup[2] = (unsigned char)(val & 0xFF);
    setup[3] = (unsigned char)(val >> 8);
    setup[4] = (unsigned char)(idx & 0xFF);
    setup[5] = (unsigned char)(idx >> 8);
    setup[6] = (unsigned char)(len & 0xFF);
    setup[7] = (unsigned char)(len >> 8);

    if (!in && len > 0) memcpy((void *)buf, dati, len);

    /* SETUP, sempre con toggle 0 */
    td_scrivi(n, OFF_TD + (n + 1) * 32, stato_base,
              token(PID_SETUP, addr, 0, 0, 8), FIS(OFF_BUF));
    n++;

    while (resta > 0) {
        unsigned int pezzo = (resta > g_dev_maxp) ? g_dev_maxp : resta;

        td_scrivi(n, OFF_TD + (n + 1) * 32, stato_base,
                  token(in ? PID_IN : PID_OUT, addr, 0, toggle, pezzo),
                  FIS(OFF_BUF) + 8 + off);
        n++;
        toggle ^= 1;
        off    += pezzo;
        resta  -= pezzo;
        if (n >= 7) break;              /* la scorta di TD e' finita */
    }

    /* Lo stato va NELLA DIREZIONE OPPOSTA ai dati, e con toggle 1. */
    td_scrivi(n, 0, stato_base,
              token(in ? PID_OUT : PID_IN, addr, 0, 1, 0), 0);
    n++;

    if (esegui(n, 1000) != 0) return -1;   /* un controllo deve finire */

    if (in && len > 0) memcpy(dati, (const void *)buf, len);
    return 0;
}

/* -----------------------------------------------------------------------------
 * Accensione del controller
 * --------------------------------------------------------------------------- */
static int hc_init(void)
{
    unsigned int i, giri;
    volatile unsigned int *fl = VIRT(OFF_FRAME);

    w16(R_CMD, CMD_HCRESET);
    for (giri = 0; giri < 100; giri++) {
        if (!(r16(R_CMD) & CMD_HCRESET)) break;
        usleep(1000);
    }
    if (r16(R_CMD) & CMD_HCRESET) return -1;

    w16(R_INTR, 0x0000);                /* niente interrupt: si aspetta a mano */
    w16(R_FRNUM, 0x0000);

    for (i = 0; i < 1024; i++) fl[i] = TD_TERM;
    w32(R_FLBASE, FIS(OFF_FRAME));

    ioport_out(g_base + R_SOF, 0x40);   /* il valore predefinito */
    w16(R_STS, 0xFFFF);                 /* pulisci lo stato */
    w16(R_CMD, CMD_RUN | CMD_CF | CMD_MAXP);

    return (r16(R_STS) & STS_HALTED) ? -1 : 0;
}

/* Resetta una porta e dice se ci si e' attaccato qualcosa. */
static int porta_reset(unsigned int p)
{
    unsigned int off = R_PORT1 + p * 2;
    unsigned int s, giri;

    s = r16(off);
    if (!(s & P_CCS)) return 0;

    w16(off, P_CSC);                    /* il cambiamento e' preso in carico */

    w16(off, P_RESET);
    usleep(60000);                      /* USB vuole almeno 10 ms; 60 e' comodo */
    w16(off, 0);
    usleep(10000);

    /* ! ABILITARE LA PORTA PUO' NON RIUSCIRE AL PRIMO COLPO, e insistere e'
     * previsto dalla specifica: il dispositivo si sta ancora svegliando. Un
     * solo tentativo dava «nessun dispositivo» su hardware perfettamente
     * funzionante. */
    for (giri = 0; giri < 10; giri++) {
        w16(off, P_PE);
        usleep(10000);
        s = r16(off);
        if (s & P_PE) break;
        w16(off, s & (P_CSC | P_PEDC));
    }
    if (!(s & P_PE)) return 0;

    g_dev_ls = (s & P_LS) ? 1 : 0;
    return 1;
}

/* -----------------------------------------------------------------------------
 * Enumerazione
 * --------------------------------------------------------------------------- */
static int enumera(unsigned int p, unsigned int indirizzo);

/* =============================================================================
 * ENUMERAZIONE E HUB — il grosso sta in usb_comune.c
 *
 * ! QUI RESTA SOLO CIO' CHE E' DI QUESTO CONTROLLER: assegnare l'indirizzo
 * (su xHCI lo fa il controller, non il driver) e tenere aggiornato
 * g_dev_maxp, che serve a controllo() per spezzare i trasferimenti. Tutto il
 * resto — descrittori, catena della configurazione, HID «boot», richieste di
 * classe agli hub — non sa che esista un UHCI, e infatti sta altrove.
 *
 * ! UN SOLO LIVELLO DI HUB. Gli hub si incatenano fino a cinque, ma ogni
 * livello in piu' e' ricorsione con lo stato del dispositivo in variabili
 * globali. Quando servira' si ripartira' da uno stato per dispositivo.
 * ========================================================================== */
static int hub_esplora(unsigned int addr_hub, unsigned int *prossimo)
{
    unsigned int porte = 0, attesa = 0, i;

    if (!usb_hub_descrittore(controllo, addr_hub, &porte, &attesa)) return 0;

    if (g_verboso) printf("uhci: hub con %u porte\n", porte);

    usb_hub_accendi(controllo, addr_hub, porte, attesa);

    for (i = 1; i <= porte; i++) {
        unsigned int vel = USB_VEL_FULL;

        if (!usb_hub_porta_pronta(controllo, addr_hub, i, &vel)) continue;

        /* L'UHCI conosce una distinzione sola: piano o no. */
        g_dev_ls = (vel == USB_VEL_LOW) ? 1 : 0;
        if (enumera(100 + i, *prossimo)) {          /* 100+ = «dietro un hub» */
            (*prossimo)++;
            return 1;
        }
    }

    return 0;
}

static int enumera(unsigned int p, unsigned int indirizzo)
{
    g_dev_maxp = 8;                     /* obbligatorio finche' non si sa */

    if (!usb_desc_corto(controllo, 0, &g_dev)) return 0;
    g_dev_maxp = g_dev.maxp0;

    /* ! L'INDIRIZZO LO ASSEGNA IL DRIVER, ED E' UNA COSA DA UHCI. Su xHCI
     * lo mette il controller con un comando: e' il motivo per cui questo
     * pezzo non e' finito in usb_comune.c insieme al resto. */
    if (controllo(0, 0x00, USB_REQ_SET_ADDR, indirizzo, 0, 0, 0, 0) != 0) return 0;
    usleep(5000);                       /* la specifica concede 2 ms */
    g_dev_addr = indirizzo;

    if (!usb_desc_lungo(controllo, g_dev_addr, &g_dev)) return 0;
    g_dev_maxp = g_dev.maxp0;

    if (g_verboso)
        printf("uhci: porta %u  USB %x.%02x  venditore %04x prodotto %04x  "
               "maxp %u%s\n", p, (g_dev.versione >> 8) & 0xFF,
               g_dev.versione & 0xFF, g_dev.venditore, g_dev.prodotto,
               g_dev_maxp, g_dev_ls ? "  (bassa velocita')" : "");

    if (g_dev.classe == USB_CLASSE_HUB) {
        unsigned int prossimo = indirizzo + 1;

        if (controllo(g_dev_addr, 0x00, USB_REQ_SET_CONF, 1, 0, 0, 0, 0) != 0)
            return 0;
        return hub_esplora(g_dev_addr, &prossimo);
    }

    if (!usb_configura_hid(controllo, g_dev_addr, &g_dev, g_verboso)) {
        if (g_verboso) printf("uhci: porta %u: non e' un HID 'boot'\n", p);
        return 0;
    }

    g_dev_proto = g_dev.proto;
    g_dev_ep    = g_dev.ep;
    return 1;
}

/* =============================================================================
 * L'ENDPOINT DI INTERRUZIONE, programmato una volta e lasciato li'
 *
 * ! LA PRIMA VERSIONE INTERROGAVA A COLPO SINGOLO, E PERDEVA RAPPORTI. Ogni
 * lettura costruiva un TD, lo metteva in lista, aspettava qualche
 * millisecondo e lo toglieva: fra un tentativo e l'altro c'era una finestra in
 * cui la coda era VUOTA, e un rapporto che arrivava li' dentro non aveva dove
 * andare. Con movimenti a raffica se ne perdeva uno ogni tanto — e la cosa
 * peggiore era che con movimenti lenti andava benissimo, cioe' la prova
 * facile passava.
 *
 * Adesso il TD sta nella lista dei frame e NON se ne va mai. Il controller lo
 * ripercorre a ogni frame — mille volte al secondo — e ritenta da solo finche'
 * il dispositivo non risponde: e' esattamente cio' che un endpoint di
 * interruzione deve essere. Il driver non manda piu' niente sul bus: guarda
 * un bit in memoria.
 *
 * ! IL CONTATORE DEI TENTATIVI E' ZERO, cioe' INFINITO, ed e' voluto. Un NAK
 * non lo consuma comunque (specifica UHCI), ma un errore di trasmissione si':
 * con un tetto basso, un solo disturbo sul cavo ritirerebbe il TD e il
 * dispositivo smetterebbe di essere ascoltato senza che nessuno lo dica.
 * ========================================================================== */
static void int_arma(void)
{
    volatile unsigned int *td = VIRT(OFF_TDI);
    volatile unsigned int *qh = VIRT(OFF_QHI);

    td[0] = TD_TERM;
    td[1] = TD_ACTIVE | (g_dev_ls ? TD_LS : 0);   /* tentativi infiniti */
    td[2] = token(PID_IN, g_dev_addr, g_dev_ep, g_ep_toggle, g_dev_maxp);
    td[3] = FIS(OFF_BUFI);

    /* ! L'ELEMENTO DELLA QH VA RIMESSO A OGNI GIRO. Quando il TD finisce, il
     * controller fa avanzare questo puntatore al TD successivo — che non c'e'
     * — e la coda resta vuota per sempre. Non rimetterlo dava un mouse che
     * funzionava per UN rapporto e poi taceva. */
    qh[0] = TD_TERM;
    qh[1] = FIS(OFF_TDI);
}

static void int_installa(void)
{
    volatile unsigned int *fl = VIRT(OFF_FRAME);
    unsigned int i;

    int_arma();
    for (i = 0; i < 1024; i++) fl[i] = FIS(OFF_QHI) | TD_QH;
}

/* Rende i byte del rapporto, 0 se non e' ancora arrivato niente, -1 se il
 * dispositivo ha rifiutato. Non tocca il bus: guarda il TD. */
static int leggi_rapporto(unsigned char *out, unsigned int max)
{
    volatile unsigned char *buf = (volatile unsigned char *)(g_dma_virt + OFF_BUFI);
    volatile unsigned int  *td  = VIRT(OFF_TDI);
    unsigned int act;

    if (td[1] & TD_ACTIVE) return 0;            /* il dispositivo tace */

    if (td[1] & TD_STALLED) { int_arma(); return -1; }

    act = (td[1] & 0x7FF);
    if (act == 0x7FF) act = 0;                  /* zero byte ricevuti */
    else              act++;

    if (act > max) act = max;
    if (act > 0) memcpy(out, (const void *)buf, act);

    g_ep_toggle ^= 1;
    int_arma();                                 /* subito pronto per il prossimo */
    return (int)act;
}


/* ! LA TRADUZIONE DEI TASTI STA IN usb_comune.c. Un rapporto HID «boot» ha
 * la stessa forma su qualunque controller, e la tabella da uso HID a scancode
 * del set 1 e' lunga: scritta due volte, si sbaglia due volte. */
static int g_kbd_pid = -1;

/* -----------------------------------------------------------------------------
 * PCI
 * --------------------------------------------------------------------------- */
static int cerca_uhci(unsigned int *irq)
{
    PciRichiesta  r;
    PciDispositivo d;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int pid = -1, ord;
    unsigned int attesa;

    /* ! SI ASPETTA IL SERVIZIO PCI, non lo si pretende gia' pronto.
     *
     * Questo driver deve poter partire come MODULO all'avvio: su una macchina
     * senza 8042 non c'e' nessuno che possa battere il comando per avviarlo —
     * la tastiera e' quella che lui stesso deve far funzionare. Ma i moduli
     * partono tutti insieme, e pci.drv puo' non aver ancora registrato il
     * proprio nome quando noi lo cerchiamo. Fallire li' vorrebbe dire una
     * macchina senza input per una corsa persa di qualche millisecondo. */
    for (attesa = 0; attesa < 50; attesa++) {
        pid = ipc_lookup(PCI_SERVIZIO);
        if (pid >= 0) break;
        usleep(100000);
    }
    if (pid < 0) {
        printf("uhci: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
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

        if (d.interfaccia != 0x00) continue;     /* 0x00 = UHCI */

        {
            int b;
            for (b = 0; b < 6; b++)
                if (d.bar[b] && d.bar_io[b]) { g_base = d.bar[b]; break; }
        }
        if (g_base == 0) continue;

        *irq = d.irq_linea;
        printf("uhci: controller in %02x:%02x.%u, porte 0x%x, IRQ %u\n",
               d.bus, d.slot, d.funzione, g_base, d.irq_linea);
        return 1;
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * Il mouse, verso i client
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

int main(int argc, char **argv)
{
    DmaZona z;
    unsigned int irq = 0;
    int rc, i, sonda = 0;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'v') g_verboso = 1;
        if (argv[i][0] == '-' && argv[i][1] == 'i') sonda = 1;
    }

    if (!cerca_uhci(&irq)) {
        printf("uhci: nessun controller UHCI sul PCI.\n");
        return 1;
    }
    if (sonda) return 0;

    rc = ioport_bind(g_base, PORTE_N);
    if (rc < 0) {
        printf("uhci: ioport_bind(0x%x, %d) fallita (%d)\n", g_base, PORTE_N, rc);
        return 1;
    }

    z.byte = DMA_BYTE;
    rc = dma_alloc(&z);
    if (rc < 0) { printf("uhci: dma_alloc fallita (%d)\n", rc); return 1; }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;
    memset((void *)g_dma_virt, 0, DMA_BYTE);

    if (hc_init() != 0) {
        printf("uhci: il controller non parte (STS 0x%x)\n", r16(R_STS));
        return 1;
    }

    {
        unsigned int p, addr = 1, trovati = 0;

        for (p = 0; p < 2; p++) {
            if (!porta_reset(p)) continue;
            if (enumera(p, addr)) {
                printf("uhci: porta %u: %s USB 'boot', endpoint %u\n", p,
                       g_dev_proto == USB_PROTO_MOUSE ? "mouse" : "tastiera",
                       g_dev_ep);
                trovati++;
                addr++;
                break;      /* un dispositivo per volta, per ora */
            }
        }

        if (!trovati) {
            printf("uhci: nessun dispositivo HID 'boot' sulle porte.\n");
            return 1;
        }
    }

    /* Da qui in poi la lista dei frame appartiene all'endpoint di
     * interruzione: niente piu' trasferimenti di controllo. */
    int_installa();

    if (g_dev_proto == USB_PROTO_TASTIERA) {
        g_kbd_pid = ipc_lookup(KBD_SERVICE_NAME);
        if (g_kbd_pid < 0) {
            printf("uhci: il servizio '%s' non c'e': a chi consegno i tasti?\n",
                   KBD_SERVICE_NAME);
            return 1;
        }
        printf("uhci: tastiera USB -> scancode al servizio '%s' (PID %d)\n",
               KBD_SERVICE_NAME, g_kbd_pid);
    } else {
        if (ipc_register(MOUSE_SERVICE_NAME) < 0) {
            printf("uhci: ipc_register('%s') fallita - c'e' gia' un mouse?\n",
                   MOUSE_SERVICE_NAME);
            return 1;
        }
        printf("uhci: servizio '%s' attivo (mouse USB)\n", MOUSE_SERVICE_NAME);
    }

    /* ! SI INTERROGA A GIRO, non si aspetta un interrupt. L'UHCI consegna gli
     * interrupt di un endpoint periodico tenendo il suo TD nella lista dei
     * frame, cioe' con uno stato che vive fra un frame e l'altro. Interrogare
     * ogni 20 ms costa un trasferimento ogni cinquantesimo di secondo — meno
     * di quanto costi sbagliare quello stato — ed e' esattamente cio' che fa
     * un mouse, che manda un rapporto solo quando si muove. */
    for (;;) {
        IpcMessage    meta;
        unsigned char payload[64];
        unsigned char rap[8];
        int n;

        n = leggi_rapporto(rap, sizeof(rap));

        if (g_dev_proto == USB_PROTO_TASTIERA) {
            if (n >= 8) usb_tastiera_rapporto(rap, g_kbd_pid);
            /* La tastiera non ha client propri: si torna subito a guardare. */
            usleep(1000);
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
