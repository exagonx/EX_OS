; =============================================================================
; tools/iso/prova-nasm.asm — la stessa prova di prova.s, nell'altra lingua
; EX-OS — Extensible Operating System
;
; Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; =============================================================================
;
; Sta sul CD degli strumenti perche' e' la prova che `nasm` non solo PARTE
; dentro EX-OS, ma produce un oggetto che `ld` collega e che poi GIRA:
;
;     nasm -f elf32 /cdrom/prova-nasm.asm -o /prova-nasm.o
;     ld -o /prova-nasm /prova-nasm.o
;     /prova-nasm
;
; Il CD e' in sola lettura — ISO 9660 non ha una mappa dello spazio libero —
; quindi l'uscita va scritta altrove: qui sulla radice del floppy, che e'
; scrivibile.
;
; ! LA STESSA COSA DI prova.s, NELL'ALTRA SINTASSI, ed e' il motivo per cui
; questo file esiste accanto a quello invece di sostituirlo. `as` parla AT&T
; ed e' fatto per ricevere quel che sputa il compilatore; NASM parla INTEL,
; che e' la lingua dei manuali, dei settori di avvio e di quasi tutto
; l'assembly scritto da una persona. Chi confronta i due file vede la stessa
; identica cosa detta in due modi, che e' il modo piu' corto di imparare la
; differenza.
;
;     as                            nasm
;     movl $4, %eax                 mov eax, 4
;     movl $messaggio, %ecx         mov ecx, messaggio
;     .ascii "..."                  db  "..."
;
; ! NON SI COLLEGA CON crt0.o NE' CON LA LIBC: non servono. L'ingresso e'
; `_start`, non `main`, e le due chiamate di sistema si fanno a mano con
; `int 0x80` — i numeri sono quelli di kernel/include/syscall.h, gli stessi
; di Linux.
;
; ! E NON SERVE UN `org`: e' `ld` a decidere dove va il programma, e per
; EX-OS lo decide a 0x08000000 (vedi lib/programma.ld). Un `org` qui dentro
; sarebbe la cosa da scrivere in un SETTORE DI AVVIO — `org 0x7c00` — dove
; nessun linker viene a mettere le mani.
; =============================================================================

            bits 32

            section .text
            global  _start

_start:
            ; write(1, messaggio, lunghezza)
            mov     eax, 4              ; SYS_WRITE
            mov     ebx, 1              ; fd 1: standard output
            mov     ecx, messaggio
            mov     edx, lunghezza
            int     0x80

            ; exit(0). Non ritorna: senza, l'esecuzione proseguirebbe su cio'
            ; che viene dopo in memoria, che non e' codice.
            mov     eax, 1              ; SYS_EXIT
            xor     ebx, ebx
            int     0x80

            section .rodata
messaggio:  db      "Assemblato con NASM dentro EX-OS.", 10
lunghezza:  equ     $ - messaggio
