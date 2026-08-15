/* =============================================================================
 * drivers/usb/usb_comune.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * USB: la meta' che NON dipende dal controller
 *
 * ! PERCHE' ESISTE. Lo stack USB si divide in due meta' di dimensione molto
 * diversa: il controller — UHCI ha porte I/O e una lista di frame, xHCI ha
 * MMIO, anelli e contesti — e TUTTO IL RESTO, che e' identico ovunque:
 * descrittori, configurazioni, classe HID, hub, traduzione dei rapporti.
 *
 * «Tutto il resto» e' il grosso, ed e' anche la parte piu' facile da sbagliare
 * in modi silenziosi. Scritta due volte, la si sbaglia due volte — e la
 * seconda si scopre mesi dopo, su una macchina che non si ha.
 *
 * -----------------------------------------------------------------------------
 * ! LA CUCITURA E' UN PUNTATORE A FUNZIONE, e la scelta va spiegata.
 *
 * Questa meta' ha bisogno di UNA cosa sola dal controller: saper fare un
 * trasferimento di controllo. Chiedergliela attraverso un puntatore a funzione
 * — invece che con un simbolo che ogni driver definisce a modo suo — vuol dire
 * che questo file non nomina nessun controller, non ha un #ifdef, e si compila
 * identico dentro tutt'e due i driver.
 *
 * ! E `dev` E' OPACO DI PROPOSITO. Su UHCI e' l'indirizzo USB, che assegna il
 * driver; su xHCI e' il numero di alloggiamento, che assegna il controller.
 * Sono due cose diverse che non si assomigliano nemmeno, e l'unico modo di
 * scrivere una volta sola il codice che ci sta sopra e' non guardarci dentro.
 * ============================================================================= */

#ifndef USB_COMUNE_H
#define USB_COMUNE_H

/* --- USB, richieste standard --------------------------------------------- */
#define USB_REQ_GET_DESC    0x06
#define USB_REQ_SET_ADDR    0x05
#define USB_REQ_SET_CONF    0x09
#define USB_REQ_SET_PROTO   0x0B    /* classe HID */
#define USB_REQ_SET_IDLE    0x0A    /* classe HID */

#define USB_DESC_DEVICE     1
#define USB_DESC_CONFIG     2
#define USB_DESC_HUB        0x29

#define USB_CLASSE_HUB      9
#define USB_CLASSE_HID      3
#define USB_SUB_BOOT        1
#define USB_PROTO_TASTIERA  1
#define USB_PROTO_MOUSE     2

/* ! LE VELOCITA' SI NOMINANO QUI, E NON CON I CODICI DI UN CONTROLLER.
 * L'xHCI ha i propri numeri (1 = full, 2 = low, 3 = high) e l'UHCI ha un bit
 * solo: usare gli uni o l'altro in questo file vorrebbe dire far entrare un
 * controller dalla finestra. Chi chiama traduce. */
#define USB_VEL_FULL        0
#define USB_VEL_LOW         1
#define USB_VEL_HIGH        2

/* Le richieste di classe verso un hub, che non hanno registri dietro */
#define USB_HUB_F_RESET     4
#define USB_HUB_F_POWER     8
#define USB_HUB_F_C_CONNECT 16
#define USB_HUB_F_C_RESET   20

/* -----------------------------------------------------------------------------
 * Il trasferimento di controllo, come lo vede questa meta'
 *
 * Rende 0 se e' andato bene, un valore negativo altrimenti. `in` dice il verso
 * dei dati; `dati` puo' essere nullo quando `len` e' 0.
 * --------------------------------------------------------------------------- */
typedef int (*UsbControllo)(unsigned int dev, unsigned int rt, unsigned int req,
                            unsigned int val, unsigned int idx,
                            void *dati, unsigned int len, int in);

/* Cio' che si sa di un dispositivo dopo averlo interrogato. */
typedef struct {
    unsigned int maxp0;         /* pacchetto massimo dell'endpoint 0 */
    unsigned int classe;        /* del DISPOSITIVO: 9 = hub */
    unsigned int versione;      /* 0x0200 = USB 2.00 */
    unsigned int venditore;
    unsigned int prodotto;

    unsigned int proto;         /* USB_PROTO_MOUSE o USB_PROTO_TASTIERA, 0 = ne' */
    unsigned int interfaccia;
    unsigned int conf;          /* il valore da dare a SET_CONFIGURATION */
    unsigned int ep;            /* endpoint di interruzione IN, 0 = nessuno */
    unsigned int ep_maxp;
    unsigned int ep_intervallo;
} UsbDispositivo;

/* -----------------------------------------------------------------------------
 * Interrogare un dispositivo
 * --------------------------------------------------------------------------- */

/* I primi otto byte del descrittore di dispositivo: dentro c'e' `maxp0`, e
 * senza quello ogni trasferimento piu' lungo si spezza male.
 *
 * ! VA CHIESTO PRIMA DI TUTTO IL RESTO, e su UHCI anche prima di assegnare
 * l'indirizzo: finche' non si sa, l'unico valore lecito e' 8. */
int usb_desc_corto(UsbControllo ctl, unsigned int dev, UsbDispositivo *d);

/* Il descrittore intero: venditore, prodotto, versione, classe. */
int usb_desc_lungo(UsbControllo ctl, unsigned int dev, UsbDispositivo *d);

/* Legge la configurazione, ci cerca un'interfaccia HID «boot» con il suo
 * endpoint di interruzione, e se la trova la ATTIVA (SET_CONFIGURATION,
 * SET_PROTOCOL, SET_IDLE).
 *
 * Rende 1 se il dispositivo e' un HID «boot» utilizzabile, 0 altrimenti. */
int usb_configura_hid(UsbControllo ctl, unsigned int dev, UsbDispositivo *d,
                      unsigned int verboso);

/* -----------------------------------------------------------------------------
 * Gli hub
 *
 * ! UN HUB SI RICONOSCE DALLA CLASSE DEL DISPOSITIVO, NON DALL'INTERFACCIA.
 * E' l'unico caso in cui quel byte dice qualcosa invece di essere zero, e va
 * guardato PRIMA di cercare un'interfaccia HID — altrimenti si conclude «non
 * e' un HID» su un hub che ha un mouse attaccato dietro.
 * --------------------------------------------------------------------------- */

/* Quante porte ha, e quanti millisecondi vuole dopo l'accensione. */
int usb_hub_descrittore(UsbControllo ctl, unsigned int dev,
                        unsigned int *porte, unsigned int *attesa_ms);

/* Accende tutte le porte dell'hub e aspetta quanto l'hub ha chiesto.
 *
 * ! LE PORTE VANNO ALIMENTATE, E POI BISOGNA ASPETTARE. Chiedere lo stato
 * prima di quel tempo risponde «niente attaccato» anche quando c'e'. */
void usb_hub_accendi(UsbControllo ctl, unsigned int dev,
                     unsigned int porte, unsigned int attesa_ms);

/* Azzera la porta `p` dell'hub e dice se ci si e' trovato un dispositivo
 * abilitato. In uscita `velocita` e' uno degli USB_VEL_*.
 * Rende 1 se la porta e' pronta per un'enumerazione. */
int usb_hub_porta_pronta(UsbControllo ctl, unsigned int dev, unsigned int p,
                         unsigned int *velocita);

/* -----------------------------------------------------------------------------
 * I rapporti HID «boot»
 *
 * ! IL PROTOCOLLO «BOOT» ESISTE APPOSTA per non dover interpretare il
 * descrittore di rapporto, che e' un piccolo linguaggio ed e' il pezzo di USB
 * che costa piu' di tutti. I rapporti hanno una forma fissa e nota.
 * --------------------------------------------------------------------------- */

/* Il rapporto di un mouse: bottoni, dx, dy. Rende 1 se c'era qualcosa da
 * leggere. Gli spostamenti si SOMMANO a quelli passati, non li sostituiscono.
 *
 * ! LA Y NON SI GIRA, al contrario del PS/2: l'HID la manda gia' positiva
 * verso il basso. Girarla «per simmetria» darebbe un puntatore che va dalla
 * parte sbagliata solo con i mouse USB. */
int usb_mouse_rapporto(const unsigned char *r, unsigned int n,
                       int *dx, int *dy, unsigned int *bottoni);

/* Il rapporto di una tastiera, tradotto in scancode del set 1 e mandato al
 * servizio `kbd_pid`.
 *
 * ! IL RAPPORTO DICE CHI E' PREMUTO ADESSO, NON COSA E' CAMBIATO: pressioni e
 * rilasci si ricavano confrontando con il rapporto PRECEDENTE, che questa
 * funzione si tiene. Chi se lo dimentica ottiene una tastiera che ripete
 * all'infinito l'ultimo tasto. */
void usb_tastiera_rapporto(const unsigned char *r, int kbd_pid);

#endif /* USB_COMUNE_H */
