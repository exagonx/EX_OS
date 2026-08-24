/* =============================================================================
 * drivers/e1000/e1000.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver per Intel 82540EM «e1000» — la scheda PREDEFINITA di QEMU.
 *
 *     /dev/e1000.drv          si aggancia alla prima scheda che trova
 *     /dev/e1000.drv -i       la sonda: dice se c'e', esce e basta
 *     /dev/e1000.drv -v       dice cosa vede, registro per registro
 *
 * Parla net_proto.h, come ne2k e pcnet: allo stack TCP/IP non importa
 * quale scheda ha sotto.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' QUESTA SCHEDA E' DIVERSA DALLE ALTRE DUE, E PERCHE' SI PUO'
 *    SCRIVERE LO STESSO
 *
 * NE2000 e PCnet rispondono nello SPAZIO I/O: `ioport_bind` piu' `inb`/`outb`,
 * e un driver in ring 3 le guida senza chiedere niente al kernel.
 *
 * L'e1000 no. I suoi registri stanno nello SPAZIO DI MEMORIA (BAR0), e per
 * leggerli bisogna mappare della memoria fisica dentro il processo. E' quello
 * che fa `mmio_map()` — SYS_MMIO_MAP, scritta il 13 agosto 2026 proprio per
 * questo driver, punto 1 del gradino 0 di DIREZIONE.md. Prima non c'era, e
 * con la sola BAR0 questo file non si sarebbe potuto scrivere.
 *
 * ! LA STRADA CHE SEMBRAVA EVITARE IL KERNEL NON FUNZIONA, e va detto qui
 * perche' e' la prima che verrebbe in mente a chiunque riapra questo file.
 * L'82540EM espone anche una FINESTRA A PORTE (BAR1), larga 8 byte in tutto:
 *
 *     BAR1 + 0x00   IOADDR   ci si scrive l'offset del registro voluto
 *     BAR1 + 0x04   IODATA   e poi ci si legge o ci si scrive il valore
 *
 * cioe' l'intero spazio dei registri raggiunto due accessi per volta, senza
 * mappare niente. E' documentata da Intel, questo driver e' nato su quella, e
 * su QEMU e' MORTA: `e1000_io_read` rende 0 ed `e1000_io_write` scarta tutto
 * (hw/net/e1000.c, letto). Non e' un dettaglio dell'emulatore da aggirare —
 * e' che quella strada li' non esiste.
 *
 * ! E il caso cattivo non era 0xFFFFFFFF ma tutti ZERO, che somiglia a un
 * valore: il reset «riusciva», il MAC veniva 00:00:00:00:00:00 e il servizio
 * si registrava lo stesso. Vedi la verifica di STATUS in main().
 *
 * reg_leggi/reg_scrivi restano l'unico punto che sa COME si arriva a un
 * registro: tutto il resto parla di offset. E' scritto cosi' apposta.
 *
 * -----------------------------------------------------------------------------
 * ! GLI ANELLI DI DESCRITTORI VOGLIONO MEMORIA FISICA CONTIGUA
 *
 * La scheda e' un bus master: legge e scrive la RAM da sola, agli indirizzi
 * FISICI che le si danno, senza passare dalla MMU. Un blocco di malloc ha un
 * indirizzo virtuale e pagine che possono stare ovunque: darglielo vuol dire
 * farle scrivere in un punto a caso della memoria fisica.
 *
 * Percio' `dma_alloc`, come per il PCnet: restituisce i due indirizzi con
 * nomi diversi — `virt` per noi, `fisico` per la scheda — proprio perche'
 * confonderli e' l'errore che questo codice non deve poter fare.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "net_proto.h"

/* -----------------------------------------------------------------------------
 * La scheda
 * --------------------------------------------------------------------------- */
#define E1000_VENDITORE   0x8086
#define E1000_82540EM     0x100E      /* il predefinito di QEMU */
#define E1000_82545EM     0x100F
#define E1000_82574L      0x10D3

/* La finestra a porte: 8 byte, due registri da 32 bit. */
#define IOADDR            0x00
#define IODATA            0x04
#define E1000_PORTE       8

/* La finestra dei registri: l'82540EM ne decodifica 128 KB, e i registri che
 * usiamo stanno tutti sotto 0x6000. Si mappa cio' che serve piu' un margine,
 * non tutto: sono pagine tolte allo spazio del processo. */
#define E1000_MMIO_BYTE   0x8000u

/* -----------------------------------------------------------------------------
 * I registri, per offset. Sono gli stessi con MMIO e con la finestra: la
 * finestra cambia COME ci si arriva, non COSA sono.
 * --------------------------------------------------------------------------- */
#define REG_CTRL          0x0000      /* controllo del dispositivo */
#define REG_STATUS        0x0008      /* stato: link, duplex, velocita' */
#define REG_EECD          0x0010      /* controllo della EEPROM */
#define REG_EERD          0x0014      /* lettura della EEPROM */
#define REG_ICR           0x00C0      /* cause dell'interruzione (si azzera leggendo) */
#define REG_IMS           0x00D0      /* maschera: accendi queste interruzioni */
#define REG_IMC           0x00D8      /* maschera: spegni queste */
#define REG_RCTL          0x0100      /* controllo della ricezione */
#define REG_TCTL          0x0400      /* controllo della trasmissione */
#define REG_TIPG          0x0410      /* spazio fra un pacchetto e l'altro */

#define REG_RDBAL         0x2800      /* base dell'anello RX, 32 bit bassi */
#define REG_RDBAH         0x2804      /* alti — sempre 0 per noi: siamo a 32 bit */
#define REG_RDLEN         0x2808      /* lunghezza dell'anello, in BYTE */
#define REG_RDH           0x2810      /* testa: la muove la SCHEDA */
#define REG_RDT           0x2818      /* coda: la muoviamo NOI */

#define REG_TDBAL         0x3800
#define REG_TDBAH         0x3804
#define REG_TDLEN         0x3808
#define REG_TDH           0x3810
#define REG_TDT           0x3818

#define REG_RAL0          0x5400      /* indirizzo MAC ricevuto, 32 bit bassi */
#define REG_RAH0          0x5404      /* alti + bit di validita' */

/* Bit di CTRL */
#define CTRL_FD           (1u << 0)   /* full duplex */
#define CTRL_ASDE         (1u << 5)   /* auto-negoziazione della velocita' */
#define CTRL_SLU          (1u << 6)   /* Set Link Up */
#define CTRL_RST          (1u << 26)  /* reset del dispositivo */

/* Bit di STATUS */
#define STATUS_FD         (1u << 0)
#define STATUS_LU         (1u << 1)   /* link up */

/* Bit di RCTL */
#define RCTL_EN           (1u << 1)   /* accendi la ricezione */
#define RCTL_SBP          (1u << 2)   /* tieni anche i pacchetti rotti */
#define RCTL_UPE          (1u << 3)   /* unicast promiscuo */
#define RCTL_MPE          (1u << 4)   /* multicast promiscuo */
#define RCTL_BAM          (1u << 15)  /* accetta il broadcast */
#define RCTL_BSIZE_2048   (0u << 16)  /* buffer da 2048 byte */
#define RCTL_SECRC        (1u << 26)  /* togli il FCS dai frame ricevuti */

/* Bit di TCTL */
#define TCTL_EN           (1u << 1)
#define TCTL_PSP          (1u << 3)   /* riempi i frame corti */
#define TCTL_CT_SHIFT     4           /* soglia di collisione */
#define TCTL_COLD_SHIFT   12          /* distanza di collisione */

/* Bit dei descrittori di trasmissione (campo comando) */
#define TXD_CMD_EOP       (1u << 0)   /* fine del pacchetto */
#define TXD_CMD_IFCS      (1u << 1)   /* calcola tu il FCS */
#define TXD_CMD_RS        (1u << 3)   /* riferisci lo stato quando hai finito */
#define TXD_STA_DD        (1u << 0)   /* fatto: il descrittore e' di nuovo mio */

/* Bit dei descrittori di ricezione (campo stato) */
#define RXD_STA_DD        (1u << 0)   /* c'e' un frame */
#define RXD_STA_EOP       (1u << 1)   /* e finisce qui */

/* Interruzioni che ci interessano */
#define ICR_TXDW          (1u << 0)   /* trasmissione conclusa */
#define ICR_LSC           (1u << 2)   /* il link e' cambiato */
#define ICR_RXDMT0        (1u << 4)   /* l'anello RX si sta svuotando */
#define ICR_RXT0          (1u << 7)   /* frame ricevuto (timer) */

/* -----------------------------------------------------------------------------
 * Anelli e buffer
 *
 * ! IL NUMERO DI DESCRITTORI DEVE ESSERE MULTIPLO DI 8, e la lunghezza
 * dell'anello multipla di 128 byte: lo dice il costruttore, e la scheda con
 * un valore storto non protesta — smette di consegnare.
 * Un descrittore e' 16 byte, quindi 8 descrittori = 128 byte esatti.
 * --------------------------------------------------------------------------- */
#define RX_N              32
#define TX_N              8
#define BUF_LEN           2048        /* deve combaciare con RCTL_BSIZE_2048 */

#define DESC_BYTE         16
#define RX_ANELLO_BYTE    (RX_N * DESC_BYTE)
#define TX_ANELLO_BYTE    (TX_N * DESC_BYTE)

/* Disposizione della zona DMA, in quest'ordine:
 *     [anello RX][anello TX][buffer RX][buffer TX]
 *
 * ! GLI ANELLI VANNO ALLINEATI A 16 BYTE. dma_alloc rende memoria allineata
 * alla pagina, quindi l'anello RX parte allineato; l'anello TX lo segue a
 * un multiplo di 16 perche' RX_ANELLO_BYTE lo e'. Se si cambiano i conteggi,
 * si ricontrolla questo. */
#define OFF_RX_ANELLO     0
#define OFF_TX_ANELLO     (OFF_RX_ANELLO + RX_ANELLO_BYTE)
#define OFF_RX_BUF        (OFF_TX_ANELLO + TX_ANELLO_BYTE)
#define OFF_TX_BUF        (OFF_RX_BUF + RX_N * BUF_LEN)
#define DMA_BYTE          (OFF_TX_BUF + TX_N * BUF_LEN)

#define PERIODO_MS        50
#define CODA_N            8

/* -----------------------------------------------------------------------------
 * Stato
 * --------------------------------------------------------------------------- */
static unsigned int  g_base = 0;        /* BAR I/O: NON si usa, si riferisce */
static unsigned int  g_mmio_fis = 0;    /* BAR di memoria: i registri */
static unsigned int  g_irq  = 0;
static unsigned int  g_bus = 0xFFFFFFFF, g_slot = 0, g_funzione = 0;
static unsigned short g_dispositivo = 0;
static char          g_modello[48] = "Intel e1000";
static unsigned char g_mac[6];

static unsigned int  g_dma_virt = 0;
static unsigned int  g_dma_fis  = 0;

static unsigned int  g_rx_coda = 0;     /* l'ultimo descrittore che abbiamo dato */
static unsigned int  g_tx_prossimo = 0;

static NetContatori  g_cont;
static int           g_verboso = 0;

static unsigned char g_coda[CODA_N][NET_FRAME_MAX];
static unsigned int  g_coda_len[CODA_N];
static int           g_coda_testa = 0, g_coda_conta = 0;
static unsigned int  g_lettore_pid = 0;

/* =============================================================================
 * L'ACCESSO AI REGISTRI — l'unico punto che sa dove sono
 *
 * Un accesso solo per registro, diretto sulla finestra che mmio_map() ha
 * messo nel nostro spazio: nessuna sequenza da proteggere, al contrario della
 * finestra a porte IOADDR/IODATA su cui questo driver era nato (due accessi
 * per registro, e in mezzo ci si poteva infilare qualcuno).
 *
 * ! LE PAGINE SONO NON CACHEABILI, e lo garantisce il kernel dentro
 * mmio_map: non e' un dettaglio di quella syscall, e' la condizione perche'
 * queste due funzioni facciano quello che dicono. Con la cache accesa le
 * scritture non arriverebbero alla scheda e le letture renderebbero valori
 * vecchi — il driver sembrerebbe funzionare.
 * ========================================================================== */
static volatile unsigned char *g_reg = 0;   /* i registri, mappati */

static unsigned int reg_leggi(unsigned int off)
{
    /* ! `volatile` NON E' DECORAZIONE. Un registro cambia senza che nessuno
     * scriva, e leggerlo ha effetti (ICR si azzera leggendolo). Senza
     * volatile il compilatore terrebbe il valore in un registro della CPU e
     * un ciclo che aspetta un bit non finirebbe mai. */
    return *(volatile unsigned int *)(g_reg + off);
}

static void reg_scrivi(unsigned int off, unsigned int val)
{
    *(volatile unsigned int *)(g_reg + off) = val;
}

/* =============================================================================
 * Descrittori
 *
 * Ricezione (16 byte):   0 indirizzo (64 bit) | 8 lunghezza | 10 checksum
 *                       12 stato | 13 errori  | 14 vlan
 * Trasmissione (16 byte): 0 indirizzo (64 bit) | 8 lunghezza | 10 cso
 *                       11 comando | 12 stato | 13 css | 14 vlan
 * ========================================================================== */
static volatile unsigned char *rx_desc(unsigned int i)
{
    return (volatile unsigned char *)(g_dma_virt + OFF_RX_ANELLO + i * DESC_BYTE);
}

static volatile unsigned char *tx_desc(unsigned int i)
{
    return (volatile unsigned char *)(g_dma_virt + OFF_TX_ANELLO + i * DESC_BYTE);
}

static unsigned char *rx_buf(unsigned int i)
{
    return (unsigned char *)(g_dma_virt + OFF_RX_BUF + i * BUF_LEN);
}

static unsigned char *tx_buf(unsigned int i)
{
    return (unsigned char *)(g_dma_virt + OFF_TX_BUF + i * BUF_LEN);
}

static unsigned int rx_buf_fis(unsigned int i)
{
    return g_dma_fis + OFF_RX_BUF + i * BUF_LEN;
}

static unsigned int tx_buf_fis(unsigned int i)
{
    return g_dma_fis + OFF_TX_BUF + i * BUF_LEN;
}

/* Scrittura di un campo a 32 bit dentro un descrittore, byte per byte.
 * ! NON si usa un puntatore a unsigned int: i descrittori sono allineati a
 * 16 e i campi a 8/10/12 non lo sarebbero tutti, e un accesso disallineato
 * su alcune CPU e' lento e su altre trappa. Byte per byte e' sempre giusto. */
static void scrivi32(volatile unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static unsigned int leggi16(volatile unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

/* =============================================================================
 * La EEPROM — da li' viene l'indirizzo MAC
 *
 * ! SI PROVA, NON SI DA' PER SCONTATA. Alcune schede (e alcune versioni di
 * QEMU) non hanno una EEPROM leggibile con EERD: in quel caso il MAC sta
 * gia' nei registri RAL0/RAH0, messo la' dal firmware. Chiedere alla EEPROM
 * e aspettare un bit che non arrivera' mai vuol dire un driver che si pianta
 * all'avvio — quindi si prova, con un limite, e si ripiega.
 * ========================================================================== */
static int eeprom_leggi(unsigned int parola, unsigned int *fuori)
{
    unsigned int i, v;

    /* EERD: bit 0 = START, bit 4 = DONE, indirizzo da bit 8 */
    reg_scrivi(REG_EERD, (parola << 8) | 1u);

    for (i = 0; i < 10000; i++) {
        v = reg_leggi(REG_EERD);
        if (v & (1u << 4)) {            /* DONE */
            *fuori = (v >> 16) & 0xFFFF;
            return 0;
        }
    }
    return -1;
}

static void leggi_mac(void)
{
    unsigned int p, i;
    int          da_eeprom = 1;

    for (i = 0; i < 3; i++) {
        if (eeprom_leggi(i, &p) != 0) { da_eeprom = 0; break; }
        g_mac[i * 2]     = (unsigned char)(p & 0xFF);
        g_mac[i * 2 + 1] = (unsigned char)((p >> 8) & 0xFF);
    }

    if (!da_eeprom) {
        /* Ripiego: il MAC che il firmware ha gia' messo nei registri. */
        unsigned int ral = reg_leggi(REG_RAL0);
        unsigned int rah = reg_leggi(REG_RAH0);

        g_mac[0] = (unsigned char)(ral & 0xFF);
        g_mac[1] = (unsigned char)((ral >> 8) & 0xFF);
        g_mac[2] = (unsigned char)((ral >> 16) & 0xFF);
        g_mac[3] = (unsigned char)((ral >> 24) & 0xFF);
        g_mac[4] = (unsigned char)(rah & 0xFF);
        g_mac[5] = (unsigned char)((rah >> 8) & 0xFF);
        if (g_verboso)
            printf("e1000: EEPROM muta, MAC preso da RAL0/RAH0\n");
    }
}

/* Rimette il MAC nei registri di filtro. Serve anche quando l'abbiamo letto
 * da li': dopo un reset RAH0 puo' aver perso il bit di validita'. */
static void imposta_filtro_mac(void)
{
    unsigned int ral, rah;

    ral = (unsigned int)g_mac[0] | ((unsigned int)g_mac[1] << 8) |
          ((unsigned int)g_mac[2] << 16) | ((unsigned int)g_mac[3] << 24);
    rah = (unsigned int)g_mac[4] | ((unsigned int)g_mac[5] << 8) |
          (1u << 31);                   /* AV: indirizzo valido */

    reg_scrivi(REG_RAL0, ral);
    reg_scrivi(REG_RAH0, rah);
}

/* =============================================================================
 * Inizializzazione
 * ========================================================================== */
static void attesa_breve(void)
{
    /* Il reset vuole qualche microsecondo. usleep e' il modo onesto: un
     * ciclo vuoto lo ottimizzerebbe via il compilatore, o durerebbe quanto
     * pare a lui. */
    usleep(20000);
}

static int reset_scheda(void)
{
    unsigned int i, ctrl;

    /* Spegni tutte le interruzioni PRIMA di toccare qualsiasi cosa: una
     * scheda che interrompe mentre la si azzera manda notifiche per un
     * driver che non e' ancora pronto a riceverle. */
    reg_scrivi(REG_IMC, 0xFFFFFFFFu);
    reg_leggi(REG_ICR);                 /* leggere azzera le cause */

    reg_scrivi(REG_CTRL, reg_leggi(REG_CTRL) | CTRL_RST);
    attesa_breve();

    /* ! SI ASPETTA CHE IL BIT SI SPENGA DA SE'. Proseguire subito dopo aver
     * scritto RST vuol dire scrivere nei registri di una scheda che si sta
     * ancora azzerando: le scritture si perdono, e il guasto si vede molto
     * dopo come «non riceve». */
    for (i = 0; i < 1000; i++) {
        ctrl = reg_leggi(REG_CTRL);
        if (!(ctrl & CTRL_RST)) break;
        usleep(1000);
    }
    if (i == 1000) return -1;

    reg_scrivi(REG_IMC, 0xFFFFFFFFu);
    reg_leggi(REG_ICR);
    return 0;
}

static void prepara_rx(void)
{
    unsigned int i;

    for (i = 0; i < RX_N; i++) {
        volatile unsigned char *d = rx_desc(i);
        int k;

        for (k = 0; k < DESC_BYTE; k++) d[k] = 0;
        scrivi32(d + 0, rx_buf_fis(i));     /* indirizzo, 32 bit bassi */
        scrivi32(d + 4, 0);                 /* alti: siamo a 32 bit */
    }

    reg_scrivi(REG_RDBAL, g_dma_fis + OFF_RX_ANELLO);
    reg_scrivi(REG_RDBAH, 0);
    reg_scrivi(REG_RDLEN, RX_ANELLO_BYTE);
    reg_scrivi(REG_RDH, 0);

    /* ! LA CODA PUNTA ALL'ULTIMO DESCRITTORE LIBERO, NON OLTRE. Mettere
     * RDT = RX_N farebbe credere alla scheda che l'anello e' pieno di
     * descrittori suoi, testa e coda coinciderebbero, e non consegnerebbe
     * niente — senza nessun errore. */
    g_rx_coda = RX_N - 1;
    reg_scrivi(REG_RDT, g_rx_coda);

    reg_scrivi(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC | RCTL_BSIZE_2048);
}

static void prepara_tx(void)
{
    unsigned int i;

    for (i = 0; i < TX_N; i++) {
        volatile unsigned char *d = tx_desc(i);
        int k;

        for (k = 0; k < DESC_BYTE; k++) d[k] = 0;
        scrivi32(d + 0, tx_buf_fis(i));
        scrivi32(d + 4, 0);
        d[12] = TXD_STA_DD;                 /* libero, per costruzione */
    }

    reg_scrivi(REG_TDBAL, g_dma_fis + OFF_TX_ANELLO);
    reg_scrivi(REG_TDBAH, 0);
    reg_scrivi(REG_TDLEN, TX_ANELLO_BYTE);
    reg_scrivi(REG_TDH, 0);
    reg_scrivi(REG_TDT, 0);
    g_tx_prossimo = 0;

    reg_scrivi(REG_TCTL, TCTL_EN | TCTL_PSP |
                         (0x0F << TCTL_CT_SHIFT) | (0x40 << TCTL_COLD_SHIFT));
    reg_scrivi(REG_TIPG, 10 | (8 << 10) | (6 << 20));
}

static int inizializza_scheda(void)
{
    if (reset_scheda() != 0) {
        printf("e1000: la scheda non esce dal reset\n");
        return -1;
    }

    leggi_mac();
    imposta_filtro_mac();

    /* Link su, duplex e velocita' negoziati dalla scheda. */
    reg_scrivi(REG_CTRL, reg_leggi(REG_CTRL) | CTRL_SLU | CTRL_ASDE);

    prepara_rx();
    prepara_tx();

    /* Solo le interruzioni che sappiamo servire. */
    reg_scrivi(REG_IMS, ICR_RXT0 | ICR_RXDMT0 | ICR_TXDW | ICR_LSC);
    return 0;
}

/* =============================================================================
 * Ricezione
 * ========================================================================== */
static void accoda(const unsigned char *f, unsigned int len)
{
    int slot;

    if (len > NET_FRAME_MAX) len = NET_FRAME_MAX;

    /* ! CODA PIENA: SI BUTTA IL PIU' VECCHIO, e si conta. Buttare il nuovo
     * sarebbe piu' facile e peggio: chi legge resterebbe con una coda di
     * roba stantia mentre il traffico vero passa. */
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
    for (;;) {
        unsigned int            i = (g_rx_coda + 1) % RX_N;
        volatile unsigned char *d = rx_desc(i);
        unsigned int            len;

        if (!(d[12] & RXD_STA_DD)) break;       /* non c'e' altro */

        len = leggi16(d + 8);

        /* ! SI GUARDA ANCHE EOP. Un frame spezzato su piu' descrittori
         * arriverebbe a pezzi, e consegnarne uno solo darebbe un pacchetto
         * troncato che sembra corrotto. Con buffer da 2048 e MTU 1500 non
         * succede, ma se un giorno cambiassero i buffer succederebbe in
         * silenzio. */
        if ((d[12] & RXD_STA_EOP) && len >= 14 && len <= NET_FRAME_MAX) {
            accoda(rx_buf(i), len);
            g_cont.ricevuti++;
        } else {
            g_cont.errori_rx++;
        }

        d[12] = 0;                               /* di nuovo della scheda */
        g_rx_coda = i;
        reg_scrivi(REG_RDT, g_rx_coda);
    }
}

/* =============================================================================
 * Trasmissione
 * ========================================================================== */
static int trasmetti(const unsigned char *f, unsigned int len)
{
    volatile unsigned char *d;
    unsigned int            i;

    if (len > NET_MTU + 14) return -1;

    d = tx_desc(g_tx_prossimo);

    /* ! SI ASPETTA CHE IL DESCRITTORE SIA TORNATO NOSTRO. Scriverci sopra
     * mentre la scheda lo sta ancora leggendo manda in rete meta' di un
     * pacchetto e meta' di un altro. */
    for (i = 0; i < 100000; i++) {
        if (d[12] & TXD_STA_DD) break;
    }
    if (i == 100000) {
        g_cont.errori_tx++;
        return -1;
    }

    memcpy(tx_buf(g_tx_prossimo), f, len);

    /* Sotto i 60 byte si riempie di zeri: e' un vincolo del mezzo, non del
     * chiamante. TCTL_PSP lo farebbe da se', ma dirlo qui vuol dire che la
     * lunghezza nel descrittore e' gia' quella vera. */
    if (len < NET_FRAME_MIN) {
        memset(tx_buf(g_tx_prossimo) + len, 0, NET_FRAME_MIN - len);
        len = NET_FRAME_MIN;
    }

    scrivi32(d + 0, tx_buf_fis(g_tx_prossimo));
    scrivi32(d + 4, 0);
    d[8]  = (unsigned char)(len & 0xFF);
    d[9]  = (unsigned char)((len >> 8) & 0xFF);
    d[10] = 0;
    d[11] = TXD_CMD_EOP | TXD_CMD_IFCS | TXD_CMD_RS;
    d[12] = 0;                                   /* lo rialzera' la scheda */
    d[13] = 0;
    d[14] = 0;
    d[15] = 0;

    g_tx_prossimo = (g_tx_prossimo + 1) % TX_N;
    reg_scrivi(REG_TDT, g_tx_prossimo);

    g_cont.inviati++;
    return 0;
}

/* =============================================================================
 * Servizio
 * ========================================================================== */
static void servi_scheda(void)
{
    unsigned int icr = reg_leggi(REG_ICR);       /* leggere azzera */

    (void)icr;      /* si guarda l'anello comunque: piu' robusto del bit */
    svuota_rx();
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

/* =============================================================================
 * Scoperta sul bus PCI
 * ========================================================================== */
static const struct { unsigned short dev; const char *nome; } g_modelli[] = {
    { E1000_82540EM, "Intel 82540EM (e1000)"        },
    { E1000_82545EM, "Intel 82545EM (e1000)"        },
    { E1000_82574L,  "Intel 82574L (e1000e)"        },
};

static int e1000_conosciuta(unsigned short dev)
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

/* ! SI ASPETTA IL BUS, NON SI PRETENDE DI TROVARLO ACCESO. Da quando la rete
 * si accende da [modules] di /boot/kernel.cfg, il kernel avvia il server PCI e
 * questo driver UNO DOPO L'ALTRO, senza aspettare in mezzo: chiedere subito
 * vuol dire chiedere a chi non si e' ancora registrato, uscire, e lasciare la
 * macchina senza rete fino al riavvio — dove magari va, perche' i tempi
 * cambiano. Chi lo lancia a mano trova il servizio gia' li' e non aspetta
 * niente. */
#define ATTESA_PCI_MS  5000
static int cerca_su_pci(void)
{
    PciDispositivo d;
    unsigned int   ord;
    int            pid, n, i;

    pid = ipc_attendi(PCI_SERVIZIO, ATTESA_PCI_MS);
    if (pid <= 0) {
        printf("e1000: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
        return -1;
    }

    for (ord = 0; ord < 16; ord++) {
        n = chiedi_pci(pid, ord, &d);
        if (n <= 0) break;

        if (d.venditore != E1000_VENDITORE) continue;
        i = e1000_conosciuta(d.dispositivo);
        if (i < 0) continue;

        /* ! SERVE LA BAR DI MEMORIA, NON LA PRIMA CHE CAPITA: e' li' che
         * stanno i registri, e senza mmio_map da ring 3 non ci si arriva.
         *
         * La BAR I/O di questa scheda esiste ma su QEMU non fa niente —
         * verificato in hw/net/e1000.c, dove e1000_io_read rende 0 ed
         * e1000_io_write scarta tutto. Si annota per la diagnostica e non si
         * usa: fino al 13 agosto 2026 serviva a passare il varco di
         * mmio_map, che adesso guarda altro. */
        g_mmio_fis = 0;
        g_base     = 0;
        {
            int b;
            for (b = 0; b < 6; b++) {
                if (d.bar[b] == 0) continue;
                if (d.bar_io[b]) { if (g_base == 0)     g_base     = d.bar[b]; }
                else             { if (g_mmio_fis == 0) g_mmio_fis = d.bar[b]; }
            }
        }
        if (g_mmio_fis == 0) {
            printf("e1000: trovata %04x:%04x ma senza BAR di memoria.\n",
                   d.venditore, d.dispositivo);
            return -1;
        }

        g_irq         = d.irq_linea;
        g_bus         = d.bus;
        g_slot        = d.slot;
        g_funzione    = d.funzione;
        g_dispositivo = d.dispositivo;
        strncpy(g_modello, g_modelli[i].nome, sizeof(g_modello) - 1);

        /* La scheda deve poter fare da bus master: senza, gli anelli di
         * descrittori non li legge nemmeno. Il BIOS spesso lo lascia
         * spento. */
        {
            PciAzione a;
            a.bus = d.bus; a.slot = d.slot; a.funzione = d.funzione;
            a.riservato = 0; a.offset = 0;
            a.bit = PCI_ABIL_IO | PCI_ABIL_BUSMASTER;
            ipc_send(pid, PCI_MSG_ABILITA, &a, sizeof(a));
            /* la risposta si scarta: se non fosse andata, il reset qui
             * sotto fallirebbe e lo diremmo li' */
        }
        return 0;
    }

    printf("e1000: nessuna scheda Intel e1000 sul bus PCI.\n");
    return -1;
}

/* =============================================================================
 * Diagnostica
 * ========================================================================== */
static void stampa_stato(void)
{
    unsigned int st = reg_leggi(REG_STATUS);

    printf("e1000: %s\n", g_modello);
    /* La BAR I/O si riferisce perche' la scheda ce l'ha, non perche' serva:
     * questo driver non la tocca, e su QEMU non risponderebbe comunque. */
    printf("       %02x:%02x.%u  BAR I/O 0x%x (non usata)  IRQ %u\n",
           g_bus, g_slot, g_funzione, g_base, g_irq);
    printf("       MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("       link %s, %s duplex   (STATUS 0x%08x)\n",
           (st & STATUS_LU) ? "SU" : "GIU'",
           (st & STATUS_FD) ? "full" : "half", st);
    printf("       DMA %u byte: virt 0x%x, fisico 0x%x\n",
           DMA_BYTE, g_dma_virt, g_dma_fis);
    printf("       anelli %d RX, %d TX da %d byte\n", RX_N, TX_N, BUF_LEN);
}

static void servi(void)
{
    IpcMessage    meta;
    unsigned char payload[IPC_MSG_MAX_DATA];

    for (;;) {
        int r = ipc_recv_timeout(&meta, payload, sizeof(payload), PERIODO_MS);

        if (r < 0) {
            g_cont.battiti++;
            servi_scheda();
            irq_done(g_irq);
            consegna();
            continue;
        }

        if (meta.sender_pid == IPC_SENDER_KERNEL &&
            meta.tipo == IPC_TYPE_IRQ_NOTIFY) {
            g_cont.notifiche_irq++;
            servi_scheda();
            irq_done(g_irq);
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
            ipc_send(meta.sender_pid, NET_MSG_CONTEGGI,
                     &g_cont, sizeof(g_cont));
            break;

        default:
            break;
        }
    }
}

static void uso(void)
{
    printf("uso: /dev/e1000.drv [-i] [-v]\n");
    printf("  (nessuna opzione)  si aggancia alla scheda e resta in servizio\n");
    printf("  -i                 la sonda: dice se c'e' e basta\n");
    printf("  -v                 dice cosa vede, registro per registro\n");
}

int main(int argc, char **argv)
{
    int sonda = 0, i, rc;
    DmaZona z;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0)      sonda = 1;
        else if (strcmp(argv[i], "-v") == 0) g_verboso = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else { uso(); return 2; }
    }

    memset(&g_cont, 0, sizeof(g_cont));

    if (cerca_su_pci() != 0) return 1;

    /* ! LA SONDA NON TOCCA LA SCHEDA. hwconfig -d la chiama su TUTTI i
     * driver del catalogo per vedere quale riconosce il proprio hardware:
     * se ognuno ne facesse il reset, sondare vorrebbe dire azzerare mezza
     * macchina. Dire «c'e'» si puo' fare con la sola enumerazione PCI. */
    if (sonda) {
        printf("e1000: %s in %02x:%02x.%u\n",
               g_modello, g_bus, g_slot, g_funzione);
        return 0;
    }

    /* ! QUI C'ERA UNA ioport_bind() CHE NON SERVIVA A NIENTE, e la sua morte
     * e' il senso del lavoro del 13 agosto 2026.
     *
     * Questo driver non usa NESSUNA porta I/O: la finestra IOADDR/IODATA
     * della BAR1 su QEMU e' morta (vedi sopra), e tutto passa dai registri
     * mappati. Le porte si prendevano lo stesso perche' mmio_map() chiedeva
     * `io_port_count != 0` per distinguere «un driver» da «un programma
     * qualunque»: l'e1000 una BAR I/O ce l'ha, quindi la rivendicava senza
     * usarla, solo per superare un controllo.
     *
     * Adesso il kernel guarda il nome dell'eseguibile — *.drv — e la domanda
     * non la fa piu' al processo. Un framebuffer, che porte I/O non ne ha
     * proprio, passa dallo stesso varco.
     */

    /* I registri, dentro il processo. */
    {
        MmioZona m;

        m.fisico = g_mmio_fis;
        m.byte   = E1000_MMIO_BYTE;
        rc = mmio_map(&m);
        if (rc < 0) {
            printf("e1000: mmio_map(0x%08x, %u) fallita (%d)\n",
                   g_mmio_fis, E1000_MMIO_BYTE, rc);
            if (rc == -EPERM)
                printf("       -EPERM: o non mi chiamo *.drv (per il kernel "
                       "non sono un driver),\n"
                       "       o quell'indirizzo e' RAM.\n");
            return 1;
        }
        g_reg = (volatile unsigned char *)m.virt;
        if (g_verboso)
            printf("e1000: registri 0x%08x -> 0x%08x (%u byte, non "
                   "cacheabili)\n", g_mmio_fis, m.virt, E1000_MMIO_BYTE);
    }

    /* ! LA PRIMA LETTURA E' UNA VERIFICA, NON UN DETTAGLIO.
     *
     * Il caso cattivo NON e' 0xFFFFFFFF: una finestra assente su bus aperto
     * legge tutti UNO, ma una che non decodifica legge tutti ZERO, che
     * somiglia a un valore. E' successo davvero con la BAR I/O di QEMU: il
     * reset «riusciva» subito (CTRL_RST letto 0 sembra gia' finito), il MAC
     * veniva 00:00:00:00:00:00, il servizio si registrava, e la rete non
     * avrebbe funzionato mai senza che niente lo dicesse.
     *
     * STATUS su una e1000 viva non e' ne' 0 ne' tutti uno. */
    {
        unsigned int st = reg_leggi(REG_STATUS);

        if (st == 0xFFFFFFFFu || st == 0x00000000u) {
            printf("e1000: i registri mappati non rispondono "
                   "(STATUS = 0x%08x).\n", st);
            printf("       La finestra e' a 0x%08x fisico; se e' quella "
                   "giusta,\n", g_mmio_fis);
            printf("       la scheda non sta decodificando.\n");
            return 1;
        }
        if (g_verboso)
            printf("e1000: registri vivi, STATUS = 0x%08x\n", st);
    }

    z.byte = DMA_BYTE;
    rc = dma_alloc(&z);
    if (rc < 0) {
        printf("e1000: dma_alloc(%u) fallita (%d)\n", DMA_BYTE, rc);
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;
    memset((void *)g_dma_virt, 0, DMA_BYTE);

    if (inizializza_scheda() != 0) return 1;

    rc = irq_bind(g_irq);
    if (rc < 0) {
        printf("e1000: irq_bind(%u) fallita (%d) - l'IRQ e' di qualcun altro?\n",
               g_irq, rc);
        return 1;
    }

    stampa_stato();

    if (ipc_register(NET_SERVIZIO_0) < 0) {
        printf("e1000: non riesco a registrare il servizio '%s'\n",
               NET_SERVIZIO_0);
        return 1;
    }
    printf("e1000: servizio '%s' attivo\n", NET_SERVIZIO_0);

    servi();
    return 0;
}
