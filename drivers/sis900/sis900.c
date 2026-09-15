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
 * Sulle SiS 900 vecchie l'indirizzo si legge dalla EEPROM. Dalla revisione
 * 0x82 in poi — e questo portatile ha la 0x91 — ce lo mette il BIOS nei tre
 * registri del filtro, e si prende da li': e' quel che fa il driver del
 * costruttore su tutte le revisioni tranne la 0x81.
 *
 * ! MA LA EEPROM C'E' LO STESSO, e il 14 settembre 2026 e' tornata a essere il
 * ripiego di quando il filtro esce a zero. Non era sparita: era in comune con
 * il controller 1394 e andava chiesto il turno. Vedi mac_dalla_eeprom().
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
EX_VERSIONE("sis900.drv", "0.017");

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
/* ! 0x08 E' LA EEPROM *E* IL PHY: e' un registro solo con due gruppi di bit, e
 * i bit della EEPROM stanno accanto a quelli del filo di gestione senza
 * toccarsi. Il dettaglio, e il perche' averne visto meta' e' costato il MAC
 * cancellato del 10 settembre 2026, sta sopra eeprom_parola().
 *
 * Quel che segue resta vero ed e' la meta' che serve qui. La prima versione di
 * questo file lo chiamava MEAR e diceva «accesso alla EEPROM», e basta:
 * incompleto, e scoperto
 * leggendo il driver del costruttore. Quel che ci passa sopra e' MDIO —
 * trentadue bit di preambolo a uno, uno di start, quattordici di comando,
 * il giro, sedici di dato — cioe' il protocollo con cui si parla al chip
 * che tiene il collegamento Ethernet. Una linea sola per il dato: 0x10 e' il
 * dato, 0x20 dice chi la pilota, 0x40 e' il clock. */
#define SIS_MII         0x08    /* il PHY (MDIO) e la EEPROM, a bit */
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

/* ! IL RESET NON SI VERIFICA SU CR, SI VERIFICA QUI. I due bit in alto di ISR
 * si accendono quando il motore di trasmissione e quello di ricezione hanno
 * finito di azzerarsi, e sono uno per motore perche' i due finiscono in
 * momenti diversi. Vedi inizializza_scheda(). */
#define IS_TXRCMP       0x02000000u   /* il motore TX ha finito il reset */
#define IS_RXRCMP       0x01000000u   /* il motore RX ha finito il reset */

/* --- CFG: cosa va rimesso dopo il reset ------------------------------------
 *
 * ! IL RESET LASCIA CFG COME PARE A LUI, E VA RISCRITTO. Sono due bit, e su
 * questa revisione servono tutti e due:
 *
 *   PESEL    dice al motore DMA come si comporta sul bus PCI; va acceso su
 *            ogni revisione, ed e' l'unico che le vecchie vogliono.
 *   RND_CNT  il ritardo casuale prima di ritentare dopo una collisione. Dalla
 *            635A in su la scheda lo vuole acceso; senza, in half duplex due
 *            schede che si scontrano ritentano insieme all'infinito.
 *
 * La nostra e' la 0x91, che sta oltre la 635A: le vuole entrambe. */
#define CFG_PESEL       0x00000008u
#define CFG_RND_CNT     0x00000400u

#define REV_SIS900B     0x03u
#define REV_SIS635A     0x90u

/* --- il filtro di ricezione ------------------------------------------------
 *
 * ! I QUATTRO BIT STANNO IN ALTO, E QUESTA E' LA CORREZIONE CHE E' COSTATA UN
 * INDIRIZZO NON ASSEGNATO. La prima versione di questo file li metteva a
 * 0x400, 0x200, 0x100 — numeri inventati, che nessuno aveva verificato.
 *
 * Il valore che il driver del costruttore scrive in questo registro e'
 * 0x80000848: il bit 31 e' l'abilitazione, e gli altri tre stanno accanto a
 * quello, non in fondo. Sono i bit 30, 29 e 28.
 *
 * ! E IL BIT DEL BROADCAST E' QUELLO CHE MANCAVA DAVVERO. Un DHCP comincia
 * mandando un DISCOVER a tutti, e la risposta del server arriva spesso in
 * broadcast — deve, perche' a quel punto non abbiamo ancora un indirizzo a
 * cui essere raggiunti. Con il bit sbagliato la scheda scarta proprio quella
 * risposta: il pacchetto esce, il server risponde, e noi non lo vediamo mai.
 * E' esattamente cio' che si e' visto sull'Acer. */
#define RFC_RFEN        0x80000000u   /* filtro acceso */
#define RFC_AAB         0x40000000u   /* accetta il broadcast */
#define RFC_AAM         0x20000000u   /* accetta il multicast */
#define RFC_AAP         0x10000000u   /* accetta tutto (promiscuo) */

/* ! RELOAD C'E' MA NON SI USA, e resta scritto perche' il giorno che qualcuno
 * ripasserà di qui sappia che e' stato provato e cosa e' successo. Dice alla
 * scheda di ricaricarsi l'indirizzo dalla EEPROM; su questa scheda una EEPROM
 * non c'e', e l'effetto e' sei zeri scritti sopra il MAC che il BIOS aveva
 * messo nei registri del filtro. Vedi leggi_mac(). */
#define CR_RELOAD       0x00000400u   /* NON usato: guasta il MAC su questa */

/* --- TXCFG e RXCFG: i campi, e da dove si sanno ----------------------------
 *
 * ! I PRIMI VALORI ERANO INVENTATI, e stava scritto che erano misurati. Lo
 * dicevano due costanti — 0x00040300 e 0x00040000 — con accanto un commento
 * che le attribuiva a una lettura di scava.py mai fatta. Questa e' la
 * correzione, e stavolta la lettura c'e'.
 *
 * Il driver del costruttore non SCRIVE questi due registri: li modifica. Legge
 * il valore, spegne i bit di una maschera e rimette dentro i suoi:
 *
 *     eax = READ(0x24);  eax &= ~0xc070007f;  eax |= nuovi;  WRITE(0x24, eax)
 *
 * La maschera dice quali campi contano davvero:
 *
 *     bit 31, 30   full duplex: ignora le collisioni e il battito di prova
 *     bit 22..20   quanto grande e' una raffica DMA
 *     bit  6..0    la soglia di partenza, in unita' da 32 byte
 *
 * ! E LEGGERE-MODIFICARE-SCRIVERE NON E' PIGNOLERIA: gli altri bit li ha messi
 * il reset della scheda, e sono giusti. Scrivere il registro intero vuol dire
 * azzerare campi che non si conoscono, e sono proprio quelli di cui non si sa
 * abbastanza per rimetterli. */
/* ! LA MASCHERA COPRE TUTTO QUEL CHE SCRIVIAMO, e prima no. Ci sono dentro
 * adesso anche TxATP (bit 28) e TxFILLT (bit 13..8): un campo che si scrive
 * senza averlo prima azzerato sotto la maschera si somma a quel che c'era. */
#define TX_MASCHERA     0xD0703F7Fu
#define RX_MASCHERA     0x1070007Fu

#define TX_FULLDUPLEX   0xC0000000u   /* bit 31, 30 */
#define RX_FULLDUPLEX   0x10000000u   /* accetta anche cio' che mandiamo noi */

/* --- La raffica DMA, e perche' non e' una preferenza -----------------------
 *
 * ! LA RAFFICA DA 512 SU UNA SCHEDA IN MODO EDB LA TIENE FERMA. E' il difetto
 * trovato il 14 settembre 2026 con il secondo referto dell'Acer, ed e' quello
 * che spiega tutto il resto.
 *
 * La prima versione diceva: «0 vuol dire 512 byte, il massimo. Su un portatile
 * del 2004 il bus non e' conteso da nessun altro». Il ragionamento sulla
 * contesa era giusto e la conclusione sbagliata, perche' la raffica qui non e'
 * una scelta di prestazioni: e' un vincolo del chip.
 *
 * Il bit 13 di CFG — EDB_MASTER_EN — dice che la scheda lavora con i buffer
 * di descrittori migliorati, e in quel modo la raffica DEVE essere da 64
 * byte. Linux non ne fa una questione di gusto, ci mette un se':
 *
 *     if (sr32(cfg) & EDB_MASTER_EN) {
 *         tx_flags = TxATP | (DMA_BURST_64  << TxMXDMA_shift) | ...
 *         rx_flags =         (DMA_BURST_64  << RxMXDMA_shift);
 *     } else {
 *         tx_flags = TxATP | (DMA_BURST_512 << TxMXDMA_shift) | ...
 *         rx_flags =         (DMA_BURST_512 << RxMXDMA_shift);
 *     }
 *
 * ! E IL SINTOMO NON E' «VA PIANO», E' «NON PARTE». Sull'Acer CFG valeva
 * 0x2408, cioe' EDB acceso, e noi chiedevamo 512: la scheda non ha mai
 * iniziato un ciclo sul bus. Quattro descrittori consegnati in trasmissione e
 * mai restituiti, seicentosettantasei traboccamenti con l'anello di ricezione
 * intatto, e il registro di stato del PCI PULITO — nessun master abort,
 * nessun target abort. Non veniva respinta: non ci provava.
 *
 * ! IL NUMERO NON E' LA DIMENSIONE, E' UN CODICE. 0 vuol dire 512 byte e 5
 * vuol dire 64: leggerlo come una quantita' e alzarlo «per andare piu' forte»
 * vuol dire chiedere 512. */
#define DMA_RAFFICA_512  0u
#define DMA_RAFFICA_64   5u
#define MXDMA_SHIFT      20

/* --- gli altri campi che Linux scrive, e che noi lasciavamo al reset -------
 *
 * ! IL RESET NON E' UNO CHE HA SCELTO. Il commento sopra TX_MASCHERA dice che
 * gli altri bit «li ha messi il reset della scheda, e sono giusti», e come
 * principio regge; ma qui si portava dietro un TxFILLT da 1 e TxATP spento,
 * che nessuno aveva deciso — erano quel che restava. Linux li scrive tutti e
 * due a ogni cambio di modo, e su questo chip Linux funziona. */
#define TX_ATP           0x10000000u   /* riempimento automatico in TX */
#define TX_FILLT_SHIFT   8
#define TX_FILL_THRESH   16u           /* 1/16 del buffer */
/* ! LE DUE SOGLIE NON SONO LO STESSO NUMERO, e per un referto sono sembrate
 * uguali per caso. Linux tiene TxDRNTH a 48 — tre quarti del buffer di
 * trasmissione — e RxDRNTH a 16, un quarto di quello di ricezione: sono due
 * buffer diversi e due frazioni diverse.
 *
 * Noi scrivevamo 48 in tutti e due. In ricezione il campo e' piu' stretto di
 * quanto credevamo e la scheda ha TRONCATO 48 a 16, che e' per l'appunto il
 * valore giusto: il registro rileggeva 0x20 invece dello 0x60 che ci
 * aspettavamo. Il risultato era corretto per sbaglio, e un valore corretto
 * per sbaglio e' un valore che il prossimo cambio rompe senza dire niente.
 *
 * A 10 Mbit Linux usa un quarto di ciascuna. */
#define TX_DRNT_100      48u
#define TX_DRNT_10       12u
#define RX_DRNT_100      16u
#define RX_DRNT_10        4u
#define RX_DRNT_SHIFT    1             /* in RX il campo parte dal bit 1 */

/* La soglia di partenza, in unita' da 32 byte. A 100 Mbit si aspetta quasi
 * tutto il frame (1536/32 = 48); a 10 Mbit basta molto meno, perche' il filo
 * e' dieci volte piu' lento della memoria. */
/* ! SOGLIA_100 e SOGLIA_10 SONO DIVENTATE DRNT_100 e DRNT_10, e il valore a
 * 10 Mbit e' passato da 16 a 12: Linux usa un quarto di quello a 100
 * (48 >> 2), e 16 era un numero scelto a occhio. */
#define CFG_EDB_MASTER  0x00002000u   /* bit 13: i buffer migliorati */

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
/* ! D_OWN NON VUOL DIRE LA STESSA COSA DALLE DUE PARTI, e questa e' la
 * correzione del 14 settembre 2026: e' il difetto che teneva ferma la
 * ricezione da quando il driver esiste.
 *
 * Il bit 31 non dice «e' della scheda». Dice: IL BUFFER CONTIENE DATI BUONI —
 * e chi lo accende e' chi quei dati ce li ha messi.
 *
 *   in TRASMISSIONE  lo accendiamo noi: il frame e' scritto, portalo fuori.
 *                    La scheda lo spegne quando l'ha trasmesso.
 *
 *   in RICEZIONE     lo accende LEI: dentro c'e' un pacchetto arrivato.
 *                    Lo spegniamo noi quando l'abbiamo consumato, ed e' cosi'
 *                    che le ridiamo il descrittore.
 *
 * ! IL SENSO SBAGLIATO NON DA' UN ERRORE, DA' IL SILENZIO. Un anello di
 * ricezione preparato con D_OWN acceso e' un anello che alla scheda risulta
 * TUTTO PIENO DI ROBA NON ANCORA LETTA: non ha un posto dove scrivere, e non
 * scrive. Dall'altra parte svuota_rx usciva al primo giro tutte le volte. I
 * pacchetti uscivano, le risposte arrivavano sul filo, e i contatori dicevano
 * zero ricevuti e overflow che salivano — che e' esattamente il referto
 * dell'Acer.
 *
 * Si controlla: in Linux sis900_init_rx_ring scrive `cmdsts = RX_BUF_SIZE`,
 * senza OWN, e sis900_rx cicla `while (rx_status & OWN)`. La stessa asimmetria
 * sta nel driver natsemi, perche' il disegno dei descrittori e' quello. */
#define D_OWN           0x80000000u   /* dati buoni: TX li mettiamo noi,
                                         RX li mette lei */
#define D_MORE          0x40000000u
#define D_INTR          0x20000000u
#define D_OK            0x08000000u   /* andato a buon fine */
#define D_LUNGHEZZA     0x00000FFFu

/* --- I bit di errore del descrittore di RICEZIONE --------------------------
 *
 * ! IN RICEZIONE NON SI GUARDA D_OK, SI GUARDANO GLI ERRORI, ed e' la
 * correzione del 14 settembre 2026 trovata col terzo referto dell'Acer. Il
 * driver diceva:
 *
 *     if ((stato & D_OK) && len > 4) { ...buono... } else { errore }
 *
 * cioe' trattava «buono» come un bit da trovare acceso. Linux fa il
 * contrario: prende il pacchetto a meno che non ci sia acceso uno degli
 * errori qui sotto, e D_OK in ricezione non lo guarda affatto.
 *
 *     if (rx_status & (ABORT|OVERRUN|TOOLONG|RUNT|RXISERR|CRCERR|FAERR))
 *             ...pacchetto corrotto...
 *     else    ...pacchetto buono...
 *
 * ! E LA DIFFERENZA NON E' DI STILE: sono due domande diverse a un chip di
 * cui non abbiamo le specifiche. «C'e' scritto che e' buono?» e «c'e' scritto
 * che e' rotto?» danno la stessa risposta solo se il chip compila sempre
 * tutti e due i gruppi di bit, e questo non lo sappiamo. Sul referto S3 otto
 * descrittori sono stati riempiti dalla scheda e tutti e otto contati come
 * errore, con ricevuti a zero.
 *
 * ! I BIT 24 E 23 NON SONO ERRORI, SONO IL DESTINATARIO: 23 da solo vuol dire
 * «indirizzato a noi», 24 multicast, tutti e due broadcast. Un DHCP arriva in
 * broadcast, quindi con quei due accesi — e metterli fra gli errori vorrebbe
 * dire buttare proprio le risposte che si stanno aspettando. */
#define D_OVERRUN       0x02000000u
#define D_TOOLONG       0x00400000u
#define D_RUNT          0x00200000u
#define D_RXISERR       0x00100000u
#define D_CRCERR        0x00080000u
#define D_FAERR         0x00040000u
#define D_ABORT         0x04000000u

#define D_RX_ERRORI     (D_ABORT | D_OVERRUN | D_TOOLONG | D_RUNT | \
                         D_RXISERR | D_CRCERR | D_FAERR)

/* Il destinatario, per il referto: non sono errori. */
#define D_DEST          0x00800000u   /* era per noi */
#define D_MCAST         0x01000000u
#define D_BCAST         0x01800000u   /* 24 e 23 insieme */

#define RX_N            8
#define TX_N            8
#define BUF_LEN         2048    /* quanto e' distanziato un buffer dall'altro */

/* ! QUANTO SPAZIO SI DICHIARA ALLA SCHEDA NON E' QUANTO SE NE E' RISERVATO, e
 * questa e' la correzione del 14 settembre 2026 — il difetto che teneva ferma
 * la ricezione anche dopo che tutto il resto funzionava.
 *
 * Il driver scriveva nel descrittore BUF_LEN, cioe' 2048, che e' 0x800. E il
 * quarto referto dell'Acer ha mostrato cosa ne faceva la scheda:
 *
 *     cmdsts 0xd0000000  len 0  - nessun errore: lo ha buttato la LUNGHEZZA
 *
 * cioe' OWN | MORE | INCCRC con lunghezza ZERO, quattromilaotto volte di
 * fila, e tutti e otto i descrittori sempre pieni. MORE acceso con lunghezza
 * zero vuol dire una cosa sola: «il pacchetto non ci sta, continua nel
 * prossimo». LA SCHEDA LEGGEVA CHE QUEL BUFFER ERA GRANDE ZERO, quindi
 * chiudeva il descrittore all'istante senza scriverci niente, e ricominciava.
 *
 * ! 0x800 E' IL PRIMO VALORE CHE CADE FUORI DAL CAMPO. L'intestazione di
 * Linux dichiara DSIZE come dodici bit (0x00000FFF), ma su questo chip il
 * campo e' piu' stretto: scrivendo 0x800 il bit non entra e alla scheda
 * arriva zero. Non e' un'ipotesi isolata — nello stesso referto RXCFG aveva
 * gia' troncato la soglia di svuotamento da 48 a 16 senza dire niente. Le
 * maschere di Linux sono piu' larghe dei campi veri, e i valori che Linux
 * usa stanno tutti dentro.
 *
 * ! E LINUX INFATTI NON SCRIVE MAI 2048: usa RX_BUF_SIZE, che vale 1536.
 * Basta e avanza — il frame piu' lungo e' 1514 piu' quattro di controllo,
 * cioe' 1518 — e sta comodamente dentro il campo.
 *
 * ! I DUE NUMERI RESTANO SEPARATI APPOSTA. BUF_LEN e' quanto dista un buffer
 * dal successivo in memoria, e conviene tenerlo tondo; RX_DICHIARATO e' quel
 * che si promette alla scheda. Confonderli e' stato il difetto. */
#define RX_DICHIARATO   1536

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

/* ! IL REGISTRO COMANDO DEL PCI, COME L'HA RILETTO pci.drv. Fino al 14
 * settembre 2026 questo numero si buttava via: cerca_su_pci() mandava
 * PCI_MSG_ABILITA, aspettava la risposta e non la guardava.
 *
 * ! ED E' IL NUMERO CHE DICE SE LA SCHEDA PUO' TOCCARE LA MEMORIA. Senza il
 * bit di bus master il ponte PCI rifiuta ogni ciclo che la scheda inizia: i
 * registri si leggono e si scrivono benissimo — quelli passano da noi, sono
 * cicli che iniziamo NOI — ma lei non riesce ne' a leggersi i descrittori ne'
 * a scriverci dentro i pacchetti. Il sintomo e' una scheda che risponde a
 * tutto e non fa niente.
 *
 * pci.drv quel registro lo rilegge apposta dopo averlo scritto, e lo rende
 * in PciEsito.comando, «perche' alcuni bit sono di sola lettura su certi
 * dispositivi, e dire acceso quando il bit non si e' alzato manderebbe il
 * driver a cercare un guasto dove non c'e'». Era esattamente quel che
 * stavamo facendo. */
static unsigned int  g_pci_comando = 0;
static int           g_pci_esito   = -1;   /* <0 = non l'abbiamo saputo */
static unsigned int  g_pci_stato   = 0;    /* registro di stato, riletto */
static unsigned int  g_raffica     = 0;    /* il codice della raffica DMA */

#define PCI_CMD_IO        0x0001
#define PCI_CMD_MEMORIA   0x0002
#define PCI_CMD_BUSMASTER 0x0004
static char          g_modello[48] = "SiS 900";
static unsigned char g_mac[6];

static unsigned int  g_dma_virt = 0;
static unsigned int  g_dma_fis  = 0;

static unsigned int  g_rx_prossimo = 0;
static unsigned int  g_tx_prossimo = 0;

/* =============================================================================
 * Gli ultimi descrittori rifiutati
 *
 * ! UN CONTATORE DICE QUANTI, NON PERCHE'. Il referto S3 dell'Acer diceva
 * «errori RX 8» e li' si fermava: otto descrittori riempiti dalla scheda e
 * scartati dal driver, senza un modo di sapere cosa ci fosse scritto dentro.
 * La causa si poteva solo indovinare, e indovinare costa un viaggio.
 *
 * Qui si tengono gli ultimi otto cmdsts rifiutati, grezzi. Otto parole di
 * memoria contro un viaggio all'Acer: e' il cambio piu' conveniente di tutto
 * questo file.
 * ========================================================================== */
#define RIFIUTATI_N 8
static unsigned int g_rifiutati[RIFIUTATI_N];
static unsigned int g_rifiutati_n = 0;

static void ricorda_rifiutato(unsigned int stato)
{
    g_rifiutati[g_rifiutati_n % RIFIUTATI_N] = stato;
    g_rifiutati_n++;
}

static NetContatori  g_cont;
static int           g_promiscuo = 0;  /* -promiscuo: accetta tutto */
static int           g_mac_dato  = 0;  /* -mac: ce l'ha detto chi lancia */
static int           g_phy = -1;       /* dove risponde il PHY */
static int           g_veloce = 1;     /* 100 Mbit */
static int           g_duplex = 1;     /* full duplex */

/* ! SI PUO' SCAVALCARE QUEL CHE DICE IL PHY, e serve quando il PHY non si
 * riesce a leggere. -1 vuol dire «decidi tu», 0 e 1 vogliono dire «e' cosi'
 * e basta».
 *
 * ! IL MOTIVO E' IL DISACCORDO SUL DUPLEX, che e' il guasto piu' insidioso di
 * una rete a cavo: due estremi che non sono d'accordo su quante direzioni
 * abbia il filo. Chi si crede full duplex trasmette quando gli pare; chi si
 * crede half duplex, mentre riceve, sente quella trasmissione e la chiama
 * collisione — butta il pacchetto che stava ricevendo e ritrasmette il
 * proprio. Il risultato e' una rete che lascia passare i pacchetti piccoli e
 * isolati e perde tutto il resto, cioe' «esce e non torna niente» senza
 * nessun errore da nessuna parte.
 *
 * E quando il filo di gestione non risponde il driver tira a indovinare 100
 * full duplex, che e' proprio l'ipotesi che produce il disaccordo: uno
 * switch che negozia con qualcuno che non risponde ripiega su HALF duplex.
 * Con `-half` si prova l'altra strada in un comando invece che in un
 * viaggio. */
static int           g_forza_duplex = -1;
static int           g_forza_veloce = -1;

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
 * IL PHY, con il protocollo letto dal binario del costruttore
 *
 * ! IL COLLEGAMENTO E' LA PRIMA COSA DA CHIEDERE, e per dieci giorni non
 * l'abbiamo chiesta. «Il DHCP non riceve offerte» ha tre spiegazioni, e la
 * prima e' che il filo non porti niente: se il PHY non ha agganciato, non
 * esiste nessun problema di anelli, di filtri o di indirizzi da cercare.
 *
 * ! IL PROTOCOLLO NON E' STATO INDOVINATO. Sta nel driver del costruttore, in
 * tre funzioni che tools/scava.py ha portato a galla:
 *
 *   manda un bit:   esi = (bit ? 0x10 : 0) + 0x20      noi pilotiamo la linea
 *                   WRITE(0x08, esi)                    clock basso
 *                   WRITE(0x08, esi | 0x40)             clock alto
 *
 *   legge un bit:   WRITE(0x08, 0x00)                   molliamo la linea
 *                   WRITE(0x08, 0x40)                   clock alto
 *                   bit = READ(0x08) & 0x10             risponde il PHY
 *
 *   un giro:        32 bit a uno (preambolo), 1 di start, 14 di comando,
 *                   il giro, 16 di dato
 *
 * Il dato viaggia su UNA linea sola: 0x10 e' il dato, 0x20 dice chi la
 * pilota, 0x40 e' il clock. Per questo leggere vuol dire spegnere 0x20 —
 * altrimenti si sta parlando sopra a chi risponde.
 * ========================================================================== */
#define MII_DATO     0x10u
#define MII_PILOTO   0x20u
#define MII_CLOCK    0x40u

static void mii_manda(unsigned int valore, int quanti)
{
    unsigned int maschera = 1u << (quanti - 1);

    while (quanti--) {
        unsigned int v = ((valore & maschera) ? MII_DATO : 0) | MII_PILOTO;

        reg_scrivi(SIS_MII, v);
        reg_scrivi(SIS_MII, v | MII_CLOCK);
        maschera >>= 1;
    }
}

static unsigned int mii_legge(int quanti)
{
    unsigned int fuori = 0, maschera = 1u << (quanti - 1);

    while (quanti--) {
        reg_scrivi(SIS_MII, 0);
        reg_scrivi(SIS_MII, MII_CLOCK);
        if (reg_leggi(SIS_MII) & MII_DATO) fuori |= maschera;
        maschera >>= 1;
    }
    return fuori;
}

/* --- I registri standard del PHY, uguali su ogni chip dal 1995 ----------- */
#define MII_BMCR     0x00   /* comando */
#define MII_BMSR     0x01   /* stato */
#define MII_ID1      0x02
#define MII_ID2      0x03
#define MII_ANAR     0x04   /* cosa offriamo noi */
#define MII_ANLPAR   0x05   /* cosa offre chi sta dall'altra parte */

#define BMSR_LINK    0x0004u  /* il collegamento c'e' */
#define BMSR_ANEG_OK 0x0020u  /* la trattativa e' finita */

/* Un registro del PHY. `phy` e' il suo indirizzo sul filo di gestione, di
 * solito 1; `reg` quale registro. */
static unsigned int mii_leggi_reg(unsigned int phy, unsigned int reg)
{
    unsigned int v;

    /* ! IL PREAMBOLO SONO TRENTADUE UNO, e non e' cerimonia: e' come il PHY
     * capisce che qualcuno sta per parlargli, e senza si perde il primo
     * comando dopo un periodo di silenzio. */
    mii_manda(0xFFFFFFFFu, 32);
    mii_manda(1, 1);                                  /* start */
    mii_manda(0x1800u | (phy << 5) | reg, 14);        /* 10 = leggi */

    /* ! UN COLPO SOLO QUI, E NON E' UN GIRO CORTO: SEMBRA, E NON LO E'.
     * Il giro MDIO dura DUE tempi di bit, e qui se ne consuma uno: la prima
     * volta che si guarda questa riga sembra il classico errore di un fronte,
     * e il 14 settembre 2026 e' stata cambiata in mii_legge(2) — sbagliando, e
     * rimessa com'era. Il conto e' scritto qui sotto perche' non si rifaccia.
     *
     * ! QUEL CHE CONTA NON E' QUANTI FRONTI, E' CHI FA IL FRONTE. Il ciclo di
     * lettura di Linux campiona nella fase BASSA, cioe' PRIMA di alzare il
     * clock, e arriva al ciclo dei dati con il clock gia' alto:
     *
     *     scrive 0 (scende)  ->  legge  ->  scrive MDC (sale)
     *
     * mii_legge() qui il fronte se lo fa da solo e campiona nella fase ALTA:
     *
     *     scrive 0 (scende)  ->  scrive MDC (sale)  ->  legge
     *
     * Sono due modi di prendere LO STESSO BIT: quello che il PHY mette fuori
     * in risposta al fronte N. Linux lo legge nella fase bassa dopo il fronte
     * N, noi nella fase alta subito dopo — e il PHY ha 300 nanosecondi per
     * presentarlo, mentre fra due accessi a una porta PCI ne passa piu' di
     * mille.
     *
     * Da qui il conto dei fronti, ed e' pari:
     *
     *     Linux   32 (preambolo) + 1 (idle) + 16 (comando: 14 veri + 2 di
     *             giro pilotati dal MAC) = 49, e il primo bit di dato e'
     *             quello del fronte 49.
     *     qui     32 + 1 + 14 = 47 fronti, poi mii_legge(1) fa il 48, e
     *             mii_legge(16) fa i fronti 49..64: il primo bit di dato e'
     *             di nuovo quello del fronte 49.
     *
     * Con mii_legge(2) i dati slitterebbero ai fronti 50..65: si perderebbe
     * il bit piu' significativo e si prenderebbe spazzatura in coda. Cioe'
     * esattamente il guasto che quella modifica voleva curare, prodotto da
     * lei.
     *
     * ! E SI CONTROLLA IN UN COLPO SOLO, sul ferro: il bit 0 di BMSR
     * (Extended Capability) vale 1 su ogni PHY vero. Se `-phy` lo mostra a
     * zero, i dati sono slittati; se vale 1, questa riga e' a posto e il
     * guasto sta altrove. */
    (void)mii_legge(1);                               /* il giro */
    v = mii_legge(16);
    (void)mii_legge(1);

    reg_scrivi(SIS_MII, 0);                           /* si molla la linea */
    return v;
}

/* Dove sta il PHY.
 *
 * ! SI CHIEDE LO STATO, NON L'IDENTIFICATIVO, e questa e' la correzione del
 * 11 settembre 2026. La prima versione leggeva il registro 2 — l'identificativo
 * del costruttore — e scartava chi rendeva 0000. Sull'Acer non trovava
 * nessuno: i PHY integrati nei chip SiS quel registro spesso non lo
 * compilano, perche' non sono un chip di qualcun altro attaccato al filo,
 * sono un pezzo della scheda stessa.
 *
 * Il registro 1 invece lo compilano tutti, ed e' obbligatorio dalla specifica
 * 802.3: dice cosa il PHY sa fare, e il suo bit 0 vale sempre 1 su un PHY
 * vero.
 *
 * ! NE' TUTTO ZERO NE' TUTTO UNO SONO UNA RISPOSTA. Il primo vuol dire linea
 * bassa, il secondo linea alta: sono i due modi in cui il silenzio si
 * presenta, e prenderli per un chip vuol dire dialogare con nessuno per tutto
 * il resto del driver. */
/* ! UN PHY RISPONDE A UN INDIRIZZO, NON A TUTTI E TRENTADUE, e questa e' la
 * correzione del 14 settembre 2026. Scartare 0000 e ffff non basta.
 *
 * Quei due valori sono il silenzio quando la linea sta ferma esattamente
 * bassa o esattamente alta. Ma basta che la lettura sia spostata di un bit —
 * un fronte contato male, un PHY che molla la linea un tempo dopo — perche' il
 * silenzio diventi 0x7fff: non e' ne' 0000 ne' ffff, quindi passava il
 * controllo, e in 0x7fff il bit del collegamento e' acceso e ANLPAR dice 100
 * Mbit full duplex. Il driver annunciava «PHY 13, collegamento SU, 100 Mbit
 * full duplex» con il filo di gestione muto.
 *
 * ! IL SEGNO CHE NON C'E' NESSUNO NON E' IL VALORE, E' CHE SONO TUTTI UGUALI.
 * Un PHY vero si distingue dagli altri trentuno indirizzi, qualunque cosa
 * risponda; una linea ferma risponde la stessa cosa dappertutto, e non c'e'
 * nessun valore che di per se' la tradisca. */
static int mii_trova(void)
{
    unsigned int phy, bmsr, silenzio;
    int trovato = -1, qualcuno_diverso = 0;

    silenzio = mii_leggi_reg(0, MII_BMSR);

    for (phy = 0; phy < 32; phy++) {
        bmsr = mii_leggi_reg(phy, MII_BMSR);

        if (bmsr != silenzio) qualcuno_diverso = 1;
        if (trovato < 0 && bmsr != 0x0000 && bmsr != 0xFFFF)
            trovato = (int)phy;
    }

    if (!qualcuno_diverso) return -1;
    return trovato;
}

/* ! E SE NON LO TROVA, SI GUARDA COSA HA RISPOSTO. «Nessuno risponde» non
 * dice se la linea sta bassa, alta, o se risponde qualcosa che non abbiamo
 * saputo riconoscere: sono tre guasti diversi e si distinguono guardando i
 * numeri grezzi. Trentadue indirizzi per quattro registri stanno in una
 * schermata. */
static void mii_setaccia(void)
{
    unsigned int phy, r;
    unsigned int primo[4];
    int tutti_uguali = 1;

    printf("sis900: setaccio del filo di gestione, registri 0-3\n");
    printf("        indirizzo  BMCR  BMSR  ID1   ID2\n");

    for (r = 0; r < 4; r++) primo[r] = mii_leggi_reg(0, r);

    for (phy = 0; phy < 32; phy++) {
        unsigned int v[4];

        for (r = 0; r < 4; r++) v[r] = mii_leggi_reg(phy, r);

        for (r = 0; r < 4; r++)
            if (v[r] != primo[r]) tutti_uguali = 0;

        printf("        %8u   %04x  %04x  %04x  %04x\n",
               phy, v[0], v[1], v[2], v[3]);
    }

    printf("\n");

    /* ! SI STAMPANO TUTTE E TRENTADUE LE RIGHE, e prima non si faceva: quelle
     * di soli ffff o soli zeri venivano saltate perche' «dicono una cosa sola
     * e la dicono trentadue volte». Il guaio e' che il silenzio non e' sempre
     * ffff o zero — se la lettura e' spostata di un bit diventa 7fff, e
     * quelle righe si stampavano tutte, una per indirizzo, come se
     * trentadue PHY avessero risposto la stessa cosa. Nasconderne alcune e
     * non altre rendeva impossibile vedere la differenza fra i due casi.
     *
     * Adesso si stampano tutte e si dice la cosa che conta: SE SONO UGUALI,
     * non ha risposto nessuno. */
    if (tutti_uguali) {
        unsigned int grezzo = reg_leggi(SIS_MII);

        printf("        ! TUTTI E TRENTADUE RISPONDONO LA STESSA COSA, cioe'\n");
        printf("        non risponde nessuno: un PHY vero si distingue dagli\n");
        printf("        altri trentuno indirizzi, qualunque cosa dica.\n");
        printf("\n");
        printf("        Il registro 0x08 letto cosi' com'e': %08x\n", grezzo);
        printf("\n");
        printf("        ffff  la linea sta alta e nessuno la tira giu'\n");
        printf("        0000  la linea sta bassa\n");
        printf("        7fff  la linea sta alta ma la lettura e' spostata di\n");
        printf("              un bit: e' il caso peggiore, perche' quel valore\n");
        printf("              passa per un PHY vivo con il collegamento su\n");
        printf("              a 100 Mbit full duplex, e non c'e' niente\n");
    } else {
        printf("        Le righe che si distinguono dalle altre sono i PHY\n");
        printf("        veri. Su un PHY vero il bit 0 di BMSR vale 1: se la\n");
        printf("        colonna BMSR mostra un numero pari, la lettura e'\n");
        printf("        spostata e velocita' e duplex sono da buttare.\n");
    }
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
        d[1] = RX_DICHIARATO;            /* vuoto: D_OWN spento = scrivici */
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
/* =============================================================================
 * La EEPROM — il ripiego, quando il filtro esce a zero
 *
 * ! 0x08 E' LA EEPROM *E* IL PHY, E DIRE «e' il PHY» ERA MEZZA VERITA'. In
 * cima a questo file c'e' scritto «0x08 NON E' LA EEPROM, E' IL PHY», e
 * nasceva da una correzione giusta: il nome MEAR da solo faceva credere che
 * ci passasse soltanto la EEPROM, e di li' era venuto il RELOAD che il MAC lo
 * cancellava. Ma il registro e' UNO SOLO CON DUE GRUPPI DI BIT, e ci passano
 * tutti e due:
 *
 *     0x40 MDC   0x20 MDDIR  0x10 MDIO      il filo del PHY
 *     0x08 EECS  0x04 EECLK  0x02 EEDO  0x01 EEDI   il filo della EEPROM
 *
 * ! E LA EEPROM NON E' NOSTRA DA SOLI. Su questa revisione e' in comune con il
 * controller 1394 dello stesso chip, e prima di toccarla si chiede il turno:
 * si accende EEREQ, si aspetta che compaia EEGNT, si legge, si rilascia con
 * EEDONE. Chi non lo fa non legge zeri per caso: legge zeri sempre, perche'
 * il filo ce l'ha in mano un altro.
 *
 * ! ED E' IL PEZZO CHE SPIEGA IL DANNO DEL RELOAD, il 10 settembre 2026. Il
 * commento in leggi_mac() concludeva «una EEPROM non c'e'». La conclusione
 * era sbagliata, l'osservazione no: RELOAD dice alla scheda di ricaricarsi il
 * MAC dalla EEPROM, e senza il turno lei ci trovava sei zeri e li scriveva
 * sopra l'indirizzo buono. Non mancava la EEPROM: mancava il permesso.
 *
 * ! RESTA UN RIPIEGO, PERO', E NON DIVENTA LA STRADA MAESTRA. Sull'Acer i
 * registri del filtro l'indirizzo ce l'hanno, perche' ce lo mette il BIOS, e
 * quella e' la strada che il driver del costruttore percorre su tutte le
 * revisioni tranne la 0x81. Questa si prova solo quando quella rende zero.
 * ========================================================================== */
#define EE_CS        0x08u
#define EE_CLK       0x04u
#define EE_DO        0x02u   /* lei verso noi */
#define EE_DI        0x01u   /* noi verso lei */

#define EE_REQ       0x00000400u   /* chiedo il turno */
#define EE_DONE      0x00000200u   /* ho finito */
#define EE_GNT       0x00000100u   /* il turno e' mio */

#define EE_LEGGI     0x0180u   /* il comando, nove bit con l'indirizzo dentro */
#define EE_MAC       0x08u     /* da qui cominciano le tre parole del MAC */

static unsigned int eeprom_parola(unsigned int dove)
{
    unsigned int comando = EE_LEGGI | (dove & 0x3F);
    unsigned int fuori = 0;
    int i;

    reg_scrivi(SIS_MII, 0);
    reg_scrivi(SIS_MII, EE_CS);

    /* I nove bit del comando, dal piu' significativo. */
    for (i = 8; i >= 0; i--) {
        unsigned int v = (comando & (1u << i)) ? (EE_DI | EE_CS) : EE_CS;

        reg_scrivi(SIS_MII, v);
        reg_scrivi(SIS_MII, v | EE_CLK);
    }
    reg_scrivi(SIS_MII, EE_CS);

    /* I sedici del dato. */
    for (i = 0; i < 16; i++) {
        reg_scrivi(SIS_MII, EE_CS);
        reg_scrivi(SIS_MII, EE_CS | EE_CLK);
        fuori = (fuori << 1) | ((reg_leggi(SIS_MII) & EE_DO) ? 1u : 0u);
    }

    reg_scrivi(SIS_MII, 0);
    return fuori;
}

/* Rende 0 se il MAC l'ha messo in g_mac, -1 se il turno non arriva. */
static int mac_dalla_eeprom(void)
{
    unsigned int i, attesa;

    reg_scrivi(SIS_MII, EE_REQ);

    for (attesa = 0; attesa < 2000; attesa++) {
        if (reg_leggi(SIS_MII) & EE_GNT) {
            for (i = 0; i < 3; i++) {
                unsigned int w = eeprom_parola(EE_MAC + i);

                g_mac[i * 2]     = (unsigned char)(w & 0xFF);
                g_mac[i * 2 + 1] = (unsigned char)((w >> 8) & 0xFF);
            }
            reg_scrivi(SIS_MII, EE_DONE);
            return 0;
        }
        usleep(1);
    }

    reg_scrivi(SIS_MII, EE_DONE);
    return -1;
}

static int cifra_esa(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int leggi_mac(void)
{
    unsigned int i, v, rfcr;

    /* Se ce l'ha dato chi ci ha lanciati, non si cerca altro. */
    if (g_mac_dato) return 0;

    /* ! IL RELOAD SI FA SOLO SULLE VECCHIE, E QUI FACEVA DANNO. Il 10 settembre
     * 2026 il MAC usciva a zero e ho aggiunto RELOAD nel registro di comando —
     * il passo che il driver di Linux fa per le SiS 635 e 735. Il giorno dopo
     * il MAC usciva a zero PEGGIO DI PRIMA, e la ragione era quella riga.
     *
     * Il driver del costruttore sceglie in base alla revisione, e si vede:
     *
     *     cmp  [ctx+0x190], 0x81
     *     jne  ramo_del_filtro
     *
     * Sulla 0x81 va a prendere il MAC dalla CMOS, agli indici 0x09..0x0E,
     * dopo aver toccato la configurazione PCI di 00:01.0. Su TUTTE LE ALTRE —
     * e la nostra e' la 0x91 — legge i tre registri del filtro e basta, senza
     * nessun RELOAD.
     *
     * ! E IL PERCHE' RELOAD FACESSE DANNO E' LA PARTE CHE VALE. Quell'ordine
     * dice alla scheda di ricaricarsi l'indirizzo DALLA EEPROM. Su questa
     * scheda una EEPROM non c'e' — il registro che credevamo suo e' il PHY,
     * vedi SIS_MII — quindi ricaricava sei zeri sopra il MAC che il BIOS
     * aveva gia' messo li'. Prima di quella riga i registri del filtro
     * contenevano l'indirizzo giusto.
     *
     * ! LA LEZIONE NON E' SUL RELOAD: e' che una correzione a un sintomo, fatta
     * senza guardare cosa fa chi la scheda la guida davvero, puo' togliere
     * quello che funzionava. La prima versione di questa funzione era giusta.
     *
     * Il filtro si spegne mentre si legge e si rimette com'era: quello si',
     * ed e' la sequenza del costruttore. */
    rfcr = reg_leggi(SIS_RXFILTCTRL);
    reg_scrivi(SIS_RXFILTCTRL, rfcr & ~RFC_RFEN);

    for (i = 0; i < 3; i++) {
        reg_scrivi(SIS_RXFILTCTRL, (i << 16));
        v = reg_leggi(SIS_RXFILTDATA);
        g_mac[i * 2]     = (unsigned char)(v & 0xFF);
        g_mac[i * 2 + 1] = (unsigned char)((v >> 8) & 0xFF);
    }

    reg_scrivi(SIS_RXFILTCTRL, rfcr);

    v = 0;
    for (i = 0; i < 6; i++) v |= g_mac[i];

    /* ! PRIMA DI ARRENDERSI SI PROVA LA EEPROM. Il filtro a zero vuol dire
     * che il BIOS li' non ha scritto niente — succede quando si e' avviato da
     * rete, o quando l'avvio rapido salta l'inizializzazione della scheda. La
     * EEPROM invece l'indirizzo ce l'ha sempre: e' dove il costruttore l'ha
     * messo. Vedi mac_dalla_eeprom(). */
    if (v == 0 && mac_dalla_eeprom() == 0) {
        v = 0;
        for (i = 0; i < 6; i++) v |= g_mac[i];
        if (v != 0)
            printf("sis900: MAC preso dalla EEPROM: "
                   "i registri del filtro erano a zero.\n");
    }

    if (v == 0) {
        printf("sis900: l'indirizzo MAC esce tutto a zero.\n");
        printf("        Ne' i registri del filtro ne' la EEPROM ce l'hanno.\n");
        printf("        Nei registri del filtro ce lo mette il BIOS\n");
        printf("        all'accensione: se sono a zero, o il BIOS non l'ha\n");
        printf("        fatto, o qualcuno li ha riscritti dopo.\n");
        printf("\n");
        printf("        Dammelo tu e la scheda parte lo stesso:\n");
        printf("          sis900.drv -mac 00:11:22:33:44:55 &\n");
        printf("\n");
        printf("        Lo trovi sull'etichetta sotto il portatile, oppure\n");
        printf("        da Windows con  ipconfig /all  alla voce\n");
        printf("        'Indirizzo fisico' della scheda Ethernet.\n");
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
 * Chiedere al PHY com'e' andata
 *
 * ! SI LEGGE DUE VOLTE LO STATO, e non e' superstizione: il bit del
 * collegamento e' «latching low» — resta a zero finche' non lo si legge, anche
 * se il filo nel frattempo e' tornato su. Una lettura sola dopo un reset dice
 * quasi sempre «giu'» e manda a cercare un guasto nel cavo.
 * ========================================================================== */
static void guarda_il_phy(void)
{
    unsigned int bmsr, anlpar;

    g_phy = mii_trova();
    if (g_phy < 0) {
        /* Non sapere dov'e' il PHY non ferma il driver: la scheda puo'
         * funzionare con i valori che il BIOS le ha lasciato. Ma va detto,
         * perche' da qui in poi velocita' e duplex sono un'ipotesi. */
        printf("sis900: nessun PHY risponde sul filo di gestione.\n");
        printf("        (tutti e trentadue gli indirizzi rendono lo stesso\n");
        printf("        valore: `sis900.drv -phy` mostra quale)\n");
        printf("        Vado avanti a 100 Mbit full duplex per ipotesi.\n");
        return;
    }

    (void)mii_leggi_reg((unsigned int)g_phy, MII_BMSR);
    bmsr   = mii_leggi_reg((unsigned int)g_phy, MII_BMSR);
    anlpar = mii_leggi_reg((unsigned int)g_phy, MII_ANLPAR);

    /* ! LA VELOCITA' NON STA IN UN REGISTRO: si ricava da cosa ha offerto chi
     * sta dall'altra parte. Sono i quattro bit di ANLPAR, dal piu' desiderabile
     * al meno: 100 full, 100 half, 10 full, 10 half. */
    if      (anlpar & 0x0100) { g_veloce = 1; g_duplex = 1; }
    else if (anlpar & 0x0080) { g_veloce = 1; g_duplex = 0; }
    else if (anlpar & 0x0040) { g_veloce = 0; g_duplex = 1; }
    else                      { g_veloce = 0; g_duplex = 0; }

    printf("sis900: PHY %d, BMSR %04x, collegamento %s", g_phy, bmsr,
           (bmsr & BMSR_LINK) ? "SU" : "GIU'");
    if (bmsr & BMSR_LINK)
        printf(", %s Mbit %s duplex",
               g_veloce ? "100" : "10", g_duplex ? "full" : "half");
    printf("\n");

    /* ! IL BIT 0 DI BMSR VALE 1 SU OGNI PHY VERO. E' «Extended Capability»,
     * e la specifica 802.3 lo vuole acceso da trent'anni: non esiste un PHY
     * che risponda con un numero pari. Se esce pari, quel che si sta leggendo
     * non e' un registro — e' la linea, spostata di un bit.
     *
     * ! E NON CI SI FERMA, SI AVVISA. Velocita' e duplex diventano
     * un'ipotesi come quando il PHY non c'e', ma la scheda puo' lavorare
     * lo stesso: fermare il driver qui vorrebbe dire niente rete per un
     * sospetto, quando andare avanti costa al massimo un duplex sbagliato —
     * che i contatori poi dicono. */
    if (!(bmsr & 0x0001)) {
        printf("        ! QUESTO NON E' UN PHY. Il bit 0 di BMSR e' zero, e\n");
        printf("        su un PHY vero vale sempre 1: quel che si legge e'\n");
        printf("        la linea di gestione, non un registro. Velocita' e\n");
        printf("        duplex qui sopra sono da buttare.\n");
        printf("        Guarda `sis900.drv -phy`: se tutti e trentadue gli\n");
        printf("        indirizzi rendono lo stesso numero, non risponde\n");
        printf("        nessuno.\n");
    }

    if (!(bmsr & BMSR_LINK)) {
        printf("        ! IL FILO NON PORTA NIENTE. Finche' resta cosi' non\n");
        printf("        c'e' nessun altro guasto da cercare: il cavo e'\n");
        printf("        attaccato? dall'altra parte c'e' qualcosa di acceso?\n");
    }
}

/* =============================================================================
 * Accensione
 * ========================================================================== */
static int inizializza_scheda(void)
{
    unsigned int i, v;
    unsigned int manca = IS_TXRCMP | IS_RXRCMP;

    /* Interrupt spenti PRIMA di toccare il reset: un interrupt che arriva a
     * meta' dell'inizializzazione trova gli anelli non ancora collegati. E il
     * filtro spento, perche' una scheda che si sta azzerando non deve stare
     * anche a decidere cosa accettare. */
    reg_scrivi(SIS_IER, 0);
    reg_scrivi(SIS_IMR, 0);
    reg_scrivi(SIS_RXFILTCTRL, 0);

    /* ! IL RESET SI CHIEDE AI TRE MOTORI INSIEME. RESET da solo riazzera la
     * parte comune e lascia i due motori DMA come stavano: e' il caso in cui
     * la scheda riparte con l'anello vecchio ancora in pancia e il primo
     * pacchetto va a finire su memoria che nel frattempo e' di qualcun altro. */
    reg_scrivi(SIS_CR, CR_RXRESET | CR_TXRESET | CR_RESET);

    /* ! NON SI GUARDA CR, SI GUARDA ISR, e questa e' la correzione del 14
     * settembre 2026. La versione di prima aspettava che il bit RESET si
     * spegnesse in CR. Su questa scheda quel bit non e' la fine del lavoro:
     * la fine la dicono TxRCMP e RxRCMP in ISR, uno per motore, e finche' non
     * si sono accesi tutti e due i descrittori scritti dopo possono essere
     * riazzerati sotto il naso.
     *
     * ! E I BIT SI ACCUMULANO PERCHE' LEGGERE ISR LO SVUOTA. Chi lo legge si
     * porta via anche il bit dell'altro motore: guardarli uno alla volta vuol
     * dire perdere quello che arriva per primo e aspettare per sempre quello
     * che non tornera' piu'. Per questo si toglie da `manca` quel che si vede,
     * invece di chiedere ogni volta se ci sono entrambi. */
    for (i = 0; i < 1000 && manca != 0; i++) {
        manca &= ~reg_leggi(SIS_ISR);
        usleep(1000);
    }
    if (manca != 0) {
        printf("sis900: il reset non finisce: manca %s.\n",
               (manca & IS_TXRCMP) ? ((manca & IS_RXRCMP) ? "TxRCMP e RxRCMP"
                                                          : "TxRCMP")
                                   : "RxRCMP");
        return -1;
    }

    /* ! CFG LO RIMETTE A POSTO CHI HA FATTO IL RESET. Prima di questa riga il
     * registro restava come l'aveva lasciato il BIOS, e non e' una scelta: e'
     * un valore che non ha guidato nessuno. Vedi CFG_PESEL. */
    if (g_rev >= REV_SIS635A || g_rev == REV_SIS900B)
        reg_scrivi(SIS_CFG, CFG_PESEL | CFG_RND_CNT);
    else
        reg_scrivi(SIS_CFG, CFG_PESEL);

    prepara_anelli();

    reg_scrivi(SIS_TXDP, g_dma_fis + OFF_TX_DESC);
    reg_scrivi(SIS_RXDP, g_dma_fis + OFF_RX_DESC);

    /* ! I MOTORI SI CONFIGURANO DOPO AVER CHIESTO AL PHY, perche' la soglia di
     * partenza dipende da quanto e' veloce il filo e i due bit del duplex da
     * come e' finita la trattativa. Con il duplex sbagliato la scheda tratta
     * ogni pacchetto che riceve mentre trasmette come una collisione. */
    guarda_il_phy();

    /* ! QUEL CHE DICE CHI LANCIA VIENE DOPO IL PHY, non prima: cosi' si vede
     * nel referto sia cosa aveva capito il driver sia cosa gli e' stato
     * imposto, e la differenza fra i due e' meta' della diagnosi. */
    if (g_forza_duplex >= 0 && g_forza_duplex != g_duplex) {
        g_duplex = g_forza_duplex;
        printf("        forzato a %s duplex\n", g_duplex ? "full" : "half");
    }
    if (g_forza_veloce >= 0 && g_forza_veloce != g_veloce) {
        g_veloce = g_forza_veloce;
        printf("        forzato a %s Mbit\n", g_veloce ? "100" : "10");
    }

    {
        unsigned int tx_drnt = g_veloce ? TX_DRNT_100 : TX_DRNT_10;
        unsigned int rx_drnt = g_veloce ? RX_DRNT_100 : RX_DRNT_10;
        unsigned int tx = reg_leggi(SIS_TXCFG) & ~TX_MASCHERA;
        unsigned int rx = reg_leggi(SIS_RXCFG) & ~RX_MASCHERA;

        /* ! LA RAFFICA LA DECIDE CFG, NON NOI. Vedi DMA_RAFFICA_64: con
         * EDB_MASTER_EN acceso la scheda vuole 64 byte, e chiedendone 512
         * non inizia nemmeno un ciclo sul bus. Si rilegge CFG invece di
         * ricordarsi cosa ci abbiamo scritto: il bit 13 non lo mettiamo
         * noi — sul portatile era gia' acceso dopo il reset. */
        unsigned int raffica = (reg_leggi(SIS_CFG) & CFG_EDB_MASTER)
                             ? DMA_RAFFICA_64 : DMA_RAFFICA_512;

        g_raffica = raffica;   /* il referto la stampa */

        tx |= TX_ATP
            | (raffica << MXDMA_SHIFT)
            | (TX_FILL_THRESH << TX_FILLT_SHIFT)
            | tx_drnt;

        /* ! IN RICEZIONE IL CAMPO PARTE DAL BIT 1, e prima ci si scriveva
         * sopra dal bit 0: la soglia che arrivava alla scheda era la meta'
         * di quella voluta. In trasmissione invece parte dal bit 0, e i due
         * registri non sono simmetrici per quanto si somiglino. */
        rx |= (raffica << MXDMA_SHIFT) | (rx_drnt << RX_DRNT_SHIFT);

        if (g_duplex) { tx |= TX_FULLDUPLEX; rx |= RX_FULLDUPLEX; }

        reg_scrivi(SIS_TXCFG, tx);
        reg_scrivi(SIS_RXCFG, rx);
    }

    /* ! IL FILTRO VUOLE IL NOSTRO MAC RISCRITTO. Il reset l'ha azzerato, e una
     * scheda con il filtro acceso e l'indirizzo a zero scarta tutto quello che
     * le arriva — compreso cio' che era per lei. */
    for (i = 0; i < 3; i++) {
        v = (unsigned int)g_mac[i * 2] |
            ((unsigned int)g_mac[i * 2 + 1] << 8);
        reg_scrivi(SIS_RXFILTCTRL, (i << 16));
        reg_scrivi(SIS_RXFILTDATA, v);
    }
    /* ! IL BROADCAST NON E' FACOLTATIVO. Senza, non funzionano ne' l'ARP —
     * che e' come si scopre l'indirizzo fisico di chiunque — ne' il DHCP. Il
     * multicast costa un bit e serve a IPv6 e a mDNS, quindi si accende. */
    reg_scrivi(SIS_RXFILTCTRL,
               RFC_RFEN | RFC_AAB | RFC_AAM | (g_promiscuo ? RFC_AAP : 0));

    /* Da qui la scheda ascolta e puo' trasmettere. */
    reg_scrivi(SIS_CR, CR_RXENA);
    reg_scrivi(SIS_IMR, IS_RXOK | IS_RXERR | IS_RXORN |
                        IS_TXOK | IS_TXERR | IS_TXURN);
    reg_scrivi(SIS_IER, 1);

    return 0;
}

/* =============================================================================
 * Una lettura della configurazione PCI, dal driver acceso
 *
 * ! IL REGISTRO DI STATO E' IL NUMERO CHE MANCAVA. Il registro comando dice se
 * alla scheda E' PERMESSO iniziare cicli sul bus; lo stato dice COM'E' ANDATA
 * quando ci ha provato. Sono due domande diverse e il referto del 14 settembre
 * 2026 sapeva rispondere solo alla prima.
 *
 *   bit 29  master abort ricevuto   la scheda ha iniziato un ciclo e non ha
 *                                   risposto nessuno: l'indirizzo non esiste
 *                                   per il ponte
 *   bit 28  target abort ricevuto   ha risposto qualcuno, e ha detto di no
 *   bit 24  errore di parita' sui dati
 *
 * Una scheda che non riesce a leggere i propri descrittori con il bus master
 * acceso deve avere uno di questi bit alto. Se non ne ha nessuno, non ci sta
 * nemmeno provando, ed e' un'altra storia.
 *
 * ! MENTRE ASPETTA LA RISPOSTA, QUALCUNO PUO' BUSSARE. Il driver ha un giro di
 * servizio solo, e qui si mette ad aspettare pci.drv: chi manda un messaggio
 * proprio in quel momento lo troverebbe buttato via. Un frame perso non
 * importa — la rete che si sta studiando gia' non va — ma una richiesta
 * rimasta senza risposta lascia appeso chi l'ha fatta, e quello si', quindi si
 * risponde. */
static unsigned int pci_leggi_config(unsigned int offset)
{
    int pid = ipc_lookup(PCI_SERVIZIO);
    PciAzione     a;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           giri;

    if (pid <= 0 || g_bus == 0xFFFFFFFF) return 0xFFFFFFFFu;

    a.bus = (unsigned char)g_bus;
    a.slot = (unsigned char)g_slot;
    a.funzione = (unsigned char)g_funzione;
    a.riservato = 0;
    a.offset = (unsigned short)offset;
    a.bit = 0;

    if (ipc_send(pid, PCI_MSG_LEGGI, &a, sizeof(a)) < 0) return 0xFFFFFFFFu;

    for (giri = 0; giri < 16; giri++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0)
            return 0xFFFFFFFFu;

        if ((int)meta.sender_pid == pid) {
            PciValore v;

            if (meta.tipo != PCI_MSG_VALORE || meta.len < sizeof(v))
                return 0xFFFFFFFFu;
            memcpy(&v, buf, sizeof(v));
            return v.valore;
        }

        /* Non e' pci.drv: e' qualcuno della rete. Chi aspetta una risposta la
         * riceve, cosi' non resta appeso; il resto si lascia cadere. */
        switch (meta.tipo) {
        case NET_MSG_INVIA:
        case NET_MSG_ANNULLA: {
            NetEsito e;

            e.codice = -EAGAIN;
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }
        case NET_MSG_RICEVI:
            g_lettore_pid = meta.sender_pid;   /* si serve dopo, come sempre */
            break;
        default:
            break;
        }
    }
    return 0xFFFFFFFFu;
}

/* =============================================================================
 * IL REFERTO — `-debug`
 *
 * ! QUESTO DRIVER NON SI PROVA QUI, E NON E' UN DETTAGLIO. QEMU la SiS 900 non
 * la emula: l'unico posto dove gira e' un portatile del 2004 che sta in
 * un'altra stanza, senza rete — perche' la rete e' proprio quel che non
 * funziona — e senza un modo di copiare e incollare uno schermo. Ogni domanda
 * costa un viaggio, e la risposta torna indietro ricopiata a mano.
 *
 * ! PER QUESTO IL REFERTO PRENDE TUTTO IN UN COLPO SOLO. Non le tre cose che
 * sembrano utili adesso: TUTTO — i registri, le trentadue righe del filo di
 * gestione, i sedici descrittori, i contatori. Un viaggio che riporta
 * venticinque numeri che non servivano e uno che serviva e' un viaggio
 * riuscito; un viaggio che riporta i tre numeri giusti per la domanda di ieri
 * e' un viaggio da rifare.
 *
 * ! E LO SCRIVE IL DRIVER ACCESO, NON CHI LO CHIEDE. E' l'unico che puo'
 * leggere la scheda senza far danni — ioport_bind non e' esclusiva, e un
 * secondo processo che «da' un'occhiata» le fa il reset sotto. Un driver di
 * EX-OS e' un processo ring3 con la sua libc: aprire un file e scriverci
 * dentro lo sa fare da se'. Vedi NET_MSG_REFERTO in net_proto.h.
 *
 * ! SI SCRIVE A BLOCCHI, NON RIGA PER RIGA. Un floppy scrive un settore alla
 * volta: una write() per riga vorrebbe dire migliaia di giri di testina. E'
 * la stessa ragione, e la stessa forma, di sonda.c.
 * ========================================================================== */
#define REFERTO_PREDEFINITO  "/SIS900.TXT"
#define REF_BUF              2048

static char         g_ref[REF_BUF];
static unsigned int g_ref_usati = 0;
static int          g_ref_fd = -1;
static int          g_ref_guasto = 0;

static void ref_svuota(void)
{
    if (g_ref_usati == 0 || g_ref_fd < 0 || g_ref_guasto) return;

    if (write(g_ref_fd, g_ref, g_ref_usati) != (ssize_t)g_ref_usati)
        g_ref_guasto = 1;   /* dischetto pieno o protetto: si smette e si dice */

    g_ref_usati = 0;
}

/* ! LE RIGHE SONO IN ASCII PURO, come tutto quel che finisce a schermo: il
 * referto si legge anche con `type` da DOS o su una console a code page 437,
 * dove una lettera accentata diventa due caratteri di spazzatura. */
static void ref(const char *fmt, ...)
{
    char riga[256];
    __builtin_va_list ap;
    int n;

    if (g_ref_guasto) return;

    __builtin_va_start(ap, fmt);
    n = vsnprintf(riga, sizeof(riga), fmt, ap);
    __builtin_va_end(ap);

    if (n <= 0) return;
    if (n > (int)sizeof(riga) - 1) n = (int)sizeof(riga) - 1;

    if (g_ref_usati + (unsigned int)n > REF_BUF) ref_svuota();
    if (g_ref_guasto) return;

    memcpy(g_ref + g_ref_usati, riga, (unsigned int)n);
    g_ref_usati += (unsigned int)n;
}

/* --- i pezzi del referto -------------------------------------------------- */

static void ref_scheda(void)
{
    ref("== LA SCHEDA ==\n\n");
    ref("  modello      %s\n", g_modello);
    ref("  PCI          %02x:%02x.%d\n", g_bus, g_slot, g_funzione);
    ref("  revisione    %02x", g_rev);
    if (g_rev == 0x91)      ref("   (SiS96x: MAC dalla EEPROM in Linux)\n");
    else if (g_rev >= 0x90) ref("   (dalla 635A in su)\n");
    else                    ref("\n");
    ref("  porte        0x%x-0x%x\n", g_base, g_base + SIS_PORTE - 1);
    ref("  IRQ          %u\n", g_irq);
    ref("  MAC          %02x:%02x:%02x:%02x:%02x:%02x\n",
        g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    ref("  MTU          %d\n", NET_MTU);
    ref("  DMA          %u byte, virtuale 0x%08x, fisico 0x%08x\n",
        DMA_BYTE, g_dma_virt, g_dma_fis);
    ref("  anelli       %d RX, %d TX, buffer da %d byte\n\n",
        RX_N, TX_N, BUF_LEN);
    ref("  dichiarati   %d byte per descrittore RX (il campo e' stretto:\n",
        RX_DICHIARATO);
    ref("               2048 non ci sta e alla scheda arriva ZERO)\n");

    /* ! IL REGISTRO COMANDO DEL PCI E' LA PRIMA COSA DA GUARDARE quando la
     * scheda risponde e non fa niente. Vedi g_pci_comando. */
    ref("  comando PCI  0x%04x   IO %s, memoria %s, BUS MASTER %s\n",
        g_pci_comando,
        (g_pci_comando & PCI_CMD_IO)        ? "si" : "no",
        (g_pci_comando & PCI_CMD_MEMORIA)   ? "si" : "no",
        (g_pci_comando & PCI_CMD_BUSMASTER) ? "si" : "NO");
    if (g_pci_esito < 0)
        ref("               (pci.drv non ha risposto: il numero qui sopra\n"
            "                non e' stato letto, e' quel che c'era prima)\n");
    if (!(g_pci_comando & PCI_CMD_BUSMASTER))
        ref("\n  ! SENZA BUS MASTER LA SCHEDA NON TOCCA LA MEMORIA. Il ponte\n"
            "    rifiuta ogni ciclo che inizia lei: non legge i descrittori e\n"
            "    non scrive i pacchetti. Tutto il resto di questo referto e'\n"
            "    una conseguenza, non una causa.\n");
    ref("\n");

    /* ! E ADESSO LA CONFIGURAZIONE COM'E' IN QUESTO MOMENTO, non com'era
     * all'avvio. Vedi pci_leggi_config(). */
    {
        unsigned int d04 = pci_leggi_config(0x04);

        ref("  --- la configurazione PCI, riletta adesso ---\n\n");
        if (d04 == 0xFFFFFFFFu) {
            ref("  Non si e' potuta leggere: pci.drv non ha risposto.\n\n");
        } else {
            unsigned int cmd = d04 & 0xFFFF;
            unsigned int st  = (d04 >> 16) & 0xFFFF;

            g_pci_stato = st;

            ref("  comando  0x%04x   IO %s, memoria %s, BUS MASTER %s\n", cmd,
                (cmd & PCI_CMD_IO)        ? "si" : "no",
                (cmd & PCI_CMD_MEMORIA)   ? "si" : "no",
                (cmd & PCI_CMD_BUSMASTER) ? "si" : "NO");
            ref("  stato    0x%04x\n", st);
            ref("      master abort ricevuto  %s   (ha iniziato un ciclo e\n",
                (st & 0x2000) ? "SI" : "no");
            ref("                                  non ha risposto nessuno)\n");
            ref("      target abort ricevuto  %s   (ha risposto qualcuno, e\n",
                (st & 0x1000) ? "SI" : "no");
            ref("                                  ha detto di no)\n");
            ref("      errore di parita' dati %s\n",
                (st & 0x0100) ? "SI" : "no");
            /* ! IL BIT 5 NON E' «BUS MASTER CAPACE», E' «66 MHz CAPACE».
             * Nel registro di stato del PCI un bit che dica «sa fare il
             * master» non esiste: quel che si puo' sapere e' se gli E'
             * PERMESSO (registro comando) e com'e' andata quando ci ha
             * provato (gli abort qui sopra). L'etichetta sbagliata e'
             * durata un referto, e diceva NO su una scheda che il master
             * lo sapeva fare benissimo. */
            ref("      66 MHz capace          %s\n",
                (st & 0x0020) ? "si" : "no");
            ref("      lista di capacita'     %s\n\n",
                (st & 0x0010) ? "si" : "no");

            if (st & (0x2000 | 0x1000))
                ref("  ! LA SCHEDA CI PROVA E VIENE RESPINTA. Il bus master e'\n"
                    "    acceso e i cicli partono, ma il ponte li interrompe:\n"
                    "    l'indirizzo fisico che le abbiamo dato non esiste\n"
                    "    per lui, o non gli e' permesso arrivarci.\n\n");
        }
    }
}

static void ref_registri(void)
{
    unsigned int cr   = reg_leggi(SIS_CR);
    unsigned int cfg  = reg_leggi(SIS_CFG);
    unsigned int isr  = reg_leggi(SIS_ISR);
    unsigned int txc  = reg_leggi(SIS_TXCFG);
    unsigned int rxc  = reg_leggi(SIS_RXCFG);
    unsigned int rfcr = reg_leggi(SIS_RXFILTCTRL);
    unsigned int flc  = reg_leggi(SIS_FLCTRL);
    unsigned int txdp = reg_leggi(SIS_TXDP);
    unsigned int rxdp = reg_leggi(SIS_RXDP);

    ref("== I REGISTRI ==\n\n");
    ref("  CR    0x%08x   RX %s, TX %s\n", cr,
        (cr & CR_RXENA) ? "acceso" : "SPENTO",
        (cr & CR_TXENA) ? "acceso" : "fermo");
    ref("  CFG   0x%08x   PESEL %s, RND_CNT %s\n", cfg,
        (cfg & CFG_PESEL)   ? "si" : "NO",
        (cfg & CFG_RND_CNT) ? "si" : "no");

    /* ! LEGGERE ISR LO AZZERA, e qui e' un prelievo: i bit che si vedono in
     * questa riga il driver non li vedra' piu'. E' il prezzo del referto, si
     * paga una volta sola, e va detto — altrimenti sembra che la scheda non
     * abbia piu' niente da dire subito dopo. */
    ref("  ISR   0x%08x   (letto = azzerato: questi bit il driver\n", isr);
    ref("                     non li vedra' piu')\n");
    ref("        RXOK %s  RXERR %s  RXORN %s\n",
        (isr & IS_RXOK)  ? "si" : "no",
        (isr & IS_RXERR) ? "SI" : "no",
        (isr & IS_RXORN) ? "SI" : "no");
    ref("        TXOK %s  TXERR %s  TXURN %s\n",
        (isr & IS_TXOK)  ? "si" : "no",
        (isr & IS_TXERR) ? "SI" : "no",
        (isr & IS_TXURN) ? "SI" : "no");

    ref("  TXCFG 0x%08x   soglia %u, ATP %s, full duplex %s\n", txc,
        txc & 0x3F, (txc & TX_ATP) ? "si" : "no",
        ((txc & TX_FULLDUPLEX) == TX_FULLDUPLEX) ? "si" : "no");
    ref("  RXCFG 0x%08x   soglia %u, accetta il proprio %s\n", rxc,
        (rxc & 0x7F) >> RX_DRNT_SHIFT, (rxc & RX_FULLDUPLEX) ? "si" : "no");

    /* ! LA RAFFICA E' LA PRIMA COSA DA CONFRONTARE CON CFG. Vedi
     * DMA_RAFFICA_64: se EDB e' acceso e la raffica e' 512, la scheda non
     * inizia un ciclo sul bus e tutto il resto del referto e' conseguenza. */
    {
        unsigned int mx_tx = (txc >> MXDMA_SHIFT) & 7;
        unsigned int mx_rx = (rxc >> MXDMA_SHIFT) & 7;
        int edb = (cfg & CFG_EDB_MASTER) != 0;

        ref("  raffica DMA   TX %u, RX %u   (0 = 512 byte, 5 = 64 byte)\n",
            mx_tx, mx_rx);
        ref("                EDB_MASTER_EN in CFG: %s\n", edb ? "acceso"
                                                              : "spento");
        if (edb && (mx_tx != DMA_RAFFICA_64 || mx_rx != DMA_RAFFICA_64))
            ref("\n  ! RAFFICA SBAGLIATA PER IL MODO EDB. Con quel bit acceso\n"
                "    la scheda vuole 64 byte (codice 5): chiedendone 512 non\n"
                "    inizia nemmeno un ciclo sul bus, e tutto il resto di\n"
                "    questo referto e' una conseguenza.\n");
        if (!edb && (mx_tx == DMA_RAFFICA_64 || mx_rx == DMA_RAFFICA_64))
            ref("\n  ! RAFFICA DA 64 SENZA EDB: si puo' fare, ma va piano\n"
                "    senza motivo.\n");
    }
    ref("  RFCR  0x%08x   acceso %s, broadcast %s, multicast %s, tutto %s\n",
        rfcr,
        (rfcr & RFC_RFEN) ? "si" : "NO",
        (rfcr & RFC_AAB)  ? "si" : "NO",
        (rfcr & RFC_AAM)  ? "si" : "no",
        (rfcr & RFC_AAP)  ? "si" : "no");
    ref("  FLCTRL 0x%08x\n", flc);
    ref("  TXDP  0x%08x   (anello TX a 0x%08x)\n",
        txdp, g_dma_fis + OFF_TX_DESC);
    ref("  RXDP  0x%08x   (anello RX a 0x%08x)\n",
        rxdp, g_dma_fis + OFF_RX_DESC);

    /* ! SE RXDP NON STA DENTRO IL NOSTRO ANELLO, LA SCHEDA GUARDA ALTROVE, e
     * quel che scrive finisce in memoria di qualcun altro. E' la cosa piu'
     * grave che questo referto possa dire, quindi si dice per esteso. */
    if (rxdp < g_dma_fis + OFF_RX_DESC ||
        rxdp >= g_dma_fis + OFF_RX_DESC + RX_N * 16)
        ref("        ! RXDP E' FUORI DALL'ANELLO: la scheda sta scrivendo\n"
            "          in memoria che non e' nostra.\n");
    ref("\n");
}

static void ref_filtro(void)
{
    unsigned int i, v, salva;

    ref("== I TRE REGISTRI DEL FILTRO, RILETTI ==\n\n");
    ref("  Ci mette il MAC il BIOS all'accensione, e ce lo riscrive\n");
    ref("  inizializza_scheda(). Se qui non c'e' il MAC di sopra, il\n");
    ref("  filtro sta scartando anche cio' che era per noi.\n\n");

    salva = reg_leggi(SIS_RXFILTCTRL);
    for (i = 0; i < 3; i++) {
        reg_scrivi(SIS_RXFILTCTRL, (i << 16) | (salva & RFC_RFEN));
        v = reg_leggi(SIS_RXFILTDATA);
        ref("  parola %u    0x%04x   -> %02x:%02x\n", i, v & 0xFFFF,
            v & 0xFF, (v >> 8) & 0xFF);
    }
    reg_scrivi(SIS_RXFILTCTRL, salva);
    ref("\n");
}

static void ref_phy(void)
{
    unsigned int phy, r, primo[6];
    int tutti_uguali = 1;

    ref("== IL FILO DI GESTIONE (MDIO), TUTTI E TRENTADUE ==\n\n");
    ref("  ind   BMCR  BMSR  ID1   ID2   ANAR  ANLPAR\n");

    for (r = 0; r < 6; r++) primo[r] = mii_leggi_reg(0, r);

    for (phy = 0; phy < 32; phy++) {
        unsigned int v[6];

        for (r = 0; r < 6; r++) v[r] = mii_leggi_reg(phy, r);
        for (r = 0; r < 6; r++) if (v[r] != primo[r]) tutti_uguali = 0;

        ref("  %3u   %04x  %04x  %04x  %04x  %04x  %04x\n",
            phy, v[0], v[1], v[2], v[3], v[4], v[5]);
    }

    ref("\n");
    if (tutti_uguali) {
        ref("  ! TUTTI E TRENTADUE RISPONDONO UGUALE: non risponde nessuno.\n");
        ref("    Un PHY vero si distingue dagli altri trentuno, qualunque\n");
        ref("    cosa dica. Il registro 0x08 grezzo: 0x%08x\n",
            reg_leggi(SIS_MII));
        ref("      ffff  la linea sta alta e nessuno la tira giu'\n");
        ref("      0000  la linea sta bassa\n");
        ref("      7fff  alta, ma con la lettura spostata di un bit: e' il\n");
        ref("            caso peggiore, perche' passa per un PHY vivo con il\n");
        ref("            collegamento su a 100 Mbit full duplex\n");
        ref("    Velocita' e duplex qui sotto sono un'ipotesi del driver,\n");
        ref("    non una misura. Prova `sis900.drv -half &`.\n");
    } else {
        ref("  Le righe che si distinguono sono i PHY veri.\n");
        ref("  ! SU UN PHY VERO IL BIT 0 DI BMSR VALE 1, cioe' la colonna\n");
        ref("    BMSR mostra un numero DISPARI. Se e' pari, la lettura e'\n");
        ref("    spostata e velocita' e duplex sono da buttare.\n");
    }
    ref("\n  il driver ha scelto: PHY %d, %s Mbit %s duplex\n\n",
        g_phy, g_veloce ? "100" : "10", g_duplex ? "full" : "half");
}

/* Quanti descrittori TX la scheda non ci ha ancora restituito. Lo riempie
 * ref_anelli() e lo legge ref_contatori(): e' la prova diretta che la scheda
 * i descrittori non li sta leggendo. */
static unsigned int g_tx_in_mano = 0;

static void ref_anelli(void)
{
    unsigned int i, pronti_rx = 0, in_mano_tx = 0;

    ref("== GLI ANELLI ==\n\n");
    ref("  ! D_OWN (bit 31) NON VUOL DIRE LA STESSA COSA DALLE DUE PARTI:\n");
    ref("    vuol dire \"il buffer contiene dati buoni\". In TX lo accendiamo\n");
    ref("    noi, in RX lo accende LEI. Un anello RX con tutti i D_OWN\n");
    ref("    accesi e' un anello che alla scheda risulta pieno.\n\n");

    ref("  RX   n   link       cmdsts     bufptr     D_OWN  OK  lunghezza\n");
    for (i = 0; i < RX_N; i++) {
        volatile unsigned int *d = desc(OFF_RX_DESC, i);
        unsigned int st = d[1];

        if (st & D_OWN) pronti_rx++;
        ref("      %2u   0x%08x 0x%08x 0x%08x  %s    %s  %u\n",
            i, d[0], st, d[2],
            (st & D_OWN) ? "SI " : "no ",
            (st & D_OK)  ? "si" : "no", st & D_LUNGHEZZA);
    }
    ref("      prossimo che leggeremo: %u\n\n", g_rx_prossimo);

    ref("  TX   n   link       cmdsts     bufptr     D_OWN  OK  lunghezza\n");
    for (i = 0; i < TX_N; i++) {
        volatile unsigned int *d = desc(OFF_TX_DESC, i);
        unsigned int st = d[1];

        if (st & D_OWN) in_mano_tx++;
        ref("      %2u   0x%08x 0x%08x 0x%08x  %s    %s  %u\n",
            i, d[0], st, d[2],
            (st & D_OWN) ? "SI " : "no ",
            (st & D_OK)  ? "si" : "no", st & D_LUNGHEZZA);
    }
    ref("      prossimo che useremo: %u\n\n", g_tx_prossimo);

    ref("  %u descrittori RX su %d hanno dentro un pacchetto da leggere\n",
        pronti_rx, RX_N);
    ref("  %u descrittori TX su %d sono ancora in mano alla scheda\n\n",
        in_mano_tx, TX_N);
    g_tx_in_mano = in_mano_tx;

    if (pronti_rx == RX_N)
        ref("  ! L'ANELLO RX E' PIENO: la scheda non ha un posto dove\n"
            "    scrivere. O nessuno sta svuotando, o i D_OWN sono nel\n"
            "    verso sbagliato.\n\n");

    /* ! E GLI ULTIMI RIFIUTATI, GREZZI. Vedi ricorda_rifiutato(): un
     * contatore dice quanti, questi dicono perche'. */
    if (g_rifiutati_n > 0) {
        unsigned int i, quanti = g_rifiutati_n < RIFIUTATI_N
                               ? g_rifiutati_n : RIFIUTATI_N;

        ref("  GLI ULTIMI %u DESCRITTORI RX RIFIUTATI (%u in tutto)\n",
            quanti, g_rifiutati_n);
        ref("  cmdsts     len   err  dest        quali bit\n");

        for (i = 0; i < quanti; i++) {
            unsigned int st = g_rifiutati[(g_rifiutati_n - quanti + i)
                                          % RIFIUTATI_N];

            ref("  0x%08x %4u  %s  %-10s ", st, st & D_LUNGHEZZA,
                (st & D_RX_ERRORI) ? "SI " : "no ",
                ((st & D_BCAST) == D_BCAST) ? "broadcast"
                  : (st & D_MCAST)          ? "multicast"
                  : (st & D_DEST)           ? "per noi"
                                            : "-");
            if (st & D_ABORT)   ref("ABORT ");
            if (st & D_OVERRUN) ref("OVERRUN ");
            if (st & D_TOOLONG) ref("TOOLONG ");
            if (st & D_RUNT)    ref("RUNT ");
            if (st & D_RXISERR) ref("RXISERR ");
            if (st & D_CRCERR)  ref("CRCERR ");
            if (st & D_FAERR)   ref("FAERR ");
            if (st & D_OK)      ref("OK ");
            if (!(st & D_RX_ERRORI))
                ref("- nessun errore: lo ha buttato la LUNGHEZZA");
            ref("\n");

            /* ! LUNGHEZZA ZERO CON «MORE» ACCESO HA UN NOME, ed e' il difetto
             * del referto S4. Vedi RX_DICHIARATO. */
            if ((st & D_LUNGHEZZA) == 0 && (st & D_MORE))
                ref("             ^ zero byte e MORE acceso: LA SCHEDA HA\n"
                    "               LETTO CHE IL BUFFER E' GRANDE ZERO. Il\n"
                    "               campo della dimensione e' piu' stretto di\n"
                    "               quel che sembra e il valore dichiarato non\n"
                    "               ci sta. Adesso dichiariamo %d.\n",
                    RX_DICHIARATO);
        }
        ref("\n");
    }
}

static void ref_contatori(void)
{
    ref("== I CONTATORI ==\n\n");
    ref("  inviati        %u\n", g_cont.inviati);
    ref("  ricevuti       %u\n", g_cont.ricevuti);
    ref("  errori TX      %u\n", g_cont.errori_tx);
    ref("  errori RX      %u\n", g_cont.errori_rx);
    ref("  troppo grandi  %u\n", g_cont.troppo_grandi);
    ref("  persi in coda  %u\n", g_cont.persi_coda);
    ref("  traboccati     %u\n", g_cont.overflow);
    ref("  interrupt      %u\n", g_cont.notifiche_irq);
    ref("  battiti        %u\n", g_cont.battiti);
    ref("  in coda adesso %d\n", g_coda_conta);
    ref("  chi aspetta    %u\n\n", g_lettore_pid);

    ref("== COSA NE SEGUE ==\n\n");

    /* ! L'ORDINE DI QUESTE DOMANDE E' IL PUNTO. Ognuna, se risponde si',
     * rende senza senso tutte quelle dopo: una scheda che non tocca la
     * memoria ha per forza gli anelli fermi, e guardare gli anelli in quel
     * caso vuol dire studiare una conseguenza credendo sia una causa. Il 14
     * settembre 2026 la prima versione di questo blocco l'ha fatto: davanti
     * a quattro descrittori TX mai restituiti e a 339 traboccamenti ha
     * concluso «e' l'anello RX» e ha consigliato `-half`, che era la strada
     * piu' lontana da quella giusta. */

    /* 1. Puo' la scheda toccare la memoria? */
    if (!(g_pci_comando & PCI_CMD_BUSMASTER)) {
        ref("  IL BUS MASTER E' SPENTO (comando PCI 0x%04x). La scheda non\n"
            "  puo' ne' leggere i descrittori ne' scrivere i pacchetti.\n"
            "  Tutto il resto di questo referto e' una conseguenza.\n",
            g_pci_comando);
        return;
    }

    /* 2. Lo fa? Il segno e' nei descrittori TX consegnati e mai restituiti:
     *    e' l'unica prova diretta che abbiamo, perche' quel bit lo spegne
     *    la scheda e nessun altro. */
    if (g_tx_in_mano > 0 && g_cont.ricevuti == 0) {
        ref("  IL BUS MASTER E' ACCESO MA LA SCHEDA NON LO USA.\n\n");
        ref("  %u descrittori TX sono stati consegnati e non sono mai\n",
            g_tx_in_mano);
        ref("  tornati indietro: quel bit lo spegne LEI quando ha finito, e\n");
        ref("  non l'ha spento. Non li ha nemmeno letti: se li avesse letti\n");
        ref("  e qualcosa fosse andato storto ci sarebbe TxERR in ISR.\n\n");

        if (g_cont.overflow > 0)
            ref("  E i %u traboccamenti sono la stessa cosa vista da sopra:\n"
                "  i frame arrivano nella FIFO della scheda, lei non riesce a\n"
                "  versarli in memoria, la FIFO strabocca. Con l'anello RX\n"
                "  intatto e libero, come si vede qui sopra.\n\n",
                g_cont.overflow);

        /* ! E QUI IL REFERTO SI DIVIDE IN DUE, che sono due guasti diversi
         * con lo stesso aspetto: una scheda che CI PROVA e viene respinta, e
         * una che non ci prova nemmeno. Lo dice il registro di stato. */
        if (g_pci_stato & (0x2000 | 0x1000)) {
            ref("  ! E IL REGISTRO DI STATO DICE CHE CI PROVA: c'e' un %s\n",
                (g_pci_stato & 0x2000) ? "master abort" : "target abort");
            ref("    abort ricevuto. I cicli partono e il ponte li\n");
            ref("    interrompe, quindi il guasto e' L'INDIRIZZO: quello\n");
            ref("    fisico che le abbiamo dato non esiste per il ponte, o\n");
            ref("    non gli e' permesso arrivarci.\n\n");
            ref("    DMA fisico 0x%08x, RXDP 0x%08x, TXDP 0x%08x\n\n",
                g_dma_fis, reg_leggi(SIS_RXDP), reg_leggi(SIS_TXDP));
            ref("    Da guardare: dma_alloc() rende davvero memoria che il\n");
            ref("    ponte sa raggiungere? E il ponte fra noi e la scheda\n");
            ref("    lascia passare i cicli che inizia lei? (`mappa.drv`)\n");
        } else {
            unsigned int cfg   = reg_leggi(SIS_CFG);
            unsigned int mx_tx = (reg_leggi(SIS_TXCFG) >> MXDMA_SHIFT) & 7;

            ref("  ! E IL REGISTRO DI STATO E' PULITO: nessun abort. La\n");
            ref("    scheda non e' stata respinta: NON CI HA PROVATO. I\n");
            ref("    cicli non partono proprio.\n\n");

            /* ! E LA PRIMA COSA DA GUARDARE E' LA RAFFICA, perche' e' il
             * difetto che questo stesso referto ha trovato sull'Acer il 14
             * settembre 2026. Vedi DMA_RAFFICA_64. */
            if ((cfg & CFG_EDB_MASTER) && mx_tx != DMA_RAFFICA_64) {
                ref("    ED E' LA RAFFICA. CFG ha EDB_MASTER_EN acceso (bit\n");
                ref("    13 di 0x%08x) e la raffica DMA vale %u, cioe' 512\n",
                    cfg, mx_tx);
                ref("    byte. In modo EDB la scheda vuole 64 byte, codice 5:\n");
                ref("    con 512 non inizia un ciclo, ed e' esattamente quel\n");
                ref("    che si vede qui sopra.\n");
            } else {
                ref("    Da guardare, in quest'ordine:\n");
                ref("      1. la raffica DMA: CFG 0x%08x, EDB %s, raffica\n",
                    cfg, (cfg & CFG_EDB_MASTER) ? "acceso" : "spento");
                ref("         %u. Con EDB acceso dev'essere 5 (64 byte).\n",
                    mx_tx);
                ref("      2. TXDP e RXDP: 0x%08x e 0x%08x, e l'anello sta\n",
                    reg_leggi(SIS_TXDP), reg_leggi(SIS_RXDP));
                ref("         a 0x%08x. Se non combaciano, la scheda guarda\n",
                    g_dma_fis);
                ref("         altrove.\n");
                ref("      3. il reset: TxRCMP e RxRCMP sono arrivati tutti\n");
                ref("         e due? Se il driver e' partito senza\n");
                ref("         lamentarsi, si'.\n");
            }
        }
        return;
    }

    /* 3. Da qui in giu' la scheda la memoria la tocca, e le domande sono
     *    quelle di sempre. */
    if (g_cont.inviati == 0)
        ref("  Non e' uscito niente. Il guasto sta SOPRA questo driver:\n"
            "  fra ip.drv e la scheda nessuno ha chiesto di trasmettere.\n"
            "  Guarda che ip.drv sia acceso e che dhcp sia partito.\n");
    else if (g_cont.ricevuti == 0 && g_cont.overflow > 0) {
        unsigned int cr = reg_leggi(SIS_CR);

        ref("  Arriva roba e non c'e' dove metterla: traboccati sale e\n"
            "  ricevuti resta zero. La scheda in memoria ci scrive (i\n"
            "  descrittori TX tornano indietro), quindi il DMA va.\n\n");

        /* ! LA PRIMA DOMANDA E' SE IL MOTORE E' ANCORA ACCESO. Vedi la riga
         * che riaccende RxENA in fondo a svuota_rx. */
        if (!(cr & CR_RXENA))
            ref("  ! E IL MOTORE DI RICEZIONE E' SPENTO: CR vale 0x%08x.\n"
                "    Non l'ha spento nessuno, si e' fermato lui: succede\n"
                "    quando finisce i descrittori o incontra un errore, e da\n"
                "    solo non riparte. I traboccamenti sono i frame arrivati\n"
                "    da quel momento in poi. Va riacceso dopo ogni giro di\n"
                "    svuotamento.\n", cr);
        else if (g_rifiutati_n > 0)
            ref("  Il motore e' acceso e i descrittori si riempiono, ma il\n"
                "  driver li scarta tutti: guarda l'elenco dei RIFIUTATI qui\n"
                "  sopra, dice quale bit li ha fatti buttare.\n");
        else
            ref("  Il motore e' acceso e nessun descrittore e' stato\n"
                "  scartato: la scheda non sta proprio riempiendo l'anello.\n"
                "  Guarda RXDP e i D_OWN qui sopra.\n");
    }
    else if (g_cont.ricevuti == 0)
        ref("  Esce e non torna niente, e l'anello non trabocca: la scheda\n"
            "  non vede proprio arrivare nulla. Tre cause, in quest'ordine:\n"
            "    1. il filtro. Se RFCR qui sopra non ha il broadcast, il\n"
            "       DHCP non puo' funzionare: prova `-promiscuo`.\n"
            "    2. il duplex, MA SOLO SE IL PHY NON HA RISPOSTO. Guarda la\n"
            "       sezione del filo di gestione: se un indirizzo si\n"
            "       distingue e il suo BMSR e' dispari, il duplex e' misurato\n"
            "       e non c'entra. Se invece non ha risposto nessuno, il\n"
            "       driver tira a indovinare 100 full mentre lo switch\n"
            "       ripiega su HALF: prova `-half`.\n"
            "    3. il cavo.\n");
    else
        ref("  LA SCHEDA RICEVE. Da qui in giu' funziona: se manca\n"
            "  l'indirizzo il guasto e' sopra, in dhcp o in ip.drv.\n"
            "  Guarda `ipcfg` e `dhcp -n`.\n");

    if (g_cont.notifiche_irq == 0 && g_cont.battiti > 0)
        ref("\n  Nessun interrupt: la scheda va avanti a battiti da 250 ms,\n"
            "  piu' lenta ma viva. L'IRQ %u e' di qualcun altro?\n", g_irq);
}

static void ref_dove_finisce(const char *percorso)
{
    /* ! LA RADICE DI QUEL DISCHETTO STA IN RAM, e un file scritto li' non
     * esiste piu' dopo lo spegnimento. Il dischetto di prova dell'Acer si
     * avvia con RAMDISCO=1 perche' su quella macchina il lettore sta
     * sull'USB e il kernel non lo sa usare: Stage 2 copia tutto il supporto
     * in memoria e la radice e' quella copia.
     *
     * ! E' GIA' COSTATO UN REFERTO, il 10 settembre 2026: due prove del video
     * «in due modalita'» sono uscite identiche perche' svga.drv aveva scritto
     * il suo byte su un disco che non esisteva piu'. Un referto perso e' un
     * viaggio perso, quindi la riga si stampa sempre.
     *
     * Chi ha una chiavetta infilata puo' saltare il passaggio scrivendoci
     * direttamente dentro. */
    /* ! /USB DA SOLO NON E' LA CHIAVETTA, E' UNA CARTELLA IN RAM COME LE
     * ALTRE. Il punto di innesto lo crea mksonda.sh sul dischetto, e c'e'
     * anche quando non c'e' infilato niente: scriverci dentro riesce, e il
     * file finisce nella copia in memoria esattamente come in radice. La
     * chiavetta e' /USB/DRIVE0, che compare solo quando automount l'ha
     * montata — e se non c'e', open() fallisce e lo si sa subito.
     *
     * Quindi il promemoria si salta solo per /USB/DRIVE..., non per /USB/. */
    if (strncmp(percorso, "/USB/DRIVE", 10) == 0) return;

    printf("\n");
    printf("        ! LA RADICE E' IN RAM: questo file sparisce quando\n");
    printf("        spegni. Portatelo via prima:\n");
    printf("          cp %s /USB/DRIVE0/\n", percorso);
    printf("\n");
    printf("        Oppure scrivilo subito sulla chiavetta:\n");
    printf("          sis900.drv -debug /USB/DRIVE0/SIS900.TXT\n");
}

/* Rende 0 se il file c'e', -errno se non si e' potuto scriverlo. */
static int ref_apri(const char *percorso)
{
    g_ref_usati  = 0;
    g_ref_guasto = 0;

    g_ref_fd = open(percorso, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (g_ref_fd < 0) return g_ref_fd;

    ref("EX-OS - referto di sis900.drv\n");
    ref("=============================\n\n");
    ref("  Preso con `sis900.drv -debug`. Tutto quel che si puo' sapere in\n");
    ref("  un colpo solo: questa scheda QEMU non la emula, quindi ogni\n");
    ref("  domanda costa un viaggio alla macchina vera, e un referto che\n");
    ref("  risponde alla domanda di ieri e' un viaggio da rifare.\n\n");

    return 0;
}

static int ref_chiudi(void)
{
    ref("\n-- fine del referto --\n");
    ref_svuota();
    close(g_ref_fd);
    g_ref_fd = -1;

    return g_ref_guasto ? -EIO : 0;
}

/* ! ANCHE «LA SCHEDA NON C'E'» E' UN REFERTO, e questa e' la parte che serve
 * quando il guasto sta prima di tutto il resto: netdetect non la vede, il
 * driver dice «nessuna SiS 900 sul bus», e la domanda diventa «e allora cosa
 * c'e' li' dentro?». Elencare ogni Ethernet che il bus dichiara risponde a
 * quella domanda senza un secondo viaggio — e dice anche il caso in cui la
 * scheda c'e' ma con un identificativo che questo driver non riconosce, che
 * e' una riga da aggiungere e non un guasto. */
static void ref_bus_pci(void)
{
    int pid = ipc_attendi(PCI_SERVIZIO, ATTESA_PCI_MS);
    unsigned int n;
    int quante = 0;

    ref("== COSA C'E' SUL BUS ==\n\n");

    if (pid <= 0) {
        ref("  Il servizio '%s' non e' attivo: senza, nessun driver PCI\n",
            PCI_SERVIZIO);
        ref("  puo' trovare niente. Avvialo con  /dev/pci.drv &\n\n");
        return;
    }

    ref("  Ogni Ethernet che il bus dichiara:\n\n");
    ref("  bus:slot.f  ven:disp  rev  IRQ  porte\n");

    for (n = 0; n < 16; n++) {
        PciRichiesta   r;
        PciDispositivo d;
        IpcMessage     meta;
        unsigned char  buf[IPC_MSG_MAX_DATA];
        int i, tentativi;
        unsigned int porte = 0;

        r.ordinale    = n;
        r.classe      = PCI_CLASSE_RETE;
        r.sottoclasse = PCI_SOTTO_ETHERNET;
        r.venditore   = PCI_QUALUNQUE;
        r.dispositivo = PCI_QUALUNQUE;

        if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) break;

        for (tentativi = 0; tentativi < 8; tentativi++) {
            if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) break;
            if ((int)meta.sender_pid == pid) break;
        }
        if ((int)meta.sender_pid != pid) break;
        if (meta.tipo == PCI_MSG_FINE) break;
        if (meta.tipo != PCI_MSG_DISPOSITIVO || meta.len < sizeof(d)) break;
        memcpy(&d, buf, sizeof(d));

        for (i = 0; i < 6; i++)
            if (d.bar_io[i] && d.bar[i] != 0) { porte = d.bar[i]; break; }

        ref("  %02x:%02x.%-2d   %04x:%04x  %02x   %-3u  0x%x%s\n",
            d.bus, d.slot, d.funzione, d.venditore, d.dispositivo,
            d.revisione, d.irq_linea, porte,
            (d.venditore == SIS_VENDITORE &&
             (d.dispositivo == SIS_900 || d.dispositivo == SIS_7016))
                ? "   <- questa e' la nostra" : "");
        quante++;
    }

    if (quante == 0) {
        ref("  nessuna.\n\n");
        ref("  ! IL BUS NON DICHIARA NEMMENO UNA ETHERNET. Non e' un guasto\n");
        ref("    di questo driver: o la scheda e' spenta nel BIOS, o pci.drv\n");
        ref("    non arriva a quel bus.\n");
    } else {
        ref("\n  %d in tutto. Questo driver riconosce 1039:0900 e\n", quante);
        ref("  1039:7016: se qui sopra c'e' una SiS con un altro numero,\n");
        ref("  e' una riga da aggiungere, non un guasto.\n");
    }
    ref("\n");
}

/* Rende 0 se il file c'e', -errno se non si e' potuto scriverlo. */
static int scrivi_referto_senza_scheda(const char *percorso)
{
    int rc = ref_apri(percorso);

    if (rc < 0) return rc;

    ref("  ! LA SCHEDA NON E' STATA TROVATA, e questo referto dice cosa\n");
    ref("    c'era al posto suo.\n\n");

    ref_bus_pci();
    return ref_chiudi();
}

static int scrivi_referto(const char *percorso)
{
    int rc;

    if (percorso == NULL || percorso[0] == '\0')
        percorso = REFERTO_PREDEFINITO;

    rc = ref_apri(percorso);
    if (rc < 0) return rc;

    ref_scheda();
    ref_registri();
    ref_filtro();
    ref_phy();
    ref_anelli();
    ref_contatori();

    return ref_chiudi();
}

/* =============================================================================
 * Spegnimento
 *
 * ! CHI ACCENDE LA SCHEDA E POI ESCE DEVE SPEGNERLA, e fino al 14 settembre
 * 2026 non lo faceva nessuno. `-l`, `-d` e `-phy` fanno l'inizializzazione
 * completa — reset, anelli, filtro, interrupt — e poi rendono zero. Il
 * processo muore, il kernel si riprende la zona DMA e la da' a qualcun altro,
 * e la scheda continua a scriverci dentro i pacchetti che arrivano: il
 * puntatore all'anello ce l'ha lei nei registri, e non sa niente di chi e'
 * morto.
 *
 * ! E L'INTERRUPT RESTA APPESO. IER e IMR restano armati, la scheda alza la
 * linea, e il proprietario non c'e' piu': nessuno chiamera' irq_done, e
 * quella linea resta chiusa per chi viene dopo.
 *
 * Tre righe, nell'ordine che conta: prima si tolgono gli interrupt — se si
 * resettasse per primo, fra il reset e lo spegnimento c'e' una finestra in
 * cui la scheda puo' ancora alzare la linea — poi si fermano i due motori,
 * e infine il reset, che e' quello che fa dimenticare alla scheda gli
 * indirizzi fisici dei nostri anelli.
 * ========================================================================== */
static void spegni_scheda(void)
{
    reg_scrivi(SIS_IER, 0);
    reg_scrivi(SIS_IMR, 0);
    (void)reg_leggi(SIS_ISR);            /* leggerlo lo azzera */

    reg_scrivi(SIS_CR, CR_RXDIS | CR_TXDIS);
    reg_scrivi(SIS_RXFILTCTRL, 0);
    reg_scrivi(SIS_CR, CR_RXRESET | CR_TXRESET | CR_RESET);
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

    /* ! CI SI FERMA DOPO UN GIRO INTERO. Se un descrittore restasse con D_OWN
     * acceso per un difetto — o se la scheda riempisse l'anello piu' in fretta
     * di quanto lo si svuota — un ciclo senza limite girerebbe per sempre
     * dentro il gestore e il sistema si fermerebbe li'. */
    for (giri = 0; giri < RX_N; giri++) {
        volatile unsigned int *d = desc(OFF_RX_DESC, g_rx_prossimo);
        unsigned int stato = d[1];
        unsigned int len;

        /* D_OWN spento vuol dire che la scheda non ci ha ancora scritto
         * dentro niente: da qui in poi l'anello e' vuoto. */
        if (!(stato & D_OWN)) break;

        len = stato & D_LUNGHEZZA;

        /* ! LA LUNGHEZZA COMPRENDE I QUATTRO BYTE DI CONTROLLO, e vanno tolti:
         * consegnarli allo stack vorrebbe dire quattro byte di spazzatura in
         * coda a ogni pacchetto, che i livelli sopra non guardano e che
         * rompono il conto di chi lo fa. */
        /* ! IL PACCHETTO E' BUONO FINCHE' NON C'E' SCRITTO CHE E' ROTTO.
         * Vedi D_RX_ERRORI: prima si cercava D_OK acceso, e sul referto S3
         * questo ha buttato otto pacchetti su otto. */
        if (!(stato & D_RX_ERRORI) && len > 4) {
            len -= 4;

            /* ! IL TETTO NON E' PIGNOLERIA: LA LUNGHEZZA LA SCRIVE LA SCHEDA.
             * Il campo e' di dodici bit, cioe' fino a 4095, mentre il buffer
             * ne tiene 2048: un descrittore corrotto — o letto mentre la
             * scheda ci sta ancora scrivendo — fa leggere ad accoda oltre la
             * fine del buffer, dentro quello del descrittore dopo.
             *
             * ! E LA CONDIZIONE DI PRIMA NON FILTRAVA NIENTE. Era
             * `len >= NET_FRAME_MIN || len > 0`: la seconda meta' e' vera per
             * ogni len positiva, quindi la prima non veniva mai guardata.
             * Sembrava un controllo e non lo era. */
            if (len >= NET_FRAME_MIN && len <= RX_DICHIARATO) {
                accoda(buffer_virt(g_rx_prossimo), len);
                g_cont.ricevuti++;
            } else {
                ricorda_rifiutato(stato);
                g_cont.errori_rx++;
            }
        } else {
            ricorda_rifiutato(stato);
            g_cont.errori_rx++;
        }

        /* Il descrittore torna alla scheda, vuoto: spegnere D_OWN E' il
         * modo di ridarglielo. */
        d[1] = RX_DICHIARATO;
        g_rx_prossimo = (g_rx_prossimo + 1) % RX_N;
    }

    /* ! IL MOTORE DI RICEZIONE VA RIACCESO OGNI VOLTA, E NON RIPARTE DA SOLO.
     * E' la correzione del 14 settembre 2026, e il referto S3 dell'Acer la
     * mostrava in una riga: CR valeva 0x00000000 — RX SPENTO — dopo che la
     * scheda aveva riempito otto descrittori. Nessuno l'aveva spento: si e'
     * fermata lei.
     *
     * Il motore RX della SiS va in stallo quando finisce i descrittori o
     * incontra un errore, e restarci e' definitivo: da quel momento i frame
     * arrivano, non entrano in memoria, e la FIFO trabocca. Nel referto si
     * leggeva cosi': otto pacchetti nel primo giro, poi millesettanta
     * traboccamenti e mai piu' un RXOK.
     *
     * Linux chiude sis900_rx() proprio con questa riga, e il commento accanto
     * dice perche':
     *
     *     // re-enable the potentially idle receive state matchine
     *     sw32(cr, RxENA | sr32(cr));
     *
     * ! E SI SCRIVE IN OR, NON SOPRA. Gli altri bit di CR — TxENA fra questi —
     * sono lo stato dei motori: riscrivere il registro intero con il solo
     * RxENA spegnerebbe la trasmissione a ogni pacchetto ricevuto. */
    reg_scrivi(SIS_CR, CR_RXENA | reg_leggi(SIS_CR));
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

    /* ! IN OR, per la stessa ragione di svuota_rx: scrivere il solo TxENA
     * spegnerebbe la ricezione a ogni pacchetto trasmesso. */
    reg_scrivi(SIS_CR, CR_TXENA | reg_leggi(SIS_CR));

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

                /* ! LA RISPOSTA SI GUARDA, e prima si buttava. Vedi
                 * g_pci_comando. */
                if ((int)meta.sender_pid == pid &&
                    meta.tipo == PCI_MSG_ESITO &&
                    meta.len >= sizeof(PciEsito)) {
                    PciEsito es;

                    memcpy(&es, buf, sizeof(es));
                    g_pci_esito   = es.codice;
                    g_pci_comando = es.comando;
                }

                if (!(g_pci_comando & PCI_CMD_BUSMASTER)) {
                    printf("sis900: ! IL BUS MASTER NON SI E' ACCESO.\n");
                    printf("        Registro comando del PCI: 0x%04x",
                           g_pci_comando);
                    if (g_pci_esito < 0)
                        printf(" (pci.drv non ha risposto)");
                    printf("\n");
                    printf("\n");
                    printf("        Senza quel bit il ponte rifiuta ogni\n");
                    printf("        ciclo che la scheda inizia: i registri\n");
                    printf("        rispondono benissimo (quelli li avviamo\n");
                    printf("        noi), ma lei non legge i descrittori e\n");
                    printf("        non scrive i pacchetti. Trasmette zero e\n");
                    printf("        riceve zero, e la FIFO trabocca.\n");
                    printf("\n");
                    printf("        Vado avanti lo stesso, cosi' `-debug`\n");
                    printf("        raccoglie il resto: ma finche' questo\n");
                    printf("        bit non si alza, non c'e' altro da\n");
                    printf("        cercare.\n");
                }
                return 1;
            }
        }
    }
    return 0;
}

/* =============================================================================
 * -stato: cosa sta succedendo davvero
 *
 * ! «NON PRENDE L'INDIRIZZO» NON E' UN GUASTO, E' UN SINTOMO, e sotto ci
 * stanno tre guasti diversi che si curano in tre modi diversi:
 *
 *   il pacchetto non esce          inviati resta a zero, o errori_tx sale
 *   esce e non torna niente        inviati sale, ricevuti resta a zero
 *   torna e lo buttiamo via        ricevuti sale, e allora il guaio e' sopra
 *
 * Un contatore distingue i tre casi in un colpo d'occhio; guardare lo schermo
 * e riferire «non funziona» non li distingue affatto. Questa funzione esiste
 * per non fare piu' un viaggio all'Acer per sapere quale dei tre e'.
 * ========================================================================== */
static void stampa_contatori(void);

static void stampa_diagnosi(void)
{
    unsigned int cr    = reg_leggi(SIS_CR);
    unsigned int cfg   = reg_leggi(SIS_CFG);
    unsigned int isr   = reg_leggi(SIS_ISR);
    unsigned int rfcr  = reg_leggi(SIS_RXFILTCTRL);

    printf("sis900: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    printf("        CR %08x  CFG %08x  ISR %08x\n", cr, cfg, isr);
    printf("        filtro %08x: acceso %s, broadcast %s, multicast %s,"
           " tutto %s\n", rfcr,
           (rfcr & RFC_RFEN) ? "si" : "NO",
           (rfcr & RFC_AAB)  ? "si" : "NO",
           (rfcr & RFC_AAM)  ? "si" : "no",
           (rfcr & RFC_AAP)  ? "si" : "no");
    if (g_phy >= 0) {
        unsigned int bmsr;

        (void)mii_leggi_reg((unsigned int)g_phy, MII_BMSR);
        bmsr = mii_leggi_reg((unsigned int)g_phy, MII_BMSR);
        printf("        PHY %d: BMSR %04x, collegamento %s, trattativa %s\n",
               g_phy, bmsr,
               (bmsr & BMSR_LINK) ? "SU" : "GIU'",
               (bmsr & BMSR_ANEG_OK) ? "finita" : "in corso");
    } else {
        printf("        PHY: nessuno risponde\n");
    }

    printf("        RX %s, TX %s\n",
           (cr & CR_RXENA) ? "acceso" : "SPENTO",
           (cr & CR_TXENA) ? "acceso" : "fermo");
    printf("\n");
    stampa_contatori();
}

/* I contatori e cosa vogliono dire. Si usa da sola quando li chiediamo al
 * driver acceso, dove tutto il resto — registri, PHY — non lo possiamo
 * guardare senza disturbarlo. */
static void stampa_contatori(void)
{
    printf("        inviati    %u    errori TX %u\n",
           g_cont.inviati, g_cont.errori_tx);
    printf("        ricevuti   %u    errori RX %u\n",
           g_cont.ricevuti, g_cont.errori_rx);
    printf("        in coda    %d    persi in coda %u\n",
           g_coda_conta, g_cont.persi_coda);
    printf("        interrupt  %u    battiti %u\n",
           g_cont.notifiche_irq, g_cont.battiti);
    /* ! L'ANELLO TRABOCCATO SI STAMPA, e prima non si stampava: il contatore
     * si alzava e non lo vedeva nessuno. E' il numero che distingue «non
     * arriva niente sul filo» da «arriva e non c'e' posto dove metterlo», che
     * sono due guasti opposti con lo stesso sintomo. Vedi D_OWN. */
    printf("        traboccati %u\n", g_cont.overflow);
    printf("\n");

    /* ! E SI DICE COSA VOGLIONO DIRE, invece di lasciare cinque numeri a chi
     * legge. Chi guarda questo schermo sta cercando di capire perche' la rete
     * non va, non sta studiando la scheda. */
    /* ! IL COLLEGAMENTO SI GUARDA PRIMA DI TUTTO, quando lo sappiamo. Con il
     * filo giu' ogni altro numero e' una conseguenza, e leggerli come cause
     * porta lontano. Chiedendo i contatori a un driver acceso il PHY non lo
     * si puo' interrogare — parlargli vorrebbe dire scrivere su registri che
     * lui sta usando — e allora questa parte si salta. */
    if (g_phy >= 0) {
        unsigned int bmsr;

        (void)mii_leggi_reg((unsigned int)g_phy, MII_BMSR);
        bmsr = mii_leggi_reg((unsigned int)g_phy, MII_BMSR);
        if (!(bmsr & BMSR_LINK)) {
            printf("        -> il filo non porta niente: e' quello, e non\n");
            printf("           serve guardare altro finche' non torna su.\n");
            return;
        }
    }

    if (g_cont.inviati == 0)
        printf("        -> non e' uscito niente: nessuno ha chiesto di\n"
               "           trasmettere, o lo stack IP non e' partito.\n");
    else if (g_cont.ricevuti == 0 && g_cont.overflow > 0)
        printf("        -> arriva roba e non c'e' dove metterla: traboccati\n"
               "           sale e ricevuti resta zero. Non e' il cavo e non e'\n"
               "           il filtro: e' l'anello di ricezione, che alla\n"
               "           scheda risulta pieno. Guarda D_OWN in svuota_rx.\n");
    else if (g_cont.ricevuti == 0)
        printf("        -> escono e non torna niente. Il cavo e' attaccato?\n"
               "           Il filtro accetta il broadcast? Prova\n"
               "           `sis900.drv -promiscuo &`: se cosi' arriva\n"
               "           qualcosa, il guaio e' nel filtro o nel MAC.\n");
    else
        printf("        -> la scheda riceve. Se manca l'indirizzo, il guaio\n"
               "           e' sopra: guarda `ipcfg` e `dhcp -n`.\n");

    if (g_cont.notifiche_irq == 0 && g_cont.battiti > 0)
        printf("        -> nessun interrupt: la scheda va avanti a battiti,\n"
               "           piu' lenta ma viva. L'IRQ %u e' di qualcun altro?\n",
               g_irq);
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
    printf("        anelli %d RX, %d TX da %d byte (RX dichiarati %d)\n",
           RX_N, TX_N, BUF_LEN, RX_DICHIARATO);
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

        /* ! IL REFERTO LO SCRIVIAMO NOI, che la scheda ce l'abbiamo in mano.
         * Vedi NET_MSG_REFERTO in net_proto.h e il blocco sopra
         * scrivi_referto().
         *
         * ! E LA RETE STA FERMA MENTRE SI SCRIVE. Un floppy e' lento, e
         * questo giro di servizio e' uno solo: per il tempo di una
         * ventina di settori nessuno viene servito. E' accettabile perche'
         * lo si chiede a mano, una volta, quando la rete gia' non va —
         * ma va detto, perche' un pacchetto perso proprio in quel momento
         * non deve mandare a cercare un guasto che non c'e'. */
        case NET_MSG_REFERTO: {
            NetEsito e;
            char     percorso[128];
            unsigned int n = meta.len;

            if (n >= sizeof(percorso)) n = sizeof(percorso) - 1;
            memcpy(percorso, payload, n);
            percorso[n] = '\0';

            e.codice = scrivi_referto(percorso);
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }

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
/* «00:11:22:33:44:55» -> sei byte. Rende 0 se va, -1 se non e' un indirizzo.
 *
 * ! SI RIFIUTA QUELLO SBAGLIATO INVECE DI PRENDERNE UN PEZZO. Un MAC scritto
 * male e accettato a meta' da' una scheda che parte e non riceve niente, cioe'
 * lo stesso sintomo che si sta cercando di curare. */
static int leggi_mac_scritto(const char *s)
{
    unsigned char m[6];
    int i, alto, basso;

    for (i = 0; i < 6; i++) {
        alto  = cifra_esa(*s++);
        basso = cifra_esa(*s++);
        if (alto < 0 || basso < 0) return -1;
        m[i] = (unsigned char)((alto << 4) | basso);

        if (i < 5) {
            if (*s != ':' && *s != '-') return -1;
            s++;
        }
    }
    if (*s != '\0') return -1;

    memcpy(g_mac, m, 6);
    return 0;
}

static void uso(void)
{
    printf("uso: /dev/sis900.drv [-i] [-l]\n\n");
    printf("  -i   sonda il bus, dice cosa ha trovato ed esce (0 = trovata).\n");
    printf("       Non tocca la scheda.\n");
    printf("  -l   accende la scheda, stampa lo stato ed esce\n");
    printf("  -d   accende la scheda e dice cosa sta succedendo:\n");
    printf("       MAC, filtro, motori, contatori\n");
    printf("  -mac IND    usa questo indirizzo invece di cercarlo.\n");
    printf("       Serve su questa revisione, dove la scheda non\n");
    printf("       lo dice: sta sull'etichetta sotto il portatile,\n");
    printf("       o in Windows con  ipconfig /all\n");
    printf("  -phy  setaccia il filo di gestione e stampa cosa\n");
    printf("       risponde a ogni indirizzo: serve quando il PHY\n");
    printf("       non si trova\n");
    printf("  -debug [FILE]  SCRIVE TUTTO IN UN FILE, ed e' il comando\n");
    printf("       da usare quando qualcosa non va: registri, le 32\n");
    printf("       righe del filo di gestione, i 16 descrittori, i\n");
    printf("       contatori e cosa ne segue. Con il driver acceso lo\n");
    printf("       scrive LUI, che la scheda ce l'ha in mano, e la\n");
    printf("       rete non si tocca. Senza FILE: %s\n",
           REFERTO_PREDEFINITO);
    printf("  -half  -full   il duplex lo dici tu, invece di\n");
    printf("  -10    -100     chiederlo al PHY. Servono quando il filo\n");
    printf("       di gestione non risponde e il driver tira a\n");
    printf("       indovinare: se `-half` fa funzionare la rete, il\n");
    printf("       guasto era il disaccordo sul duplex.\n");
    printf("  -promiscuo  accetta OGNI pacchetto, non solo i suoi.\n");
    printf("       Se cosi' la rete va e senza no, il guaio e'\n");
    printf("       nel filtro o nell'indirizzo MAC.\n\n");
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
    int sonda = 0, solo_elenco = 0, diagnosi = 0, setaccio = 0, referto = 0;
    const char *percorso = REFERTO_PREDEFINITO;
    int i, rc;
    DmaZona z;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) { sonda = 1; continue; }
        if (strcmp(argv[i], "-l") == 0) { solo_elenco = 1; continue; }
        if (strcmp(argv[i], "-promiscuo") == 0) { g_promiscuo = 1; continue; }
        if (strcmp(argv[i], "-mac") == 0 && i + 1 < argc) {
            if (leggi_mac_scritto(argv[++i]) != 0) {
                printf("sis900: '%s' non e' un indirizzo MAC.\n", argv[i]);
                printf("        Si scrive cosi': 00:11:22:33:44:55\n");
                return 1;
            }
            g_mac_dato = 1;
            continue;
        }
        if (strcmp(argv[i], "-d") == 0) { diagnosi = 1; continue; }
        if (strcmp(argv[i], "-phy") == 0) { setaccio = 1; continue; }
        if (strcmp(argv[i], "-debug") == 0) {
            referto = 1;
            /* Il percorso e' facoltativo, e non comincia per meno: cosi'
             * `-debug -half` non si mangia l'opzione dopo. */
            if (i + 1 < argc && argv[i + 1][0] != '-') percorso = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-half") == 0) { g_forza_duplex = 0; continue; }
        if (strcmp(argv[i], "-full") == 0) { g_forza_duplex = 1; continue; }
        if (strcmp(argv[i], "-10") == 0)   { g_forza_veloce = 0; continue; }
        if (strcmp(argv[i], "-100") == 0)  { g_forza_veloce = 1; continue; }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            uso();
            return 0;
        }
        printf("sis900: non conosco '%s'. Prova -h.\n", argv[i]);
        return 1;
    }

    /* =========================================================================
     * ! -d SI CHIEDE AL DRIVER CHE STA LAVORANDO, e questa e' la correzione
     * dell'11 settembre 2026. La prima versione accendeva la scheda e
     * stampava i propri contatori, che erano a zero per costruzione: quel
     * processo era appena nato e non aveva mai trasmesso niente. C'era pure
     * il commento che lo ammetteva, e l'ho spedito lo stesso come strumento
     * di diagnosi.
     *
     * ! E FACEVA DANNO, non solo niente. Con la rete accesa, `-d` avviava un
     * SECONDO driver che resettava la scheda sotto al primo: si guardava lo
     * stato di una rete che si stava rompendo nel guardarla.
     *
     * Adesso, se il servizio c'e', gli si chiedono i contatori e non si tocca
     * niente. La scheda la si accende solo se nessuno la sta gia' guidando.
     * ===================================================================== */
    if (diagnosi) {
        int pid = ipc_lookup(NET_SERVIZIO_0);

        if (pid > 0) {
            IpcMessage    meta;
            unsigned char buf[IPC_MSG_MAX_DATA];
            int           t;

            printf("sis900: il servizio '%s' e' acceso (PID %d): chiedo a lui.\n",
                   NET_SERVIZIO_0, pid);

            if (ipc_send(pid, NET_MSG_CONTATORI, NULL, 0) < 0) {
                printf("        non risponde.\n");
                return 1;
            }
            for (t = 0; t < 8; t++) {
                if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) {
                    printf("        non risponde.\n");
                    return 1;
                }
                if ((int)meta.sender_pid == pid) break;
            }
            if (meta.tipo != NET_MSG_CONTEGGI || meta.len < sizeof(g_cont)) {
                printf("        ha risposto qualcos'altro.\n");
                return 1;
            }
            memcpy(&g_cont, buf, sizeof(g_cont));
            stampa_contatori();
            return 0;
        }

        printf("sis900: nessun driver acceso: accendo la scheda e guardo.\n");
        printf("        I contatori saranno a zero: nessuno ha ancora\n");
        printf("        trasmesso niente. Per quelli veri, lancia prima\n");
        printf("        `sis900.drv &` e poi `sis900.drv -d`.\n\n");
    }

    /* ! -debug FUNZIONA IN TUTTI E DUE I CASI, ed e' l'unico. Con il driver
     * acceso glielo si CHIEDE — e' lui che ha la scheda in mano, e il file lo
     * scrive lui; senza, si accende la scheda qui e si scrive da soli.
     *
     * ! ED E' IL COMANDO DA USARE, non -l e non -phy. Quelli due la scheda la
     * toccano, quindi con la rete accesa si rifiutano di partire; e presi a
     * rete ferma dicono meno, perche' a rete ferma i contatori sono tutti a
     * zero e gli anelli sono vuoti. Il referto invece coglie la scheda mentre
     * lavora, che e' l'unico momento in cui i numeri vogliono dire qualcosa. */
    if (referto) {
        int pid = ipc_lookup(NET_SERVIZIO_0);

        if (pid > 0) {
            IpcMessage    meta;
            unsigned char buf[IPC_MSG_MAX_DATA];
            int           t;
            NetEsito      e;

            printf("sis900: il driver e' acceso (PID %d): scrive lui, che\n",
                   pid);
            printf("        la scheda ce l'ha in mano.\n");

            if (ipc_send(pid, NET_MSG_REFERTO,
                         percorso, strlen(percorso) + 1) < 0) {
                printf("        non risponde.\n");
                return 1;
            }

            /* ! L'ATTESA E' LUNGA PERCHE' DI MEZZO C'E' UN FLOPPY. Scrivere
             * venti settori su un dischetto non e' istantaneo, e una
             * scadenza da due secondi qui direbbe «non risponde» a un driver
             * che sta solo girando la testina. */
            for (t = 0; t < 8; t++) {
                if (ipc_recv_timeout(&meta, buf, sizeof(buf), 15000) < 0) {
                    printf("        non risponde: il referto non c'e'.\n");
                    return 1;
                }
                if ((int)meta.sender_pid == pid) break;
            }

            if (meta.tipo != NET_MSG_ESITO || meta.len < sizeof(e)) {
                printf("        ha risposto qualcos'altro.\n");
                return 1;
            }
            memcpy(&e, buf, sizeof(e));

            if (e.codice != 0) {
                printf("        non e' riuscito a scrivere %s (%d).\n",
                       percorso, e.codice);
                printf("        Il dischetto e' pieno, o la linguetta di\n");
                printf("        protezione e' aperta?\n");
                return 1;
            }

            printf("        scritto %s\n", percorso);
            ref_dove_finisce(percorso);
            return 0;
        }

        printf("sis900: nessun driver acceso: accendo la scheda e guardo.\n");
        printf("        I contatori saranno a zero e gli anelli vuoti,\n");
        printf("        perche' nessuno ha ancora trasmesso niente. Per un\n");
        printf("        referto che dica qualcosa, lancia prima la rete:\n");
        printf("          netdetect -c\n");
        printf("          /dev/ip.drv &\n");
        printf("          dhcp -r &\n");
        printf("        e POI  sis900.drv -debug\n\n");
    }

    /* ! -l E -phy NON SI FANNO SOPRA UN DRIVER ACCESO, e questa e' la
     * correzione del 14 settembre 2026.
     *
     * ioport_bind NON E' ESCLUSIVA — si veda sys_ioport_bind in
     * kernel/syscall/syscall_impl.c: aggiunge una finestra al processo che
     * chiede, e non guarda chi altro ce l'ha gia'. E' voluto, serve ai driver
     * che devono toccare isole di porte lontane. Ma vuol dire che un secondo
     * sis900.drv prende le stesse porte senza che niente lo fermi, fa il suo
     * reset, e la rete di chi stava lavorando si spegne a meta' di un
     * trasferimento.
     *
     * ! IL DANNO E' PEGGIO DI UN RESET: dopo, questo processo esce e la zona
     * DMA che aveva chiesto torna al kernel, mentre la scheda ha ancora nei
     * registri l'indirizzo fisico di quegli anelli. Il driver vero non c'entra
     * niente e si trova la scheda azzerata sotto.
     *
     * `-d` da questo non e' toccato: quando il servizio c'e' chiede a lui i
     * contatori via IPC e la scheda non la sfiora. E' il modo giusto, ed e'
     * per questo che la diagnosi vera si fa con quello. */
    if ((solo_elenco || setaccio) && ipc_lookup(NET_SERVIZIO_0) > 0) {
        printf("sis900: il driver e' gia' acceso, e questo comando gli\n");
        printf("        spegnerebbe la rete sotto: fa il reset della scheda\n");
        printf("        e poi esce, lasciandole in mano indirizzi che non\n");
        printf("        sono piu' di nessuno.\n");
        printf("\n");
        printf("        Con la rete accesa si guarda cosi':\n");
        printf("          sis900.drv -debug   TUTTO in un file, scritto dal\n");
        printf("                              driver stesso\n");
        printf("          sis900.drv -d       i soli contatori, a schermo\n");
        printf("        Nessuno dei due tocca la scheda: la domanda la\n");
        printf("        fanno al driver, via messaggio.\n");
        printf("\n");
        printf("        Per -l o -phy serve la scheda libera: ferma prima il\n");
        printf("        driver.\n");
        return 1;
    }

    rc = cerca_su_pci();
    if (rc < 0) return 1;
    if (rc == 0) {
        printf("sis900: nessuna SiS 900 (1039:0900 o 7016) sul bus PCI.\n");
        printf("        `netdetect` elenca le schede viste.\n");

        /* ! ANCHE COSI' IL REFERTO SI SCRIVE. «La scheda non c'e'» e' una
         * risposta, e la domanda subito dopo — «e allora cosa c'e'?» —
         * merita di non costare un secondo viaggio. */
        if (referto) {
            if (scrivi_referto_senza_scheda(percorso) == 0) {
                printf("        scritto lo stesso %s: dentro c'e' l'elenco\n",
                       percorso);
                printf("        di quel che il bus dichiara.\n");
                ref_dove_finisce(percorso);
            }
        }
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

    /* ! CHI GUARDA E BASTA SPEGNE PRIMA DI USCIRE. Vedi spegni_scheda(). */
    if (referto) {
        rc = scrivi_referto(percorso);
        spegni_scheda();
        if (rc != 0) {
            printf("sis900: non riesco a scrivere %s (%d).\n", percorso, rc);
            return 1;
        }
        printf("sis900: scritto %s\n", percorso);
        ref_dove_finisce(percorso);
        return 0;
    }

    if (solo_elenco) { stampa_stato();    spegni_scheda(); return 0; }
    if (setaccio)    { mii_setaccia();    spegni_scheda(); return 0; }
    if (diagnosi)    { stampa_diagnosi(); spegni_scheda(); return 0; }

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
