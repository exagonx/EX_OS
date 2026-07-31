/* =============================================================================
 * bootloader/stage2/fat12.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 *
 * FIX BUG #4: fat12_read_sectors() ora usa il prefisso FS: (segmento con
 * limite 4GB, impostato da enable_unreal_mode() in entry.asm) per la copia
 * a indirizzi > 1MB. Il vecchio codice usava "rep movsd" senza prefisso di
 * segmento, che in Real Mode accede solo ai 16 bit bassi dell'indirizzo e
 * quindi scriveva a 0x0000 invece che a 0x100000.
 * ============================================================================= */

#include "stage2.h"

/* =============================================================================
 * Costanti geometria floppy 1.44MB
 * ============================================================================= */
#define BYTES_PER_SECTOR        512
#define SECTORS_PER_CLUSTER     1
#define RESERVED_SECTORS        1
#define NUM_FATS                2
#define ROOT_ENTRY_COUNT        224
#define TOTAL_SECTORS           2880
#define SECTORS_PER_FAT         9
#define SECTORS_PER_TRACK       18
#define NUM_HEADS               2

#define FAT1_LBA                1
#define FAT2_LBA                (FAT1_LBA + SECTORS_PER_FAT)
#define ROOT_DIR_LBA            (FAT2_LBA + SECTORS_PER_FAT)
#define ROOT_DIR_SECTORS        ((ROOT_ENTRY_COUNT * 32) / BYTES_PER_SECTOR)
#define DATA_START_LBA          (ROOT_DIR_LBA + ROOT_DIR_SECTORS)

/* =============================================================================
 * Stato interno
 * ============================================================================= */
static uint8_t  g_drive      = 0;
static uint8_t  g_fat_loaded = 0;

/* FAT_BUFFER_ADDR ora è 0xA000 (aggiornato in stage2.h, FIX BUG #2) */
#define fat_buffer       ((uint8_t *)(FAT_BUFFER_ADDR))
#define root_dir_buffer  ((DirEntry *)(ROOT_DIR_ADDR))

/* =============================================================================
 * Helper: conversione indirizzo → segmento:offset per INT 13h
 * Funziona per indirizzi < ~64KB (Real Mode standard).
 * Per indirizzi >= 64KB usiamo il buffer intermedio + unreal mode.
 * ============================================================================= */
static inline uint16_t addr_to_segment(uint32_t addr) {
    return (uint16_t)(addr >> 4);
}
static inline uint16_t addr_to_offset(uint32_t addr) {
    return (uint16_t)(addr & 0x000F);
}

/* =============================================================================
 * fat12_init — Inizializza il modulo FAT12
 * Carica FAT1 a FAT_BUFFER_ADDR (0xA000 — FIX BUG #2)
 * ============================================================================= */
void fat12_init(uint8_t drive)
{
    uint16_t ret;

    g_drive = drive;

    print_progress("Caricamento FAT12...");

    ret = read_disk_sectors(
        FAT1_LBA,
        SECTORS_PER_FAT,
        addr_to_segment(FAT_BUFFER_ADDR),
        addr_to_offset(FAT_BUFFER_ADDR)
    );

    if (ret != 0)
        print_error("Impossibile leggere la FAT dal disco!");

    g_fat_loaded = 1;
    print_status("FAT12 caricata", TRUE);
}

/* =============================================================================
 * fat12_get_next_cluster — Legge la prossima entry FAT12 (12 bit)
 * ============================================================================= */
uint16_t fat12_get_next_cluster(uint16_t cluster)
{
    uint32_t byte_offset;
    uint16_t value;

    if (!g_fat_loaded)
        print_error("FAT non caricata! Chiama fat12_init() prima.");

    byte_offset = cluster + (cluster / 2);

    value = (uint16_t)fat_buffer[byte_offset] |
            ((uint16_t)fat_buffer[byte_offset + 1] << 8);

    if (cluster & 1)
        value >>= 4;
    else
        value &= 0x0FFF;

    if (value >= FAT12_END_MIN)
        return FAT12_END;

    return value;
}

/* =============================================================================
 * fat12_set_cluster — Scrive un'entry nella FAT
 * ============================================================================= */
static void fat12_set_cluster(uint16_t cluster, uint16_t value)
{
    uint32_t byte_offset;
    uint16_t existing;

    byte_offset = cluster + (cluster / 2);

    existing = (uint16_t)fat_buffer[byte_offset] |
               ((uint16_t)fat_buffer[byte_offset + 1] << 8);

    if (cluster & 1)
        existing = (existing & 0x000F) | ((value & 0x0FFF) << 4);
    else
        existing = (existing & 0xF000) | (value & 0x0FFF);

    fat_buffer[byte_offset]     = (uint8_t)(existing & 0xFF);
    fat_buffer[byte_offset + 1] = (uint8_t)((existing >> 8) & 0xFF);
}

/* =============================================================================
 * fat12_write_fat — Riscrive FAT1 e FAT2 sul disco
 * ============================================================================= */
static int fat12_write_fat(void)
{
    uint16_t ret;

    ret = write_disk_sectors(FAT1_LBA, SECTORS_PER_FAT,
        addr_to_segment(FAT_BUFFER_ADDR), addr_to_offset(FAT_BUFFER_ADDR));
    if (ret != 0) return -1;

    ret = write_disk_sectors(FAT2_LBA, SECTORS_PER_FAT,
        addr_to_segment(FAT_BUFFER_ADDR), addr_to_offset(FAT_BUFFER_ADDR));
    if (ret != 0) return -1;

    return 0;
}

/* =============================================================================
 * fat12_find_file — Cerca un file nella root directory
 * ============================================================================= */
int fat12_find_file(const char *name83, DirEntry *out_entry)
{
    uint16_t i;
    const DirEntry *entry;

    for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
        entry = &root_dir_buffer[i];

        if (entry->name[0] == 0x00) break;
        if ((uint8_t)entry->name[0] == 0xE5) continue;
        if (entry->attributes & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) continue;

        uint8_t match = 1;
        uint8_t j;
        for (j = 0; j < 11; j++) {
            if (entry->name[j] != (uint8_t)name83[j]) {
                match = 0;
                break;
            }
        }

        if (match) {
            if (out_entry) {
                uint8_t k;
                uint8_t       *dst = (uint8_t *)out_entry;
                const uint8_t *src = (const uint8_t *)entry;
                for (k = 0; k < sizeof(DirEntry); k++)
                    dst[k] = src[k];
            }
            return (int)i;
        }
    }

    return -1;
}

/* =============================================================================
 * fat12_read_sectors — Legge settori in un indirizzo fisico a 32 bit
 *
 * FIX BUG #4: Per destinazioni >= 64KB usa il prefisso FS: (segmento con
 * limite 4GB, impostato da enable_unreal_mode() chiamata da _start).
 * Il vecchio codice usava "rep movsd" senza prefisso, che in Real Mode
 * tronca EDI a 16 bit e scrive a 0x0000 invece di 0x100000.
 *
 * Con Unreal Mode attivo:
 *   - DS/ES rimangono con base 0 e limite 64KB (Real Mode normale)
 *   - FS/GS hanno base 0 e limite 4GB (da enable_unreal_mode)
 *   - "fs rep movsd" accede all'indirizzo fisico completo a 32 bit
 *
 * Ritorna: 0 = successo, -1 = errore
 * ============================================================================= */
int fat12_read_sectors(uint16_t lba, uint8_t count, uint32_t dest)
{
    uint16_t ret;

    if (dest < 0x10000UL) {
        /* Destinazione bassa (< 64KB): scrittura diretta via segmento normale */
        ret = read_disk_sectors(
            lba, count,
            addr_to_segment(dest),
            addr_to_offset(dest)
        );
        return (ret == 0) ? 0 : -1;
    }

    /*
     * Destinazione alta (>= 64KB): leggi in buffer basso (DISK_BUFFER_ADDR)
     * poi copia a destinazione alta tramite Unreal Mode (prefisso FS:).
     *
     * DISK_BUFFER_ADDR = 0xB000 (sotto 64KB, accessibile in Real Mode normale).
     * Il ciclo copia un settore (512 byte = 128 dword) per volta per evitare
     * problemi di buffer overflow.
     */
    uint8_t  s;
    uint32_t current_dest = dest;

    for (s = 0; s < count; s++) {
        /* Leggi 1 settore nel buffer basso */
        ret = read_disk_sectors(
            (uint16_t)(lba + s), 1,
            addr_to_segment(DISK_BUFFER_ADDR),
            addr_to_offset(DISK_BUFFER_ADDR)
        );
        if (ret != 0) return -1;

        /* Copia 512 byte (128 dword) da DISK_BUFFER_ADDR a current_dest
         * usando il prefisso FS: per accedere all'indirizzo fisico completo.
         *
         * Nota: FS ha limite 4GB grazie a enable_unreal_mode() in entry.asm.
         * Il prefisso FS: permette a movsd di usare EDI come indirizzo fisico
         * a 32 bit anche in Real Mode, aggirando il limite del segmento ES.
         */
        uint32_t src_addr = DISK_BUFFER_ADDR;
        uint32_t dst_addr = current_dest;
        uint32_t dwords   = BYTES_PER_SECTOR / 4;  /* 128 */

        /*
         * Copia dal buffer basso (DISK_BUFFER_ADDR < 64KB) alla destinazione
         * alta (>= 1MB) usando il prefisso FS: (Unreal Mode, limite 4GB).
         * Usiamo constraints GCC invece di pushad/popad (non disponibili
         * in gas x86-64 anche con -m32).
         */
        __asm__ volatile (
            "1:\n"
            "  movl (%%esi), %%eax\n"
            "  fs movl %%eax, (%%edi)\n"
            "  addl $4, %%esi\n"
            "  addl $4, %%edi\n"
            "  loop 1b\n"
            : "+S"(src_addr), "+D"(dst_addr), "+c"(dwords)
            :
            : "eax", "memory"
        );

        current_dest += BYTES_PER_SECTOR;
    }

    return 0;
}

/* =============================================================================
 * fat12_load_file — Carica un file completo seguendo la catena FAT
 * ============================================================================= */
int fat12_load_file(uint16_t first_cluster, uint32_t file_size,
                    uint32_t dest_addr)
{
    uint16_t cluster         = first_cluster;
    uint32_t loaded          = 0;
    uint32_t dest            = dest_addr;
    uint32_t bytes_remaining = file_size;

    while (cluster != FAT12_END && cluster < FAT12_RESERVED_MIN) {
        uint16_t lba;
        uint32_t bytes_this_cluster;

        lba = DATA_START_LBA + (cluster - 2) * SECTORS_PER_CLUSTER;

        bytes_this_cluster = (uint32_t)SECTORS_PER_CLUSTER * BYTES_PER_SECTOR;
        if (bytes_this_cluster > bytes_remaining)
            bytes_this_cluster = bytes_remaining;

        if (fat12_read_sectors(lba, SECTORS_PER_CLUSTER, dest) != 0) {
            print_string("ERRORE: lettura cluster ");
            print_hex16(cluster);
            print_newline();
            return -1;
        }

        dest            += bytes_this_cluster;
        loaded          += bytes_this_cluster;
        bytes_remaining -= bytes_this_cluster;

        if (bytes_remaining == 0) break;

        cluster = fat12_get_next_cluster(cluster);

        if (cluster == 0 || cluster == FAT12_BAD) {
            print_error("Catena FAT corrotta!");
            return -1;
        }
    }

    return 0;
}

/* =============================================================================
 * fat12_find_free_cluster
 * ============================================================================= */
static uint16_t fat12_find_free_cluster(void)
{
    uint16_t cluster;
    for (cluster = 2; cluster < 2848; cluster++) {
        if (fat12_get_next_cluster(cluster) == FAT12_FREE)
            return cluster;
    }
    return 0;
}

/* =============================================================================
 * fat12_write_file — Scrive un file nella root directory
 * ============================================================================= */
int fat12_write_file(const char *name83, const void *data, uint32_t size)
{
    DirEntry       existing;
    int            entry_idx;
    uint16_t       first_cluster = 0;
    uint16_t       prev_cluster  = 0;
    uint32_t       bytes_written = 0;
    const uint8_t *src           = (const uint8_t *)data;
    uint16_t       i;

    entry_idx = fat12_find_file(name83, &existing);

    if (entry_idx >= 0) {
        uint16_t cluster = existing.first_cluster;
        while (cluster != FAT12_END && cluster >= 2 && cluster < FAT12_RESERVED_MIN) {
            uint16_t next = fat12_get_next_cluster(cluster);
            fat12_set_cluster(cluster, FAT12_FREE);
            cluster = next;
        }
        first_cluster = 0;
    }

    uint32_t bytes_remaining = size;
    uint8_t  sector_buf[512];

    while (bytes_remaining > 0) {
        uint16_t new_cluster;
        uint16_t lba;
        uint32_t chunk;
        uint32_t i32;

        new_cluster = fat12_find_free_cluster();
        if (new_cluster == 0) {
            print_error("Disco pieno!");
            return -1;
        }

        if (prev_cluster != 0)
            fat12_set_cluster(prev_cluster, new_cluster);
        else
            first_cluster = new_cluster;

        fat12_set_cluster(new_cluster, FAT12_END);

        lba = DATA_START_LBA + (new_cluster - 2) * SECTORS_PER_CLUSTER;

        for (i32 = 0; i32 < 512; i32++) sector_buf[i32] = 0;

        chunk = bytes_remaining;
        if (chunk > BYTES_PER_SECTOR) chunk = BYTES_PER_SECTOR;

        for (i32 = 0; i32 < chunk; i32++)
            sector_buf[i32] = src[bytes_written + i32];

        if (write_disk_sectors(lba, 1,
            addr_to_segment((uint32_t)sector_buf),
            addr_to_offset((uint32_t)sector_buf)) != 0) {
            print_error("Errore scrittura dati su disco!");
            return -1;
        }

        bytes_written   += chunk;
        bytes_remaining -= chunk;
        prev_cluster     = new_cluster;
    }

    if (fat12_write_fat() != 0) {
        print_error("Errore aggiornamento FAT!");
        return -1;
    }

    {
        DirEntry *dir    = root_dir_buffer;
        DirEntry *target = NULL;

        if (entry_idx >= 0) {
            target = &dir[entry_idx];
        } else {
            for (i = 0; i < ROOT_ENTRY_COUNT; i++) {
                if (dir[i].name[0] == 0x00 || (uint8_t)dir[i].name[0] == 0xE5) {
                    target = &dir[i];
                    break;
                }
            }
        }

        if (target == NULL) {
            print_error("Root directory piena!");
            return -1;
        }

        uint8_t j;
        for (j = 0; j < 8; j++) target->name[j] = (uint8_t)name83[j];
        for (j = 0; j < 3; j++) target->ext[j]  = (uint8_t)name83[8 + j];
        target->attributes    = ATTR_ARCHIVE;
        target->first_cluster = first_cluster;
        target->file_size     = size;
        target->time          = 0;
        target->date          = 0;
        for (j = 0; j < 10; j++) target->reserved[j] = 0;

        if (write_disk_sectors(ROOT_DIR_LBA, ROOT_DIR_SECTORS,
            addr_to_segment(ROOT_DIR_ADDR),
            addr_to_offset(ROOT_DIR_ADDR)) != 0) {
            print_error("Errore scrittura root directory!");
            return -1;
        }
    }

    return 0;
}
