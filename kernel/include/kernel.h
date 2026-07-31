/* =============================================================================
 * kernel/include/kernel.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef KERNEL_H
#define KERNEL_H

/* =============================================================================
 * Tipi base (freestanding — no stdint.h)
 * ============================================================================= */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;
typedef uint32_t            size_t;
typedef int32_t             ssize_t;
typedef uint32_t            uintptr_t;
typedef int32_t             ptrdiff_t;

#define NULL                ((void*)0)
#define TRUE                1
#define FALSE               0

/* =============================================================================
 * Macro utili
 * ============================================================================= */
#define ALIGN_UP(val, align)    (((val) + ((align)-1)) & ~((align)-1))
#define ALIGN_DOWN(val, align)  ((val) & ~((align)-1))
#define ARRAY_SIZE(arr)         (sizeof(arr) / sizeof((arr)[0]))
#define OFFSETOF(type, member)  ((size_t)&((type*)0)->member)
#define MIN(a,b)    ((a) < (b) ? (a) : (b))
#define MAX(a,b)    ((a) > (b) ? (a) : (b))

/* Attributi GCC */
#define PACKED          __attribute__((packed))
#define NORETURN        __attribute__((noreturn))
#define UNUSED          __attribute__((unused))
#define ALWAYS_INLINE   __attribute__((always_inline))
#define NOINLINE        __attribute__((noinline))
#define ALIGNED(n)      __attribute__((aligned(n)))
#define SECTION(s)      __attribute__((section(s)))
#define BARRIER()       __asm__ volatile("" ::: "memory")

/* =============================================================================
 * Struttura BootInfo — passata da Stage 2 al kernel
 * DEVE essere identica a quella in stage2.h
 * ============================================================================= */
typedef struct PACKED {
    uint32_t    magic;          /* 0x4D594F53 = "EXOS" */
    uint8_t     boot_drive;     /* Numero drive di boot */
    uint32_t    mem_lower;      /* KB di RAM sotto 1MB */
    uint32_t    mem_upper;      /* KB di RAM sopra 1MB */
    uint32_t    e820_count;     /* Numero entry E820 */
    uint32_t    e820_addr;      /* Indirizzo fisico tabella E820 */
    uint32_t    kernel_size;    /* Dimensione kernel in byte */
} BootInfo;

#define BOOTINFO_MAGIC  0x4D594F53  /* "EXOS" */

/* Struttura entry E820 */
typedef struct PACKED {
    uint32_t    base_low;
    uint32_t    base_high;
    uint32_t    length_low;
    uint32_t    length_high;
    uint32_t    type;
} E820Entry;

#define E820_TYPE_USABLE        1
#define E820_TYPE_RESERVED      2
#define E820_TYPE_ACPI_RECLAIM  3
#define E820_TYPE_ACPI_NVS      4
#define E820_TYPE_BAD           5

/* =============================================================================
 * Layout memoria kernel (indirizzi fisici)
 * ============================================================================= */
#define KERNEL_PHYS_BASE    0x00100000  /* 1MB: inizio kernel */
#define KERNEL_HEAP_BASE    0x00400000  /* 4MB: inizio heap kernel */
#define KERNEL_HEAP_SIZE    0x00400000  /* 4MB: dimensione heap kernel */
#define USER_SPACE_BASE     0x00800000  /* 8MB: inizio spazio utente */
#define USER_SPACE_END      0xC0000000  /* 3GB: fine spazio utente */

/*
 * FIX BUG #3: BOOTINFO_PHYS_ADDR aggiornato da 0xB000 a 0xC000.
 * Stage 2 scrive BootInfo a BOOTINFO_ADDR = 0xC000 (stage2.h).
 * In precedenza era 0xB000 = DISK_BUFFER_ADDR: il buffer di lettura
 * settori sovrascriveva BootInfo durante il caricamento del kernel.
 * Il kernel leggeva quindi dati corrotti (l'ultimo cluster del kernel).
 */
#define BOOTINFO_PHYS_ADDR  0x0000C000

/* =============================================================================
 * Selettori GDT (devono corrispondere a gdt.c)
 * ============================================================================= */
#define GDT_NULL_SEL        0x00
#define GDT_KERNEL_CODE_SEL 0x08
#define GDT_KERNEL_DATA_SEL 0x10
#define GDT_USER_CODE_SEL   0x18
#define GDT_USER_DATA_SEL   0x20
#define GDT_TSS_SEL         0x28

/* =============================================================================
 * Livelli di log
 * ============================================================================= */
#define LOG_NONE    0
#define LOG_ERROR   1
#define LOG_WARN    2
#define LOG_INFO    3
#define LOG_DEBUG   4

/* =============================================================================
 * Dichiarazioni funzioni kernel core
 * ============================================================================= */

/* kernel_main.c */
void kernel_main(BootInfo *info);

/* arch/x86/vga.c */
void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_setcolor(uint8_t fg, uint8_t bg);
void vga_puts_at(const char *s, uint8_t row, uint8_t col);

/* arch/x86/gdt.c */
void gdt_install(void);

/* arch/x86/idt.c */
void idt_install(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags);

/* arch/x86/isr.c */
void isr_install(void);

/* Funzioni ASM in entry.asm */
void        gdt_flush(uint32_t gdtr_ptr);
void        idt_flush(uint32_t idtr_ptr);
void        tss_flush(uint16_t tss_selector);
uint32_t    read_cr0(void);
void        write_cr0(uint32_t val);
uint32_t    read_cr2(void);
uint32_t    read_cr3(void);
void        write_cr3(uint32_t val);
void        interrupts_enable(void);
void        interrupts_disable(void);
uint32_t    read_eflags(void);
uint8_t     port_inb(uint16_t port);
void        port_outb(uint16_t port, uint8_t val);
uint16_t    port_inw(uint16_t port);
void        port_outw(uint16_t port, uint16_t val);
uint32_t    port_inl(uint16_t port);
void        port_outl(uint16_t port, uint32_t val);
void        io_delay(void);
void        cpuid(uint32_t code, uint32_t *eax, uint32_t *ebx,
                  uint32_t *ecx, uint32_t *edx);

/* kprintf.c */
void kprintf(const char *fmt, ...);
void klog(int level, const char *fmt, ...);
void kpanic(const char *fmt, ...) NORETURN;
void klog_set_level(int level);

/* Registro dei problemi (ERROR/WARN). Popolato da klog() sempre, anche
 * quando il livello di log corrente li nasconde a video: serve al boot
 * silenzioso, che pulisce lo schermo e poi li ripropone. Vedi kprintf.c. */
uint32_t klog_problem_count(void);
void     klog_replay_problems(void);

#endif /* KERNEL_H */
