; =============================================================================
; kernel/arch/x86_64/isr64.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
; =============================================================================
;
; GLI INGRESSI DEGLI INTERRUPT A 64 BIT (@EXOS-64, tappa 3 — 10 ottobre 2026)
;
; Un ingresso per vettore, tutti uguali: mettono sulla pila cio' che manca
; perche' quel che c'e' sopra sia un InterruptFrame (kernel/include/idt.h) e
; chiamano isr64_gestisci(InterruptFrame *). Al ritorno rimettono i registri
; e riprendono con iretq.
;
; ! L'ORDINE DEI PUSH E' LA STRUTTURA, LETTA AL CONTRARIO. Il primo campo di
; InterruptFrame (r15) e' l'ultimo che si mette. Chi cambia una delle due
; cambia l'altra: un registro fuori posto qui e' un programma che riprende con
; i valori di un altro registro, e non lo dice nessuno.
;
; ! IL CODICE D'ERRORE C'E' SOLO PER ALCUNE ECCEZIONI (8, 10-14, 17, 21, 29,
; 30). Per le altre se ne mette uno finto a zero, cosi' la forma e' una sola.
;
; ! LA PILA E' ALLINEATA A 16 SENZA FARE NIENTE, e il conto va scritto perche'
; il C lo pretende: in long mode il processore allinea RSP a 16 PRIMA di
; mettere i suoi cinque valori (40 byte), poi qui se ne aggiungono 2 (errore e
; vettore) e 15 registri: 22 valori, 176 byte, multiplo di 16.
; =============================================================================

[BITS 64]
extern isr64_gestisci

%macro INGRESSO 1               ; senza codice d'errore
global isr64_%1
isr64_%1:
    push qword 0
    push qword %1
    jmp  comune
%endmacro

%macro INGRESSO_ERR 1           ; il processore ha gia' messo il codice
global isr64_%1
isr64_%1:
    push qword %1
    jmp  comune
%endmacro

section .text

%assign v 0
%rep 256
  %if v == 8 || (v >= 10 && v <= 14) || v == 17 || v == 21 || v == 29 || v == 30
    INGRESSO_ERR v
  %else
    INGRESSO v
  %endif
  %assign v v + 1
%endrep

comune:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld                         ; il C vuole DF a zero, chi e' stato interrotto no
    mov  rdi, rsp               ; il primo argomento: InterruptFrame *
    call isr64_gestisci
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rdi
    pop  rsi
    pop  rbp
    pop  rbx
    pop  rdx
    pop  rcx
    pop  rax
    add  rsp, 16                ; via il vettore e il codice d'errore
    iretq

; La tavola degli ingressi, per idt64.c: 256 indirizzi.
section .rodata
global isr64_tavola
isr64_tavola:
%assign v 0
%rep 256
    dq isr64_ %+ v
  %assign v v + 1
%endrep
