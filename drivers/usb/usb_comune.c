/* =============================================================================
 * drivers/usb/usb_comune.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * USB: la meta' che non dipende dal controller — vedi usb_comune.h per il
 * perche' esiste e per come e' cucita ai driver.
 *
 * ! QUESTO FILE NON DEVE MAI NOMINARE UN CONTROLLER. Ne' un registro, ne' un
 * TRB, ne' un TD, ne' un #ifdef che distingua i due. Se un giorno serve
 * saperlo, vuol dire che la cucitura e' nel posto sbagliato e va spostata —
 * non che ci vuole un'eccezione.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "usb_comune.h"

/* =============================================================================
 * I DESCRITTORI
 * ========================================================================== */

int usb_desc_corto(UsbControllo ctl, unsigned int dev, UsbDispositivo *d)
{
    unsigned char b[8];

    /* ! SI PARTE DA 8 E NON DA QUELLO CHE SI SPERA. Finche' non si e' letto
     * questo descrittore, l'unica dimensione di pacchetto che la specifica
     * garantisce e' 8: chiederne di piu' spezza il trasferimento in modo che
     * non somiglia a un errore di dimensione. */
    d->maxp0 = 8;

    if (ctl(dev, 0x80, USB_REQ_GET_DESC, (USB_DESC_DEVICE << 8), 0, b, 8, 1) != 0)
        return 0;

    d->maxp0 = b[7] ? b[7] : 8;
    return 1;
}

int usb_desc_lungo(UsbControllo ctl, unsigned int dev, UsbDispositivo *d)
{
    unsigned char b[18];

    if (ctl(dev, 0x80, USB_REQ_GET_DESC, (USB_DESC_DEVICE << 8), 0, b, 18, 1) != 0)
        return 0;

    if (b[0] < 18 || b[1] != USB_DESC_DEVICE) return 0;

    d->versione  = (unsigned int)b[2] | ((unsigned int)b[3] << 8);
    d->classe    = b[4];
    d->maxp0     = b[7] ? b[7] : d->maxp0;
    d->venditore = (unsigned int)b[8]  | ((unsigned int)b[9]  << 8);
    d->prodotto  = (unsigned int)b[10] | ((unsigned int)b[11] << 8);
    return 1;
}

/* =============================================================================
 * LA CONFIGURAZIONE, E LA CATENA CHE SI SCORRE CON LE LUNGHEZZE
 * ========================================================================== */

int usb_configura_hid(UsbControllo ctl, unsigned int dev, UsbDispositivo *d,
                      unsigned int verboso)
{
    unsigned char b[64];
    unsigned int  tot, i, i_if = 0, trovato = 0;

    d->proto = 0;
    d->ep    = 0;

    /* Prima i 9 byte di testa, che dicono quanto e' lunga tutta la catena. */
    if (ctl(dev, 0x80, USB_REQ_GET_DESC, (USB_DESC_CONFIG << 8), 0, b, 9, 1) != 0)
        return 0;

    tot     = (unsigned int)b[2] | ((unsigned int)b[3] << 8);
    d->conf = b[5];
    if (tot > sizeof(b)) tot = sizeof(b);

    if (ctl(dev, 0x80, USB_REQ_GET_DESC, (USB_DESC_CONFIG << 8), 0, b, tot, 1) != 0)
        return 0;

    /* ! SI SCORRE LA CATENA CON LA LUNGHEZZA DI OGNI PEZZO, NON A PASSI FISSI.
     * Una configurazione e' una fila di blocchi di misura diversa —
     * interfacce, endpoint, roba di classe che non ci riguarda — e saltare di
     * misura fissa vuol dire leggere campi a caso appena un dispositivo ne
     * infila uno in mezzo. */
    i = 9;
    while (i + 2 <= tot) {
        unsigned int len = b[i], tipo = b[i + 1];

        if (len == 0) break;

        if (tipo == 4 && i + 8 <= tot) {                /* INTERFACE */
            i_if = b[i + 2];
            if (b[i + 5] == USB_CLASSE_HID && b[i + 6] == USB_SUB_BOOT &&
                (b[i + 7] == USB_PROTO_MOUSE || b[i + 7] == USB_PROTO_TASTIERA)) {
                d->proto = b[i + 7];
                trovato  = 1;
            } else {
                trovato = 0;    /* un'altra interfaccia: i suoi endpoint non
                                 * sono i nostri */
            }
        } else if (tipo == 5 && trovato && i + 6 <= tot) {  /* ENDPOINT */
            /* IN, e di tipo interruzione. */
            if ((b[i + 2] & 0x80) && (b[i + 3] & 0x03) == 3) {
                d->ep            = b[i + 2] & 0x0F;
                d->ep_maxp       = (unsigned int)b[i + 4] |
                                   (((unsigned int)b[i + 5] & 0x07) << 8);
                d->ep_intervallo = b[i + 6];
                break;
            }
        }
        i += len;
    }

    if (!trovato || d->ep == 0) {
        if (verboso) printf("usb: non e' un HID 'boot'\n");
        return 0;
    }

    d->interfaccia = i_if;

    if (ctl(dev, 0x00, USB_REQ_SET_CONF, d->conf, 0, 0, 0, 0) != 0) return 0;

    /* Protocollo «boot»: rapporti di forma fissa e nota. */
    if (ctl(dev, 0x21, USB_REQ_SET_PROTO, 0, i_if, 0, 0, 0) != 0) return 0;

    /* ! SET_IDLE PUO' FALLIRE, E NON E' UN GUASTO. Certi dispositivi non la
     * implementano; serve solo a dire «non ripetermi il rapporto se non
     * cambia niente», e senza si riceve qualche rapporto in piu'. */
    (void)ctl(dev, 0x21, USB_REQ_SET_IDLE, 0, i_if, 0, 0, 0);

    return 1;
}

/* =============================================================================
 * GLI HUB — le porte che non stanno sul controller
 * ========================================================================== */

int usb_hub_descrittore(UsbControllo ctl, unsigned int dev,
                        unsigned int *porte, unsigned int *attesa_ms)
{
    unsigned char b[16];

    if (ctl(dev, 0xA0, USB_REQ_GET_DESC, (USB_DESC_HUB << 8), 0, b, 8, 1) != 0)
        return 0;

    *porte = b[2];
    if (*porte == 0) return 0;
    if (*porte > 8) *porte = 8;     /* quante ne guardiamo, non quante ne ha */

    /* Il quinto byte e' in unita' da 2 ms. */
    *attesa_ms = (unsigned int)b[5] * 2u;
    return 1;
}

void usb_hub_accendi(UsbControllo ctl, unsigned int dev,
                     unsigned int porte, unsigned int attesa_ms)
{
    unsigned int i;

    for (i = 1; i <= porte; i++)
        (void)ctl(dev, 0x23, 0x03, USB_HUB_F_POWER, i, 0, 0, 0);

    /* I 100 ms in piu' non sono superstizione: sono il tempo che la specifica
     * concede a un dispositivo per presentarsi dopo che ha corrente. */
    usleep(attesa_ms * 1000u + 100000u);
}

int usb_hub_porta_pronta(UsbControllo ctl, unsigned int dev, unsigned int p,
                         unsigned int *velocita)
{
    unsigned char st[4];
    unsigned int  giri;

    if (ctl(dev, 0xA3, 0x00, 0, p, st, 4, 1) != 0) return 0;
    if (!(st[0] & 0x01)) return 0;                  /* niente attaccato */

    (void)ctl(dev, 0x23, 0x01, USB_HUB_F_C_CONNECT, p, 0, 0, 0);

    if (ctl(dev, 0x23, 0x03, USB_HUB_F_RESET, p, 0, 0, 0) != 0) return 0;

    /* ! SI ASPETTA CHE IL RESET FINISCA DAVVERO, invece di contare i
     * millisecondi: l'hub lo dice nel proprio stato, e chiederglielo costa
     * meno che indovinare un'attesa buona per ogni hub del mondo. */
    for (giri = 0; giri < 20; giri++) {
        usleep(20000);
        if (ctl(dev, 0xA3, 0x00, 0, p, st, 4, 1) != 0) break;
        if (st[2] & 0x10) break;                    /* C_PORT_RESET */
    }
    (void)ctl(dev, 0x23, 0x01, USB_HUB_F_C_RESET, p, 0, 0, 0);

    if (ctl(dev, 0xA3, 0x00, 0, p, st, 4, 1) != 0) return 0;
    if (!(st[0] & 0x02)) return 0;                  /* non abilitata */

    /* I bit 9 e 10 dello stato della porta. Nessuno dei due acceso vuol dire
     * full speed, che e' il caso di gran lunga piu' comune. */
    if      (st[1] & 0x02) *velocita = USB_VEL_LOW;
    else if (st[1] & 0x04) *velocita = USB_VEL_HIGH;
    else                   *velocita = USB_VEL_FULL;
    return 1;
}

/* =============================================================================
 * IL MOUSE
 * ========================================================================== */

int usb_mouse_rapporto(const unsigned char *r, unsigned int n,
                       int *dx, int *dy, unsigned int *bottoni)
{
    if (n < 3) return 0;

    *bottoni = 0;
    if (r[0] & 0x01) *bottoni |= MOUSE_BTN_SIN;
    if (r[0] & 0x02) *bottoni |= MOUSE_BTN_DES;
    if (r[0] & 0x04) *bottoni |= MOUSE_BTN_CEN;

    /* ! SI SOMMA, NON SI SOSTITUISCE. La mailbox IPC e' profonda quattro
     * messaggi: un mouse che mandasse un evento per movimento la riempirebbe
     * in un decimo di secondo. Sommando, un client lento riceve MENO messaggi
     * ma lo spostamento GIUSTO — che per un puntatore e' cio' che conta. */
    *dx += (int)(signed char)r[1];
    *dy += (int)(signed char)r[2];
    return 1;
}

/* =============================================================================
 * LA TASTIERA — da uso HID a scancode del set 1
 *
 * ! NON SI REGISTRA IL SERVIZIO "kbd": SI MANDANO SCANCODE A CHI LO SERVE
 * GIA'. Rifare qui le mappe di tastiera, l'editing di riga, il modo raw,
 * l'eco e la commutazione con Alt+Fn vorrebbe dire riscrivere meta' di kbd.c
 * — la meta' sottile. Traducendo, tutto quello continua a funzionare senza
 * sapere che l'USB esista. E' anche cio' che fa il firmware di una scheda
 * madre quando emula il legacy, solo che qui si vede invece di succedere di
 * nascosto in SMM.
 *
 * La tabella e' indicizzata per USO HID; il bit 0x80 vuol dire «esteso»,
 * cioe' preceduto da 0xE0 come sull'AT.
 * ========================================================================== */
#define EXT 0x80

static const unsigned char hid2set1[0x68] = {
    /* 00 */ 0,0,0,0,
    /* 04 a..z */ 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,
                  0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,
                  0x15,0x2C,
    /* 1E 1..0 */ 0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,
    /* 28 */ 0x1C,0x01,0x0E,0x0F,0x39,
    /* 2D */ 0x0C,0x0D,0x1A,0x1B,0x2B,0x2B,0x27,0x28,0x29,0x33,0x34,0x35,0x3A,
    /* 3A F1..F12 */ 0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,0x57,0x58,
    /* 46 */ 0,0x46,0,
    /* 49 Ins Home PgUp Del End PgDn -> tutti estesi */
             0x52|EXT,0x47|EXT,0x49|EXT,0x53|EXT,0x4F|EXT,0x51|EXT,
    /* 4F frecce */ 0x4D|EXT,0x4B|EXT,0x50|EXT,0x48|EXT,
    /* 53 */ 0x45,0x35|EXT,0x37,0x4A,0x4E,0x1C|EXT,
    /* 59 tastierino 1..9,0,. */ 0x4F,0x50,0x51,0x4B,0x4C,0x4D,0x47,0x48,0x49,
             0x52,0x53
};

/* I modificatori stanno nel primo byte, un bit per tasto. */
static const unsigned char mod2set1[8] = {
    0x1D, 0x2A, 0x38, 0x5B|EXT,     /* Ctrl, Shift, Alt, Gui  sinistri */
    0x1D|EXT, 0x36, 0x38|EXT, 0x5C|EXT   /*                    destri  */
};

static unsigned char g_rap_prec[8];

/* Aggiunge make o break di un codice alla scorta da mandare. */
static unsigned int accoda(unsigned char *out, unsigned int n,
                           unsigned char cod, int premuto)
{
    if (cod == 0 || n + 2 >= 32) return n;

    if (cod & EXT) out[n++] = 0xE0;
    out[n++] = (unsigned char)((cod & 0x7F) | (premuto ? 0x00 : 0x80));
    return n;
}

void usb_tastiera_rapporto(const unsigned char *r, int kbd_pid)
{
    unsigned char out[32];
    unsigned int  n = 0;
    unsigned int  i, j;

    /* Modificatori: si guarda ogni bit che e' cambiato. */
    for (i = 0; i < 8; i++) {
        unsigned int ora = (r[0] >> i) & 1;
        unsigned int pri = (g_rap_prec[0] >> i) & 1;
        if (ora != pri) n = accoda(out, n, mod2set1[i], (int)ora);
    }

    /* Rilasci: c'era prima, non c'e' adesso. */
    for (i = 2; i < 8; i++) {
        unsigned char u = g_rap_prec[i];
        int ancora = 0;
        if (u == 0) continue;
        for (j = 2; j < 8; j++) if (r[j] == u) { ancora = 1; break; }
        if (!ancora && u < sizeof(hid2set1)) n = accoda(out, n, hid2set1[u], 0);
    }

    /* Pressioni: c'e' adesso, non c'era prima. */
    for (i = 2; i < 8; i++) {
        unsigned char u = r[i];
        int prima = 0;
        if (u == 0) continue;
        /* 0x01 in tutte e sei le posizioni vuol dire «troppi tasti insieme»:
         * il rapporto non dice quali, quindi non c'e' niente da tradurre. */
        if (u == 0x01) continue;
        for (j = 2; j < 8; j++) if (g_rap_prec[j] == u) { prima = 1; break; }
        if (!prima && u < sizeof(hid2set1)) n = accoda(out, n, hid2set1[u], 1);
    }

    memcpy(g_rap_prec, r, 8);

    if (n > 0 && kbd_pid >= 0)
        ipc_send((unsigned int)kbd_pid, KBD_MSG_SCANCODE, out, n);
}
