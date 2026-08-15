/* =============================================================================
 * lib/libc.ld — Linker script per libc.so (ELF32 shared library)
 * EX-OS — Extensible Operating System
 *
 * FIX BUG #3: file assente dal repository, causava il fallimento di
 * 'make all' (LD non trovava -T lib/libc.ld). Aggiunto il linker script
 * minimo per produrre una shared library ELF32 i386 position-independent.
 *
 * Note:
 *  - OUTPUT_FORMAT elf32-i386: obbligatorio per cross-toolchain i686-elf.
 *  - Il punto di caricamento è 0: le shared library ELF sono PIC,
 *    l'indirizzo reale è deciso dal dynamic linker (dynlink.c) a runtime.
 *  - PHDRS espliciti: garantisce che il linker emetta i Program Header
 *    corretti (PT_LOAD per .text e .data, PT_DYNAMIC per .dynamic) che
 *    dynlink.c si aspetta di trovare per mappare la libreria.
 * ============================================================================= */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)

PHDRS
{
    text    PT_LOAD FLAGS(5);       /* R+X — codice */
    data    PT_LOAD FLAGS(6);       /* R+W — dati */
    dynamic PT_DYNAMIC FLAGS(6);    /* dynamic linking info */
}

SECTIONS
{
    /* Sezione codice (read + execute) */
    .text 0 :
    {
        *(.text.unlikely)
        *(.text*)
        *(.gnu.linkonce.t*)
    } :text

    /* Costanti e dati read-only */
    .rodata :
    {
        *(.rodata*)
        *(.gnu.linkonce.r*)
    } :text

    /* Allinea il confine tra text e data a pagina */
    . = ALIGN(4096);

    /* Sezione dati inizializzati (read + write) */
    .data :
    {
        *(.data*)
        *(.gnu.linkonce.d*)
    } :data

    /* Tabelle di linking dinamico — devono stare nel segmento data */
    .dynamic :
    {
        *(.dynamic)
    } :data :dynamic

    .got :
    {
        *(.got.plt)
        *(.got)
    } :data

    /* Dati non inizializzati */
    .bss :
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

    /* Scarta sezioni non necessarie nel binario finale */
    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.rel.eh_frame*)
    }
}
