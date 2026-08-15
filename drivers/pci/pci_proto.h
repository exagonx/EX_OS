/* =============================================================================
 * drivers/pci/pci_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Protocollo IPC del servizio PCI (/dev/pci.drv).
 *
 * Come kbd_proto.h, questo header è l'unico punto di contatto fra le due
 * sponde e non deve dipendere da niente: solo #define e struct, nessun
 * header del kernel, nessuna libc. Lo includono il server e i suoi
 * client — i driver di scheda (ne2k.drv, pcnet.drv) e netdetect.
 *
 *   client                              pci (/dev/pci.drv)
 *     |  PCI_MSG_CERCA (classe, ordinale) |
 *     |---------------------------------->|
 *     |     PCI_MSG_DISPOSITIVO / _FINE   |
 *     |<----------------------------------|
 *     |  PCI_MSG_ABILITA (io|bm)          |
 *     |---------------------------------->|  scrive il registro comando
 *     |     PCI_MSG_ESITO                 |
 *     |<----------------------------------|
 *
 * -----------------------------------------------------------------------------
 * ! NON C'E' UNA SCRITTURA DI CONFIGURAZIONE GENERICA, ED E' VOLUTO
 *
 * La simmetria suggerirebbe PCI_MSG_LEGGI/PCI_MSG_SCRIVI su un offset
 * qualunque. La lettura c'è; la scrittura no, e la differenza non è
 * pigrizia.
 *
 * Leggere lo spazio di configurazione non può rompere niente. Scriverlo
 * sì, e non solo per il dispositivo di chi scrive: i BAR dicono al ponte
 * dove risponde ogni scheda, e riprogrammare i BAR del controller ATA
 * significa perdere il disco da sotto i piedi al kernel — che di quella
 * scrittura non sa nulla. Un servizio che accetta «scrivi 4 byte a questo
 * offset di questo dispositivo» da chiunque non è un server di
 * enumerazione, è un buco attraverso cui un processo qualunque controlla
 * il chipset.
 *
 * Perciò l'unica scrittura esposta è quella che serve davvero — accendere
 * e spegnere un dispositivo — e ha un nome che dice cosa fa. Tocca solo
 * il registro comando (offset 0x04) e solo tre bit. Se un giorno servirà
 * scrivere altro, si aggiungerà un'altra operazione con un altro nome e
 * un altro perché scritto qui sopra, non una scrittura generica.
 * ============================================================================= */

#ifndef PCI_PROTO_H
#define PCI_PROTO_H

/* Nome sotto cui il server si registra (ipc_register/ipc_lookup) */
#define PCI_SERVIZIO    "pci"

/* -----------------------------------------------------------------------------
 * Tipi di messaggio. Richieste sotto 128, risposte da 128 in su: così un
 * messaggio fuori posto si riconosce a colpo d'occhio nei log.
 * --------------------------------------------------------------------------- */
#define PCI_MSG_ELENCA      1   /* PciRichiesta.ordinale = indice assoluto */
#define PCI_MSG_CERCA       2   /* per classe/sottoclasse, oppure venditore/dispositivo */
#define PCI_MSG_LEGGI       3   /* legge 4 byte di configurazione (sola lettura) */
#define PCI_MSG_ABILITA     4   /* accende i bit del registro comando */
#define PCI_MSG_DISABILITA  5   /* li spegne (vedi sotto: serve alla ripartenza) */
#define PCI_MSG_RISCANSIONE 6   /* rifà l'enumerazione da zero */

#define PCI_MSG_DISPOSITIVO 128 /* risposta con un PciDispositivo */
#define PCI_MSG_FINE        129 /* nessun (altro) dispositivo corrisponde */
#define PCI_MSG_VALORE      130 /* risposta con un PciValore */
#define PCI_MSG_ESITO       131 /* risposta con un PciEsito */

/* -----------------------------------------------------------------------------
 * Bit del registro comando che il server accetta di toccare.
 *
 * PCI_ABIL_BUSMASTER è quello che conta per una scheda di rete: senza,
 * la scheda non può leggere e scrivere in RAM da sola, e tutto il
 * modello DMA non parte.
 *
 * ! PCI_MSG_DISABILITA non è la coppia decorativa di PCI_MSG_ABILITA.
 * È il pezzo che rende possibile la ripartenza di un driver: una scheda
 * lasciata bus master da un processo morto continua a scrivere nei
 * buffer che il kernel ha già liberato, e il danno arriva minuti dopo,
 * in un punto qualunque della memoria. Chi fa ripartire un driver deve
 * spegnere il bus mastering PRIMA di ricaricarlo.
 * --------------------------------------------------------------------------- */
#define PCI_ABIL_IO         0x0001  /* risponde nello spazio I/O */
#define PCI_ABIL_MEMORIA    0x0002  /* risponde nello spazio di memoria */
#define PCI_ABIL_BUSMASTER  0x0004  /* può iniziare cicli sul bus (DMA) */

/* Classi PCI che ci interessano per ora (codice classe, byte alto) */
#define PCI_CLASSE_RETE     0x02    /* controller di rete */
#define PCI_SOTTO_ETHERNET  0x00    /* sottoclasse: Ethernet */
#define PCI_CLASSE_PONTE    0x06    /* ponte */
#define PCI_SOTTO_PCI_PCI   0x04    /* sottoclasse: ponte PCI-PCI */

/* Valore convenzionale per «non filtrare su questo campo» */
#define PCI_QUALUNQUE       0xFFFF

/* -----------------------------------------------------------------------------
 * Un dispositivo, come lo vede un client.
 *
 * I BAR arrivano già interpretati: bar[i] è l'indirizzo base con i bit di
 * tipo tolti, bar_io[i] dice in quale spazio. Un driver che deve fare
 * ioport_bind() legge bar[0] e basta, senza rifare il mascheramento —
 * che è esattamente il genere di dettaglio che ognuno sbaglia a modo suo.
 *
 * ! NON C'E' LA DIMENSIONE DELLA FINESTRA, E NON SI PUO' AGGIUNGERE
 * SENZA PAGARLA. Si misura in un modo solo: scrivere 0xFFFFFFFF nel BAR,
 * rileggere quali bit sono rimasti a zero, e riscrivere il valore
 * originale. Per il tempo che passa fra le due scritture il dispositivo
 * decodifica un indirizzo assurdo — e farlo in fase di enumerazione
 * vuol dire farlo su OGNI dispositivo, compreso il controller ATA che
 * il kernel sta usando in quel momento per leggere il disco.
 *
 * Un server di enumerazione non deve scrivere sull'hardware di nessuno.
 * La dimensione, del resto, la sa già chi la usa: un driver NE2000
 * chiede 32 porte perché una NE2000 ha 32 registri, non perché
 * gliel'ha detto il bus.
 * --------------------------------------------------------------------------- */
typedef struct {
    unsigned short venditore;    /* es. 0x1022 AMD, 0x10EC Realtek */
    unsigned short dispositivo;  /* es. 0x2000 PCnet, 0x8029 NE2000 PCI */
    unsigned char  bus;
    unsigned char  slot;         /* 0..31 */
    unsigned char  funzione;     /* 0..7 */
    unsigned char  revisione;
    unsigned char  classe;
    unsigned char  sottoclasse;
    unsigned char  interfaccia;  /* prog-IF */
    unsigned char  irq_linea;    /* IRQ come programmato dal BIOS, 0xFF = nessuno */
    unsigned int   bar[6];       /* base già mascherata, 0 = BAR non usato */
    unsigned char  bar_io[6];    /* 1 = spazio I/O, 0 = spazio di memoria */
} PciDispositivo;

/* Richiesta di elenco o ricerca.
 *
 * ordinale = quale corrispondenza si vuole, partendo da 0. Un client che
 * cerca «la seconda scheda Ethernet» manda ordinale = 1. Questo evita di
 * dover mantenere uno stato di iterazione nel server, che dovrebbe
 * ricordarsi a che punto è per ogni client — e sbagliarsi appena due
 * client iterano insieme. */
typedef struct {
    unsigned int   ordinale;
    unsigned short classe;       /* PCI_QUALUNQUE = non filtrare */
    unsigned short sottoclasse;  /* PCI_QUALUNQUE = non filtrare */
    unsigned short venditore;    /* PCI_QUALUNQUE = non filtrare */
    unsigned short dispositivo;  /* PCI_QUALUNQUE = non filtrare */
} PciRichiesta;

/* Identifica un dispositivo già trovato, per le operazioni successive */
typedef struct {
    unsigned char  bus;
    unsigned char  slot;
    unsigned char  funzione;
    unsigned char  riservato;
    unsigned short offset;       /* solo per PCI_MSG_LEGGI: multiplo di 4 */
    unsigned short bit;          /* solo per ABILITA/DISABILITA: PCI_ABIL_* */
} PciAzione;

/* Risposta a PCI_MSG_LEGGI */
typedef struct {
    unsigned int   valore;
} PciValore;

/* Risposta a PCI_MSG_ABILITA/DISABILITA/RISCANSIONE.
 * codice 0 = fatto, negativo = errno. */
typedef struct {
    int            codice;
    unsigned int   comando;      /* registro comando dopo l'operazione */
} PciEsito;

#endif /* PCI_PROTO_H */
