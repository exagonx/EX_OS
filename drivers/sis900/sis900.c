/* =============================================================================
 * drivers/sis900/sis900.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SiS 900 — la scheda di rete integrata dell'Acer Aspire 3000
 *
 * Parla il protocollo di drivers/net/net_proto.h come ne2k, pcnet ed e1000:
 * lo stack IP non sa e non deve sapere che scheda ha sotto.
 *
 * -----------------------------------------------------------------------------
 * ! LA MAPPA DEI REGISTRI E' STATA MISURATA, NON TROVATA IN UN DOCUMENTO
 *
 * Di questa scheda SiS non ha mai pubblicato le specifiche. La mappa qui sotto
 * viene dal driver Windows del portatile, letto con tools/scava.py: si cercano
 * le chiamate a WRITE_PORT_ULONG e READ_PORT_ULONG e si guarda quale offset
 * viene sommato alla base I/O prima di ognuna. L'elenco degli offset e' la
 * mappa dei registri.
 *
 * ! E IL MODO IN CUI SONO USATI LI CONFERMA. Non e' un'ipotesi da verificare
 * sul ferro: e' una misura che si controlla da sola. Il registro 0x10 esce
 * dal binario con ventisette letture e zero scritture — e' uno stato che si
 * legge e si azzera leggendolo; 0x14 e 0x18 escono con sole scritture — sono
 * maschere; la fascia 0x80-0x98 con sole letture — sono contatori. Un numero
 * sbagliato nella mappa non produrrebbe questa coerenza.
 *
 * ! QUELLO CHE SI E' PRESO SONO I FATTI, NON IL CODICE. Da scava.py esce una
 * descrizione — «il registro 0x00 e' il piu' scritto», «0x10 si legge e
 * basta» — e da quella si scrive codice proprio. La differenza fra leggere un
 * libro e fotocopiarlo. Il perche' sia legittimo farlo sta in drv_prop/leggimi.md
 * e nella Direttiva 2009/24/CE articolo 6.
 *
 * -----------------------------------------------------------------------------
 * ! IL MAC NON STA IN UNA EEPROM, SU QUESTA REVISIONE
 *
 * Sulle SiS 900 vecchie l'indirizzo si legge dalla EEPROM attraverso il
 * registro MEAR. Dalla revisione 0x82 in poi — e questo portatile ha la 0x91 —
 * il MAC sta dentro il ponte sud, e si prende mettendo il registro di filtro
 * in modo APC e leggendo tre parole. E' la sola parte di questo driver che
 * dipende dalla revisione, ed e' segnata.
 *
 * -----------------------------------------------------------------------------
 * ! COSA NON FA, DETTO SUBITO
 *
 *   - non negozia la velocita' e non tocca il PHY oltre il minimo: la
 *     negoziazione l'ha gia' fatta il PHY da solo all'accensione, e questo
 *     driver si limita a chiedergli com'e' andata.
 *   - niente scatter-gather: un frame per descrittore. La MTU e' 1500 e i
 *     buffer sono da 2048, quindi non serve.
 *   - niente multicast selettivo: o solo il proprio MAC piu' il broadcast, o
 *     tutto. Il filtro fine si aggiunge quando servira' a qualcosa.
 * ============================================================================= */

#include "libc.h"
#include "net_proto.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `sis900.drv -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("sis900.drv", "0.001");

#define SIS_VENDITORE   0x1039
#define SIS_900         0x0900
#define SIS_7016        0x7016
#define SIS_PORTE       0x100   /* quanto spazio I/O occupa */

/* =============================================================================
 * I registri
 *
 * ! GLI OFFSET VENGONO DALLA MISURA, I NOMI DALL'USO. scava.py dice che
 * esistono e come sono usati; come si chiamano lo dice il ruolo che quell'uso
 * rivela, e dove il ruolo non e' chiaro il nome dice che non lo e'.
 * ========================================================================== */
#define SIS_CR          0x00    /* comando: il piu' scritto e il piu' letto */
#define SIS_CFG         0x04    /* configurazione */
#define SIS_MEAR        0x08    /* accesso alla EEPROM, a bit */
#define SIS_PTSCR       0x0C    /* prova e stato */
#define SIS_ISR         0x10    /* stato interrupt: si legge e si azzera */
#define SIS_IMR         0x14    /* maschera interrupt: sola scrittura */
#define SIS_IER         0x18    /* abilitazione interrupt: sola scrittura */
#define SIS_TXDP        0x20    /* dove comincia l'anello di trasmissione */
#define SIS_TXCFG       0x24    /* configurazione della trasmissione */
#define SIS_RXDP        0x30    /* dove comincia l'anello di ricezione */
#define SIS_RXCFG       0x34    /* configurazione della ricezione */
#define SIS_FLCTRL      0x38    /* controllo di flusso */
#define SIS_RXFILTCTRL  0x48    /* filtro: quale registro, e acceso o spento */
#define SIS_RXFILTDATA  0x4C    /* filtro: il valore */

/* --- CR: i comandi, un bit per ciascuno ---------------------------------- */
#define CR_TXRESET      0x00000010u
#define CR_RXRESET      0x00000020u
#define CR_RESET        0x00000100u
#define CR_TXENA        0x00000001u
#define CR_TXDIS        0x00000002u
#define CR_RXENA        0x00000004u
#define CR_RXDIS        0x00000008u

/* --- ISR/IMR: cosa e' successo ------------------------------------------- */
#define IS_RXOK         0x00000001u   /* un pacchetto e' arrivato */
#define IS_RXERR        0x00000004u
#define IS_RXORN        0x00000020u   /* l'anello e' traboccato */
#define IS_TXOK         0x00000040u   /* un pacchetto e' partito */
#define IS_TXERR        0x00000100u
#define IS_TXURN        0x00000400u   /* la scheda non ha fatto in tempo */

/* --- il filtro di ricezione ---------------------------------------------- */
#define RFC_RFEN        0x80000000u   /* filtro acceso */
#define RFC_AAB         0x00000400u   /* accetta il broadcast */
#define RFC_AAM         0x00000200u   /* accetta il multicast */
#define RFC_AAP         0x00000100u   /* accetta tutto (promiscuo) */
#define RFADDR_MAC0     0x00000000u   /* i tre registri con il nostro MAC */

/* --- TXCFG e RXCFG: soglie -------------------------------------------------
 *
 * ! LA SOGLIA DI PARTENZA NON E' UN DETTAGLIO. Dice quanti byte del frame
 * devono essere nella scheda prima che cominci a metterli sul filo. Troppo
 * bassa e la scheda va in svuotamento (TXURN) su un bus occupato; troppo alta
 * e ogni pacchetto aspetta. I valori qui sono quelli che il driver del
 * costruttore usa, letti con scava.py: 64 byte di soglia, e il massimo di
 * riempimento. */
#define TXCFG_VALORE    0x00040300u   /* ATP, soglia 64, riempimento pieno */
#define RXCFG_VALORE    0x00040000u   /* soglia 64 */

/* =============================================================================
 * I descrittori
 *
 * ! LA SCHEDA LEGGE E SCRIVE IN MEMORIA DA SOLA, e questo cambia due cose. La
 * prima e' che serve memoria FISICAMENTE CONTIGUA, perche' la scheda non sa
 * niente della paginazione: la da' dma_alloc(), che rende i due indirizzi
 * separati — quello che vediamo noi e quello che vede lei. La seconda e' che
 * ogni indirizzo scritto in un descrittore dev'essere quello FISICO, e
 * confondersi produce una scheda che fa DMA su memoria di qualcun altro.
 *
 * Un descrittore sono tre parole:
 *
 *   link     l'indirizzo fisico del prossimo (l'ultimo torna al primo)
 *   cmdsts   il bit 31 dice CHI LO POSSIEDE, i bit bassi la lunghezza
 *   bufptr   l'indirizzo fisico del buffer
 * ========================================================================== */
#define D_OWN           0x80000000u   /* 1 = e' della scheda */
#define D_MORE          0x40000000u
#define D_INTR          0x20000000u
#define D_OK            0x08000000u   /* andato a buon fine */
#define D_LUNGHEZZA     0x00000FFFu

#define RX_N            8
#define TX_N            8
#define BUF_LEN         2048

/* Dentro la zona DMA: i descrittori prima, i buffer dopo. Offset tondi perche'
 * gli anelli vogliono l'allineamento a quattro byte e partire da li' lo
 * garantisce senza doverlo calcolare. */
#define OFF_RX_DESC     0x0000
#define OFF_TX_DESC     0x0100
#define OFF_BUFFER      0x1000
#define DMA_BYTE        (OFF_BUFFER + (RX_N + TX_N) * BUF_LEN)

#define PERIODO_MS      250     /* il battito: vedi ne2k.c */
#define ATTESA_PCI_MS   5000

/* =============================================================================
 * Stato
 * ========================================================================== */
static unsigned int  g_base = 0;
static unsigned int  g_irq  = 0;
static unsigned int  g_bus = 0xFFFFFFFF, g_slot = 0, g_funzione = 0;
static unsigned int  g_rev = 0;
static char          g_modello[48] = "SiS 900";
static unsigned char g_mac[6];

static unsigned int  g_dma_virt = 0;
static unsigned int  g_dma_fis  = 0;

static unsigned int  g_rx_prossimo = 0;
static unsigned int  g_tx_prossimo = 0;

static NetContatori  g_cont;

#define CODA_N          16
static unsigned char g_coda[CODA_N][NET_FRAME_MAX];
static unsigned int  g_coda_len[CODA_N];
static int           g_coda_testa = 0, g_coda_conta = 0;
static unsigned int  g_lettore_pid = 0;

/* =============================================================================
 * Accesso ai registri
 *
 * ! TUTTI A TRENTADUE BIT, e non e' un'ipotesi: il driver del costruttore usa
 * WRITE_PORT_ULONG e READ_PORT_ULONG per ognuno di questi, mai le varianti a
 * byte tranne che sul registro della EEPROM.
 * ========================================================================== */
static void reg_scrivi(unsigned int r, unsigned int v)
{
    ioport_out32(g_base + r, v);
}

static unsigned int reg_leggi(unsigned int r)
{
    unsigned int v = 0;

    ioport_in32(g_base + r, &v);
    return v;
}

/* =============================================================================
 * Gli anelli
 * ========================================================================== */
static volatile unsigned int *desc(unsigned int base, unsigned int i)
{
    return (volatile unsigned int *)(g_dma_virt + base + i * 16);
}

static unsigned char *buffer_virt(unsigned int i)
{
    return (unsigned char *)(g_dma_virt + OFF_BUFFER + i * BUF_LEN);
}

static unsigned int buffer_fisico(unsigned int i)
{
    return g_dma_fis + OFF_BUFFER + i * BUF_LEN;
}

static void prepara_anelli(void)
{
    unsigned int i;

    /* ! IL COLLEGAMENTO E' CIRCOLARE, e va scritto prima di accendere la
     * scheda. Un anello dove l'ultimo descrittore non torna al primo e' un
     * anello che finisce: la scheda arriva in fondo e si ferma, e il sintomo e'
     * una rete che funziona per otto pacchetti. */
    for (i = 0; i < RX_N; i++) {
        volatile unsigned int *d = desc(OFF_RX_DESC, i);

        d[0] = g_dma_fis + OFF_RX_DESC + ((i + 1) % RX_N) * 16;
        d[1] = D_OWN | BUF_LEN;          /* pronto, e vuoto: e' della scheda */
        d[2] = buffer_fisico(i);
        d[3] = 0;
    }

    for (i = 0; i < TX_N; i++) {
        volatile unsigned int *d = desc(OFF_TX_DESC, i);

        d[0] = g_dma_fis + OFF_TX_DESC + ((i + 1) % TX_N) * 16;
        d[1] = 0;                        /* nostro: non c'e' niente da mandare */
        d[2] = buffer_fisico(RX_N + i);
        d[3] = 0;
    }

    g_rx_prossimo = 0;
    g_tx_prossimo = 0;
}

/* =============================================================================
 * L'indirizzo MAC
 *
 * ! DALLA REVISIONE 0x82 IN POI NON STA IN UNA EEPROM. Su questo portatile la
 * revisione e' 0x91 e il MAC lo tiene il ponte sud: si mette il registro di
 * filtro in modo APC — bit 31 spento, il che vuol dire «indirizzo, non dato» —
 * e si leggono tre parole da RXFILTDATA, una per ogni coppia di byte.
 *
 * ! E SE ESCE TUTTO ZERO O TUTTO FF NON E' UN MAC. Una scheda con un indirizzo
 * cosi' non riceve niente e non se ne capisce il perche': meglio fermarsi e
 * dirlo.
 * ========================================================================== */
static int leggi_mac(void)
{
    unsigned int i, v;

    for (i = 0; i < 3; i++) {
        reg_scrivi(SIS_RXFILTCTRL, (i << 16));
        v = reg_leggi(SIS_RXFILTDATA);
        g_mac[i * 2]     = (unsigned char)(v & 0xFF);
        g_mac[i * 2 + 1] = (unsigned char)((v >> 8) & 0xFF);
    }

    v = 0;
    for (i = 0; i < 6; i++) v |= g_mac[i];
    if (v == 0) {
        printf("sis900: l'indirizzo MAC esce tutto a zero.\n");
        printf("        Su questa revisione lo tiene il ponte sud: se il\n");
        printf("        BIOS non l'ha programmato, non c'e' modo di\n");
        printf("        indovinarlo.\n");
        return -1;
    }

    v = 0xFF;
    for (i = 0; i < 6; i++) v &= g_mac[i];
    if (v == 0xFF) {
        printf("sis900: l'indirizzo MAC esce tutto a ff: la scheda non\n");
        printf("        risponde a quelle porte.\n");
        return -1;
    }

    return 0;
}

/* =============================================================================
 * Accensione
 * ========================================================================== */
static int inizializza_scheda(void)
{
    unsigned int i, v;

    /* ! IL RESET SI ASPETTA, E CON UNA SCADENZA. Il bit si azzera da solo
     * quando la scheda ha finito; se non si azzera mai, aspettare per sempre
     * vorrebbe dire un driver che non torna e una macchina che sembra bloccata
     * all'avvio. */
    reg_scrivi(SIS_CR, CR_RESET);
    for (i = 0; i < 1000; i++) {
        if ((reg_leggi(SIS_CR) & CR_RESET) == 0) break;
        usleep(1000);
    }
    if (i >= 1000) {
        printf("sis900: il reset non finisce: la scheda non risponde.\n");
        return -1;
    }

    /* Interrupt spenti mentre si prepara: un interrupt che arriva a meta'
     * dell'inizializzazione trova gli anelli non ancora collegati. */
    reg_scrivi(SIS_IER, 0);
    reg_scrivi(SIS_IMR, 0);
    (void)reg_leggi(SIS_ISR);        /* si legge per azzerarlo */

    prepara_anelli();

    reg_scrivi(SIS_TXDP, g_dma_fis + OFF_TX_DESC);
    reg_scrivi(SIS_RXDP, g_dma_fis + OFF_RX_DESC);
    reg_scrivi(SIS_TXCFG, TXCFG_VALORE);
    reg_scrivi(SIS_RXCFG, RXCFG_VALORE);

    /* ! IL FILTRO VUOLE IL NOSTRO MAC RISCRITTO. Il reset l'ha azzerato, e una
     * scheda con il filtro acceso e l'indirizzo a zero scarta tutto quello che
     * le arriva — compreso cio' che era per lei. */
    for (i = 0; i < 3; i++) {
        v = (unsigned int)g_mac[i * 2] |
            ((unsigned int)g_mac[i * 2 + 1] << 8);
        reg_scrivi(SIS_RXFILTCTRL, (i << 16));
        reg_scrivi(SIS_RXFILTDATA, v);
    }
    reg_scrivi(SIS_RXFILTCTRL, RFC_RFEN | RFC_AAB);

    /* Da qui la scheda ascolta e puo' trasmettere. */
    reg_scrivi(SIS_CR, CR_RXENA);
    reg_scrivi(SIS_IMR, IS_RXOK | IS_RXERR | IS_RXORN |
                        IS_TXOK | IS_TXERR | IS_TXURN);
    reg_scrivi(SIS_IER, 1);

    return 0;
}

/* =============================================================================
 * Ricezione
 * ========================================================================== */
static void accoda(const unsigned char *f, unsigned int len)
{
    int posto;

    if (len > NET_FRAME_MAX) { g_cont.troppo_grandi++; return; }

    if (g_coda_conta >= CODA_N) {
        /* ! SI BUTTA IL PIU' VECCHIO, NON IL PIU' NUOVO. Chi legge vuole
         * quello che sta succedendo adesso; tenere i vecchi e scartare i nuovi
         * fa una coda che invecchia e non si svuota mai. */
        g_coda_testa = (g_coda_testa + 1) % CODA_N;
        g_coda_conta--;
        g_cont.persi_coda++;
    }

    posto = (g_coda_testa + g_coda_conta) % CODA_N;
    memcpy(g_coda[posto], f, len);
    g_coda_len[posto] = len;
    g_coda_conta++;
}

static void svuota_rx(void)
{
    int giri;

    /* ! CI SI FERMA DOPO UN GIRO INTERO. Se un descrittore restasse marcato
     * nostro per un difetto, un ciclo senza limite girerebbe per sempre dentro
     * il gestore e il sistema si fermerebbe li'. */
    for (giri = 0; giri < RX_N; giri++) {
        volatile unsigned int *d = desc(OFF_RX_DESC, g_rx_prossimo);
        unsigned int stato = d[1];
        unsigned int len;

        if (stato & D_OWN) break;        /* e' ancora della scheda: finito */

        len = stato & D_LUNGHEZZA;

        /* ! LA LUNGHEZZA COMPRENDE I QUATTRO BYTE DI CONTROLLO, e vanno tolti:
         * consegnarli allo stack vorrebbe dire quattro byte di spazzatura in
         * coda a ogni pacchetto, che i livelli sopra non guardano e che
         * rompono il conto di chi lo fa. */
        if ((stato & D_OK) && len > 4) {
            len -= 4;
            if (len >= NET_FRAME_MIN || len > 0) {
                accoda(buffer_virt(g_rx_prossimo), len);
                g_cont.ricevuti++;
            }
        } else {
            g_cont.errori_rx++;
        }

        /* Il descrittore torna alla scheda, vuoto. */
        d[1] = D_OWN | BUF_LEN;
        g_rx_prossimo = (g_rx_prossimo + 1) % RX_N;
    }
}

/* =============================================================================
 * Trasmissione
 * ========================================================================== */
static int trasmetti(const unsigned char *f, unsigned int len)
{
    volatile unsigned int *d;
    unsigned char *b;

    if (len > NET_FRAME_MAX) return -EMSGSIZE;
    if (len == 0)            return -EINVAL;

    d = desc(OFF_TX_DESC, g_tx_prossimo);

    /* ! SE IL DESCRITTORE E' ANCORA DELLA SCHEDA, L'ANELLO E' PIENO. Scrivergli
     * sopra vorrebbe dire cambiare sotto il naso alla scheda un pacchetto che
     * sta trasmettendo. */
    if (d[1] & D_OWN) {
        g_cont.errori_tx++;
        return -EAGAIN;
    }

    b = buffer_virt(RX_N + g_tx_prossimo);
    memcpy(b, f, len);

    /* ! SOTTO I SESSANTA BYTE SI RIEMPIE DI ZERI. Un frame piu' corto non e'
     * valido e uno switch lo butta. Lo fa il driver perche' e' un vincolo del
     * mezzo, non di chi scrive il pacchetto — cosi' sta scritto in
     * net_proto.h. */
    if (len < NET_FRAME_MIN) {
        memset(b + len, 0, NET_FRAME_MIN - len);
        len = NET_FRAME_MIN;
    }

    /* ! IL POSSESSO SI CEDE PER ULTIMO. Scrivere D_OWN prima della lunghezza
     * vuol dire dare alla scheda un descrittore che sta ancora cambiando. */
    d[2] = buffer_fisico(RX_N + g_tx_prossimo);
    d[1] = D_OWN | (len & D_LUNGHEZZA);

    reg_scrivi(SIS_CR, CR_TXENA);

    g_tx_prossimo = (g_tx_prossimo + 1) % TX_N;
    g_cont.inviati++;
    return 0;
}

static void controlla_tx(void)
{
    unsigned int i;

    for (i = 0; i < TX_N; i++) {
        volatile unsigned int *d = desc(OFF_TX_DESC, i);
        unsigned int stato = d[1];

        if ((stato & D_OWN) || stato == 0) continue;
        if (!(stato & D_OK)) g_cont.errori_tx++;
        d[1] = 0;                        /* consumato */
    }
}

/* =============================================================================
 * Il giro di servizio della scheda
 * ========================================================================== */
static void servi_scheda(void)
{
    unsigned int isr = reg_leggi(SIS_ISR);   /* leggerlo lo azzera */

    if (isr & IS_RXORN) g_cont.overflow++;

    /* ! SI GUARDANO GLI ANELLI ANCHE SENZA INTERRUPT. Questa funzione la
     * chiama anche il battito, e su una macchina dove l'interrupt non arrivasse
     * la rete continuerebbe a funzionare — piu' lenta, e i contatori
     * direbbero perche'. */
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
 * Ricerca sul bus PCI
 * ========================================================================== */
static int cerca_su_pci(void)
{
    int pid = ipc_attendi(PCI_SERVIZIO, ATTESA_PCI_MS);
    unsigned int n;

    if (pid <= 0) {
        printf("sis900: il servizio '%s' non e' attivo.\n", PCI_SERVIZIO);
        printf("        Avvialo con  /dev/pci.drv &\n");
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

        /* ! DUE IDENTIFICATIVI, UNA SCHEDA. La stessa Ethernet si presenta
         * come 0900 da sola e come 7016 quando il ponte sud la mostra insieme
         * al proprio: si guidano allo stesso modo, e lasciarne fuori uno vuol
         * dire un driver che su certe macchine non trova niente. */
        if (d.venditore != SIS_VENDITORE) continue;
        if (d.dispositivo != SIS_900 && d.dispositivo != SIS_7016) continue;

        for (i = 0; i < 6; i++) {
            if (d.bar_io[i] && d.bar[i] != 0) {
                PciAzione a;

                g_base     = d.bar[i];
                g_irq      = d.irq_linea;
                g_bus      = d.bus;
                g_slot     = d.slot;
                g_funzione = d.funzione;
                g_rev      = d.revisione;

                snprintf(g_modello, sizeof(g_modello),
                         "SiS 900 rev %02x", g_rev);

                /* ! IL BUS MASTER NON E' FACOLTATIVO. Senza quel bit il ponte
                 * PCI rifiuta ogni ciclo che la scheda inizia: i registri si
                 * leggono e si scrivono benissimo — quelli passano da noi — ma
                 * la scheda non riesce a leggere i propri descrittori, e il
                 * sintomo e' una scheda presente che non fa niente. */
                a.bus = d.bus; a.slot = d.slot; a.funzione = d.funzione;
                a.riservato = 0; a.offset = 0;
                a.bit = PCI_ABIL_IO | PCI_ABIL_BUSMASTER;
                ipc_send(pid, PCI_MSG_ABILITA, &a, sizeof(a));
                for (tentativi = 0; tentativi < 8; tentativi++) {
                    if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0)
                        break;
                    if ((int)meta.sender_pid == pid) break;
                }
                return 1;
            }
        }
    }
    return 0;
}

static void stampa_stato(void)
{
    printf("sis900: %s\n", g_modello);
    printf("        PCI %02x:%02x.%d\n", g_bus, g_slot, g_funzione);
    printf("        porte  0x%x-0x%x\n", g_base, g_base + SIS_PORTE - 1);
    printf("        IRQ    %u\n", g_irq);
    printf("        MAC    %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("        MTU    %d\n", NET_MTU);
    printf("        DMA    %u byte a 0x%08x (fisico 0x%08x)\n",
           DMA_BYTE, g_dma_virt, g_dma_fis);
    printf("        anelli %d RX, %d TX da %d byte\n", RX_N, TX_N, BUF_LEN);
}

/* =============================================================================
 * Il ciclo di servizio — stesso schema di ne2k e pcnet, perche' il protocollo
 * e' lo stesso e le ragioni pure.
 * ========================================================================== */
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
            ipc_send(meta.sender_pid, NET_MSG_CONTEGGI,
                     &g_cont, sizeof(g_cont));
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
 * ========================================================================== */
static void uso(void)
{
    printf("uso: /dev/sis900.drv [-i] [-l]\n\n");
    printf("  -i   sonda il bus, dice cosa ha trovato ed esce (0 = trovata).\n");
    printf("       Non tocca la scheda.\n");
    printf("  -l   accende la scheda, stampa lo stato ed esce\n\n");
    printf("Senza argomenti cerca la SiS 900 (1039:0900) sul bus PCI, si\n");
    printf("registra come servizio '%s' e resta acceso. Va lanciato\n",
           NET_SERVIZIO_0);
    printf("con '&'.\n\n");
    printf("! LA MAPPA DEI REGISTRI E' MISURATA, non presa da un documento:\n");
    printf("  SiS non ne ha mai pubblicato uno. Viene dal driver del\n");
    printf("  costruttore, letto con tools/scava.py.\n");
}

int main(int argc, char **argv)
{
    int sonda = 0, solo_elenco = 0, i, rc;
    DmaZona z;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) { sonda = 1; continue; }
        if (strcmp(argv[i], "-l") == 0) { solo_elenco = 1; continue; }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uso();
            return 0;
        }
        printf("sis900: non conosco '%s'. Prova -h.\n", argv[i]);
        return 1;
    }

    rc = cerca_su_pci();
    if (rc < 0) return 1;
    if (rc == 0) {
        printf("sis900: nessuna SiS 900 (1039:0900 o 7016) sul bus PCI.\n");
        printf("        `netdetect` elenca le schede viste.\n");
        return 1;
    }

    /* ! CON -i SI SMETTE QUI, E LA SCHEDA NON SI TOCCA. La sonda di
     * `hwconfig -d` gira su un sistema acceso, dove il driver giusto puo'
     * essere gia' in funzione: resettare la scheda sotto a chi la sta
     * guidando ferma la rete. Leggere dal bus e' invece sempre sicuro — e'
     * una domanda a /dev/pci.drv, non un accesso alla periferica. */
    if (sonda) {
        printf("sis900: trovata a %02x:%02x.%d, porte 0x%x, IRQ %u, rev %02x\n",
               g_bus, g_slot, g_funzione, g_base, g_irq, g_rev);
        return 0;
    }

    rc = ioport_bind(g_base, SIS_PORTE);
    if (rc < 0) {
        printf("sis900: ioport_bind(0x%x, %d) fallita (%d)\n",
               g_base, SIS_PORTE, rc);
        return 1;
    }

    if (leggi_mac() != 0) return 1;

    /* ! LA MEMORIA SI CHIEDE PRIMA DI TOCCARE LA SCHEDA. I descrittori
     * contengono indirizzi fisici: senza la zona DMA non c'e' niente da
     * scriverci dentro, e una scheda accesa con puntatori a zero comincia a
     * fare DMA sulla pagina zero. */
    z.byte = DMA_BYTE;
    rc = dma_alloc(&z);
    if (rc < 0) {
        printf("sis900: dma_alloc(%u) fallita (%d)\n", DMA_BYTE, rc);
        printf("        Serve memoria fisicamente contigua: e' la risorsa\n");
        printf("        piu' scarsa del sistema, e questo e' il messaggio\n");
        printf("        che dice che e' finita.\n");
        return 1;
    }
    g_dma_virt = z.virt;
    g_dma_fis  = z.fisico;

    if (inizializza_scheda() != 0) return 1;

    if (solo_elenco) { stampa_stato(); return 0; }

    rc = irq_bind(g_irq);
    if (rc < 0) {
        printf("sis900: irq_bind(%u) fallita (%d) - l'IRQ e' di qualcun "
               "altro?\n", g_irq, rc);
        return 1;
    }

    rc = ipc_register(NET_SERVIZIO_0);
    if (rc < 0) {
        printf("sis900: ipc_register('%s') fallita (%d) - c'e' gia' un "
               "driver di rete?\n", NET_SERVIZIO_0, rc);
        return 1;
    }

    printf("sis900: %s su PCI %02x:%02x.%d, IRQ %u, "
           "MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_modello, g_bus, g_slot, g_funzione, g_irq,
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("sis900: servizio '%s' attivo\n", NET_SERVIZIO_0);

    servi();
    return 0;
}
