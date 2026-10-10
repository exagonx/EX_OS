; =============================================================================
; kernel/arch/x86_64/entry.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
; =============================================================================
;
; L'INGRESSO DEL KERNEL A 64 BIT (@EXOS-64, tappa 1 — 10 ottobre 2026)
;
; ! IL CARICATORE NON CAMBIA. Stage 1 e Stage 2 sono gli stessi della versione
; a 32 bit, byte per byte: Stage 2 carica KERNEL.BIN a 0x100000, passa in modo
; protetto a 32 bit con i segmenti piatti e salta li'. E' un contratto che
; funziona su dischetto, CD, disco e chiavetta, e riscriverlo per i 64 bit
; vorrebbe dire due caricatori da tenere uguali.
;
; Quindi il kernel a 64 bit COMINCIA A 32: le prime istruzioni di questo file
; girano nel modo in cui Stage 2 lo lascia, e sono loro a portare il
; processore in «long mode». Come fa Linux, e per la stessa ragione.
;
; I PASSI, nell'ordine in cui il processore li pretende:
;   1. si controlla che il processore ABBIA i 64 bit (CPUID 0x80000001, bit
;      29 di EDX). Su un Pentium non c'e', e allora si dice sulla seriale e ci
;      si ferma: un salto in long mode su un processore che non lo ha e' un
;      riavvio senza un perche';
;   2. si azzera la .bss: Stage 2 copia i byte del file, e la .bss nel file
;      non c'e'. Dentro ci sono le tabelle delle pagine e la pila;
;   3. le tabelle delle pagine: il primo gigabyte mappato su se stesso, a
;      pagine da 2 MB. In long mode la paginazione e' obbligatoria;
;   4. CR4.PAE, poi EFER.LME, poi CR0.PG: da qui il processore e' in long
;      mode «di compatibilita'», ancora a 32 bit;
;   5. una GDT con un segmento di codice a 64 bit, e un salto lontano dentro
;      di lui: solo adesso le istruzioni sono a 64 bit.
;
; ! NIENTE PILA FINO AL PASSO 2: la pila sta nella .bss, che prima e' sporca.
; I registri con cui Stage 2 arriva si mettono da parte SUBITO, in .data:
; quando il kernel leggera' le informazioni di avvio le trovera' li'.
; =============================================================================

%define COM1        0x3F8
%define MSR_EFER    0xC0000080

extern kmain64
extern __bss_inizio
extern __bss_fine

section .avvio
[BITS 32]
global _start
_start:
    cli
    mov  [avvio_reg + 0],  eax
    mov  [avvio_reg + 4],  ebx
    mov  [avvio_reg + 8],  ecx
    mov  [avvio_reg + 12], edx
    mov  [avvio_reg + 16], esi
    mov  [avvio_reg + 20], edi

    ; --- 2. la .bss a zero (senza pila: solo registri) -----------------------
    mov  edi, __bss_inizio
    mov  ecx, __bss_fine
    sub  ecx, edi
    shr  ecx, 2
    xor  eax, eax
    cld
    rep  stosd
    mov  esp, pila_cima

    ; --- 1. il processore ha i 64 bit? ----------------------------------------
    mov  eax, 0x80000000
    cpuid
    cmp  eax, 0x80000001
    jb   .niente_64
    mov  eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz   .niente_64

    ; --- 3. le tabelle: PML4[0] -> PDPT[0] -> PD, 512 pagine da 2 MB ----------
    mov  eax, tab_pdpt
    or   eax, 3                     ; presente, scrivibile
    mov  [tab_pml4], eax
    mov  eax, tab_pd
    or   eax, 3
    mov  [tab_pdpt], eax
    xor  ecx, ecx
.pagine:
    mov  eax, ecx
    shl  eax, 21                    ; ecx * 2 MB
    or   eax, 0x83                  ; presente, scrivibile, pagina grande
    mov  [tab_pd + ecx * 8], eax
    inc  ecx
    cmp  ecx, 512
    jb   .pagine

    ; --- 4. PAE, LME, paginazione ---------------------------------------------
    mov  eax, cr4
    or   eax, 1 << 5                ; PAE
    mov  cr4, eax
    mov  eax, tab_pml4
    mov  cr3, eax
    mov  ecx, MSR_EFER
    rdmsr
    or   eax, 1 << 8                ; LME
    wrmsr
    mov  eax, cr0
    or   eax, 1 << 31               ; PG
    mov  cr0, eax

    ; --- 5. il segmento a 64 bit ----------------------------------------------
    lgdt [gdt64_punt]
    jmp  0x08:lungo

.niente_64:
    mov  esi, msg_niente
.scrivi:
    lodsb
    test al, al
    jz   .fermo
    mov  bl, al
    mov  edx, COM1 + 5
.aspetta:
    in   al, dx
    test al, 0x20
    jz   .aspetta
    mov  edx, COM1
    mov  al, bl
    out  dx, al
    jmp  .scrivi
.fermo:
    hlt
    jmp  .fermo

[BITS 64]
lungo:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  fs, ax
    mov  gs, ax
    mov  rsp, pila_cima
    xor  rbp, rbp
    call kmain64
.fine:
    cli
    hlt
    jmp  .fine

section .data
align 8
global avvio_reg
avvio_reg:  times 6 dd 0            ; eax ebx ecx edx esi edi, come li lascia Stage 2

align 16
gdt64:
    dq 0                            ; il descrittore nullo
    dq 0x00209A0000000000           ; 0x08: codice a 64 bit (L=1), ring 0
    dq 0x0000920000000000           ; 0x10: dati, ring 0
gdt64_punt:
    dw gdt64_punt - gdt64 - 1
    dq gdt64

msg_niente: db 13, 10, "EX-OS a 64 bit: questo processore non ha i 64 bit (niente long mode).", 13, 10
            db "Serve la versione a 32 bit.", 13, 10, 0

section .bss
align 4096
tab_pml4:   resb 4096
tab_pdpt:   resb 4096
tab_pd:     resb 4096
pila:       resb 16384
pila_cima:
