/* =============================================================================
 * bin/sh/start.S
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * L'ingresso della shell: prende argc e argv dallo stack e li passa a
 * shell_main().
 *
 * ⚠️ PERCHE' UN FILE ASSEMBLY PER TRE ISTRUZIONI. Fino ad agosto 2026 la
 * shell aveva un `void _start(void)` in C e non vedeva i propri
 * argomenti — quindi `sh -c "comando"` non esisteva, e senza quello non
 * possono esistere system() e popen() nella libc.
 *
 * Leggerli dal C non si puo': GCC su x86-32 IGNORA
 * __attribute__((naked)) e genera comunque un prologo (push ebx;
 * sub esp,N) che sposta ESP prima che il nostro codice arrivi a
 * guardarlo. E' la stessa ragione, riga per riga, per cui esiste
 * lib/start.S — che qui non si puo' riusare perche' quello chiama
 * _libc_start, e la shell la libc non la collega: parla con il kernel
 * attraverso i propri involucri sh_*.
 *
 * Lo stack all'ingresso lo costruisce sys_spawn:
 *
 *     [esp+0]  = 0        finto indirizzo di ritorno (non si ritorna mai)
 *     [esp+4]  = argc
 *     [esp+8]  = argv
 *     [esp+12] = envp     (NULL se il padre non ha passato un ambiente)
 * ============================================================================= */

.section .text
.global _start
.type   _start, @function

_start:
    movl  8(%esp), %ecx      /* argv */
    movl  4(%esp), %eax      /* argc */
    pushl %ecx
    pushl %eax
    call  shell_main         /* non ritorna: chiama sh_exit() */
    hlt
.size _start, .-_start

/* Dice al linker che questo file non richiede uno stack eseguibile. */
.section .note.GNU-stack,"",@progbits
