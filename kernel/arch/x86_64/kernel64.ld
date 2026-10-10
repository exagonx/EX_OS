/* =============================================================================
 * kernel/arch/x86_64/kernel64.ld — dove sta il kernel a 64 bit (@EXOS-64)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A 0x100000, come quello a 32 bit: e' li' che Stage 2 lo copia e salta. La
 * sezione .avvio viene PRIMA di tutto: il primo byte del file e' la prima
 * istruzione. __bss_inizio e __bss_fine servono a entry.asm, che la azzera.
 * ============================================================================= */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;
    _kernel_start = .;

    .text : {
        *(.avvio)
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }

    . = ALIGN(4096);
    __bss_inizio = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(4);
    __bss_fine = .;
    /* Fin dove arriva il kernel: da qui in su la RAM e' dell'allocatore
     * delle pagine (pmm.c), che ci mette per prima la sua mappa. */
    _kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
