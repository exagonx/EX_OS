/* =============================================================================
 * kernel/include/fat12.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef FAT12_H
#define FAT12_H

#include "kernel.h"

/* Entry directory FAT12 (32 byte, layout hardware) */
typedef struct PACKED {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attr;
    uint8_t  reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t first_cluster;
    uint32_t file_size;
} Fat12DirEntry;

/* Attributi FAT12 */
#define FAT12_ATTR_READONLY  0x01
#define FAT12_ATTR_HIDDEN    0x02
#define FAT12_ATTR_SYSTEM    0x04
#define FAT12_ATTR_VOLUME    0x08
#define FAT12_ATTR_DIRECTORY 0x10
#define FAT12_ATTR_ARCHIVE   0x20

/* Stat risultante da fat12_stat() */
typedef struct {
    uint32_t size;
    uint16_t first_cluster;
    uint8_t  attr;
    uint16_t date;
    uint16_t time;
} Fat12Stat;

/* Interfaccia pubblica */
int  fat12_init(uint8_t drive);
/* Come fat12_init ma SILENZIOSA e senza ritentativi: risponde alla sola
 * domanda «c'e' un floppy?». Se riesce il driver e' gia' pronto. Vedi il
 * commento su g_sondaggio in fat12.c. */
int  fat12_sonda(uint8_t drive);
int  fat12_open(const char *path, uint32_t flags);
int  fat12_read(int handle, void *buf, uint32_t size, uint32_t offset);
/* ⚠️ `offset` e' arrivato ad agosto 2026, e prima non c'era: la scrittura
 * si accodava SEMPRE alla fine del file, qualunque cosa avesse fatto
 * lseek(). Vedi il commento in fat12_write(). */
int  fat12_write(int handle, const void *buf, uint32_t size, uint32_t offset);
int  fat12_close(int handle);
int  fat12_stat(const char *path, Fat12Stat *st);
/* Elenca una directory a partire dalla 'start'-esima voce valida.
 * La paginazione serve perche' il buffer del chiamante e' limitato: senza,
 * le directory piu' grandi del buffer venivano troncate in SILENZIO. */
int  fat12_readdir_path(const char *path, Fat12DirEntry *out_buf,
                         uint32_t max_entries, uint32_t *count_out,
                         uint32_t start);
void fat12_format_name(const Fat12DirEntry *entry, char *out /* >= 13 byte */);
/* 1 se il controller floppy ha risposto e il filesystem e' montato.
 * ⚠️ Un fallimento e' il segnale di un avvio da CD per emulazione
 * floppy: vedi il commento in kernel/fs/fat12.c. */
int  fat12_pronto(void);

int  fat12_delete(const char *path);
/* Cambia il NOME senza spostare i dati. ⚠️ SOLO nella stessa directory
 * (-3), destinazione che non deve esistere (-2): stesse regole di fat.c ed
 * ext2.c. Vedi il commento esteso in kernel/fs/fat12.c. */
int  fat12_rename(const char *da, const char *a);

/* Crea una directory. Solo nella root: il driver risolve i percorsi a un
 * solo livello, quindi una directory più in profondità sarebbe corretta
 * sul supporto ma irraggiungibile. Ritorna 0, -17 se esiste già. */
int  fat12_mkdir(const char *path);

/* Cancella una directory VUOTA. Rifiuta se contiene qualcosa (-39): senza
 * cancellazione ricorsiva i file rimasti dentro diventerebbero
 * irraggiungibili e i loro cluster persi. Solo nella root, come mkdir. */
int  fat12_rmdir(const char *path);

/* Riversa su disco FAT, root directory e settori in cache modificati.
 * Da chiamare prima di spegnere o riavviare. Richiede interrupt
 * abilitati (il driver FDC usa attese su g_ticks e IRQ6). */
int  fat12_sync(void);

/* Spegne il motore del floppy. Da chiamare SOLO da un punto in cui e'
 * certo che nessun trasferimento sia in corso: il driver FDC non e'
 * rientrante e spegnere il motore a meta' di una lettura la corrompe.
 * Il prossimo accesso al disco lo riaccende pagando una volta i 300 ms di
 * stabilizzazione. */
void fat12_motor_off(void);

/* =============================================================================
 * Accesso GREZZO al supporto floppy, per l'astrazione a blocchi (blk.c).
 *
 * Passa dalla STESSA cache usata dal filesystem, e non e' un dettaglio:
 * se il livello a blocchi leggesse il disco scavalcando la cache,
 * vedrebbe dati vecchi ogni volta che il FAT12 ha una scrittura ancora
 * sporca in memoria — e viceversa una scrittura grezza non
 * invaliderebbe la copia in cache. Due viste incoerenti dello stesso
 * supporto sono un modo perfetto per corrompere un filesystem.
 *
 * lba e' assoluto sul floppy (0..2879). Ritorna 0, <0 su errore.
 * ============================================================================= */
int  fat12_dev_read (uint32_t lba, void *buf);
int  fat12_dev_write(uint32_t lba, const void *buf);

#endif /* FAT12_H */
