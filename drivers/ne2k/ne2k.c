/* =============================================================================
 * drivers/ne2k/ne2k.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver NE2000 / DP8390 di EX-OS (/dev/ne2k.drv) — PROCESSO RING3.
 *
 *   /dev/ne2k.drv           trova la scheda, si registra come "rete0"
 *   /dev/ne2k.drv -i        stampa cosa ha trovato ed esce
 *   /dev/ne2k.drv -p 0x300 -q 3
 *                           scheda ISA a porte e IRQ dichiarati (vedi sotto)
 *
 * Guida la famiglia NE2000: la Realtek RTL8029 su PCI (l'onnipresente
 * "ne2k_pci"), i cloni Winbond/VIA/KTI, e le vecchie schede ISA se gli
 * si dice dove stanno.
 *
 * -----------------------------------------------------------------------------
 * PERCHE' PROPRIO QUESTA SCHEDA, CHE NESSUNO PRODUCE PIU'
 *
 * Perche' non ha DMA verso la memoria di sistema. La RAM dei pacchetti
 * sta SULLA SCHEDA, e ci si accede attraverso una porta di I/O: il
 * "DMA remoto", che di DMA ha solo il nome — e' un contatore interno che
 * avanza a ogni lettura della porta dati.
 *
 * La conseguenza e' che questo driver puo' girare in ring3 OGGI, senza
 * che il kernel sappia niente di indirizzi fisici, pagine bloccate o
 * bus mastering. La PCnet, che il DMA vero ce l'ha, dovra' aspettare
 * quella parte; questa no. Ed e' il modo giusto di aprire un capitolo
 * nuovo: prima far funzionare la catena intera (scheda -> driver -> IPC
 * -> stack) sul pezzo di hardware piu' semplice, poi complicare UNA cosa
 * per volta.
 *
 * -----------------------------------------------------------------------------
 * ! LA MASCHERA DELL'IRQ NON E' FACOLTATIVA
 *
 * Gli interrupt PCI sono A LIVELLO: la scheda tiene la linea alta finche'
 * non le si azzera il registro ISR. Un driver ring3 non gira dentro
 * l'interrupt — riceve un messaggio e verra' schedulato dopo — quindi
 * senza mascheramento l'interrupt riparte subito dopo l'iret e questo
 * processo non riceve mai la CPU per andare ad azzerare quel registro.
 *
 * Da agosto 2026 il kernel maschera l'IRQ prima di notificare, e sta al
 * driver riaprirlo con irq_done() DOPO aver servito la scheda. Se questa
 * chiamata sparisse, la rete funzionerebbe per un pacchetto e poi mai
 * piu'. Vedi kernel/arch/x86/isr.c.
 *
 * -----------------------------------------------------------------------------
 * ! IL BATTITO: PERCHE' SI GUARDA LA SCHEDA ANCHE SENZA INTERRUPT
 *
 * ipc_notify_irq() non blocca: se la mailbox di questo processo e' piena
 * nel momento in cui arriva l'interrupt, la notifica viene SCARTATA. A
 * quel punto la linea resterebbe mascherata per sempre e la scheda muta,
 * senza un errore da nessuna parte.
 *
 * Percio' l'attesa ha una scadenza: ogni PERIODO_MS senza messaggi si
 * guarda comunque la scheda e si riapre la linea. Una notifica persa
 * costa un ritardo, non un'interfaccia morta — che e' la differenza fra
 * un guasto che si nota e uno che si scopre due settimane dopo.
 *
 * -----------------------------------------------------------------------------
 * ! LE SCHEDE ISA NON SI SONDANO DA SOLE, E NON E' PIGRIZIA
 *
 * Una NE2000 ISA non ha spazio di configurazione: per trovarla bisogna
 * scrivere sulla sua porta di reset e vedere se risponde. Il punto e'
 * che se a quell'indirizzo c'e' un'ALTRA scheda, le si e' appena scritto
 * addosso — e la lista dei "soliti indirizzi" (0x300, 0x280, 0x320,
 * 0x340, 0x360) e' esattamente la lista degli indirizzi che si
 * contendevano tutte le schede ISA del mondo.
 *
 * Percio' l'indirizzo va detto: `-p 0x300 -q 3`. Sondare a tentativi
 * l'hardware di qualcun altro non e' una comodita' da aggiungere, e'
 * un danno da non fare.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "net_proto.h"

/* =============================================================================
 * Registri DP8390. Gli offset 0x00-0x0F cambiano significato secondo la
 * pagina selezionata nei bit 6-7 del registro comando: e' il motivo per
 * cui ogni accesso qui sotto dice esplicitamente in che pagina si trova.
 * ============================================================================= */
#define NE_PORTE        32      /* 0x00-0x1F: quante ne rivendichiamo */

#define NE_CR           0x00    /* comando (tutte le pagine) */

/* Pagina 0, lettura */
#define NE_ISR          0x07    /* stato interrupt */
#define NE_RSR          0x0C    /* stato ricezione */

/* Pagina 0, scrittura */
#define NE_PSTART       0x01    /* prima pagina dell'anello di ricezione */
#define NE_PSTOP        0x02    /* prima pagina OLTRE l'anello */
#define NE_BNRY         0x03    /* ultima pagina letta dal driver */
#define NE_TPSR         0x04    /* pagina di partenza del buffer di invio */
#define NE_TBCR0        0x05    /* byte da trasmettere, basso */
#define NE_TBCR1        0x06    /* byte da trasmettere, alto */
#define NE_RSAR0        0x08    /* indirizzo DMA remoto, basso */
#define NE_RSAR1        0x09    /* indirizzo DMA remoto, alto */
#define NE_RBCR0        0x0A    /* byte del DMA remoto, basso */
#define NE_RBCR1        0x0B    /* byte del DMA remoto, alto */
#define NE_RCR          0x0C    /* configurazione ricezione */
#define NE_TCR          0x0D    /* configurazione trasmissione */
#define NE_DCR          0x0E    /* configurazione dati */
#define NE_IMR          0x0F    /* maschera interrupt */

/* Pagina 1 */
#define NE_PAR0         0x01    /* indirizzo MAC, sei byte */
#define NE_CURR         0x07    /* pagina in cui la scheda sta scrivendo */
#define NE_MAR0         0x08    /* filtro multicast, otto byte */

/* Fuori dal DP8390: aggiunte della scheda NE2000 */
#define NE_DATA         0x10    /* porta dati del DMA remoto */
#define NE_RESET        0x1F    /* leggere e riscrivere qui = reset */

/* Bit del registro comando */
#define CR_STP          0x01    /* ferma */
#define CR_STA          0x02    /* avvia */
#define CR_TXP          0x04    /* trasmetti adesso */
#define CR_RD_LEGGI     0x08    /* DMA remoto: lettura */
#define CR_RD_SCRIVI    0x10    /* DMA remoto: scrittura */
#define CR_RD_ANNULLA   0x20    /* DMA remoto: annulla/completa */
#define CR_PAGINA1      0x40

/* Comandi composti che ricorrono, scritti una volta sola per non
 * sbagliarli in giro: "pagina 0, avviata, nessun DMA in corso". */
#define CR_P0           (CR_RD_ANNULLA | CR_STA)          /* 0x22 */
#define CR_P1           (CR_PAGINA1 | CR_RD_ANNULLA | CR_STA)  /* 0x62 */
#define CR_P0_FERMO     (CR_RD_ANNULLA | CR_STP)          /* 0x21 */

/* Bit di ISR / IMR */
#define ISR_PRX         0x01    /* pacchetto ricevuto */
#define ISR_PTX         0x02    /* pacchetto trasmesso */
#define ISR_RXE         0x04    /* errore in ricezione */
#define ISR_TXE         0x08    /* errore in trasmissione */
#define ISR_OVW         0x10    /* l'anello e' traboccato */
#define ISR_CNT         0x20    /* contatori a fondo scala */
#define ISR_RDC         0x40    /* DMA remoto completato */
#define ISR_RST         0x80    /* la scheda e' ferma / resettata */

/* Bit di RSR (stato del pacchetto ricevuto) */
#define RSR_PRX         0x01    /* ricevuto senza errori */

/* Configurazione */
#define DCR_16BIT       0x49    /* parole da 16 bit, FIFO 8 byte, no loopback */
#define TCR_LOOPBACK    0x02
#define TCR_NORMALE     0x00
#define RCR_MONITOR     0x20    /* non accetta niente: si usa durante l'init */
#define RCR_BROADCAST   0x04    /* accetta unicast per noi + broadcast */

/* =============================================================================
 * Mappa della memoria sulla scheda (16 KB, pagine da 256 byte)
 *
 *   0x40-0x45   buffer di trasmissione (6 pagine = 1536 byte, un frame)
 *   0x46-0x7F   anello di ricezione
 *
 * Sono i valori canonici della NE2000 e non conviene inventarne altri:
 * ogni scheda clone e ogni emulatore e' stato provato con questi.
 * ============================================================================= */
#define PAG_TX          0x40
#define PAG_RX_INIZIO   0x46
#define PAG_RX_FINE     0x80

/* Modelli PCI che questo driver sa guidare. Sono schede diverse con la
 * stessa programmazione: la NE2000 e' stata copiata cosi' tante volte
 * che il registro comando e' diventato uno standard di fatto. */
typedef struct { unsigned short ven, dev; const char *nome; } ModelloPci;

static const ModelloPci g_modelli[] = {
    { 0x10EC, 0x8029, "Realtek RTL8029(AS)" },
    { 0x1050, 0x0940, "Winbond W89C940"     },
    { 0x1106, 0x0926, "VIA VT86C926 Amazon" },
    { 0x8E2E, 0x3000, "KTI ET32P2"          },
};
#define N_MODELLI ((int)(sizeof(g_modelli) / sizeof(g_modelli[0])))

/* =============================================================================
 * Stato del driver
 * ============================================================================= */
static unsigned int  g_base;            /* prima porta di I/O */
static unsigned int  g_irq;
static unsigned char g_mac[6];
static char          g_modello[32];
static unsigned int  g_bus = 0xFFFFFFFF, g_slot, g_funzione;
static NetContatori  g_cont;

/* Coda dei frame ricevuti. Otto sono un compromesso: abbastanza per
 * assorbire una raffica mentre lo stack e' occupato, poco abbastanza da
 * non tenere 100 KB fermi. Quando e' piena si scarta il piu' VECCHIO —
 * in rete un pacchetto vecchio vale meno di uno nuovo, e la ritrasmissione
 * di chi lo aspettava e' gia' partita. */
#define CODA_FRAME 8
static struct {
    unsigned int  len;
    unsigned char dati[NET_FRAME_MAX];
} g_coda[CODA_FRAME];
static int g_coda_testa = 0, g_coda_conta = 0;

/* Chi ha chiesto un frame e sta aspettando. 0 = nessuno. Un solo lettore:
 * lo stack di rete e' un processo solo, e due lettori che si dividono i
 * frame a caso non sarebbero utili a nessuno. */
static unsigned int g_lettore_pid = 0;

/* Ogni quanto guardare la scheda anche senza interrupt (vedi il commento
 * di testa sul battito). 250 ms e' abbastanza raro da non pesare e
 * abbastanza fitto da non far sembrare la rete morta. */
#define PERIODO_MS 250

/* =============================================================================
 * Accesso ai registri
 *
 * Ogni accesso e' una syscall, che il kernel verifica contro il range
 * dichiarato con ioport_bind(). Costa piu' di una `out` diretta, ed e'
 * il prezzo del fatto che un errore qui non puo' toccare il resto della
 * macchina.
 * ============================================================================= */
static void reg_out(unsigned int off, unsigned int v)
{
    ioport_out(g_base + off, v);
}

static int reg_in(unsigned int off)
{
    return ioport_in(g_base + off);
}

/* =============================================================================
 * DMA remoto — l'unico modo di arrivare alla RAM della scheda
 *
 * Si programma indirizzo e lunghezza, si dice "leggi" o "scrivi", e poi
 * si trasferisce una parola per volta dalla porta dati. Il contatore
 * interno avanza da solo e si avvolge a PSTOP quando si e' dentro
 * l'anello di ricezione — per questo un pacchetto a cavallo della fine
 * dell'anello si legge in una volta sola, senza spezzare la lettura.
 *
 * ! IN PAROLE, QUINDI I BYTE DISPARI VANNO GESTITI. Il DCR e' impostato
 * a 16 bit: la porta dati trasferisce due byte per volta e una lunghezza
 * dispari lascerebbe fuori l'ultimo. Si legge una parola in piu' e si
 * butta la meta' che non serve.
 * ============================================================================= */
static void dma_leggi(unsigned int sorgente, unsigned char *dst, unsigned int len)
{
    unsigned int i;

    reg_out(NE_CR, CR_P0);
    reg_out(NE_ISR, ISR_RDC);            /* azzera la fine del DMA precedente */
    reg_out(NE_RBCR0, len & 0xFF);
    reg_out(NE_RBCR1, (len >> 8) & 0xFF);
    reg_out(NE_RSAR0, sorgente & 0xFF);
    reg_out(NE_RSAR1, (sorgente >> 8) & 0xFF);
    reg_out(NE_CR, CR_RD_LEGGI | CR_STA);

    for (i = 0; i < len; i += 2) {
        int p = ioport_in16(g_base + NE_DATA);

        if (p < 0) p = 0;                /* syscall fallita: byte a zero */
        dst[i] = (unsigned char)(p & 0xFF);
        if (i + 1 < len) dst[i + 1] = (unsigned char)((p >> 8) & 0xFF);
    }
}

static void dma_scrivi(unsigned int destinazione, const unsigned char *src,
                       unsigned int len)
{
    unsigned int i;

    reg_out(NE_CR, CR_P0);
    reg_out(NE_ISR, ISR_RDC);
    reg_out(NE_RBCR0, len & 0xFF);
    reg_out(NE_RBCR1, (len >> 8) & 0xFF);
    reg_out(NE_RSAR0, destinazione & 0xFF);
    reg_out(NE_RSAR1, (destinazione >> 8) & 0xFF);
    reg_out(NE_CR, CR_RD_SCRIVI | CR_STA);

    for (i = 0; i < len; i += 2) {
        unsigned int p = src[i];

        if (i + 1 < len) p |= (unsigned int)src[i + 1] << 8;
        ioport_out16(g_base + NE_DATA, p);
    }

    /* Si aspetta che la scheda dica di aver finito, altrimenti la
     * trasmissione partirebbe da un buffer scritto a meta'. Il ciclo ha
     * un tetto: una scheda che non risponde deve far fallire l'invio,
     * non fermare il driver. */
    for (i = 0; i < 20000; i++) {
        int isr = reg_in(NE_ISR);
        if (isr < 0 || (isr & ISR_RDC)) break;
    }
    reg_out(NE_ISR, ISR_RDC);
}

/* =============================================================================
 * Accensione
 * ============================================================================= */
static int reset_scheda(void)
{
    int v, i;

    v = reg_in(NE_RESET);
    if (v < 0) return -1;
    reg_out(NE_RESET, v);

    for (i = 0; i < 20000; i++) {
        int isr = reg_in(NE_ISR);
        if (isr < 0) return -1;
        if (isr & ISR_RST) return 0;
    }
    return -1;   /* la scheda non ha mai detto di essersi resettata */
}

/* Legge i 32 byte della PROM. In modalita' a 16 bit ogni byte esce
 * DUPLICATO dentro una parola, quindi il MAC sta nei byte pari e la
 * firma 0x57 0x57 finisce a 28 e 30. E' il modo canonico di riconoscere
 * una NE2000, ed e' anche il modo di accorgersi che a quell'indirizzo
 * non c'e' nessuna NE2000. */
static void leggi_prom(unsigned char *prom32)
{
    reg_out(NE_CR, CR_P0_FERMO);
    reg_out(NE_DCR, DCR_16BIT);
    reg_out(NE_RBCR0, 0);
    reg_out(NE_RBCR1, 0);
    reg_out(NE_RCR, RCR_MONITOR);
    reg_out(NE_TCR, TCR_LOOPBACK);

    dma_leggi(0, prom32, 32);
}

static int inizializza_scheda(void)
{
    unsigned char prom[32];
    int i;

    if (reset_scheda() != 0) return -1;
    reg_out(NE_ISR, 0xFF);

    leggi_prom(prom);

    if (prom[28] != 0x57 || prom[30] != 0x57) {
        /* Non e' una NE2000. Si dice cosa si e' letto invece di dire solo
         * "non trovata": se un clone usa un'altra firma, il numero qui
         * sotto e' l'unica cosa che permette di aggiungerlo. */
        printf("ne2k: a 0x%x non c'e' una NE2000 (firma %02x %02x, attesa 57 57)\n",
               g_base, prom[28], prom[30]);
        return -1;
    }

    for (i = 0; i < 6; i++) g_mac[i] = prom[i * 2];

    /* --- Anello e buffer di trasmissione --- */
    reg_out(NE_CR, CR_P0_FERMO);
    reg_out(NE_DCR, DCR_16BIT);
    reg_out(NE_RBCR0, 0);
    reg_out(NE_RBCR1, 0);
    reg_out(NE_RCR, RCR_MONITOR);
    reg_out(NE_TCR, TCR_LOOPBACK);
    reg_out(NE_TPSR, PAG_TX);
    reg_out(NE_PSTART, PAG_RX_INIZIO);
    reg_out(NE_BNRY, PAG_RX_INIZIO);
    reg_out(NE_PSTOP, PAG_RX_FINE);
    reg_out(NE_ISR, 0xFF);
    reg_out(NE_IMR, 0x00);

    /* --- Pagina 1: indirizzo nostro, filtro, posizione di scrittura --- */
    reg_out(NE_CR, CR_PAGINA1 | CR_RD_ANNULLA | CR_STP);
    for (i = 0; i < 6; i++) reg_out(NE_PAR0 + (unsigned)i, g_mac[i]);
    for (i = 0; i < 8; i++) reg_out(NE_MAR0 + (unsigned)i, 0xFF);

    /* ! CURR = PSTART + 1 e BNRY = PSTART: uguali significherebbe
     * "anello vuoto" e "anello pieno" allo stesso tempo, e la prima
     * lettura vedrebbe un pacchetto che non c'e'. */
    reg_out(NE_CURR, PAG_RX_INIZIO + 1);

    /* --- Si parte --- */
    reg_out(NE_CR, CR_P0);
    reg_out(NE_TCR, TCR_NORMALE);
    reg_out(NE_RCR, RCR_BROADCAST);
    reg_out(NE_ISR, 0xFF);
    reg_out(NE_IMR, ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW);

    return 0;
}

/* =============================================================================
 * Ricezione
 * ============================================================================= */
static void accoda(const unsigned char *f, unsigned int len)
{
    int pos;

    if (g_coda_conta == CODA_FRAME) {
        /* Piena: si butta il piu' vecchio. Vedi il commento su CODA_FRAME. */
        g_coda_testa = (g_coda_testa + 1) % CODA_FRAME;
        g_coda_conta--;
        g_cont.persi_coda++;
    }

    pos = (g_coda_testa + g_coda_conta) % CODA_FRAME;
    g_coda[pos].len = len;
    memcpy(g_coda[pos].dati, f, len);
    g_coda_conta++;
}

/* Svuota l'anello della scheda. Ritorna 0 normalmente, -1 se l'anello e'
 * incoerente e va rifatta l'inizializzazione. */
static int svuota_anello(void)
{
    int giri;

    for (giri = 0; giri < 64; giri++) {
        unsigned char hdr[4], frame[NET_FRAME_MAX];
        int  curr, bnry;
        unsigned int pagina, prossima, len;

        reg_out(NE_CR, CR_P1);
        curr = reg_in(NE_CURR);
        reg_out(NE_CR, CR_P0);
        bnry = reg_in(NE_BNRY);

        if (curr < 0 || bnry < 0) return -1;

        pagina = (unsigned int)bnry + 1;
        if (pagina >= PAG_RX_FINE) pagina = PAG_RX_INIZIO;
        if (pagina == (unsigned int)curr) return 0;      /* anello vuoto */

        dma_leggi(pagina * 256, hdr, 4);
        prossima = hdr[1];
        len      = (unsigned int)hdr[2] | ((unsigned int)hdr[3] << 8);

        /* ! SI CONTROLLA `prossima` PRIMA DI USARLA. E' un byte scritto
         * dalla scheda; se per un guasto (o per un anello traboccato
         * durante la lettura) finisce fuori dall'intervallo, scriverlo in
         * BNRY manderebbe l'anello in uno stato da cui non esce piu' e
         * questo ciclo girerebbe a vuoto per sempre. Meglio ricominciare
         * da capo che insistere su dati che non tornano. */
        if (prossima < PAG_RX_INIZIO || prossima >= PAG_RX_FINE) return -1;
        if (len < 4 + 14) { g_cont.errori_rx++; return -1; }

        len -= 4;   /* i primi quattro byte sono l'intestazione della scheda */

        if (len > NET_FRAME_MAX) {
            g_cont.troppo_grandi++;
        } else if (hdr[0] & RSR_PRX) {
            dma_leggi(pagina * 256 + 4, frame, len);
            accoda(frame, len);
            g_cont.ricevuti++;
        } else {
            g_cont.errori_rx++;
        }

        /* BNRY resta UNA pagina indietro rispetto a quella liberata: se
         * arrivasse a coincidere con CURR la scheda leggerebbe l'anello
         * come vuoto invece che pieno, e ricomincerebbe a sovrascrivere
         * dal principio. */
        reg_out(NE_BNRY, (prossima == PAG_RX_INIZIO) ? PAG_RX_FINE - 1
                                                     : prossima - 1);
    }
    return 0;
}

/* =============================================================================
 * Trabocco dell'anello
 *
 * ! QUI SI REINIZIALIZZA TUTTA LA SCHEDA, E NON PERCHE' SIA ELEGANTE.
 *
 * La procedura ufficiale per uscire da un OVW e' una sequenza di undici
 * passi con un ramo che dipende dal fatto che una trasmissione fosse in
 * corso o no, e dal fatto che sia poi finita o vada rilanciata. E' nota
 * per essere il punto in cui i driver NE2000 sbagliano — e' un percorso
 * che si esegue di rado e quindi si prova di rado.
 *
 * Un trabocco significa che dei pacchetti li abbiamo GIA' persi: quello
 * che serve e' tornare in uno stato noto, non conservare il conservabile.
 * La reinizializzazione e' la stessa che gira all'avvio, cioe' la strada
 * piu' battuta del file. Costa qualche millisecondo in un caso raro, e in
 * cambio non c'e' un secondo percorso di recupero che nessuno prova mai.
 * ============================================================================= */
static void recupera_trabocco(void)
{
    g_cont.overflow++;
    if (inizializza_scheda() != 0)
        printf("ne2k: reinizializzazione dopo trabocco fallita\n");
}

/* =============================================================================
 * Trasmissione
 * ============================================================================= */
static int attendi_tx(void)
{
    int i;

    for (i = 0; i < 20000; i++) {
        int isr = reg_in(NE_ISR);

        if (isr < 0) return -1;
        if (isr & ISR_TXE) { reg_out(NE_ISR, ISR_TXE | ISR_PTX); return -1; }
        if (isr & ISR_PTX) { reg_out(NE_ISR, ISR_PTX); return 0; }
    }
    return -1;
}

static int trasmetti(const unsigned char *f, unsigned int len)
{
    unsigned char buf[NET_FRAME_MAX];
    unsigned int  n = len;

    if (len < 14 || len > NET_FRAME_MAX) return -EINVAL;

    /* ! RIEMPIMENTO A 60 BYTE. Sotto quella misura il frame non e'
     * valido e uno switch lo scarta senza dire niente: una richiesta ARP
     * e' lunga 42 byte, quindi senza questo riempimento la prima cosa che
     * si prova a fare non funziona. Riempie il driver e non chi compone
     * il pacchetto, perche' e' un vincolo del mezzo. */
    memcpy(buf, f, len);
    if (n < NET_FRAME_MIN) {
        memset(buf + n, 0, NET_FRAME_MIN - n);
        n = NET_FRAME_MIN;
    }

    dma_scrivi(PAG_TX * 256, buf, n);

    reg_out(NE_TPSR, PAG_TX);
    reg_out(NE_TBCR0, n & 0xFF);
    reg_out(NE_TBCR1, (n >> 8) & 0xFF);
    reg_out(NE_CR, CR_RD_ANNULLA | CR_TXP | CR_STA);

    /* Si aspetta la fine PRIMA di tornare: il buffer di trasmissione sulla
     * scheda e' uno solo, e una seconda dma_scrivi mentre la prima e'
     * ancora in volo sovrascriverebbe il pacchetto a meta' invio. */
    if (attendi_tx() != 0) { g_cont.errori_tx++; return -EIO; }

    g_cont.inviati++;
    return 0;
}

/* =============================================================================
 * Servizio della scheda
 * ============================================================================= */
static void servi_scheda(void)
{
    int giri;

    for (giri = 0; giri < 32; giri++) {
        int isr = reg_in(NE_ISR);

        if (isr <= 0) return;   /* 0 = niente da fare, <0 = syscall fallita */

        if (isr & ISR_OVW) {
            recupera_trabocco();
            return;             /* dopo la reinizializzazione ISR e' pulito */
        }

        /* ! SI AZZERA IL BIT PRIMA DI SVUOTARE, NON DOPO. Un pacchetto che
         * arriva mentre stiamo leggendo l'anello rialza PRX: azzerarlo
         * dopo cancellerebbe la segnalazione di un pacchetto che non
         * abbiamo ancora letto, e quello resterebbe fermo nell'anello
         * fino al pacchetto successivo — o per sempre. */
        if (isr & (ISR_PRX | ISR_RXE)) {
            reg_out(NE_ISR, isr & (ISR_PRX | ISR_RXE));
            if (svuota_anello() != 0) { recupera_trabocco(); return; }
        }

        /* PTX e TXE li consuma attendi_tx(); qui si azzerano solo quelli
         * che nessuno ha raccolto, per non restare a girare su un bit
         * acceso che non interessa a nessuno. */
        if (isr & (ISR_PTX | ISR_TXE | ISR_CNT | ISR_RDC))
            reg_out(NE_ISR, isr & (ISR_PTX | ISR_TXE | ISR_CNT | ISR_RDC));
    }
}

/* Consegna un frame a chi lo sta aspettando, se c'e' l'uno e l'altro. */
static void consegna(void)
{
    if (g_lettore_pid == 0 || g_coda_conta == 0) return;

    if (ipc_send(g_lettore_pid, NET_MSG_FRAME,
                 g_coda[g_coda_testa].dati, g_coda[g_coda_testa].len) < 0) {
        /* Il lettore e' sparito: si dimentica e si tiene il frame, che
         * potrebbe interessare a chi arriva dopo. */
        g_lettore_pid = 0;
        return;
    }

    g_coda_testa = (g_coda_testa + 1) % CODA_FRAME;
    g_coda_conta--;
    g_lettore_pid = 0;
}

/* =============================================================================
 * Ricerca della scheda
 * ============================================================================= */
static const ModelloPci *modello_noto(unsigned short ven, unsigned short dev)
{
    int i;

    for (i = 0; i < N_MODELLI; i++)
        if (g_modelli[i].ven == ven && g_modelli[i].dev == dev)
            return &g_modelli[i];
    return NULL;
}

/* Chiede al server PCI la n-esima scheda Ethernet e guarda se la sa
 * guidare. Ritorna 1 se trovata, 0 se non ce ne sono di note, -1 se il
 * server non risponde. */
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
    int pid = ipc_attendi(PCI_SERVIZIO, ATTESA_PCI_MS);
    unsigned int n;

    if (pid <= 0) {
        printf("ne2k: il servizio '%s' non e' attivo.\n", PCI_SERVIZIO);
        printf("      Avvialo con  /dev/pci.drv &  oppure indica la scheda\n");
        printf("      a mano:      /dev/ne2k.drv -p 0x300 -q 3\n");
        return -1;
    }

    for (n = 0; n < 16; n++) {
        PciRichiesta   r;
        PciDispositivo d;
        IpcMessage     meta;
        unsigned char  buf[IPC_MSG_MAX_DATA];
        const ModelloPci *m;
        int i, tentativi;

        r.ordinale    = n;
        r.classe      = PCI_CLASSE_RETE;
        r.sottoclasse = PCI_SOTTO_ETHERNET;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;

        /* Si controlla chi ha risposto: ipc_recv consegna il prossimo
         * messaggio, non "la risposta alla mia domanda". */
        for (tentativi = 0; tentativi < 8; tentativi++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
            if ((int)meta.sender_pid == pid) break;
        }
        if ((int)meta.sender_pid != pid) return -1;

        if (meta.tipo == PCI_MSG_FINE) break;
        if (meta.tipo != PCI_MSG_DISPOSITIVO || meta.len < sizeof(d)) return -1;
        memcpy(&d, buf, sizeof(d));

        m = modello_noto(d.venditore, d.dispositivo);
        if (m == NULL) continue;

        for (i = 0; i < 6; i++) {
            if (d.bar_io[i] && d.bar[i] != 0) {
                PciAzione a;

                g_base     = d.bar[i];
                g_irq      = d.irq_linea;
                g_bus      = d.bus;
                g_slot     = d.slot;
                g_funzione = d.funzione;
                strncpy(g_modello, m->nome, sizeof(g_modello) - 1);
                g_modello[sizeof(g_modello) - 1] = '\0';

                /* Il BIOS di solito l'ha gia' fatto, ma chiederlo costa un
                 * messaggio e toglie un modo di fallire in silenzio: una
                 * scheda con la decodifica I/O spenta risponde 0xFF a
                 * tutto, e sembra assente invece che spenta. */
                a.bus = d.bus; a.slot = d.slot; a.funzione = d.funzione;
                a.riservato = 0; a.offset = 0; a.bit = PCI_ABIL_IO;
                ipc_send(pid, PCI_MSG_ABILITA, &a, sizeof(a));
                for (tentativi = 0; tentativi < 8; tentativi++) {
                    if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) break;
                    if ((int)meta.sender_pid == pid) break;
                }
                return 1;
            }
        }
    }
    return 0;
}

/* =============================================================================
 * Modo informativo
 * ============================================================================= */
static void stampa_stato(void)
{
    printf("ne2k: %s\n", g_modello);
    if (g_bus != 0xFFFFFFFF)
        printf("      PCI %02x:%02x.%d\n", g_bus, g_slot, g_funzione);
    else
        printf("      scheda ISA (indirizzo dichiarato a mano)\n");
    printf("      porte  0x%x-0x%x\n", g_base, g_base + NE_PORTE - 1);
    printf("      IRQ    %u\n", g_irq);
    printf("      MAC    %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("      MTU    %d\n", NET_MTU);
}

/* =============================================================================
 * Loop di servizio
 * ============================================================================= */
static void servi(void)
{
    IpcMessage    meta;
    unsigned char payload[IPC_MSG_MAX_DATA];

    for (;;) {
        int r = ipc_recv_timeout(&meta, payload, sizeof(payload), PERIODO_MS);

        if (r < 0) {
            /* Scaduta l'attesa: il battito. Si guarda la scheda e si
             * riapre la linea anche se nessuna notifica e' arrivata —
             * vedi il commento di testa sul perche' una notifica si puo'
             * perdere. */
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
            /* DOPO aver azzerato l'ISR della scheda, mai prima: riaprire
             * con la linea ancora alta rimette in piedi la tempesta. */
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
            /* L'ultimo che chiede vince, come nel driver di tastiera: due
             * lettori che si dividono i frame a caso non servirebbero a
             * nessuno, e ricordarne uno solo evita di dover decidere a
             * chi tocca. */
            g_lettore_pid = meta.sender_pid;
            break;

        case NET_MSG_ANNULLA: {
            NetEsito e;

            if (g_lettore_pid == meta.sender_pid) g_lettore_pid = 0;
            e.codice = 0;
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }

        case NET_MSG_CONTATORI:
            ipc_send(meta.sender_pid, NET_MSG_CONTEGGI, &g_cont, sizeof(g_cont));
            break;

        default: {
            NetEsito e;

            /* Si risponde comunque: un client fermo in ipc_recv per un
             * messaggio che non abbiamo capito e' un processo bloccato
             * che nessuno riesce piu' a ricondurre a questa riga. */
            e.codice = -EINVAL;
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }
        }

        /* Anche dopo una richiesta di un client si guarda la scheda: se
         * una notifica di interrupt e' andata persa, questo e' il primo
         * momento in cui ce ne accorgiamo. */
        servi_scheda();
        irq_done(g_irq);
        consegna();
    }
}

/* =============================================================================
 * main
 * ============================================================================= */
static void uso(void)
{
    printf("uso: ne2k.drv                  cerca la scheda su PCI e avvia il servizio\n");
    printf("     ne2k.drv -i               sonda il bus, dice cosa ha trovato\n");
    printf("                               ed esce (0 = c'e'). Non tocca la scheda.\n");
    printf("     ne2k.drv -p PORTA -q IRQ  scheda ISA a un indirizzo dichiarato\n\n");
    printf("Una NE2000 ISA non si cerca da sola: per trovarla bisognerebbe\n");
    printf("scrivere sulle porte dove stanno di solito, e se li' c'e'\n");
    printf("un'altra scheda le si scrive addosso. L'indirizzo va detto.\n");
}

int main(int argc, char **argv)
{
    int solo_info = 0, i, rc;

    g_base = 0;
    g_irq  = 0xFFFFFFFF;
    strncpy(g_modello, "NE2000 compatibile", sizeof(g_modello) - 1);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            solo_info = 1;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            g_base = (unsigned int)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            g_irq = (unsigned int)strtoul(argv[++i], NULL, 0);
        } else {
            uso();
            return 1;
        }
    }

    if (g_base == 0) {
        int t = cerca_su_pci();

        if (t < 0) return 1;
        if (t == 0) {
            printf("ne2k: nessuna scheda NE2000 sul bus PCI.\n");
            printf("      Per una scheda ISA:  /dev/ne2k.drv -p 0x300 -q 3\n");
            return 1;
        }
    } else if (g_irq == 0xFFFFFFFF) {
        printf("ne2k: con -p serve anche -q (l'IRQ di una scheda ISA non\n");
        printf("      si puo' dedurre: e' un ponticello, non un registro).\n");
        return 1;
    }

    /* ! CON -i SI SMETTE QUI, E LA SCHEDA NON SI TOCCA.
     *
     * A questo punto il bus PCI ha gia' detto tutto quello che serve per
     * rispondere alla domanda di -i — «questo driver serve su questa
     * macchina?» — e da qui in poi si comincerebbe a scrivere sui
     * registri: ioport_bind, reset, inizializzazione.
     *
     * Farlo e' pericoloso proprio nel momento in cui -i viene usato. La
     * sonda di `hwconfig -d` gira su un sistema ACCESO, dove l'autoexec
     * ha gia' avviato il driver giusto: resettare la scheda sotto a chi
     * la sta guidando ferma la rete, e il reset di una scheda occupata
     * risponde con uno stato che non e' quello atteso. Il sintomo era
     * un driver che dichiarava di non servire sulla macchina la cui
     * scheda stava guidando in quel momento.
     *
     * Leggere dal bus e' invece sempre sicuro: e' una domanda a
     * /dev/pci.drv, non un accesso alla periferica. */
    if (solo_info) return 0;

    if (g_irq > 15) {
        printf("ne2k: IRQ %u non valido (0-15).\n", g_irq);
        return 1;
    }

    rc = ioport_bind(g_base, NE_PORTE);
    if (rc < 0) {
        printf("ne2k: ioport_bind(0x%x, %d) fallita (%d)\n", g_base, NE_PORTE, rc);
        return 1;
    }

    if (inizializza_scheda() != 0) return 1;

    if (solo_info) { stampa_stato(); return 0; }

    /* L'IRQ si rivendica solo in modo servizio: prenderlo per stampare
     * due righe lo toglierebbe a un driver che sta davvero lavorando. */
    rc = irq_bind(g_irq);
    if (rc < 0) {
        printf("ne2k: irq_bind(%u) fallita (%d) - l'IRQ e' di qualcun altro?\n",
               g_irq, rc);
        return 1;
    }

    rc = ipc_register(NET_SERVIZIO_0);
    if (rc < 0) {
        printf("ne2k: ipc_register('%s') fallita (%d) - esco\n",
               NET_SERVIZIO_0, rc);
        return 1;
    }

    stampa_stato();
    printf("ne2k: servizio '%s' attivo\n", NET_SERVIZIO_0);

    servi();
    return 0;
}
