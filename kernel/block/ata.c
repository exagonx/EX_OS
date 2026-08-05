/* =============================================================================
 * kernel/block/ata.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver ATA/IDE in PIO. Vedi kernel/include/ata.h per il razionale sulle
 * capacita' dichiarate; qui sotto ci sono le trappole del protocollo.
 *
 * SI USA IL POLLING, NON L'IRQ14/15. Gli stub ci sono gia'
 * (kernel/arch/x86/idt.c), ma il bit nIEN nel registro di controllo
 * disattiva gli interrupt del dispositivo e si aspetta leggendo lo stato.
 * E' la scelta piu' sicura per partire: niente handler da coordinare con
 * il driver FDC, e soprattutto niente dipendenza dal routing degli
 * interrupt su una macchina sconosciuta — che su questo stesso progetto ha
 * gia' fatto perdere un'intera sessione con l'IRQ6 del floppy.
 *
 * LE ATTESE SONO IN TEMPO REALE, NON A CONTEGGIO. E' la lezione gia'
 * pagata tre volte in questo progetto (loop di NOP dell'FDC, KBC_POLL_MAX,
 * usleep sotto il tick): un'attesa ancorata alla velocita' della CPU vale
 * tempi diversi su macchine diverse, e su hardware vero fallisce. Qui si
 * usa g_ticks, che e' il PIT.
 *
 * TRAPPOLE DEL PROTOCOLLO ATA, e come sono trattate:
 *
 *  - Ritardo di 400 ns dopo aver selezionato l'unita' o scritto un
 *    comando: i registri non sono validi subito. Si ottiene leggendo
 *    QUATTRO volte lo stato alternato (0x3F6), che non consuma lo stato
 *    pendente come farebbe leggere 0x1F7.
 *  - Bus "flottante": se non c'e' nulla attaccato, lo stato legge 0xFF.
 *    Va riconosciuto PRIMA di aspettare qualunque cosa, o si aspetta a
 *    vuoto fino al timeout su ogni slot vuoto.
 *  - IDENTIFY su un ATAPI fallisce con ERR: la firma nei registri LBA
 *    mid/high (0x14/0xEB) dice che e' un lettore ottico. Va riconosciuto,
 *    altrimenti lo si scambia per un disco rotto.
 *  - Le stringhe di IDENTIFY sono a byte scambiati dentro ogni parola a
 *    16 bit. Senza de-swap il modello esce come "TSHIAB..." invece di
 *    "HITACHI...".
 *  - Il conteggio settori 0 significa 256 (LBA28) o 65536 (LBA48). Non si
 *    manda mai 0.
 *  - Con LBA48 i registri sono a due livelli: si scrive prima il byte
 *    ALTO poi quello BASSO sulla stessa porta. Invertire l'ordine da
 *    indirizzi silenziosamente sbagliati — cioe' letture della zona
 *    sbagliata del disco senza alcun errore segnalato.
 * ============================================================================= */

#include "kernel.h"
#include "ata.h"
#include "sched.h"      /* g_ticks */

/* Le porte, gli offset dei registri e i bit di stato stanno in ata.h:
 * sono lo stesso bus che usa kernel/block/atapi.c, e una seconda copia
 * sarebbe una copia da tenere allineata a mano. */

/* Comandi */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_FLUSH           0xE7
#define ATA_CMD_FLUSH_EXT       0xEA
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1    /* IDENTIFY per i dispositivi ATAPI */
#define ATA_CMD_READ_NATIVE     0xF8
#define ATA_CMD_READ_NATIVE_EXT 0x27

/* Scadenze reali, in millisecondi. Generose: uno spin-up puo' durare
 * secondi su un disco dell'epoca, e un timeout troppo corto si
 * presenterebbe come "disco assente". */
#define ATA_TMO_BSY_MS      5000    /* attesa che il comando finisca */
#define ATA_TMO_DRQ_MS      3000    /* attesa dati pronti            */
#define ATA_TMO_IDENT_MS    3000

/* Massimo settori per singolo comando. LBA28 usa un conteggio a 8 bit
 * (0 = 256, ma non lo usiamo), LBA48 a 16 bit. */
#define ATA_MAX_N28         255
#define ATA_MAX_N48         65535

static AtaDevice g_dev[ATA_MAX_DEVICES];
static int       g_trovati = 0;

/* =============================================================================
 * Helper di basso livello
 * ============================================================================= */

uint16_t ata_base_io(int canale)
{
    return (canale == 0) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
}

uint16_t ata_base_ctrl(int canale)
{
    return (canale == 0) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
}

/* Nomi brevi per l'uso interno: il resto del file era gia' scritto cosi'
 * e rinominare ogni chiamata avrebbe sporcato un diff che deve restare
 * leggibile. */
#define base_io(c)      ata_base_io(c)
#define base_ctrl(c)    ata_base_ctrl(c)
#define ata_400ns(c)    ata_ritardo(c)

/* Ritardo di ~400 ns: quattro letture dello stato ALTERNATO.
 * Va letto 0x3F6 e non 0x1F7 perche' lo stato alternato non ha effetti
 * collaterali, mentre leggere il registro di stato normale azzera
 * l'interrupt pendente. */
void ata_ritardo(int canale)
{
    uint16_t c = ata_base_ctrl(canale);
    port_inb(c); port_inb(c); port_inb(c); port_inb(c);
}

/* Attesa in millisecondi ancorata al PIT. Serve ai lettori ottici, che
 * dopo l'inserimento di un disco rispondono "non ancora pronto" per
 * qualche secondo e vanno risondati a intervalli, non a raffica. */
void ata_attesa_ms(uint32_t ms)
{
    uint32_t fine = g_ticks + (ms + 9) / 10;
    while (g_ticks < fine) { /* il PIT avanza sotto interrupt */ }
}

/* Aspetta che BSY si abbassi, entro una scadenza REALE.
 * Ritorna lo stato letto, o -1 su timeout. */
int ata_attendi_non_bsy(int canale, uint32_t timeout_ms)
{
    uint16_t io    = base_io(canale);
    uint32_t scad  = g_ticks + (timeout_ms + 9) / 10;
    uint8_t  st;

    for (;;) {
        st = port_inb(io + ATA_REG_STATUS);

        /* Bus flottante: nessun dispositivo. Non ha senso attendere. */
        if (st == 0xFF) return -1;

        if (!(st & ATA_SR_BSY)) return (int)st;

        if (g_ticks >= scad) {
            klog(LOG_ERROR, "ATA: timeout BSY sul canale %d (stato=0x%02x)",
                 canale, st);
            return -1;
        }
    }
}

/* Aspetta DRQ (dati pronti) dopo che BSY si e' abbassato, SENZA stampare
 * nulla. Ritorna 0, -1 su timeout o bus assente, -2 se il dispositivo ha
 * alzato ERR/DF. Vedi ata.h per il motivo per cui la variante muta e'
 * quella di base: su ATAPI un ERR e' spesso "vassoio vuoto", cioe' una
 * risposta, non un guasto. */
int ata_attendi_drq_muto(int canale, uint32_t timeout_ms, uint8_t *stato_out)
{
    uint16_t io   = base_io(canale);
    uint32_t scad = g_ticks + (timeout_ms + 9) / 10;

    for (;;) {
        uint8_t st = port_inb(io + ATA_REG_STATUS);

        if (stato_out) *stato_out = st;

        if (st == 0xFF) return -1;

        /* ERR e DF vanno controllati PRIMA di DRQ: un comando fallito
         * puo' non alzare mai DRQ, e aspettarlo significherebbe restare
         * fino al timeout invece di riportare subito l'errore vero. */
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return -2;

        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;

        if (g_ticks >= scad) return -1;
    }
}

int ata_attendi_drq(int canale, uint32_t timeout_ms)
{
    uint8_t st = 0;
    int     r  = ata_attendi_drq_muto(canale, timeout_ms, &st);

    if (r == -2) {
        uint8_t err = port_inb(base_io(canale) + ATA_REG_ERROR);
        klog(LOG_ERROR, "ATA: errore canale %d (stato=0x%02x errore=0x%02x)",
             canale, st, err);
        return -1;
    }
    if (r == -1 && st != 0xFF) {
        klog(LOG_ERROR, "ATA: timeout DRQ sul canale %d (stato=0x%02x)",
             canale, st);
    }
    return (r < 0) ? -1 : 0;
}

/* Seleziona master/slave. Il ritardo dopo la selezione non e' opzionale:
 * i registri dell'unita' appena selezionata non sono validi prima. */
void ata_seleziona(int canale, int unita, uint8_t testa_o_lba)
{
    uint16_t io = base_io(canale);
    port_outb(io + ATA_REG_DRIVE,
              (uint8_t)(0xA0 | (unita ? 0x10 : 0x00) | (testa_o_lba & 0x0F)));
    ata_400ns(canale);
}

/* Copia una stringa IDENTIFY de-swappando i byte dentro ogni parola. */
static void ata_stringa(const uint16_t *id, int prima_parola, int n_parole,
                        char *out)
{
    int i;
    int j = 0;

    for (i = 0; i < n_parole; i++) {
        uint16_t w = id[prima_parola + i];
        out[j++] = (char)((w >> 8) & 0xFF);
        out[j++] = (char)(w & 0xFF);
    }
    out[j] = '\0';

    /* Toglie gli spazi finali: IDENTIFY riempie con spazi, non con NUL. */
    while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '\0')) {
        out[--j] = '\0';
    }
}

/* =============================================================================
 * READ NATIVE MAX ADDRESS — la capacita' di fabbrica
 *
 * E' il comando che smaschera lo spazio nascosto. Ritorna l'LBA PIU' ALTO
 * indirizzabile, quindi il numero di settori e' quel valore + 1 — un
 * fuori-di-uno qui significherebbe sbagliare di un settore la dimensione
 * del disco, ed e' esattamente il genere di errore che in un programma di
 * partizionamento si paga caro.
 * ============================================================================= */
static uint64_t ata_native_max(int canale, int unita, int lba48)
{
    uint16_t io = base_io(canale);
    int      st;

    if (lba48) {
        ata_seleziona(canale, unita, 0);
        port_outb(io + ATA_REG_DRIVE, (uint8_t)(0x40 | (unita ? 0x10 : 0)));
        ata_400ns(canale);
        port_outb(io + ATA_REG_COMMAND, ATA_CMD_READ_NATIVE_EXT);
    } else {
        port_outb(io + ATA_REG_DRIVE, (uint8_t)(0xE0 | (unita ? 0x10 : 0)));
        ata_400ns(canale);
        port_outb(io + ATA_REG_COMMAND, ATA_CMD_READ_NATIVE);
    }

    ata_400ns(canale);
    st = ata_attendi_non_bsy(canale, ATA_TMO_BSY_MS);

    /* Non tutti i dischi supportano questo comando: se fallisce si
     * ritorna 0 e il chiamante usa la sola IDENTIFY. Non e' un errore
     * fatale, e' un'informazione che non si e' potuta ottenere. */
    if (st < 0 || ((uint8_t)st & (ATA_SR_ERR | ATA_SR_DF))) return 0;

    if (lba48) {
        uint64_t lo, hi;
        /* Il registro va letto due volte: prima il byte basso, poi
         * riletto con HOB=1 nel registro di controllo per quello alto. */
        lo  = (uint64_t)port_inb(io + ATA_REG_LBA0);
        lo |= (uint64_t)port_inb(io + ATA_REG_LBA1) << 8;
        lo |= (uint64_t)port_inb(io + ATA_REG_LBA2) << 16;

        port_outb(base_ctrl(canale), 0x80 | ATA_CTRL_NIEN);   /* HOB=1 */
        ata_400ns(canale);
        hi  = (uint64_t)port_inb(io + ATA_REG_LBA0) << 24;
        hi |= (uint64_t)port_inb(io + ATA_REG_LBA1) << 32;
        hi |= (uint64_t)port_inb(io + ATA_REG_LBA2) << 40;
        port_outb(base_ctrl(canale), ATA_CTRL_NIEN);          /* HOB=0 */

        return (lo | hi) + 1;
    } else {
        uint32_t lba;
        lba  = (uint32_t)port_inb(io + ATA_REG_LBA0);
        lba |= (uint32_t)port_inb(io + ATA_REG_LBA1) << 8;
        lba |= (uint32_t)port_inb(io + ATA_REG_LBA2) << 16;
        lba |= (uint32_t)(port_inb(io + ATA_REG_DRIVE) & 0x0F) << 24;
        return (uint64_t)lba + 1;
    }
}

/* =============================================================================
 * Rilevamento di una singola unita'
 * ============================================================================= */
static void ata_rileva(int idx, int canale, int unita)
{
    uint16_t   io = base_io(canale);
    AtaDevice *d  = &g_dev[idx];
    uint16_t   id[256];
    int        st, i;
    uint8_t    cl, ch;

    d->presente = 0;
    d->tipo     = ATA_TYPE_NONE;
    d->canale   = (uint8_t)canale;
    d->unita    = (uint8_t)unita;

    /* Interrupt del dispositivo disabilitati: si lavora in polling. */
    port_outb(base_ctrl(canale), ATA_CTRL_NIEN);

    ata_seleziona(canale, unita, 0);

    /* Bus flottante: slot vuoto, si esce subito senza aspettare nulla. */
    st = port_inb(io + ATA_REG_STATUS);
    if (st == 0xFF || st == 0x00) return;

    /* Azzera i registri prima di IDENTIFY: alcuni dispositivi rispondono
     * male se contengono residui di un comando precedente. */
    port_outb(io + ATA_REG_SECCOUNT, 0);
    port_outb(io + ATA_REG_LBA0, 0);
    port_outb(io + ATA_REG_LBA1, 0);
    port_outb(io + ATA_REG_LBA2, 0);

    port_outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns(canale);

    st = port_inb(io + ATA_REG_STATUS);
    if (st == 0) return;    /* niente qui */

    if (ata_attendi_non_bsy(canale, ATA_TMO_IDENT_MS) < 0) return;

    /* Un ATAPI rifiuta IDENTIFY con ERR, ma lascia la propria firma nei
     * registri LBA mid/high. Senza questo controllo un lettore CD
     * sembrerebbe un disco guasto. */
    cl = port_inb(io + ATA_REG_LBA1);
    ch = port_inb(io + ATA_REG_LBA2);

    if (cl == 0x14 && ch == 0xEB) {
        d->presente = 1;
        d->tipo     = ATA_TYPE_ATAPI;

        /* IDENTIFY PACKET DEVICE: le stringhe stanno nelle stesse parole
         * di IDENTIFY, ma il comando e' un altro perche' l'unita' ha
         * appena RIFIUTATO quello normale. Senza questo passaggio il
         * lettore comparirebbe in `disk` come una riga vuota, e un
         * dispositivo senza nome e' indistinguibile da uno non
         * riconosciuto.
         *
         * Un fallimento qui non toglie il dispositivo: resta un ATAPI, e
         * lo si dira' senza il modello. La capacita' NON si chiede qui —
         * un lettore la conosce solo quando ha un disco dentro, e la
         * risposta cambia a ogni inserimento: la chiede atapi.c, ogni
         * volta che serve. */
        port_outb(io + ATA_REG_SECCOUNT, 0);
        port_outb(io + ATA_REG_LBA0, 0);
        port_outb(io + ATA_REG_LBA1, 0);
        port_outb(io + ATA_REG_LBA2, 0);
        port_outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        ata_400ns(canale);

        if (ata_attendi_drq_muto(canale, ATA_TMO_IDENT_MS, NULL) == 0) {
            for (i = 0; i < 256; i++) id[i] = port_inw(io + ATA_REG_DATA);

            ata_stringa(id, 27, 20, d->modello);
            ata_stringa(id, 10, 10, d->seriale);
            ata_stringa(id, 23,  4, d->firmware);
        }

        klog(LOG_INFO, "ATA: canale %d %s: lettore ottico ATAPI%s%s",
             canale, unita ? "slave" : "master",
             d->modello[0] ? " " : "", d->modello);
        return;
    }
    if (cl != 0x00 || ch != 0x00) {
        d->presente = 1;
        d->tipo     = ATA_TYPE_UNKNOWN;
        klog(LOG_WARN, "ATA: canale %d %s: firma sconosciuta (0x%02x 0x%02x)",
             canale, unita ? "slave" : "master", cl, ch);
        return;
    }

    if (ata_attendi_drq(canale, ATA_TMO_IDENT_MS) < 0) return;

    for (i = 0; i < 256; i++) {
        id[i] = port_inw(io + ATA_REG_DATA);
    }

    d->presente = 1;
    d->tipo     = ATA_TYPE_ATA;

    ata_stringa(id, 27, 20, d->modello);
    ata_stringa(id, 10, 10, d->seriale);
    ata_stringa(id, 23,  4, d->firmware);

    /* Parola 83 bit 10: comandi a 48 bit supportati.
     * Parola 82 bit 10: Host Protected Area supportata. */
    d->lba48 = (id[83] & (1 << 10)) ? 1 : 0;
    d->hpa   = (id[82] & (1 << 10)) ? 1 : 0;

    /* Capacita' dichiarata: parole 60-61 (28 bit) oppure 100-103 (48 bit).
     * Con LBA48 le 60-61 restano valide ma SATURANO a 0x0FFFFFFF, quindi
     * per un disco grande darebbero 128 GiB qualunque sia la dimensione
     * vera: e' una delle strade classiche per cui un sistema "vede meno
     * disco di quanto ce n'e'". */
    if (d->lba48) {
        d->settori = (uint64_t)id[100]
                   | ((uint64_t)id[101] << 16)
                   | ((uint64_t)id[102] << 32)
                   | ((uint64_t)id[103] << 48);
    } else {
        d->settori = 0;
    }

    if (d->settori == 0) {
        d->settori = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    }

    /* Capacita' di fabbrica, per scoprire lo spazio nascosto. */
    d->settori_nativi = ata_native_max(canale, unita, d->lba48);

    d->clippato = (d->settori_nativi > d->settori) ? 1 : 0;

    klog(LOG_INFO, "ATA: canale %d %s: '%s' %u MB%s%s",
         canale, unita ? "slave" : "master", d->modello,
         (uint32_t)(d->settori / 2048),
         d->lba48 ? " LBA48" : " LBA28",
         d->clippato ? " [SPAZIO NASCOSTO]" : "");

    if (d->clippato) {
        klog(LOG_WARN, "ATA: il disco dichiara %u MB ma di fabbrica ne ha %u "
             "— HPA attiva o jumper di limitazione",
             (uint32_t)(d->settori / 2048),
             (uint32_t)(d->settori_nativi / 2048));
    }
}

int ata_init(void)
{
    int canale, unita, idx = 0;

    g_trovati = 0;

    for (idx = 0; idx < ATA_MAX_DEVICES; idx++) {
        g_dev[idx].presente = 0;
        g_dev[idx].tipo     = ATA_TYPE_NONE;
    }

    klog(LOG_INFO, "ATA: rilevamento unita' sui canali primario e secondario...");

    idx = 0;
    for (canale = 0; canale < 2; canale++) {
        for (unita = 0; unita < 2; unita++) {
            ata_rileva(idx, canale, unita);
            if (g_dev[idx].tipo == ATA_TYPE_ATA) g_trovati++;
            idx++;
        }
    }

    if (g_trovati == 0) {
        klog(LOG_INFO, "ATA: nessun disco rigido trovato");
    }

    return g_trovati;
}

const AtaDevice *ata_get_device(int indice)
{
    if (indice < 0 || indice >= ATA_MAX_DEVICES) return NULL;
    return &g_dev[indice];
}

/* =============================================================================
 * Trasferimento
 *
 * Un solo punto per lettura e scrittura: e' la stessa lezione imparata sul
 * driver FDC, dove due copie quasi identiche avevano fatto sopravvivere a
 * lungo un baco corretto solo in una delle due.
 * ============================================================================= */
static int ata_rw(int indice, uint64_t lba, uint32_t n, void *buf, int scrivi)
{
    const AtaDevice *d;
    uint16_t  io;
    int       canale, unita, usa48;
    uint8_t  *p = (uint8_t *)buf;
    uint32_t  s;

    d = ata_get_device(indice);
    if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATA) return -1;
    if (n == 0) return -1;

    /* CONTROLLO DI LIMITE — non e' una formalita'.
     * Una lettura oltre la fine del disco ritorna un errore; una
     * SCRITTURA oltre la fine, su un disco che la accettasse per
     * troncamento dell'indirizzo, andrebbe a finire da un'altra parte. Il
     * controllo va fatto qui, una volta, dove passano tutti. */
    if (lba + n > d->settori || lba + n < lba /* overflow */) {
        klog(LOG_ERROR, "ATA: accesso fuori disco (lba=%u+%u, settori=%u)",
             (uint32_t)lba, n, (uint32_t)d->settori);
        return -1;
    }

    canale = d->canale;
    unita  = d->unita;
    io     = base_io(canale);

    /* LBA48 se serve davvero (oltre i 2^28 settori indirizzabili o piu' di
     * 255 settori per comando) e se il disco lo supporta. */
    usa48 = d->lba48 && ((lba + n) > 0x0FFFFFFFull || n > ATA_MAX_N28);

    if (!usa48 && (lba + n) > 0x0FFFFFFFull) {
        klog(LOG_ERROR, "ATA: LBA oltre i 28 bit ma il disco non supporta LBA48");
        return -1;
    }

    while (n > 0) {
        uint32_t blocco = n;
        uint32_t max    = usa48 ? ATA_MAX_N48 : ATA_MAX_N28;
        int      st;

        if (blocco > max) blocco = max;

        port_outb(base_ctrl(canale), ATA_CTRL_NIEN);

        if (usa48) {
            port_outb(io + ATA_REG_DRIVE, (uint8_t)(0x40 | (unita ? 0x10 : 0)));
            ata_400ns(canale);

            /* ORDINE OBBLIGATORIO: prima il byte ALTO, poi quello BASSO
             * sulla stessa porta. Il dispositivo tiene due livelli e
             * invertirli produce indirizzi sbagliati SENZA errore. */
            port_outb(io + ATA_REG_SECCOUNT, (uint8_t)((blocco >> 8) & 0xFF));
            port_outb(io + ATA_REG_LBA0,     (uint8_t)((lba >> 24) & 0xFF));
            port_outb(io + ATA_REG_LBA1,     (uint8_t)((lba >> 32) & 0xFF));
            port_outb(io + ATA_REG_LBA2,     (uint8_t)((lba >> 40) & 0xFF));

            port_outb(io + ATA_REG_SECCOUNT, (uint8_t)(blocco & 0xFF));
            port_outb(io + ATA_REG_LBA0,     (uint8_t)(lba & 0xFF));
            port_outb(io + ATA_REG_LBA1,     (uint8_t)((lba >> 8) & 0xFF));
            port_outb(io + ATA_REG_LBA2,     (uint8_t)((lba >> 16) & 0xFF));

            port_outb(io + ATA_REG_COMMAND,
                      scrivi ? ATA_CMD_WRITE_PIO_EXT : ATA_CMD_READ_PIO_EXT);
        } else {
            port_outb(io + ATA_REG_DRIVE,
                      (uint8_t)(0xE0 | (unita ? 0x10 : 0) | ((lba >> 24) & 0x0F)));
            ata_400ns(canale);

            port_outb(io + ATA_REG_SECCOUNT, (uint8_t)blocco);
            port_outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
            port_outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
            port_outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));

            port_outb(io + ATA_REG_COMMAND,
                      scrivi ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);
        }

        ata_400ns(canale);

        /* Un settore alla volta: il dispositivo alza DRQ per OGNI settore
         * del blocco, non una volta sola. Trasferire tutto dopo un solo
         * DRQ e' un errore classico che funziona finche' il blocco e' di
         * un settore e poi corrompe i dati. */
        for (s = 0; s < blocco; s++) {
            if (ata_attendi_drq(canale, ATA_TMO_DRQ_MS) < 0) return -1;

            /* ⚠️ UNA ISTRUZIONE, NON UN CICLO. Fino ad agosto 2026 qui
             * c'era un ciclo che chiamava port_inw 256 volte per settore:
             * duemila istruzioni per 512 byte, ed e' da li' che venivano
             * gli 0,75 MB/s misurati — non dal controller e non dal disco.
             *
             * L'ordine dei byte in memoria e' lo stesso che il ciclo
             * componeva a mano (basso, poi alto): su x86 `rep insw` fa
             * esattamente quello, quindi i dati sul disco non cambiano. */
            if (scrivi) port_outsw(io + ATA_REG_DATA, p, ATA_SECTOR_SIZE / 2);
            else        port_insw (io + ATA_REG_DATA, p, ATA_SECTOR_SIZE / 2);
            p += ATA_SECTOR_SIZE;
        }

        /* Dopo una scrittura il dispositivo puo' restare occupato: va
         * atteso prima del comando successivo. */
        st = ata_attendi_non_bsy(canale, ATA_TMO_BSY_MS);
        if (st < 0 || ((uint8_t)st & (ATA_SR_ERR | ATA_SR_DF))) {
            klog(LOG_ERROR, "ATA: %s fallita a lba=%u (stato=0x%02x)",
                 scrivi ? "scrittura" : "lettura", (uint32_t)lba, (uint8_t)st);
            return -1;
        }

        lba += blocco;
        n   -= blocco;
    }

    return 0;
}

int ata_read(int indice, uint64_t lba, uint32_t n, void *buf)
{
    return ata_rw(indice, lba, n, buf, 0);
}

int ata_write(int indice, uint64_t lba, uint32_t n, const void *buf)
{
    /* Il cast toglie il const: ata_rw non modifica il buffer quando
     * scrivi != 0, ma condividere una sola funzione vale il cast. */
    return ata_rw(indice, lba, n, (void *)buf, 1);
}

int ata_flush(int indice)
{
    const AtaDevice *d = ata_get_device(indice);
    uint16_t io;
    int      st;

    if (d == NULL || !d->presente || d->tipo != ATA_TYPE_ATA) return -1;

    io = base_io(d->canale);

    port_outb(io + ATA_REG_DRIVE, (uint8_t)(0xE0 | (d->unita ? 0x10 : 0)));
    ata_400ns(d->canale);
    port_outb(io + ATA_REG_COMMAND, d->lba48 ? ATA_CMD_FLUSH_EXT : ATA_CMD_FLUSH);
    ata_400ns(d->canale);

    st = ata_attendi_non_bsy(d->canale, ATA_TMO_BSY_MS);
    if (st < 0 || ((uint8_t)st & (ATA_SR_ERR | ATA_SR_DF))) return -1;

    return 0;
}
