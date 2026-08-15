/* =============================================================================
 * drivers/floppy/floppy.ld — EX-OS floppy driver linker script
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(drv_init)

SECTIONS
{
    . = 0x0;

    .hash    : { *(.hash)    }
    .dynsym  : { *(.dynsym)  }
    .dynstr  : { *(.dynstr)  }
    .dynamic : { *(.dynamic) }

    .text    : { *(.text) *(.text.*) }
    .rodata  : { *(.rodata) *(.rodata.*) }

    . = ALIGN(4096);
    .data    : { *(.data) *(.data.*) }
    .bss     : {
        _bss_start = .;
        *(.bss) *(.bss.*) *(COMMON)
        _bss_end = .;
    }

    .rel.text    : { *(.rel.text)    }
    .rel.data    : { *(.rel.data)    }
    .rel.rodata  : { *(.rel.rodata)  }
    .rel.bss     : { *(.rel.bss)     }
    .rel.dyn     : { *(.rel.dyn)     }
    .rel.plt     : { *(.rel.plt)     }

    .note.GNU-stack (INFO) : { }

    /DISCARD/ : { *(.comment) *(.note.gnu*) *(.eh_frame*) *(.gnu.version*) }
}
