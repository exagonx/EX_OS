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
/* ⚠️ -Ttext-segment E' PARTE DEL CONTRATTO, non una preferenza.  Senza,
   `ld` carica dove vuole lui — 0x08048000, il default storico di Linux —
   mentre gli script di link di /bin dicono 0x08000000.  Il caricatore del
   kernel accetta entrambi (verifica solo che il PT_LOAD stia dentro lo
   spazio utente), quindi il difetto non si vedeva: si vedeva soltanto che
   un binario prodotto dal cross e uno prodotto dal Makefile stavano a
   indirizzi diversi, il che rende inconfrontabili due disassemblati dello
   stesso programma.  Le specs devono dire cio' che fanno.  */
#undef  LINK_SPEC
#define LINK_SPEC "-m elf_i386 -static -e _start -Ttext-segment=0x08000000 %{shared:%eEX-OS non supporta le librerie condivise per i programmi}"

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
/* ⚠️ DUE COSE DIVERSE CHE SI CHIAMANO UGUALE.  DWARF2_UNWIND_INFO 0 (qui
   sotto) toglie l'unwind delle ECCEZIONI; le tabelle ASINCRONE — quelle
   che servono a un debugger per srotolare lo stack in un punto qualunque —
   GCC le emette lo stesso, ed erano 5,6 KB di .eh_frame in ogni binario.
   Su un floppy da 1.44 MB con venti programmi sono 110 KB di peso morto.
   Si tolgono da qui, non da li'.  */
#undef  CC1_SPEC
#define CC1_SPEC "%{!fpic:%{!fPIC:%{!fpie:%{!fPIE:-fno-pic}}}} %{!fasynchronous-unwind-tables:-fno-asynchronous-unwind-tables}"

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

/* =============================================================================
   I TIPI FONDAMENTALI — dirli qui, o li indovina qualcun altro

   ⚠️ Senza queste righe il bersaglio prendeva i valori predefiniti, che per
   un ELF a 32 bit generico sono `long unsigned int` per size_t e `long int`
   per int32_t.  Sono larghezze GIUSTE — 32 bit entrambe — ma tipi
   DIVERSI da quelli del gcc di sistema con -m32, che usa `unsigned int` e
   `int`.  La differenza non e' accademica:

     - un sorgente che includa <stddef.h> accanto a un header che dichiari
       size_t a mano non compila piu' («conflicting types for 'size_t'»);
     - `int32_t` diventa `long int`, e ogni printf("%d", un_int32) diventa
       un avviso, in un progetto che compila con -Wall -Wextra.

   i386-linux dichiara esattamente questi tre, ed e' il bersaglio con cui
   EX-OS condivide l'ABI: allinearsi a lui e' la scelta che non sorprende
   nessuno.  UINT32_TYPE segue INT32_TYPE, e INTPTR/UINTPTR seguono
   PTRDIFF/SIZE da newlib-stdint.h — quindi si sistemano da soli.

   Vanno DOPO newlib-stdint.h nella catena di tm_file, ed e' cosi': exos.h
   e' l'ultimo (vedi il caso i[34567]86-*-exos* in config.gcc).
   ============================================================================= */
#undef  SIZE_TYPE
#define SIZE_TYPE      "unsigned int"

#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE   "int"

#undef  INT32_TYPE
#define INT32_TYPE     "int"

#undef  UINT32_TYPE
#define UINT32_TYPE    "unsigned int"
