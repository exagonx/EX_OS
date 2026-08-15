/* =============================================================================
 * kernel/include/ata.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver ATA/IDE in PIO — accesso DIRETTO al disco, mai tramite BIOS.
 *
 * PERCHE' NON IL BIOS. La richiesta era esplicita: se il BIOS dichiara 32
 * GB e il disco ne ha 64, il sistema deve vedere e usare tutti e 64. Il
 * BIOS non c'entra comunque nulla qui — il kernel gira in modo protetto e
 * INT 13h non e' piu' chiamabile — ma il punto sostanziale resta: la
 * capacita' NON si chiede a nessun intermediario, si chiede al disco con
 * IDENTIFY DEVICE, e si verifica con READ NATIVE MAX ADDRESS.
 *
 * LE TRE CAPACITA' CHE UN DISCO PUO' DICHIARARE, e perche' non coincidono:
 *
 *   1. quella del BIOS      — limitata dalle barriere storiche (504 MB,
 *                             2.1 GB, 8.4 GB, 32 GB, 137 GB). Irrilevante
 *                             per noi, ma e' cio' che l'utente vede
 *                             all'avvio ed e' il motivo del sospetto.
 *   2. IDENTIFY DEVICE      — quanto il disco dichiara ADESSO. Se c'e' una
 *                             HPA attiva o un jumper di limitazione, e'
 *                             gia' un valore ridotto.
 *   3. READ NATIVE MAX      — la capacita' di fabbrica, che l'HPA non
 *                             nasconde.
 *
 * Se (3) > (2) il disco ha spazio nascosto: capita con l'Host Protected
 * Area e con i jumper "clip" che molti dischi dell'epoca del Pentium II
 * avevano proprio per farsi accettare dai BIOS con la barriera dei 32 GB.
 * Il driver rileva la differenza e la RIPORTA; non la rimuove da solo —
 * togliere una HPA e' una modifica persistente al disco e deve essere una
 * decisione esplicita, non un effetto collaterale del rilevamento.
 * ============================================================================= */

#ifndef ATA_H
#define ATA_H

#include "kernel.h"

/* Un canale ha due unita' (master/slave), e i canali sono due. */
#define ATA_MAX_DEVICES     4

#define ATA_SECTOR_SIZE     512

/* Tipo di unita' trovata */
#define ATA_TYPE_NONE       0   /* niente su questo slot */
#define ATA_TYPE_ATA        1   /* disco rigido */
#define ATA_TYPE_ATAPI      2   /* CD/DVD: vedi kernel/block/atapi.c */
#define ATA_TYPE_UNKNOWN    3   /* firma non riconosciuta */

typedef struct {
    uint8_t  presente;      /* 0 = slot vuoto */
    uint8_t  tipo;          /* ATA_TYPE_* */
    uint8_t  canale;        /* 0 = primario, 1 = secondario */
    uint8_t  unita;         /* 0 = master, 1 = slave */

    uint8_t  lba48;         /* 1 se supporta gli indirizzi a 48 bit */
    uint8_t  hpa;           /* 1 se supporta l'Host Protected Area */
    uint8_t  clippato;      /* 1 se nativo > dichiarato: spazio nascosto */

    /* Capacita' in SETTORI. 64 bit perche' con LBA48 non basta un uint32:
     * 2^32 settori sono gia' 2 TB, e il campo nativo puo' superarli. */
    uint64_t settori;       /* quanto il disco dichiara ora (IDENTIFY)   */
    uint64_t settori_nativi;/* capacita' di fabbrica (READ NATIVE MAX)   */

    char     modello[41];   /* IDENTIFY parole 27-46, gia' de-swappate   */
    char     seriale[21];   /* IDENTIFY parole 10-19                     */
    char     firmware[9];   /* IDENTIFY parole 23-26                     */
} AtaDevice;

/* Rileva le unita' sui due canali. Va chiamata dopo l'abilitazione degli
 * interrupt: le attese usano g_ticks. Ritorna quante unita' ATA ha
 * trovato. */
int  ata_init(void);

/* Numero di slot esaminati (sempre ATA_MAX_DEVICES) e accesso ai dati. */
const AtaDevice *ata_get_device(int indice);

/* Legge/scrive `n` settori a partire da `lba`. Ritorna 0, <0 su errore.
 * Sceglie da solo LBA28 o LBA48 in base a cosa serve e a cosa il disco
 * supporta. `n` massimo per chiamata: 255 (LBA28) / 65535 (LBA48). */
int  ata_read (int indice, uint64_t lba, uint32_t n, void *buf);
int  ata_write(int indice, uint64_t lba, uint32_t n, const void *buf);

/* Svuota la cache di scrittura del disco. Da chiamare prima di spegnere e
 * dopo qualunque modifica alla tabella delle partizioni. */
int  ata_flush(int indice);

/* =============================================================================
 * IL BUS CONDIVISO CON I LETTORI OTTICI
 *
 * Da qui in giu' c'e' cio' che kernel/block/atapi.c usa di ata.c. Un
 * lettore CD sta sugli STESSI canali, con gli STESSI registri e le stesse
 * due trappole di temporizzazione (il ritardo di 400 ns dopo la selezione,
 * l'attesa ancorata al PIT e non alla velocita' della CPU).
 *
 * Sono esportate invece di essere duplicate perche' una seconda copia
 * delle regole di attesa e' una copia che un giorno diverge: la lezione e'
 * gia' scritta in testa a ata_rw(), dove due funzioni quasi identiche
 * avevano fatto sopravvivere a lungo un baco corretto solo in una delle
 * due. ATAPI cambia i COMANDI, non il bus.
 * ============================================================================= */

/* Porte dei due canali */
#define ATA_PRIMARY_IO      0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_IO    0x170
#define ATA_SECONDARY_CTRL  0x376

/* Offset dal registro base. Sul ramo ATAPI LBA1/LBA2 non contengono un
 * indirizzo: sono il conteggio dei byte che il dispositivo consegna per
 * ogni DRQ. */
#define ATA_REG_DATA        0
#define ATA_REG_ERROR       1
#define ATA_REG_FEATURES    1
#define ATA_REG_SECCOUNT    2
#define ATA_REG_LBA0        3
#define ATA_REG_LBA1        4
#define ATA_REG_LBA2        5
#define ATA_REG_DRIVE       6
#define ATA_REG_STATUS      7
#define ATA_REG_COMMAND     7

/* Bit del registro di stato */
#define ATA_SR_BSY          0x80
#define ATA_SR_DRDY         0x40
#define ATA_SR_DF           0x20
#define ATA_SR_DRQ          0x08
#define ATA_SR_ERR          0x01

/* Bit del registro di controllo */
#define ATA_CTRL_NIEN       0x02
#define ATA_CTRL_SRST       0x04

uint16_t ata_base_io  (int canale);
uint16_t ata_base_ctrl(int canale);

/* Ritardo di ~400 ns: quattro letture dello stato ALTERNATO. */
void ata_ritardo(int canale);

/* Seleziona master/slave, con il ritardo obbligatorio. */
void ata_seleziona(int canale, int unita, uint8_t testa_o_lba);

/* Aspetta che BSY si abbassi entro una scadenza REALE (millisecondi).
 * Ritorna lo stato letto, o -1 su timeout o bus flottante. */
int  ata_attendi_non_bsy(int canale, uint32_t timeout_ms);

/* Aspetta DRQ. Ritorna 0, -1 su timeout o bus assente.
 *
 * La variante MUTA esiste per ATAPI, dove un errore del dispositivo e'
 * flusso normale e non un guasto: "vassoio vuoto" arriva come ERR, e
 * stamparlo come errore riempirebbe il log a ogni sondaggio del lettore.
 * Ritorna 0, -1 (timeout o bus assente) oppure -2 se il dispositivo ha
 * alzato ERR/DF, con lo stato in `stato_out` se non e' NULL. */
int  ata_attendi_drq(int canale, uint32_t timeout_ms);
int  ata_attendi_drq_muto(int canale, uint32_t timeout_ms, uint8_t *stato_out);

/* Attesa in millisecondi ancorata al PIT (g_ticks), non a un conteggio di
 * cicli: e' la lezione gia' pagata tre volte in questo progetto. */
void ata_attesa_ms(uint32_t ms);

#endif /* ATA_H */
