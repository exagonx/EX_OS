; =============================================================================
; kernel/arch/x86_64/cambio64.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
; =============================================================================
;
; IL CAMBIO DI CONTESTO A 64 BIT (@EXOS-64, tappa 3d — 10 ottobre 2026)
;
; Le stesse quattro funzioni di kernel/sched/context_switch.asm, con gli
; stessi nomi: context_switch, i due trampolini del primo avvio,
; sched_enter_usermode, pit_configure. Lo scheduler (sched.c) e' comune e non
; sa quale dei due file c'e' sotto.
;
; ! LA PILA SALVATA HA LA STESSA FORMA DI QUELLA A 32 BIT, PAROLA PER PAROLA,
; ed e' voluto. proc_create (sched.c) prepara a mano la pila di un processo
; che non ha mai girato, come se context_switch l'avesse appena salvata:
; quattordici parole. A 32 bit sono
;
;     GS FS ES DS | EDI ESI EBP (ESP) EBX EDX ECX EAX | EFLAGS | ritorno
;
; A 64 bit i segmenti non si salvano (in long mode non contano), ma ci sono
; quattro registri in piu' che il C vuole ritrovare: R12..R15. Stanno NEI
; QUATTRO POSTI DEI SEGMENTI:
;
;     R15 R14 R13 R12 | RDI RSI RBP (RSP) RBX RDX RCX RAX | RFLAGS | ritorno
;
; Cosi' proc_create resta una, senza un #if: scrive le sue quattordici parole
; (larghe quanto un puntatore) e i quattro «segmenti» che ci mette - il
; selettore dei dati del kernel - finiscono in R12..R15 di un processo che
; non li ha mai usati. Il trampolino trova l'ingresso in RAX e la pila del
; programma in RCX, come a 32 bit in EAX ed ECX.
;
; R8..R11 non si salvano: per la convenzione di chiamata chi chiama una
; funzione li da' per persi, e context_switch e' una funzione chiamata dal C.
; (RAX, RCX, RDX, RSI, RDI si salvano solo per tenere la forma.)
; =============================================================================

[BITS 64]

extern bkl_lascia

%define SEL_U_DATI    0x1B
%define SEL_U_CODICE  0x23

section .text

; void context_switch(vaddr_t *old_sp, vaddr_t new_sp, uint32_t new_cr3)
;                     rdi              rsi             edx
global context_switch
context_switch:
    pushfq
    push rax
    push rcx
    push rdx
    push rbx
    push rsp                ; il posto che a 32 bit «popad» salta
    push rbp
    push rsi
    push rdi
    push r12                ; i quattro posti dei segmenti
    push r13
    push r14
    push r15

    mov  [rdi], rsp         ; la pila di chi esce

    mov  edx, edx           ; CR3 arriva in 32 bit: la meta' alta a zero
    mov  rax, cr3
    cmp  rax, rdx
    je   .stesso_spazio     ; stesso spazio: non si butta via il TLB
    mov  cr3, rdx
.stesso_spazio:

    mov  rsp, rsi           ; la pila di chi entra
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  rdi
    pop  rsi
    pop  rbp
    add  rsp, 8
    pop  rbx
    pop  rdx
    pop  rcx
    pop  rax
    popfq                   ; con IF: da qui gli interrupt sono come li aveva lasciati
    ret

; Il primo avvio di un programma: RAX = ingresso, RCX = la sua pila.
; ! NON SI TOCCANO FS E GS: in long mode caricare un selettore in GS ne
; azzera la base, e la base di GS e' la memoria locale del filo, appena
; scritta da gdt_set_tls_base. Restano nulli dall'avvio (gdt64.c).
global proc_entry_stub_user
proc_entry_stub_user:
    cli
    push rax
    push rcx
    call bkl_lascia
    pop  rcx
    pop  rax
    mov  dx, SEL_U_DATI
    mov  ds, dx
    mov  es, dx
    push qword SEL_U_DATI   ; SS
    push rcx                ; RSP del programma
    push qword 0x202        ; RFLAGS: interrupt accesi
    push qword SEL_U_CODICE ; CS, ring 3
    push rax                ; RIP
    iretq

; Il primo avvio di un filo del kernel: RAX = la funzione.
; La pila si mette come dopo una «call» (16 byte meno 8): e' quello che il C
; si aspetta entrando in una funzione. In fondo un ritorno a zero: un filo
; del kernel non torna, e se lo facesse l'errore di pagina a 0 lo direbbe.
global proc_entry_stub_kernel
proc_entry_stub_kernel:
    and  rsp, -16
    push qword 0
    jmp  rax

; void sched_enter_usermode(vaddr_t entry, vaddr_t user_sp)   rdi, rsi
global sched_enter_usermode
sched_enter_usermode:
    cli
    push rdi
    push rsi
    call bkl_lascia
    pop  rsi
    pop  rdi
    mov  dx, SEL_U_DATI
    mov  ds, dx
    mov  es, dx
    push qword SEL_U_DATI
    push rsi
    push qword 0x202
    push qword SEL_U_CODICE
    push rdi
    iretq

; void pit_configure(uint32_t frequency_hz)   edi
global pit_configure
pit_configure:
    mov  ecx, edi
    mov  eax, 1193182
    xor  edx, edx
    div  ecx                ; EAX = il divisore
    mov  ecx, eax
    mov  al, 0x36
    out  0x43, al           ; canale 0, onda quadra
    mov  al, cl
    out  0x40, al
    mov  al, ch
    out  0x40, al
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
