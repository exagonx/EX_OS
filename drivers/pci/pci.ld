/* =============================================================================
 * drivers/pci/pci.ld — EX-OS PCI server linker script
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * ET_EXEC statico, stesso schema di drivers/kbd/kbd.ld e di bin/ls/ls.ld.
 *
 * L'indirizzo base coincide con quello di kbd.drv e dei programmi di
 * /bin, e non è un conflitto: ogni processo ha la propria page directory,
 * quindi due processi allo stesso indirizzo virtuale non si vedono e non
 * si toccano. Sarebbe un problema solo se girassero nello stesso spazio
 * di indirizzi — cioè se fossero moduli dentro il kernel, che è
 * esattamente ciò da cui questi driver sono usciti.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x08000000;

    .text : {
        *(.text._start)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.gnu*)
        *(.rel*)
        *(.rela*)
    }
}
