/* tools/prove64/primo.ld — dove sta il primo programma a 64 bit: all'inizio
 * dello spazio dei programmi, come quelli a 32 bit (lib/programma.ld). */
ENTRY(_start)
SECTIONS
{
    . = 0x08048000;
    .text   : { *(.text*) }
    . = ALIGN(4096);
    .rodata : { *(.rodata*) }
    . = ALIGN(4096);
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
