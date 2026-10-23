/* =============================================================================
 * drivers/usb/usb_sportello.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Lo sportello dei driver USB: cosa c'e' sulle porte, e «riprova» (23 ottobre
 * 2026, l'utente: il comando `usb -status`, `-unplug`, `-refresh`)
 *
 * ! LO SA SOLO IL DRIVER DEL CONTROLLER. Quali porte ci sono, cosa c'e'
 * infilato, se una chiavetta e' servita, espulsa o non si e' mai svegliata:
 * lo strato a blocchi vede solo le chiavette che sono arrivate fino in fondo,
 * e un mouse non lo vede affatto. Percio' ogni driver (uhci, ohci, ehci, xhci)
 * apre uno sportello - un servizio IPC «usb.<nome>» - e ci risponde.
 *
 * ! E «RIPROVA» E' L'UNICO MODO DI RIPRENDERE UNA CHIAVETTA RIMASTA INFILATA.
 * Il driver non riprova una porta gia' provata finche' non la vede vuota per
 * tre secondi (vedi ASSENZE_PER_SCOLLEGATA in ehci.c): giusto per un mouse,
 * che altrimenti verrebbe enumerato ogni secondo, ma una chiavetta espulsa
 * con `usb -unplug` e lasciata li' non tornava piu' senza staccarla. Riprova
 * toglie il segno alle porte occupate che non stanno servendo niente.
 *
 * Questo file e' l'intero contratto fra i driver e /bin/usb: lo includono
 * tutt'e due, e non contiene codice.
 * ============================================================================= */
#ifndef USB_SPORTELLO_H
#define USB_SPORTELLO_H

/* Il servizio: "usb.ehci", "usb.ohci", ... ; un secondo driver dello stesso
 * tipo prende "usb.ehci2", e /bin/usb li cerca tutti. */
#define USB_SPORTELLO_PREFISSO  "usb."

/* I tipi dei messaggi: lontani da quelli del mouse (MOUSE_MSG_*) e del PCI,
 * che arrivano nella stessa casella. */
#define USB_MSG_STATO       0x55530001u   /* -> USB_MSG_STATO_R, UsbStato  */
#define USB_MSG_RIPROVA     0x55530002u   /* -> USB_MSG_RIPROVA_R, unsigned int:
                                           *    quante porte riprova */
#define USB_MSG_STATO_R     0x55530081u
#define USB_MSG_RIPROVA_R   0x55530082u

/* Cosa c'e' su una porta */
#define USB_P_VUOTA         0   /* niente infilato                           */
#define USB_P_COLLEGATA     1   /* c'e' qualcosa, non ancora guardato         */
#define USB_P_ERRORE        2   /* non risponde all'enumerazione             */
#define USB_P_MASSA         3   /* chiavetta o disco, servito come `blk`     */
#define USB_P_ESPULSA       4   /* tolta con unplug/eject, ancora infilata   */
#define USB_P_NON_PRONTA    5   /* memoria di massa che non diventa pronta   */
#define USB_P_MOUSE         6
#define USB_P_TASTIERA      7
#define USB_P_ALTRO         8   /* un dispositivo che questo driver non usa  */

#define USB_SPORTELLO_PORTE 16

typedef struct {
    unsigned char  ctl;         /* quale controller del driver, da 0         */
    unsigned char  porta;       /* da 1, come la conta l'utente              */
    unsigned char  stato;       /* USB_P_*                                   */
    unsigned char  classe;      /* del dispositivo (0 = la dice l'interfaccia) */
    unsigned short venditore;
    unsigned short prodotto;
    char           blk[8];      /* "usb0" se servito come dispositivo a blocchi */
    char           nome[32];    /* «venditore prodotto» dall'INQUIRY, o ""   */
} UsbPortaStato;                /* 48 byte */

typedef struct {
    char           chi[12];     /* "ehci", "ohci"...                          */
    unsigned int   controller;  /* quanti ne guida                            */
    unsigned int   porte;       /* quante porte in tutto                      */
    unsigned int   n;           /* quante voci valgono in `p`                 */
    UsbPortaStato  p[USB_SPORTELLO_PORTE];
} UsbStato;                     /* 792 byte: sta in un messaggio IPC (1536)  */

#endif /* USB_SPORTELLO_H */
