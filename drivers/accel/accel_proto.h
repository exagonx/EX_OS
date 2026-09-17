/* =============================================================================
 * drivers/accel/accel_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il protocollo dell'acceleratore 2D — chi ha i privilegi e chi disegna
 *
 * ! ESISTE PERCHE' wserver NON E' UN DRIVER, E NON DEVE DIVENTARLO.
 *
 * Il motore 2D di una scheda si pilota con `mmio_map` e `ioport_bind`, che
 * sono riservate agli eseguibili caricati da un file *.drv. wserver quei
 * privilegi li ha PERSI APPOSTA il 19 agosto 2026 (vedi il commento in testa a
 * drivers/wserver/wserver.c): si chiamava /dev/wserver.drv solo per ottenere
 * mmio_map, e quel nome teneva la grafica fuori dalla multiutenza — un utente
 * normale non poteva eseguire il server, perche' /dev e' di root.
 *
 * ! E LA RIPARAZIONE SBAGLIATA E' A PORTATA DI MANO UN'ALTRA VOLTA: rimettere
 * wserver fra i driver per dargli il motore 2D. Sarebbe tornare indietro di un
 * mese, e per di piu' dando la capacita' LARGA (ioport_bind, dma_alloc) al
 * posto di quella stretta. Il server chiede un RETTANGOLO PIENO a chi i
 * privilegi ce li ha gia'; non chiede i privilegi.
 *
 * -----------------------------------------------------------------------------
 * ! IL PROTOCOLLO PASSA OPERAZIONI, NON PIXEL — ed e' cio' che lo rende
 * sostenibile. Un giro di IPC costa decine di microsecondi; riempire lo schermo
 * costa MILLISECONDI. Finche' il messaggio dice «riempi questo rettangolo» e
 * non «ecco due megabyte», il costo del messaggio non si vede. Se un giorno
 * qualcuno volesse mandare i pixel di qua, la risposta e' la memoria condivisa,
 * non un messaggio piu' grande.
 *
 * ! E PER LO STESSO MOTIVO C'E' UNA SOGLIA, dall'altra parte. Sotto una certa
 * misura il giro di IPC costa piu' del riempimento stesso, e mandare la
 * richiesta e' una perdita netta: chi chiama deve saperlo. Vedi ACC_SOGLIA_PX.
 *
 * -----------------------------------------------------------------------------
 * ! CHI NON TROVA IL SERVIZIO DISEGNA DA SE', E NON E' UN RIPIEGO.
 *
 * La stragrande maggioranza delle schede che EX-OS incontra non ha un motore 2D
 * pilotabile: VESA, il framebuffer generico, QEMU. Li' `ipc_lookup` rende un
 * errore e il server usa le sue primitive MMX, che riempiono a 377 MB/s — cioe'
 * al limite del bus per la SCRITTURA. Il motore vale il doppio su quella, e
 * quattordici volte sulla copia schermo->schermo, che la CPU paga con una
 * lettura dal framebuffer.
 *
 * Il ramo software non e' codice morto tenuto per scrupolo: e' il ramo NORMALE,
 * ed e' quello che gira su ogni macchina dove non c'e' una SiS.
 * ============================================================================= */
#ifndef ACCEL_PROTO_H
#define ACCEL_PROTO_H

/* Il nome con cui il servizio si registra. Sta in un posto solo: due stringhe
 * uguali scritte in due file divergono al primo ripensamento, e il sintomo
 * sarebbe un'accelerazione che non si accende senza dire perche'. */
#define ACC_SERVIZIO    "accel2d"

/* =============================================================================
 * ! LA SOGLIA E' IN PIXEL, NON IN BYTE, e il numero non e' scelto a occhio.
 *
 * Un riempimento via CPU va a 377 MB/s misurati sull'Acer, il motore a 734:
 * il motore risparmia meta' del tempo, non tutto. Quindi il messaggio va
 * pagato con META' del costo del rettangolo, non col suo intero — ed e' il
 * conto che al primo tentativo avevo sbagliato, mettendo la soglia a 4096.
 *
 *     32768 pixel a 32 bit = 128 KB
 *         CPU     340 us
 *         motore  175 us
 *         risparmio  165 us, contro qualche decina di un giro di IPC.
 *
 *     4096 pixel = 16 KB
 *         CPU      43 us
 *         motore   22 us
 *         risparmio  21 us: MENO del messaggio. Sarebbe stata una perdita.
 *
 * E lo sfondo intero, 800x600 = 480000 pixel, sono 5,1 ms contro 2,6: li' i
 * microsecondi del messaggio spariscono.
 *
 * ! SOPRA LA SOGLIA CI VANNO POCHE COSE PER FOTOGRAMMA, E VA BENE COSI'. Ogni
 * richiesta e' un'attesa dentro la quale il ciclo principale non gira: mille
 * rettangoli acceleratati sarebbero mille attese, e la mailbox di wserver e'
 * profonda QUATTRO messaggi. La soglia non protegge solo dal costo del
 * messaggio, protegge dal numero dei messaggi.
 *
 * ! E' UNA COSTANTE CHE VA RIMISURATA SE CAMBIA QUALCOSA: la velocita' della
 * CPU, quella del motore, o il costo di un giro di IPC. Non e' una legge, e'
 * il punto di pareggio di TRE numeri, e il terzo — il costo dell'IPC — e'
 * l'unico che non e' ancora stato misurato su questa macchina. Finche' non lo
 * e', la soglia sta LARGA apposta: sbagliare per eccesso costa un riempimento
 * software, sbagliare per difetto costa un fotogramma piu' lento di prima.
 * ============================================================================= */
#define ACC_SOGLIA_PX   32768u

/* I messaggi. */
#define ACC_MSG_INFO    0x2D01u   /* chiede chi c'e' e su cosa disegna  */
#define ACC_MSG_RIEMPI  0x2D02u   /* un rettangolo pieno                */
#define ACC_MSG_COPIA   0x2D03u   /* schermo -> schermo                 */
#define ACC_MSG_ESITO   0x2D80u   /* la risposta a RIEMPI e a COPIA     */
#define ACC_MSG_INFO_R  0x2D81u   /* la risposta a INFO                 */

typedef struct {
    unsigned int x, y, w, h;
    unsigned int colore;          /* ARGB come lo vede chi chiede: e' il
                                   * servizio a impacchettarlo per la
                                   * profondita' vera della scheda. Chi chiama
                                   * non deve sapere se lo schermo e' a 16 o a
                                   * 32 bit — quello lo sa chi ci disegna. */
} AccRiempi;

typedef struct {
    unsigned int sx, sy, dx, dy, w, h;
} AccCopia;

typedef struct {
    int esito;                    /* 0, oppure un -errno */
} AccEsito;

typedef struct {
    unsigned int larghezza, altezza, bit, passo;
    char         nome[16];        /* «sis», per dire chi sta accelerando */

    /* ! QUANTE RICHIESTE HA SERVITO, ED E' L'UNICO MODO DI SAPERE SE
     * L'ACCELERAZIONE SI STA USANDO DAVVERO.
     *
     * Un motore che non si accende non da' nessun sintomo salvo essere lento,
     * e «lento» non si distingue da «normale» se non si e' mai visto l'altro.
     * Sulla macchina di prova non c'e' uno schermo da guardare ne' un seriale
     * da leggere: questo contatore e' cio' che trasforma «credo che wserver lo
     * usi» in un numero. Zero dopo aver mosso le finestre vuol dire che il
     * server sta disegnando da se', e allora c'e' qualcosa da capire. */
    unsigned int servite;
} AccInfo;

#endif /* ACCEL_PROTO_H */
