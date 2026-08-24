/* =============================================================================
 * drivers/pcnet/pcnet.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * AMD PCnet-PCI II / PCnet-FAST III (Am79C970/C970A/C971/C972/C973)
 * — driver di rete in RING3, come /dev/ne2k.drv.
 *
 * Parla il protocollo di drivers/net/net_proto.h, quindi lo stack IP non
 * sa e non deve sapere quale delle due schede c'e' sotto.
 *
 * -----------------------------------------------------------------------------
 * ! QUESTA SCHEDA E' DIVERSA DALLA NE2000, E LA DIFFERENZA E' TUTTO
 *
 * La NE2000 tiene la memoria dei pacchetti SULLA SCHEDA: ci si arriva da
 * una porta di I/O, un byte per volta, e il kernel non deve sapere niente
 * di indirizzi fisici. Per questo il driver ne2k e' stato il primo: non
 * chiedeva niente di nuovo al sistema.
 *
 * Il PCnet e' un BUS MASTER: legge e scrive la RAM DI SISTEMA da solo,
 * agli indirizzi fisici che gli si sono dati, senza passare dalla MMU e
 * senza chiedere permesso a nessuno. Ne discendono tre requisiti che il
 * driver non puo' soddisfare da solo:
 *
 *   1. il bit BUS MASTER nel comando PCI, altrimenti il ponte blocca ogni
 *      ciclo che la scheda inizia — e la scheda sembra muta, non spenta;
 *   2. memoria FISICAMENTE CONTIGUA di cui si conosca l'indirizzo fisico
 *      (SYS_DMA_ALLOC: vedi kernel/include/syscall.h);
 *   3. che quella memoria non si muova mai piu'.
 *
 * ! UN INDIRIZZO SBAGLIATO QUI NON DA' UN ERRORE. Dare alla scheda un
 * indirizzo virtuale invece di uno fisico non produce un fault e non
 * ferma niente: produce una scheda che scrive pacchetti dentro un punto a
 * caso della memoria fisica. Su una macchina piccola quel punto e' spesso
 * il kernel, e il sintomo arriva minuti dopo, altrove. E' il motivo per
 * cui `dma_alloc` restituisce i due indirizzi separati e con nomi diversi:
 * `virt` per noi, `fisico` per la scheda.
 *
 * -----------------------------------------------------------------------------
 * MODO DI ACCESSO: DWIO (32 bit)
 *
 * La scheda si accende in WIO (registri a 16 bit, RAP a +0x12, RESET a
 * +0x14). Una scrittura a 32 bit sul RDP la porta in DWIO, dove i registri
 * si spostano:
 *
 *      +0x00  APROM      i 6 byte del MAC, come li ha lasciati l'EEPROM
 *      +0x10  RDP        dato del registro CSR selezionato
 *      +0x14  RAP        quale registro (CSR o BCR)
 *      +0x18  RESET      una lettura ferma e azzera la scheda
 *      +0x1C  BDP        dato del registro BCR selezionato
 *
 * Si usa DWIO perche' le strutture in memoria sono comunque a 32 bit
 * (SWSTYLE 2): tenere l'accesso a 16 bit vorrebbe dire due larghezze
 * diverse nello stesso driver per nessun vantaggio.
 *
 * ! IL RESET SI FA IN WIO. E' l'unica operazione che deve avvenire prima
 * del cambio di modo, perche' il modo lo si stabilisce dopo il reset — e
 * l'offset del registro RESET e' diverso nei due modi. Farlo in DWIO su
 * una scheda appena accesa significa leggere +0x18, che in WIO non e' il
 * reset.
 *
 * -----------------------------------------------------------------------------
 * LE STRUTTURE IN MEMORIA (SWSTYLE 2)
 *
 * BCR20 sceglie il formato di init block e descrittori. Lo stile 2 e'
 * quello del PCnet-PCI II: strutture a 32 bit, descrittori da 16 byte.
 *
 * Blocco di inizializzazione, 28 byte — la scheda lo legge UNA volta, al
 * comando INIT, e da li' ricava tutto il resto:
 *
 *      +0   MODE (16)        0 = normale
 *      +2   RLEN (8)         log2(numero descrittori RX) nei bit 7..4
 *      +3   TLEN (8)         idem per TX
 *      +4   PADR[6]          il nostro MAC
 *      +10  riservato (16)
 *      +12  LADRF[8]         filtro multicast, tutto zero
 *      +20  RDRA (32)        indirizzo FISICO dell'anello RX
 *      +24  TDRA (32)        indirizzo FISICO dell'anello TX
 *
 * Descrittore, 16 byte. Il campo che conta e' il bit OWN:
 *
 *      +0   indirizzo FISICO del buffer
 *      +4   OWN(31) | flag | 1111(15..12) | BCNT(11..0)
 *      +8   stato / byte ricevuti
 *      +12  riservato
 *
 * ! BCNT E' IN COMPLEMENTO A DUE, su dodici bit, e i quattro bit sopra
 * devono essere UNI. Un buffer da 2048 byte si dichiara come
 * (-2048) & 0xFFF con 0xF000 sopra. Scriverci 2048 in chiaro non da' un
 * errore: da' una scheda che crede di avere un buffer di 2048 byte
 * NEGATIVI e si comporta di conseguenza.
 *
 * ! OWN DICE DI CHI E' IL DESCRITTORE, e le due direzioni lo usano al
 * contrario. In ricezione OWN=1 significa «e' della scheda, aspetta»; in
 * trasmissione OWN=1 significa «l'ho passato alla scheda, non toccarlo».
 * In entrambi i casi e' lei a rimetterlo a zero quando ha finito, ed e'
 * l'unico modo che abbiamo di saperlo.
 * ============================================================================= */

#include "libc.h"
#include "net_proto.h"
#include "pci_proto.h"

/* =============================================================================
 * Registri
 * ============================================================================= */
#define PCN_PORTE       0x20    /* quanto spazio I/O occupa la scheda */

/* Offset in modo WIO (prima del cambio) */
#define WIO_RESET       0x14

/* Offset in modo DWIO */
#define PCN_RDP         0x10
#define PCN_RAP         0x14
#define PCN_RESET       0x18
#define PCN_BDP         0x1C

/* CSR0 — stato e comandi */
#define C0_INIT         0x0001
#define C0_STRT         0x0002
#define C0_STOP         0x0004
#define C0_TDMD         0x0008  /* «guarda subito l'anello TX» */
#define C0_TXON         0x0010
#define C0_RXON         0x0020
#define C0_IENA         0x0040
#define C0_INTR         0x0080
#define C0_IDON         0x0100  /* init block letto */
#define C0_TINT         0x0200
#define C0_RINT         0x0400
#define C0_MERR         0x0800
#define C0_MISS         0x1000
#define C0_CERR         0x2000
#define C0_BABL         0x4000
#define C0_ERR          0x8000

/* I bit di CSR0 che si azzerano riscrivendoli a uno. Si azzerano TUTTI
 * insieme e in un colpo solo: leggere lo stato, decidere, e poi azzerare
 * solo quelli capiti lascerebbe gli altri alti — e la scheda continuerebbe
 * a tenere la linea di interrupt occupata per un motivo che nessuno
 * guarda. */
#define C0_DA_AZZERARE  (C0_IDON|C0_TINT|C0_RINT|C0_MERR|C0_MISS|C0_CERR| \
                         C0_BABL|C0_ERR)

/* Descrittori: bit del secondo campo */
#define D_OWN           0x80000000u
#define D_ERR           0x40000000u
#define D_STP           0x02000000u  /* primo pezzo del pacchetto */
#define D_ENP           0x01000000u  /* ultimo pezzo */
#define D_UNI           0x0000F000u  /* i quattro bit sopra BCNT, sempre a uno */

/* =============================================================================
 * Anelli e buffer
 *
 * ! POTENZE DI DUE, E NON PER ELEGANZA: la scheda riceve il LOGARITMO
 * del numero di descrittori, quindi un anello da 6 non e' esprimibile.
 * ============================================================================= */
#define RX_LOG          3
#define TX_LOG          3
#define RX_N            (1 << RX_LOG)
#define TX_N            (1 << TX_LOG)
#define BUF_LEN         2048    /* per un frame da 1514 avanza, ed e' potenza di due */

/* Disposizione dentro la zona DMA. Gli anelli vanno allineati a 16 byte;
 * partire da offset tondi lo garantisce senza doverlo calcolare. */
#define OFF_INIT        0x0000
#define OFF_RX_DESC     0x0100
#define OFF_TX_DESC     0x0200
#define OFF_BUFFER      0x1000
#define DMA_BYTE        (OFF_BUFFER + (RX_N + TX_N) * BUF_LEN)

#define PERIODO_MS      250     /* il battito: vedi ne2k.c */

/* =============================================================================
 * Stato
 * ============================================================================= */
static unsigned int  g_base = 0;
static unsigned int  g_irq  = 0;
static unsigned int  g_bus = 0xFFFFFFFF, g_slot = 0, g_funzione = 0;
static char          g_modello[48] = "AMD PCnet-PCI";
static unsigned char g_mac[6];

static unsigned int  g_dma_virt = 0;    /* come la vediamo noi */
static unsigned int  g_dma_fis  = 0;    /* come la vede la scheda */

static unsigned int  g_rx_prossimo = 0;
static unsigned int  g_tx_prossimo = 0;

static NetContatori  g_cont;

/* Coda dei frame ricevuti, in attesa che qualcuno li chieda. Stessa
 * politica del ne2k: si accumula qui, si consegna a richiesta. */
#define CODA_N          16
static unsigned char g_coda[CODA_N][NET_FRAME_MAX];
static unsigned int  g_coda_len[CODA_N];
static int           g_coda_testa = 0, g_coda_conta = 0;
static unsigned int  g_lettore_pid = 0;

/* =============================================================================
 * Accesso ai registri
 * ============================================================================= */
static unsigned int rdp_in(void)
{
    unsigned int v = 0;
    ioport_in32(g_base + PCN_RDP, &v);
    return v & 0xFFFF;
}

static void csr_scrivi(unsigned int reg, unsigned int val)
{
    ioport_out32(g_base + PCN_RAP, reg);
    ioport_out32(g_base + PCN_RDP, val & 0xFFFF);
}

static unsigned int csr_leggi(unsigned int reg)
{
    ioport_out32(g_base + PCN_RAP, reg);
    return rdp_in();
}

static void bcr_scrivi(unsigned int reg, unsigned int val)
{
    ioport_out32(g_base + PCN_RAP, reg);
    ioport_out32(g_base + PCN_BDP, val & 0xFFFF);
}

/* =============================================================================
 * Accesso alla memoria condivisa con la scheda
 *
 * Due indirizzi per la stessa cosa: `virt` per noi, `fisico` per lei.
 * Queste due funzioni sono l'unico posto in cui la conversione avviene, e
 * tenerla in un posto solo e' quello che impedisce di scambiarli.
 * ============================================================================= */
static volatile unsigned int *desc(unsigned int offset)
{
    return (volatile unsigned int *)(g_dma_virt + offset);
}

static unsigned char *buffer_virt(unsigned int indice)
{
    return (unsigned char *)(g_dma_virt + OFF_BUFFER + indice * BUF_LEN);
}

static unsigned int buffer_fisico(unsigned int indice)
{
    return g_dma_fis + OFF_BUFFER + indice * BUF_LEN;
}

/* BCNT di un buffer: complemento a due su 12 bit, con i quattro bit
 * sopra a uno. Vedi il commento di testa. */
static unsigned int bcnt(unsigned int len)
{
    return D_UNI | ((unsigned int)(-(int)len) & 0x0FFF);
}

/* =============================================================================
 * Inizializzazione della scheda
 * ============================================================================= */
static void leggi_mac(void)
{
    int i;

    /* L'APROM sta nei primi 16 byte dello spazio I/O e si legge a byte:
     * e' l'unica cosa che si puo' leggere prima di aver deciso il modo. */
    for (i = 0; i < 6; i++) g_mac[i] = (unsigned char)ioport_in(g_base + i);
}

static int reset_e_dwio(void)
{
    unsigned int c0;

    /* ! In WIO: la scheda appena accesa e' li', e l'offset del RESET e'
     * un altro. Vedi il commento di testa. */
    ioport_in16(g_base + WIO_RESET);
    usleep(1000);

    /* Una scrittura a 32 bit sul RDP porta in DWIO. Il valore non conta:
     * conta la larghezza. */
    ioport_out32(g_base + PCN_RDP, 0);
    usleep(1000);

    /* ! SI VERIFICA CHE IL CAMBIO ABBIA PRESO. Dopo un reset CSR0 vale
     * STOP; se leggiamo 0xFFFF stiamo parlando con il nulla (decodifica
     * I/O spenta), se leggiamo altro il modo non e' quello che crediamo.
     * Andare avanti senza controllare significa scrivere l'indirizzo
     * dell'init block in un registro a caso. */
    c0 = csr_leggi(0);
    if (c0 == 0xFFFF) {
        printf("pcnet: la scheda non risponde (0xFFFF): decodifica I/O spenta?\n");
        return -1;
    }
    if ((c0 & C0_STOP) == 0) {
        printf("pcnet: dopo il reset CSR0 = 0x%x, atteso STOP: modo non "
               "riconosciuto\n", c0);
        return -1;
    }
    return 0;
}

static void prepara_anelli(void)
{
    unsigned int i;

    /* RX: i buffer sono della scheda fin da subito — deve poterci
     * scrivere dentro senza aspettare noi. */
    for (i = 0; i < RX_N; i++) {
        volatile unsigned int *d = desc(OFF_RX_DESC + i * 16);

        d[0] = buffer_fisico(i);
        d[1] = D_OWN | bcnt(BUF_LEN);
        d[2] = 0;
        d[3] = 0;
    }

    /* TX: sono nostri finche' non c'e' niente da mandare. */
    for (i = 0; i < TX_N; i++) {
        volatile unsigned int *d = desc(OFF_TX_DESC + i * 16);

        d[0] = buffer_fisico(RX_N + i);
        d[1] = 0;
        d[2] = 0;
        d[3] = 0;
    }

    g_rx_prossimo = 0;
    g_tx_prossimo = 0;
}

static void prepara_init_block(void)
{
    volatile unsigned char  *b  = (volatile unsigned char *)(g_dma_virt + OFF_INIT);
    volatile unsigned short *b16 = (volatile unsigned short *)b;
    volatile unsigned int   *b32 = (volatile unsigned int *)b;
    int i;

    b16[0] = 0;                       /* MODE: ricezione normale */
    b[2]   = (unsigned char)(RX_LOG << 4);
    b[3]   = (unsigned char)(TX_LOG << 4);
    for (i = 0; i < 6; i++) b[4 + i] = g_mac[i];
    b16[5] = 0;                       /* riservato */
    for (i = 0; i < 8; i++) b[12 + i] = 0;   /* LADRF: nessun multicast */
    b32[5] = g_dma_fis + OFF_RX_DESC;        /* +20 RDRA */
    b32[6] = g_dma_fis + OFF_TX_DESC;        /* +24 TDRA */
}

static int inizializza_scheda(void)
{
    int giri;

    if (reset_e_dwio() != 0) return -1;

    /* SWSTYLE 2: strutture a 32 bit, descrittori da 16 byte. Va scritto
     * PRIMA dell'init block, perche' decide come la scheda lo legge. */
    bcr_scrivi(20, 2);

    prepara_anelli();
    prepara_init_block();

    /* L'indirizzo dell'init block, spezzato in due registri a 16 bit. */
    csr_scrivi(1, (g_dma_fis + OFF_INIT) & 0xFFFF);
    csr_scrivi(2, ((g_dma_fis + OFF_INIT) >> 16) & 0xFFFF);

    /* Nessuna maschera: tutti gli interrupt che sappiamo trattare. */
    csr_scrivi(3, 0);

    csr_scrivi(0, C0_INIT);

    /* ! SI ASPETTA IDON, E CON UNA SCADENZA. La scheda deve andare a
     * LEGGERE l'init block dalla memoria: e' il primo ciclo di bus master
     * che fa, ed e' esattamente il momento in cui si scopre se il bit
     * BUS MASTER e' stato abilitato davvero. Senza scadenza, un bit
     * dimenticato darebbe un driver fermo per sempre invece di un
     * messaggio. */
    for (giri = 0; giri < 1000; giri++) {
        if (csr_leggi(0) & C0_IDON) break;
        usleep(1000);
    }
    if ((csr_leggi(0) & C0_IDON) == 0) {
        printf("pcnet: la scheda non ha letto il blocco di inizializzazione.\n");
        printf("       E' il primo accesso che fa alla RAM: quasi sempre\n");
        printf("       significa che il bit BUS MASTER del PCI non e' acceso.\n");
        return -1;
    }

    /* IDON si azzera riscrivendolo, e si parte. */
    csr_scrivi(0, C0_IDON | C0_STRT | C0_IENA);
    return 0;
}

/* =============================================================================
 * Ricezione
 * ============================================================================= */
static void accoda(const unsigned char *f, unsigned int len)
{
    int p;

    if (len == 0) return;
    if (len > NET_FRAME_MAX) { g_cont.troppo_grandi++; return; }

    if (g_coda_conta == CODA_N) {
        /* ! SI BUTTA IL PIU' VECCHIO. Una coda piena significa che
         * nessuno sta leggendo; in quel caso il frame utile e' l'ultimo
         * arrivato, non il primo — una risposta ARP di dieci secondi fa
         * non serve piu' a nessuno. */
        g_coda_testa = (g_coda_testa + 1) % CODA_N;
        g_coda_conta--;
        g_cont.persi_coda++;
    }

    p = (g_coda_testa + g_coda_conta) % CODA_N;
    memcpy(g_coda[p], f, len);
    g_coda_len[p] = len;
    g_coda_conta++;
}

static void svuota_rx(void)
{
    int giri;

    for (giri = 0; giri < RX_N * 2; giri++) {
        volatile unsigned int *d = desc(OFF_RX_DESC + g_rx_prossimo * 16);
        unsigned int stato = d[1];
        unsigned int len;

        if (stato & D_OWN) break;       /* e' ancora della scheda: finito */

        if (stato & D_ERR) {
            g_cont.errori_rx++;
        } else {
            /* MCNT: quanti byte sono arrivati davvero, nei 12 bit bassi
             * del terzo campo. Comprende i 4 byte di FCS, che allo stack
             * non servono. */
            len = d[2] & 0x0FFF;
            if (len > 4) len -= 4;
            if (len > 0) {
                accoda(buffer_virt(g_rx_prossimo), len);
                g_cont.ricevuti++;
            }
        }

        /* Si restituisce il descrittore alla scheda. L'ordine conta: il
         * conteggio prima, OWN per ultimo — e' OWN che le dice «adesso e'
         * tuo», e metterlo per primo le permetterebbe di riempire il
         * buffer mentre stiamo ancora scrivendo la lunghezza. */
        d[2] = 0;
        d[1] = D_OWN | bcnt(BUF_LEN);

        g_rx_prossimo = (g_rx_prossimo + 1) % RX_N;
    }
}

/* =============================================================================
 * Trasmissione
 * ============================================================================= */
static int trasmetti(const unsigned char *f, unsigned int len)
{
    volatile unsigned int *d;
    unsigned char         *b;
    unsigned int           i;

    if (len == 0) return -EINVAL;
    if (len > NET_FRAME_MAX) { g_cont.troppo_grandi++; return -EINVAL; }

    d = desc(OFF_TX_DESC + g_tx_prossimo * 16);
    if (d[1] & D_OWN) {
        /* Tutti i descrittori sono in mano alla scheda: il mittente
         * riprovi. Bloccare qui vorrebbe dire un driver che non risponde
         * piu' a nessun altro messaggio. */
        return -EAGAIN;
    }

    b = buffer_virt(RX_N + g_tx_prossimo);
    memcpy(b, f, len);

    /* ! SI RIEMPIE FINO A 60 BYTE. Sotto quella soglia il frame non e'
     * valido e uno switch lo scarta: chi trasmette meno deve pareggiare, e
     * tocca al driver perche' e' un vincolo del mezzo, non del protocollo.
     * I byte di riempimento vanno azzerati e non lasciati com'erano: quel
     * che c'era prima era un altro pacchetto, e finirebbe sul cavo. */
    if (len < 60) {
        for (i = len; i < 60; i++) b[i] = 0;
        len = 60;
    }

    d[2] = 0;
    d[1] = D_OWN | D_STP | D_ENP | bcnt(len);

    /* «Guarda subito l'anello»: senza, la scheda ci arriverebbe al
     * proprio ritmo. IENA va riscritto insieme, perche' CSR0 e' un
     * registro solo. */
    csr_scrivi(0, C0_IENA | C0_TDMD);

    g_tx_prossimo = (g_tx_prossimo + 1) % TX_N;
    g_cont.inviati++;
    return (int)len;
}

static void controlla_tx(void)
{
    unsigned int i;

    for (i = 0; i < TX_N; i++) {
        volatile unsigned int *d = desc(OFF_TX_DESC + i * 16);

        if ((d[1] & D_OWN) == 0 && (d[1] & D_ERR)) {
            g_cont.errori_tx++;
            d[1] = 0;
        }
    }
}

/* =============================================================================
 * Servizio della scheda
 * ============================================================================= */
static void servi_scheda(void)
{
    unsigned int c0 = csr_leggi(0);

    if (c0 == 0xFFFF) return;           /* sparita: niente da fare */

    /* Si azzerano TUTTI i bit di stato in un colpo, tenendo IENA acceso.
     * Vedi il commento su C0_DA_AZZERARE. */
    if (c0 & C0_DA_AZZERARE)
        csr_scrivi(0, (c0 & C0_DA_AZZERARE) | C0_IENA);

    if (c0 & C0_MISS) g_cont.overflow++;

    svuota_rx();
    controlla_tx();
}

static void consegna(void)
{
    if (g_lettore_pid == 0 || g_coda_conta == 0) return;

    if (ipc_send(g_lettore_pid, NET_MSG_FRAME,
                 g_coda[g_coda_testa], g_coda_len[g_coda_testa]) == 0) {
        g_coda_testa = (g_coda_testa + 1) % CODA_N;
        g_coda_conta--;
        g_lettore_pid = 0;
    }
}

/* =============================================================================
 * Ricerca della scheda sul bus PCI
 * ============================================================================= */
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
        printf("pcnet: il servizio '%s' non e' attivo.\n", PCI_SERVIZIO);
        printf("       Avvialo con  /dev/pci.drv &\n");
        return -1;
    }

    for (n = 0; n < 16; n++) {
        PciRichiesta   r;
        PciDispositivo d;
        IpcMessage     meta;
        unsigned char  buf[IPC_MSG_MAX_DATA];
        int i, tentativi;

        r.ordinale    = n;
        r.classe      = PCI_CLASSE_RETE;
        r.sottoclasse = PCI_SOTTO_ETHERNET;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;

        for (tentativi = 0; tentativi < 8; tentativi++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
            if ((int)meta.sender_pid == pid) break;
        }
        if ((int)meta.sender_pid != pid) return -1;

        if (meta.tipo == PCI_MSG_FINE) break;
        if (meta.tipo != PCI_MSG_DISPOSITIVO || meta.len < sizeof(d)) return -1;
        memcpy(&d, buf, sizeof(d));

        /* 1022:2000 copre tutta la famiglia: Am79C970, C970A, C971, C972
         * e C973 si presentano con lo stesso identificativo e si guidano
         * allo stesso modo. Le differenze (100 Mbit, HomePNA) stanno nei
         * BCR, non nel modello di programmazione usato qui. */
        if (d.venditore != 0x1022 || d.dispositivo != 0x2000) continue;

        for (i = 0; i < 6; i++) {
            if (d.bar_io[i] && d.bar[i] != 0) {
                PciAzione a;

                g_base     = d.bar[i];
                g_irq      = d.irq_linea;
                g_bus      = d.bus;
                g_slot     = d.slot;
                g_funzione = d.funzione;

                /* ! QUI IL BUS MASTER NON E' FACOLTATIVO, e questa e' la
                 * riga che distingue questo driver dal ne2k. Senza quel
                 * bit il ponte PCI rifiuta ogni ciclo che la scheda
                 * inizia: i registri si leggono e si scrivono benissimo —
                 * quelli passano da noi — ma la scheda non riesce nemmeno
                 * a leggere il proprio blocco di inizializzazione, e il
                 * sintomo e' una scheda che sembra presente e non fa
                 * niente. */
                a.bus = d.bus; a.slot = d.slot; a.funzione = d.funzione;
                a.riservato = 0; a.offset = 0;
                a.bit = PCI_ABIL_IO | PCI_ABIL_BUSMASTER;
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
    printf("pcnet: %s\n", g_modello);
    printf("       PCI %02x:%02x.%d\n", g_bus, g_slot, g_funzione);
    printf("       porte  0x%x-0x%x\n", g_base, g_base + PCN_PORTE - 1);
    printf("       IRQ    %u\n", g_irq);
    printf("       MAC    %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("       MTU    %d\n", NET_MTU);
    printf("       DMA    %u byte a 0x%08x (fisico 0x%08x)\n",
           DMA_BYTE, g_dma_virt, g_dma_fis);
    printf("       anelli %d RX, %d TX da %d byte\n", RX_N, TX_N, BUF_LEN);
}

/* =============================================================================
 * Loop di servizio — identico nello schema a quello del ne2k, perche' il
 * protocollo e' lo stesso e le ragioni pure.
 * ============================================================================= */
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

            e.codice = -EINVAL;
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }
        }

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
    printf("uso: /dev/pcnet.drv [-i] [-p PORTA -q IRQ]\n\n");
    printf("  -i        sonda il bus, dice cosa ha trovato ed esce\n");
    printf("            (0 = trovata). Non tocca la scheda.\n");
    printf("  -l        sinonimo di -i, il nome di prima\n");
    printf("  -p PORTA  indirizzo I/O dichiarato a mano\n");
    printf("  -q IRQ    linea di interrupt dichiarata a mano\n\n");
    printf("Senza argomenti cerca la scheda sul bus PCI, si registra come\n");
    printf("servizio '%s' e resta acceso. Va lanciato con '&'.\n", NET_SERVIZIO_0);
}

/* =============================================================================
 * ! SONDARE UNA SCHEDA GIA' GUIDATA LA SPEGNE
 *
 * `-l` sembra un comando innocuo — «dimmi cosa c'e'» — e non lo e': per
 * leggere lo stato bisogna resettare la scheda e reinizializzarla, e se un
 * altro processo la sta guidando quel reset gli porta via la rete sotto i
 * piedi. Non da' un errore a nessuno dei due: da' una connessione che
 * muore mentre qualcuno guardava altrove.
 *
 * E' successo alla prima prova di questo driver, e il sintomo era
 * illeggibile: `CSR0 = 0x3b, atteso STOP`. Non era un difetto — era
 * l'altro driver che nel frattempo aveva reinizializzato la scheda.
 *
 * Se il servizio c'e' gia', `-l` lo INTERROGA invece di sondare
 * l'hardware: la risposta e' migliore (viene da chi la scheda la sta
 * usando davvero) e non rompe niente.
 * ============================================================================= */
static int stato_dal_servizio(int pid)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    NetStato      s;
    int           i;

    if (ipc_send(pid, NET_MSG_INFO, NULL, 0) < 0) return -1;

    for (i = 0; i < 8; i++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo != NET_MSG_STATO || meta.len < sizeof(s)) return -1;

        memcpy(&s, buf, sizeof(s));
        printf("pcnet: %s - gia' in servizio (PID %d)\n", s.modello, pid);
        printf("       PCI %02x:%02x\n", s.bus, s.slot);
        printf("       porte  0x%x\n", s.porta_base);
        printf("       IRQ    %u\n", s.irq);
        printf("       MAC    %02x:%02x:%02x:%02x:%02x:%02x\n",
               s.mac[0], s.mac[1], s.mac[2], s.mac[3], s.mac[4], s.mac[5]);
        printf("       MTU    %u\n", s.mtu);
        printf("\nLa scheda non e' stata toccata: e' di chi la sta gia'\n");
        printf("guidando. `nettest -c` mostra i suoi contatori.\n");
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    DmaZona z;
    int     solo_elenco = 0, sonda = 0, i, rc;

    memset(&g_cont, 0, sizeof(g_cont));

    for (i = 1; i < argc; i++) {
        /* -i e' il nome della convenzione: OGNI driver di EX-OS lo
         * accetta, sonda il proprio hardware, dice cosa ha trovato ed
         * esce con 0 se si applica a questa macchina. E' cosi' che
         * hwconfig sceglie quali driver installare senza sapere niente
         * dei singoli driver. -l resta perche' era il nome di prima e
         * sta scritto in appunti e script gia' esistenti.
         *
         * ! I DUE NON SONO SINONIMI FINO IN FONDO, e la differenza sta
         * nel blocco qui sotto. Vedi li'. */
        if (strcmp(argv[i], "-i") == 0)      { sonda = 1; solo_elenco = 1; }
        else if (strcmp(argv[i], "-l") == 0) solo_elenco = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            g_base = (unsigned int)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc)
            g_irq = (unsigned int)atoi(argv[++i]);
        else { uso(); return 1; }
    }

    /* Prima di qualunque accesso all'hardware: c'e' gia' un driver?
     *
     * ! CON -i QUESTA SCORCIATOIA NON SI PRENDE, ed e' l'unico punto in
     * cui -i e -l si comportano diversamente. Chiedere lo stato al
     * servizio di rete gia' attivo va benissimo per -l, che vuole
     * mostrare «la scheda di rete di questa macchina» e non ha motivo di
     * ripetere il lavoro di chi la sta gia' guidando.
     *
     * Per -i sarebbe la risposta a un'ALTRA domanda. -i chiede «serve
     * QUESTO driver qui?», e su una macchina con una NE2000 accesa
     * dall'autoexec la scorciatoia faceva rispondere di si' — pcnet
     * riferiva i dati della scheda del vicino come fossero suoi, e
     * l'installatore metteva sul disco un driver per una scheda AMD che
     * quella macchina non ha. La domanda su cosa c'e' sul bus va fatta
     * al bus. */
    if (!sonda) {
        int altro = ipc_lookup(NET_SERVIZIO_0);

        if (altro > 0) {
            if (solo_elenco) return stato_dal_servizio(altro) == 0 ? 0 : 1;

            printf("pcnet: il servizio '%s' e' gia' attivo (PID %d).\n",
                   NET_SERVIZIO_0, altro);
            printf("       Un secondo driver resetterebbe la scheda sotto al\n");
            printf("       primo, e la rete si fermerebbe senza dire perche'.\n");
            return 1;
        }
    }

    if (g_base == 0) {
        rc = cerca_su_pci();
        if (rc < 0) return 1;
        if (rc == 0) {
            printf("pcnet: nessuna AMD PCnet (1022:2000) sul bus PCI.\n");
            printf("       `netdetect` elenca le schede viste.\n");
            return 1;
        }
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
    if (sonda) return 0;

    rc = ioport_bind(g_base, PCN_PORTE);
    if (rc < 0) {
        printf("pcnet: ioport_bind(0x%x, %d) fallita (%d)\n",
               g_base, PCN_PORTE, rc);
        return 1;
    }

    leggi_mac();

    /* ! LA MEMORIA SI CHIEDE PRIMA DI TOCCARE LA SCHEDA. L'init block
     * contiene gli indirizzi degli anelli: senza la zona DMA non c'e'
     * niente da scrivere dentro, e una scheda inizializzata con indirizzi
     * a zero comincia a fare DMA sulla pagina zero. */
    z.byte = DMA_BYTE;
    rc = dma_alloc(&z);
    if (rc < 0) {
        printf("pcnet: dma_alloc(%u) fallita (%d)\n", DMA_BYTE, rc);
        printf("       Serve memoria fisicamente contigua: e' la risorsa\n");
        printf("       piu' scarsa del sistema, e questo e' il messaggio\n");
        printf("       che dice che e' finita.\n");
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;

    if (inizializza_scheda() != 0) return 1;

    if (solo_elenco) { stampa_stato(); return 0; }

    rc = irq_bind(g_irq);
    if (rc < 0) {
        printf("pcnet: irq_bind(%u) fallita (%d) - l'IRQ e' di qualcun altro?\n",
               g_irq, rc);
        return 1;
    }

    rc = ipc_register(NET_SERVIZIO_0);
    if (rc < 0) {
        printf("pcnet: ipc_register('%s') fallita (%d) - c'e' gia' un driver "
               "di rete?\n", NET_SERVIZIO_0, rc);
        return 1;
    }

    printf("pcnet: %s su PCI %02x:%02x.%d, IRQ %u, "
           "MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_modello, g_bus, g_slot, g_funzione, g_irq,
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("pcnet: servizio '%s' attivo\n", NET_SERVIZIO_0);

    servi();
    return 0;
}
