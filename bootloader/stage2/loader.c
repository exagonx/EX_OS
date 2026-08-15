/* =============================================================================
 * bootloader/stage2/loader.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 *
 * FIX BUG #5: jump_to_kernel() ora delega interamente a jump_to_kernel_asm()
 * definita in entry.asm. Il codice C precedente usava inline assembly con
 * .code32 dentro un binario flat 16-bit, producendo byte errati e una
 * transizione a Protected Mode non funzionante.
 * ============================================================================= */

#include "stage2.h"

/* =============================================================================
 * Dichiarazione della stub ASM per la transizione PM → kernel
 * Definita in entry.asm (sezione [BITS 16] + [BITS 32])
 * Firma: void jump_to_kernel_asm(uint32_t kernel_addr, uint32_t info_addr)
 * ============================================================================= */
extern void jump_to_kernel_asm(uint32_t kernel_addr, uint32_t info_addr);

/* =============================================================================
 * detect_memory — Costruisce mappa memoria E820 via BIOS INT 15h
 * ============================================================================= */
int detect_memory(E820Entry *map, uint32_t *count)
{
    uint32_t ebx   = 0;
    uint32_t n     = 0;
    uint32_t magic = 0x534D4150; /* "SMAP" */

    print_progress("Rilevamento mappa memoria (E820)...");

    do {
        E820Entry *entry = &map[n];
        uint32_t  eax_out, ebx_out;
        uint16_t  cf;

        __asm__ volatile (
            "int $0x15\n"
            "jc  1f\n"
            "xor %%cx, %%cx\n"
            "jmp 2f\n"
            "1: mov $1, %%cx\n"
            "2:\n"
            : "=a"(eax_out),
              "=b"(ebx_out),
              "=c"(cf)
            : "a"(0xE820),
              "b"(ebx),
              "c"(24),
              "d"(magic),
              "D"((uint32_t)entry)
            : "memory"
        );

        if (cf || eax_out != magic) {
            if (n == 0) goto fallback;
            break;
        }

        n++;
        ebx = ebx_out;
        if (ebx == 0) break;

    } while (n < 32);

    *count = n;
    return (n > 0) ? 0 : -1;

fallback:
    print_string("  E820 non disponibile, uso fallback INT 15/88\r\n");

    uint32_t ext_kb = 0;
    __asm__ volatile (
        "mov $0x88, %%ah\n"
        "int $0x15\n"
        "jc  1f\n"
        "movzwl %%ax, %0\n"
        "jmp 2f\n"
        "1: movl $3072, %0\n"
        "2:\n"
        : "=r"(ext_kb)
        :
        : "eax"
    );

    map[0].base_low    = 0;
    map[0].base_high   = 0;
    map[0].length_low  = 640 * 1024;
    map[0].length_high = 0;
    map[0].type        = E820_TYPE_USABLE;

    map[1].base_low    = 0x100000;
    map[1].base_high   = 0;
    map[1].length_low  = ext_kb * 1024;
    map[1].length_high = 0;
    map[1].type        = E820_TYPE_USABLE;

    *count = 2;
    return 0;
}

/* =============================================================================
 * print_memory_map
 * ============================================================================= */
void print_memory_map(E820Entry *map, uint32_t count)
{
    uint32_t i;
    uint32_t total_kb = 0;

    print_string("  Mappa memoria:\r\n");
    print_string("  Base             Dimensione       Tipo\r\n");
    print_string("  ------------------------------------------------\r\n");

    for (i = 0; i < count; i++) {
        print_string("  ");
        print_hex32(map[i].base_low);
        print_string("  ");
        print_hex32(map[i].length_low);
        print_string("  ");

        switch (map[i].type) {
            case E820_TYPE_USABLE:       print_string("Usabile");          break;
            case E820_TYPE_RESERVED:     print_string("Riservato");        break;
            case E820_TYPE_ACPI_RECLAIM: print_string("ACPI Reclaimable"); break;
            case E820_TYPE_ACPI_NVS:     print_string("ACPI NVS");         break;
            case E820_TYPE_BAD:          print_string("Danneggiato");      break;
            default:                     print_string("Sconosciuto");      break;
        }
        print_newline();

        if (map[i].type == E820_TYPE_USABLE)
            total_kb += map[i].length_low / 1024;
    }

    print_string("  ------------------------------------------------\r\n");
    print_string("  RAM totale utilizzabile: ");
    print_dec(total_kb);
    print_string(" KB (");
    print_dec(total_kb / 1024);
    print_string(" MB)\r\n");
}

/* =============================================================================
 * enable_a20 — Abilita linea A20
 * ============================================================================= */
static void enable_a20(void)
{
    uint8_t val;

    print_progress("Abilitazione linea A20...");

    /* Metodo 1: Fast A20 via porta 0x92 */
    __asm__ volatile (
        "inb  $0x92, %0\n"
        "orb  $0x02, %0\n"
        "andb $0xFE, %0\n"
        "outb %0, $0x92\n"
        : "=a"(val)
        :
    );

    /* Delay */
    __asm__ volatile (
        "mov $1000, %%cx\n"
        "1: loop 1b\n"
        ::: "cx"
    );

    /* Metodo 2: Via KBC (8042) come rinforzo */
    __asm__ volatile (
        "1: inb $0x64, %%al\n"
        "testb $0x02, %%al\n"
        "jnz 1b\n"
        ::: "al"
    );
    __asm__ volatile (
        "movb $0xD1, %%al\n"
        "outb %%al, $0x64\n"
        ::: "al"
    );
    __asm__ volatile (
        "1: inb $0x64, %%al\n"
        "testb $0x02, %%al\n"
        "jnz 1b\n"
        ::: "al"
    );
    __asm__ volatile (
        "movb $0xDF, %%al\n"
        "outb %%al, $0x60\n"
        ::: "al"
    );

    __asm__ volatile (
        "mov $10000, %%cx\n"
        "1: loop 1b\n"
        ::: "cx"
    );

    print_status("Linea A20 abilitata", TRUE);
}

/* =============================================================================
 * jump_to_kernel — Prepara BootInfo e delega il salto a jump_to_kernel_asm()
 *
 * FIX BUG #5: Questa funzione C non gestisce più la transizione PM in linea.
 * Ora prepara i dati e chiama la stub in assembly puro (entry.asm) che
 * gestisce correttamente il passaggio 16-bit → 32-bit Protected Mode.
 *
 * Questa funzione NON ritorna.
 * ============================================================================= */
void jump_to_kernel(uint32_t kernel_addr, BootInfo *info)
{
    print_progress("Trasferimento controllo al kernel...");
    print_string("  Kernel addr: ");
    print_hex32(kernel_addr);
    print_newline();
    print_string("  BootInfo   : ");
    print_hex32((uint32_t)((unsigned long)info));
    print_newline();

    /*
     * Delega al stub ASM in entry.asm che:
     *  1. Esegue lgdt con la GDT temporanea
     *  2. Imposta CR0.PE=1
     *  3. Far jump a 32-bit (0x08:pm_entry32)
     *  4. Carica i registri segmento in 32-bit
     *  5. Imposta ESP e pusha BootInfo*
     *  6. Salta a kernel_addr (0x100000)
     */
    jump_to_kernel_asm(kernel_addr, (uint32_t)((unsigned long)info));

    /* Non raggiunto — jump_to_kernel_asm non ritorna */
    __builtin_unreachable();
}

/* =============================================================================
 * loader_main — Funzione principale Stage 2
 * ============================================================================= */
void loader_main(uint8_t drive)
{
    DirEntry    kernel_entry;
    E820Entry   memory_map[32];
    uint32_t    mem_count = 0;
    BootInfo   *bootinfo;
    int         found;
    int         ret;

    /* 1. Banner */
    print_banner();

    print_progress("Stage 2 avviato");
    print_string("  Drive di boot: ");
    print_hex8(drive);
    print_newline();

    /* 2. Inizializza FAT12 */
    fat12_init(drive);

    /* 3. Rileva mappa memoria */
    print_progress("Rilevamento RAM...");
    ret = detect_memory(memory_map, &mem_count);
    if (ret != 0)
        print_error("Impossibile rilevare la mappa di memoria!");
    print_memory_map(memory_map, mem_count);

    /* 4. Cerca KERNEL.BIN nella root directory */
    print_progress("Ricerca KERNEL.BIN...");
    found = fat12_find_file("KERNEL  BIN", &kernel_entry);
    if (found < 0)
        print_error("KERNEL.BIN non trovato nel floppy!");

    print_status("KERNEL.BIN trovato", TRUE);
    print_string("  Cluster iniziale: ");
    print_hex16(kernel_entry.first_cluster);
    print_string("  Dimensione: ");
    print_dec(kernel_entry.file_size);
    print_string(" byte\r\n");

    if (kernel_entry.file_size == 0 || kernel_entry.first_cluster < 2)
        print_error("KERNEL.BIN e' vuoto o corrotto!");

    /* 5. Carica KERNEL.BIN in memoria a 0x100000 (1MB)
     * FIX BUG #4: fat12_read_sectors usa rep movsd con FS: prefix (unreal mode)
     * abilitato in enable_unreal_mode() chiamata da _start prima di loader_main. */
    print_progress("Caricamento kernel in memoria a 0x100000...");
    ret = fat12_load_file(
        kernel_entry.first_cluster,
        kernel_entry.file_size,
        KERNEL_LOAD_ADDR
    );
    if (ret != 0)
        print_error("Errore nel caricamento del kernel!");
    print_status("Kernel caricato a 0x100000", TRUE);

    /* 6. Costruisci BootInfo per il kernel
     * FIX BUG #3: BOOTINFO_ADDR ora è 0xC000 (non 0xB000 = DISK_BUFFER_ADDR) */
    print_progress("Preparazione BootInfo...");
    bootinfo = (BootInfo *)BOOTINFO_ADDR;

    bootinfo->magic       = BOOTINFO_MAGIC;
    bootinfo->boot_drive  = drive;
    bootinfo->e820_count  = mem_count;
    bootinfo->e820_addr   = (uint32_t)memory_map;
    bootinfo->kernel_size = kernel_entry.file_size;

    {
        uint32_t i;
        bootinfo->mem_lower = 640;
        bootinfo->mem_upper = 0;
        for (i = 0; i < mem_count; i++) {
            if (memory_map[i].type == E820_TYPE_USABLE) {
                if (memory_map[i].base_low >= 0x100000)
                    bootinfo->mem_upper += memory_map[i].length_low / 1024;
            }
        }
    }

    print_status("BootInfo costruito", TRUE);
    print_string("  Magic   : ");
    print_hex32(bootinfo->magic);
    print_newline();
    print_string("  RAM bassa: ");
    print_dec(bootinfo->mem_lower);
    print_string(" KB\r\n");
    print_string("  RAM alta : ");
    print_dec(bootinfo->mem_upper);
    print_string(" KB\r\n");

    /* 7. Abilita A20 */
    enable_a20();

    /* 8. Salta al kernel (la GDT è caricata dentro jump_to_kernel_asm) */
    print_status("Trasferimento controllo al kernel", TRUE);
    print_string("\r\n");

    jump_to_kernel(KERNEL_LOAD_ADDR, bootinfo);

    /* Non raggiunto */
    print_error("jump_to_kernel() e' tornato — impossibile!");
}
