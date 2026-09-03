/* =============================================================================
 * tools/iso/prova.s — il programma piu' piccolo che dimostra la catena
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Sta sul CD degli strumenti perche' e' la prova che `as` e `ld` non solo
 * PARTONO dentro EX-OS, ma producono un binario che poi GIRA:
 *
 *     /cdrom/bin/as -o /prova.o /cdrom/prova.s
 *     /cdrom/bin/ld -o /prova /prova.o
 *     /prova
 *
 * Il CD e' in sola lettura — ISO 9660 non ha una mappa dello spazio
 * libero — quindi l'uscita va scritta altrove: qui sulla radice del
 * floppy, che e' scrivibile.
 *
 * PERCHE' IN ASSEMBLY E NON IN C. Perche' il compilatore ancora non c'e':
 * `as` traduce, `ld` collega, e in mezzo manca il pezzo che trasforma un
 * sorgente C in assembly. Questo file e' scritto nella lingua che gli
 * strumenti presenti sanno gia' parlare.
 *
 * ! NON SI LINKA CON crt0.o NE' CON LA LIBC: non ci sono sul CD, e non
 * servono. L'ingresso e' `_start`, non `main`, e le due syscall si fanno
 * a mano con `int $0x80` — i numeri sono quelli di
 * kernel/include/syscall.h, gli stessi di Linux.
 * ============================================================================= */

    .text
    .globl  _start

_start:
    /* write(1, messaggio, lunghezza) */
    movl    $4, %eax            /* SYS_WRITE */
    movl    $1, %ebx            /* fd 1: standard output */
    movl    $messaggio, %ecx
    movl    $lunghezza, %edx
    int     $0x80

    /* exit(0). Non ritorna: senza, l'esecuzione proseguirebbe su cio' che
     * viene dopo in memoria, che non e' codice. */
    movl    $1, %eax            /* SYS_EXIT */
    xorl    %ebx, %ebx
    int     $0x80

    .section .rodata
messaggio:
    .ascii  "Assemblato e collegato dentro EX-OS.\n"
    .equ    lunghezza, . - messaggio
