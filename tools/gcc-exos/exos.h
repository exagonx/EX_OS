/* Definizioni per Intel 386 con EX-OS — Extensible Operating System.
   Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* =============================================================================
 * CHE COS'E' QUESTO FILE, e perche' non basta il bersaglio i386-elf.
 *
 * i386-elf produce ELF a 32 bit e va benissimo per il codice bare metal,
 * ma non sa NIENTE del sistema sotto: non conosce il file di avvio, non
 * sa quale linker script usare, non definisce un predefinito che il
 * codice possa controllare con #ifdef. Ogni programma di EX-OS finora ha
 * rimediato a mano — `gcc -m32 -ffreestanding` piu' uno script di link
 * scritto per ognuno. Funziona, e non e' un bersaglio: e' una consuetudine
 * che ogni nuovo programma deve ricordarsi.
 *
 * Con i386-exos quelle scelte stanno QUI, in un posto solo:
 *
 *   - __exos__ / __EXOS__ predefiniti, cosi' il codice portato da altri
 *     sistemi puo' riconoscere dove sta compilando;
 *   - il file di avvio (crt0.o) e la libc aggiunti da soli al link;
 *   - l'indirizzo di caricamento dei programmi utente (0x08000000) e
 *     l'ingresso _start dichiarati nelle specs.
 *
 * PERCHE' 0x08000000. E' l'indirizzo a cui il caricatore ELF del kernel
 * mappa i programmi utente, ed e' scritto in ogni script di link di /bin
 * (vedi bin/ls/ls.ld). I programmi non girano mai in concorrenza allo
 * stesso indirizzo: il kernel ne carica uno per processo, nello spazio di
 * indirizzamento di quel processo.
 *
 * NIENTE CODICE INDIPENDENTE DALLA POSIZIONE. Il caricatore di EX-OS non
 * fa rilocazione per i programmi normali (solo dynlink.c la fa, e solo
 * per i driver): un binario PIE non verrebbe rilocato e salterebbe nel
 * vuoto. Il bersaglio quindi disabilita PIE per costruzione, invece di
 * lasciarlo a un'opzione che qualcuno prima o poi dimentica.
 * ============================================================================= */

/* Predefiniti del sistema.  TARGET_OS_CPP_BUILTINS e' il gancio che ogni
   bersaglio usa per dire "qui sopra ci gira questo".  */
#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__exos__");		\
      builtin_define ("__EXOS__");		\
      builtin_define ("__ELF__");		\
      builtin_assert ("system=exos");		\
    }						\
  while (0)

/* EX-OS non ha ancora una libreria condivisa per i programmi normali: si
   compila e si linka tutto dentro un binario statico.  Dichiararlo qui
   evita che il driver provi a linkare dinamicamente e fallisca in un
   punto che non assomiglia alla causa.  */
#undef  LINK_SPEC
#define LINK_SPEC "-m elf_i386 -static -e _start %{shared:%eEX-OS non supporta le librerie condivise per i programmi}"

/* Il file di avvio: chiama main() con argc e argv e poi exit().  E' il
   lib/start.S di EX-OS, installato come crt0.o.  */
#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "crt0%O%s"

/* Niente file di chiusura: EX-OS non ha costruttori e distruttori globali
   da eseguire (nessun supporto per .init_array nel caricatore ELF).  */
#undef  ENDFILE_SPEC
#define ENDFILE_SPEC ""

/* La libc di EX-OS.  -lc di suo, piu' libgcc che ci mette il compilatore.  */
#undef  LIB_SPEC
#define LIB_SPEC "-lc"

/* I programmi utente sono statici e non rilocabili: vedi la nota in testa
   al file.  */
#undef  CC1_SPEC
#define CC1_SPEC "%{!fpic:%{!fPIC:%{!fpie:%{!fPIE:-fno-pic}}}}"

/* EX-OS non ha ancora un gestore di eccezioni ne' unwind delle chiamate:
   le tabelle .eh_frame sarebbero peso morto in ogni binario, su un floppy
   da 1.44 MB.  */
#undef  DWARF2_UNWIND_INFO
#define DWARF2_UNWIND_INFO 0

/* Lo stack cresce verso il basso e non c'e' esecuzione sullo stack: il
   caricatore mappa le pagine utente senza permesso di esecuzione oltre il
   segmento di codice.  */
#undef  TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END file_end_indicate_exec_stack
