; =============================================================================
; kernel/arch/x86/smp_tramp.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
; =============================================================================
;
; Il trampolino dei processori in piu' (7 ottobre 2026, tappa 1 dell'SMP)
;
; Un processore che non e' quello d'avvio si sveglia come un 8086: modo reale,
; CS:IP = (vettore << 8):0, dove il vettore e' quello scritto nel messaggio di
; avvio (SIPI). Non sa niente del kernel. Questo pezzo di codice lo porta dal
; modo reale al modo protetto con la STESSA GDT, le STESSE tabelle delle pagine
; e gli stessi CR0/CR4 del processore d'avvio, gli da' una pila e salta in C
; (smp_ap_entra, in smp.c).
;
; ! NON GIRA DOVE E' COLLEGATO. smp.c lo COPIA all'indirizzo fisico
; SMP_TRAMP_BASE, sotto il megabyte, perche' e' li' che un processore in modo
; reale puo' cominciare. Per questo:
;   - in modo reale ogni indirizzo e' uno scarto dall'inizio (la macro R), e
;     si legge con DS = CS;
;   - in modo protetto ogni indirizzo e' SMP_TRAMP_BASE + scarto, scritto per
;     esteso: un'etichetta nuda varrebbe l'indirizzo dentro il kernel, cioe'
;     la copia che nessuno ha riempito.
;
; ! SMP_TRAMP_BASE DEVE RESTARE UGUALE A QUELLO DI smp.c, e deve essere un
; multiplo di 4096 sotto 0x100000: il vettore del SIPI e' l'indirizzo diviso
; 4096 e sta in un byte.
;
; I dati (GDT, IDT, registri di controllo, pila, punto d'ingresso) li scrive
; smp.c nella copia, prima di svegliare ciascun processore.
; =============================================================================

SMP_TRAMP_BASE equ 0x8000

%define R(x) ((x) - smp_tramp_inizio)

section .text

global smp_tramp_inizio
global smp_tramp_fine
global smp_tramp_gdt
global smp_tramp_idt
global smp_tramp_cr0
global smp_tramp_cr3
global smp_tramp_cr4
global smp_tramp_esp
global smp_tramp_entra

; L'interrupt spurio dell'APIC locale: non vuole la conferma (EOI), si torna
; e basta. Sta FUORI dal pezzo che si copia.
global smp_spurio
global smp_isr_battito
global smp_isr_messaggio
extern smp_ap_battito
extern smp_ap_messaggio
[BITS 32]
smp_spurio:
    iretd

; Il timer dell'APIC locale e il messaggio da un altro processore (IPI), sui
; processori in piu'. ! NON PASSANO DAL GESTORE COMUNE DEGLI INTERRUPT (isr.c):
; quello aggiorna lo scheduler e il PIC, che sono del processore d'avvio. Qui
; si salvano i registri, si chiama la funzione C che conta e conferma, e si
; torna. I segmenti sono gia' quelli del kernel: questi processori non
; scendono mai in ring 3.
smp_isr_battito:
    pushad
    cld
    call    smp_ap_battito
    popad
    iretd

smp_isr_messaggio:
    pushad
    cld
    call    smp_ap_messaggio
    popad
    iretd

align 16
[BITS 16]
smp_tramp_inizio:
    cli
    cld
    mov     ax, cs
    mov     ds, ax

    ; ! o32: senza, in un segmento a 16 bit lgdt carica solo 24 bit della base,
    ; e la GDT del kernel sta sopra i 16 MB appena la RAM e' tanta.
    o32 lgdt [R(smp_tramp_gdt)]

    mov     eax, cr0
    or      al, 1                       ; PE: modo protetto, ancora senza pagine
    mov     cr0, eax
    jmp     dword 0x08:(SMP_TRAMP_BASE + R(.protetto))

[BITS 32]
.protetto:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     fs, ax
    mov     gs, ax

    ; ! CR4 PRIMA DI CR3 E DI CR0: il kernel mappa la RAM a blocchi da 4 MB
    ; (PSE), e accendere le pagine senza quel bit vorrebbe dire leggere le voci
    ; della directory come se puntassero a tabelle.
    mov     eax, [SMP_TRAMP_BASE + R(smp_tramp_cr4)]
    mov     cr4, eax
    mov     eax, [SMP_TRAMP_BASE + R(smp_tramp_cr3)]
    mov     cr3, eax
    mov     eax, [SMP_TRAMP_BASE + R(smp_tramp_cr0)]
    mov     cr0, eax

    mov     esp, [SMP_TRAMP_BASE + R(smp_tramp_esp)]
    lidt    [SMP_TRAMP_BASE + R(smp_tramp_idt)]

    mov     eax, [SMP_TRAMP_BASE + R(smp_tramp_entra)]
    call    eax

.fermo:                                 ; smp_ap_entra non torna; se tornasse
    cli
    hlt
    jmp     .fermo

align 4
smp_tramp_gdt:      dw 0                ; limite
                    dd 0                ; base
                    dw 0
smp_tramp_idt:      dw 0
                    dd 0
                    dw 0
smp_tramp_cr0:      dd 0
smp_tramp_cr3:      dd 0
smp_tramp_cr4:      dd 0
smp_tramp_esp:      dd 0
smp_tramp_entra:    dd 0
smp_tramp_fine:

section .note.GNU-stack noalloc noexec nowrite progbits
