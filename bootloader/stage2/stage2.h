/* =============================================================================
 * bootloader/stage2/stage2.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef STAGE2_H
#define STAGE2_H

/* =============================================================================
 * Tipi base (no stdint.h in freestanding — li definiamo noi)
 * ============================================================================= */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef uint32_t            size_t;
typedef uint32_t            uintptr_t;

#define NULL                ((void*)0)
#define TRUE                1
#define FALSE               0

/* =============================================================================
 * Indirizzi di memoria fissi (layout concordato con Stage 1)
 *
 * Mappa completa (indirizzi fisici in Real Mode):
 *   0x0500          Stage 2 caricato da Stage 1
 *   0x7000          Stack Stage 2 (top, cresce verso il basso)
 *   0x7C00          Stage 1 + BPB FAT12
 *   0x7E00          Root directory (14 * 512 = 7168 byte, fine a 0x9A00)
 *   0xA000          FAT1 (9 * 512 = 4608 byte, fine a 0xB200)
 *                   FIX BUG #2: spostata da 0x9000 a 0xA000 per non
 *                   sovrapporsi alla root dir (0x7E00..0x9A00)
 *   0xB000          Buffer temporaneo lettura settori (DISK_BUFFER, 512 byte)
 *   0xC000          BootInfo per il kernel
 *                   FIX BUG #3: spostata da 0xB000 a 0xC000 per non
 *                   coincidere con DISK_BUFFER_ADDR (0xB000)
 *   0x100000 (1MB)  Kernel caricato da Stage 2
 * ============================================================================= */

/* Stage 2 è caricato qui da Stage 1 */
#define STAGE2_LOAD_ADDR    0x0500

/* Stack Stage 2 (top, cresce verso il basso) */
#define STAGE2_STACK_TOP    0x7000

/* Root directory FAT12 caricata da Stage 1 */
#define ROOT_DIR_ADDR       0x7E00

/* FIX BUG #2: FAT1 spostata a 0xA000 (era 0x9000).
 * La root dir occupa 0x7E00..0x9A00; la FAT deve stare dopo 0x9A00.
 * 0xA000 garantisce margine sufficiente. */
#define FAT_BUFFER_ADDR     0xA000

/* Buffer temporaneo per lettura settori (1 settore = 512 byte) */
#define DISK_BUFFER_ADDR    0xB000

/* Kernel caricato qui (1MB = 0x100000) */
#define KERNEL_LOAD_ADDR    0x100000

/* Mappa E820 costruita qui (massimo 32 entry * 24 byte = 768 byte) */
#define E820_MAP_ADDR       0x0A800

/* FIX BUG #3: BootInfo spostata a 0xC000 (era 0xB000 = uguale a DISK_BUFFER).
 * Il kernel leggerà BootInfo da KERNEL_PHYS_BASE - sizeof(BootInfo) oppure
 * dall'indirizzo fisso 0xC000 (aggiornare BOOTINFO_PHYS_ADDR in kernel.h). */
#define BOOTINFO_ADDR       0xC000

/* =============================================================================
 * Strutture FAT12
 * ============================================================================= */

/* BPB (BIOS Parameter Block) — layout fisico nel boot sector */
typedef struct __attribute__((packed)) {
    uint8_t     jump[3];            /* EB xx 90 */
    uint8_t     oem_name[8];        /* "EXOS    " */
    uint16_t    bytes_per_sector;   /* 512 */
    uint8_t     sectors_per_cluster;/* 1 */
    uint16_t    reserved_sectors;   /* 1 */
    uint8_t     num_fats;           /* 2 */
    uint16_t    root_entry_count;   /* 224 */
    uint16_t    total_sectors_16;   /* 2880 */
    uint8_t     media_type;         /* 0xF0 */
    uint16_t    sectors_per_fat;    /* 9 */
    uint16_t    sectors_per_track;  /* 18 */
    uint16_t    num_heads;          /* 2 */
    uint32_t    hidden_sectors;     /* 0 */
    uint32_t    total_sectors_32;   /* 0 */
    /* Extended BPB */
    uint8_t     drive_number;       /* 0x00 */
    uint8_t     reserved1;
    uint8_t     boot_signature;     /* 0x29 */
    uint32_t    volume_id;
    uint8_t     volume_label[11];   /* "EX-OS      " */
    uint8_t     fs_type[8];         /* "FAT12   " */
} BPB;

/* Entry nella root directory FAT12 (32 byte) */
typedef struct __attribute__((packed)) {
    uint8_t     name[8];            /* Nome file (padded con spazi) */
    uint8_t     ext[3];             /* Estensione (padded con spazi) */
    uint8_t     attributes;         /* Attributi file */
    uint8_t     reserved[10];       /* Riservato */
    uint16_t    time;               /* Ora modifica */
    uint16_t    date;               /* Data modifica */
    uint16_t    first_cluster;      /* Primo cluster */
    uint32_t    file_size;          /* Dimensione in byte */
} DirEntry;

/* Attributi file */
#define ATTR_READ_ONLY      0x01
#define ATTR_HIDDEN         0x02
#define ATTR_SYSTEM         0x04
#define ATTR_VOLUME_ID      0x08
#define ATTR_DIRECTORY      0x10
#define ATTR_ARCHIVE        0x20

/* Valori speciali cluster FAT12 */
#define FAT12_FREE          0x000   /* Cluster libero */
#define FAT12_RESERVED_MIN  0xFF0   /* Inizio range riservati */
#define FAT12_BAD           0xFF7   /* Cluster danneggiato */
#define FAT12_END_MIN       0xFF8   /* Fine catena (minimo) */
#define FAT12_END           0xFFF   /* Fine catena */

/* =============================================================================
 * Struttura mappa memoria E820
 * ============================================================================= */
typedef struct __attribute__((packed)) {
    uint32_t    base_low;       /* Indirizzo base (32 bit bassi) */
    uint32_t    base_high;      /* Indirizzo base (32 bit alti) */
    uint32_t    length_low;     /* Lunghezza in byte (32 bit bassi) */
    uint32_t    length_high;    /* Lunghezza in byte (32 bit alti) */
    uint32_t    type;           /* Tipo regione */
} E820Entry;

/* Tipi regione E820 */
#define E820_TYPE_USABLE        1   /* RAM utilizzabile */
#define E820_TYPE_RESERVED      2   /* Riservato (BIOS, hardware) */
#define E820_TYPE_ACPI_RECLAIM  3   /* ACPI reclaimable */
#define E820_TYPE_ACPI_NVS      4   /* ACPI non-volatile */
#define E820_TYPE_BAD           5   /* Bad memory */

/* Struttura passata al kernel con tutte le info di boot */
typedef struct __attribute__((packed)) {
    uint32_t    magic;              /* 0x4D594F53 = "EXOS" */
    uint8_t     boot_drive;         /* Numero drive di boot */
    uint32_t    mem_lower;          /* KB di RAM sotto 1MB */
    uint32_t    mem_upper;          /* KB di RAM sopra 1MB */
    uint32_t    e820_count;         /* Numero entry E820 */
    uint32_t    e820_addr;          /* Indirizzo fisico tabella E820 */
    uint32_t    kernel_size;        /* Dimensione kernel in byte */
} BootInfo;

#define BOOTINFO_MAGIC      0x4D594F53  /* "EXOS" in hex */

/* =============================================================================
 * Dichiarazioni funzioni — print.c
 * ============================================================================= */
void print_string(const char *s);
void print_char(char c);
void print_newline(void);
void print_hex8(uint8_t val);
void print_hex16(uint16_t val);
void print_hex32(uint32_t val);
void print_dec(uint32_t val);
void print_status(const char *msg, int ok);
void print_progress(const char *msg);
void print_error(const char *msg);    /* Non ritorna */
void print_banner(void);

/* =============================================================================
 * Dichiarazioni funzioni — fat12.c
 * ============================================================================= */
void    fat12_init(uint8_t drive);
int     fat12_find_file(const char *name83, DirEntry *out_entry);
int     fat12_load_file(uint16_t first_cluster, uint32_t file_size,
                        uint32_t dest_addr);
int     fat12_write_file(const char *name83, const void *data,
                         uint32_t size);
uint16_t fat12_get_next_cluster(uint16_t cluster);
int     fat12_read_sectors(uint16_t lba, uint8_t count, uint32_t dest);

/* =============================================================================
 * Dichiarazioni funzioni — loader.c
 * ============================================================================= */
void    loader_main(uint8_t drive);
int     detect_memory(E820Entry *map, uint32_t *count);
void    print_memory_map(E820Entry *map, uint32_t count);
void    jump_to_kernel(uint32_t kernel_addr, BootInfo *info);

/* =============================================================================
 * Dichiarazioni funzioni ASM — entry.asm
 * ============================================================================= */
extern uint16_t read_disk_sectors(uint16_t lba, uint8_t count,
                                   uint16_t segment, uint16_t offset);
extern uint16_t write_disk_sectors(uint16_t lba, uint8_t count,
                                    uint16_t segment, uint16_t offset);
extern uint32_t get_memory_size(void);
extern void     halt16(void);
extern void     print16(const char *s);

#endif /* STAGE2_H */
