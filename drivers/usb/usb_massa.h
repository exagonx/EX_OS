/* =============================================================================
 * drivers/usb/usb_massa.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LE CHIAVETTE — il trasporto bulk-only e i comandi SCSI
 *
 * ! DOVE STA, E PERCHE' STA LI'. Accanto a usb_comune.c, cioe' nella meta'
 * dello stack USB che NON dipende dal controller. Una chiavetta parla lo
 * stesso identico protocollo su UHCI, OHCI, EHCI e xHCI: cambia solo COME si
 * fa un trasferimento bulk, e quello arriva da fuori come puntatore a
 * funzione — la stessa cucitura di UsbControllo, per la stessa ragione.
 *
 * Scritto una volta, servira' quattro driver. Scritto dentro il primo, la
 * seconda copia avrebbe sbagliato da sola.
 *
 * -----------------------------------------------------------------------------
 * ! IL PROTOCOLLO, IN TRE MOSSE E MEZZA
 *
 *   1. si manda un CBW di 31 byte sull'endpoint OUT: dentro c'e' un comando
 *      SCSI e quanto ci si aspetta di scambiare;
 *   2. si scambiano i dati, IN o OUT secondo il comando;
 *   3. si legge un CSW di 13 byte sull'endpoint IN: dice se e' andata.
 *
 * La mezza mossa e' il RECUPERO: se il dispositivo non ha niente da dire
 * blocca («stalla») l'endpoint, e finche' non gli si toglie il blocco tutto
 * quello che segue fallisce. E' la parte che si dimentica, ed e' il motivo per
 * cui una chiavetta «funziona e poi smette» dopo il primo settore illeggibile.
 *
 * -----------------------------------------------------------------------------
 * ! I SETTORI SONO 512 BYTE PERCHE' LO CHIEDIAMO NOI
 *
 * READ CAPACITY dice quanto e' grande un blocco: quasi sempre 512, sulle
 * chiavette moderne a volte 4096. Lo strato a blocchi di EX-OS pero' ragiona
 * in settori da 512 (vedi blkr3_offri), quindi un supporto a 4096 va
 * TRADOTTO, non rifiutato — e la traduzione sta qui, in un punto solo.
 * ============================================================================= */

#ifndef USB_MASSA_H
#define USB_MASSA_H

#include "usb_comune.h"

/* Come si fa un trasferimento bulk. Rende i byte trasferiti (>= 0), oppure un
 * valore negativo; USB_MASSA_STALLO dice «l'endpoint e' bloccato», che non e'
 * un guasto ma una risposta.
 *
 * ! `dati` PUO' ESSERE MEMORIA QUALUNQUE. Chi attua questa funzione copia da
 * e verso la propria zona DMA: pretendere che il chiamante sappia dove sta la
 * memoria buona per il controller vorrebbe dire far entrare il controller
 * dentro questo file, che e' esattamente cio' che si sta evitando. */
#define USB_MASSA_STALLO   (-2)

typedef int (*UsbBulk)(unsigned int dev, unsigned int ep, void *dati,
                       unsigned int len, int in);

/* Chiamata DOPO aver sbloccato un endpoint, se il driver la fornisce.
 *
 * ! SENZA DI QUESTA, DOPO IL PRIMO ERRORE NON FUNZIONA PIU' NIENTE su un
 * controller che il conto dei pacchetti se lo tiene da solo. Una
 * CLEAR_FEATURE(ENDPOINT_HALT) azzera il «toggle» dalla parte del
 * DISPOSITIVO; se il driver non lo azzera dalla sua, i due lati restano
 * sfasati e ogni pacchetto successivo viene scartato in silenzio — non un
 * errore: un trasferimento che non finisce mai.
 *
 * L'xHCI non ne ha bisogno (il toggle sta nel contesto dell'endpoint, e lo
 * rimette lui), e infatti la lascia nulla. */
typedef void (*UsbToggleAzzera)(unsigned int dev, unsigned int ep, int in);

typedef struct {
    UsbControllo ctl;           /* per il recupero: CLEAR_FEATURE e reset */
    UsbBulk      bulk;
    UsbToggleAzzera toggle_azzera;  /* puo' essere nulla */
    unsigned int dev;           /* opaco: slot su xHCI, indirizzo su UHCI */
    unsigned int ep_in, ep_out;
    unsigned int interfaccia;
    unsigned int verboso;

    unsigned int tag;           /* cresce a ogni comando: lo rimanda il CSW */

    /* Riempiti da usb_massa_capacita() */
    unsigned int blocchi;       /* quanti blocchi ha il supporto */
    unsigned int byte_blocco;   /* 512, a volte 4096 */
    unsigned int settori;       /* la stessa capacita' in settori da 512 */
} UsbMassa;

/* TEST UNIT READY, con i tentativi che servono: una chiavetta appena infilata
 * risponde «non pronta» qualche volta prima di dire di si'. */
int usb_massa_pronta(UsbMassa *m);

/* INQUIRY: copia in `nome` fino a `max` byte di «venditore prodotto». */
int usb_massa_nome(UsbMassa *m, char *nome, unsigned int max);

/* READ CAPACITY(10): riempie blocchi, byte_blocco e settori. */
int usb_massa_capacita(UsbMassa *m);

/* Lettura e scrittura, in SETTORI DA 512, come li chiede lo strato a blocchi.
 * Rendono 0, o un valore negativo. */
int usb_massa_leggi (UsbMassa *m, unsigned int lba, unsigned int n, void *buf);
int usb_massa_scrivi(UsbMassa *m, unsigned int lba, unsigned int n,
                     const void *buf);

/* Offre il supporto come dispositivo a blocchi e resta a servirlo: non
 * ritorna finche' il dispositivo c'e'. `nome` e' come si chiamera' ("usb0").
 *
 * ! IL CICLO STA QUI E NON NEL DRIVER DEL CONTROLLER, perche' e' identico per
 * tutti e quattro: prendi una richiesta, traducila in READ(10) o WRITE(10),
 * rispondi. Metterlo in xhci.drv avrebbe voluto dire riscriverlo in ehci.drv,
 * e la seconda copia si sarebbe accorta dei casi limite con un anno di
 * ritardo. */
int usb_massa_servi(UsbMassa *m, const char *nome);

#endif /* USB_MASSA_H */
