; =============================================================================
; kernel/arch/x86/isr_stubs.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
; =============================================================================

[BITS 32]

extern isr_handler      ; Handler C in isr.c
extern irq_handler      ; Handler C IRQ in isr.c
extern syscall_handler  ; Handler C syscall in syscall/syscall.c

; =============================================================================
; Macro per stub senza error code (la CPU non lo pusha)
; =============================================================================
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli
    push dword 0        ; Error code fittizio
    push dword %1       ; Numero vettore
    jmp isr_common_stub
%endmacro

; =============================================================================
; Macro per stub CON error code (la CPU lo ha già pushato)
; =============================================================================
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    ; Error code già sullo stack dalla CPU
    push dword %1       ; Numero vettore
    jmp isr_common_stub
%endmacro

; =============================================================================
; Macro per IRQ hardware (rimappati a vettori 32-47)
; =============================================================================
%macro IRQ 2
global irq%1
irq%1:
    cli
    push dword 0        ; No error code per IRQ
    push dword %2       ; Numero vettore (32 + n)
    jmp irq_common_stub
%endmacro

; =============================================================================
; Definizione stub per tutte le eccezioni CPU (0-31)
; =============================================================================

; Eccezioni senza error code
ISR_NOERRCODE 0     ; #DE Division Error
ISR_NOERRCODE 1     ; #DB Debug
ISR_NOERRCODE 2     ; NMI
ISR_NOERRCODE 3     ; #BP Breakpoint
ISR_NOERRCODE 4     ; #OF Overflow
ISR_NOERRCODE 5     ; #BR Bound Range Exceeded
ISR_NOERRCODE 6     ; #UD Invalid Opcode
ISR_NOERRCODE 7     ; #NM Device Not Available
ISR_ERRCODE   8     ; #DF Double Fault
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun (obsoleto)
ISR_ERRCODE   10    ; #TS Invalid TSS
ISR_ERRCODE   11    ; #NP Segment Not Present
ISR_ERRCODE   12    ; #SS Stack-Segment Fault
ISR_ERRCODE   13    ; #GP General Protection Fault
ISR_ERRCODE   14    ; #PF Page Fault
ISR_NOERRCODE 15    ; Riservato Intel
ISR_NOERRCODE 16    ; #MF x87 FPU Floating-Point Error
ISR_ERRCODE   17    ; #AC Alignment Check
ISR_NOERRCODE 18    ; #MC Machine Check
ISR_NOERRCODE 19    ; #XM/#XF SIMD Floating-Point Exception
ISR_NOERRCODE 20    ; #VE Virtualization Exception
ISR_ERRCODE   21    ; #CP Control Protection Exception
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_ERRCODE   29    ; #HV Hypervisor Injection (AMD)
ISR_ERRCODE   30    ; #VC VMM Communication (AMD)
ISR_NOERRCODE 31

; =============================================================================
; Definizione stub per IRQ hardware (0-15)
; IRQ n → vettore 32+n
; =============================================================================
IRQ  0, 32      ; IRQ0  → INT 32  Timer PIT
IRQ  1, 33      ; IRQ1  → INT 33  Tastiera PS/2
IRQ  2, 34      ; IRQ2  → INT 34  Cascade
IRQ  3, 35      ; IRQ3  → INT 35  COM2
IRQ  4, 36      ; IRQ4  → INT 36  COM1
IRQ  5, 37      ; IRQ5  → INT 37  LPT2/Sound
IRQ  6, 38      ; IRQ6  → INT 38  Floppy
IRQ  7, 39      ; IRQ7  → INT 39  LPT1/Spurious
IRQ  8, 40      ; IRQ8  → INT 40  RTC
IRQ  9, 41      ; IRQ9  → INT 41  ACPI
IRQ 10, 42      ; IRQ10 → INT 42
IRQ 11, 43      ; IRQ11 → INT 43
IRQ 12, 44      ; IRQ12 → INT 44  PS/2 Mouse
IRQ 13, 45      ; IRQ13 → INT 45  FPU
IRQ 14, 46      ; IRQ14 → INT 46  ATA Primary
IRQ 15, 47      ; IRQ15 → INT 47  ATA Secondary

; =============================================================================
; Stub syscall int 0x80
; ============================================================================= */
global isr128
isr128:
    ; Non disabilitare interrupt per syscall (trap gate nella IDT)
    push dword 0            ; No error code
    push dword 128          ; Numero vettore
    jmp syscall_stub

; =============================================================================
; isr_common_stub — Routine comune per tutte le eccezioni CPU
; =============================================================================
; =============================================================================
; ! GS NON SI TOCCA — e' il thread pointer del processo
;
; Questi tre stub salvavano DS e poi caricavano il selettore dati del kernel
; in DS, ES, FS **e GS**; all'uscita rimettevano in tutti e quattro il DS
; salvato. Su un ritorno a ring3 quello significa GS = 0x23, cioe' il
; selettore dati utente qualunque cosa il processo ci avesse messo.
;
; Da agosto 2026 un processo ci tiene il descrittore TLS (0x33), la cui base
; e' il suo thread pointer. Se il primo tick di timer glielo riscrive, ogni
; accesso a una variabile __thread finisce a leggere da un altro segmento —
; ed e' un guasto che compare a caso, perche' dipende da quando arriva
; l'interrupt.
;
; La soluzione e' non toccarlo affatto: **il kernel non usa GS**. Non c'e'
; una riga di codice kernel che dereferenzi %gs — niente percpu, niente
; stack canary — quindi lasciarlo con il valore dell'utente non espone
; nulla. E il valore che l'utente ci puo' mettere e' comunque limitato da
; una regola della CPU: `mov gs, ax` con un selettore piu' privilegiato di
; CPL solleva #GP al momento del caricamento, non dopo.
;
; ES e FS restano allineati a DS: non li usa nessuno, e cambiarli sarebbe
; rumore in un diff che ha gia' una ragione sola.
; =============================================================================
isr_common_stub:
    ; Salva tutti i registri general-purpose
    pushad

    ; Salva DS e imposta segmento dati kernel (GS no: vedi sopra)
    mov ax, ds
    push eax                ; Salva DS originale
    mov ax, 0x10            ; Kernel data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax

    ; Chiama handler C: isr_handler(InterruptFrame *frame)
    ; ESP punta alla struttura InterruptFrame
    push esp                ; Argomento: puntatore al frame
    call isr_handler
    add esp, 4              ; Pulisci argomento

    ; Ripristina DS
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax

    ; Ripristina registri general-purpose
    popad

    ; Rimuovi int_no e err_code dallo stack
    add esp, 8

    ; Ritorno da interrupt (ripristina EIP, CS, EFLAGS + ESP/SS se ring3)
    iret

; =============================================================================
; irq_common_stub — Routine comune per tutti gli IRQ hardware
; =============================================================================
irq_common_stub:
    pushad

    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax

    ; Chiama handler C: irq_handler(InterruptFrame *frame)
    push esp
    call irq_handler
    add esp, 4

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax

    popad
    add esp, 8
    iret

; =============================================================================
; syscall_stub — Routine per int 0x80
;
; Convenzione syscall (stile Linux):
;   EAX = numero syscall
;   EBX = argomento 1
;   ECX = argomento 2
;   EDX = argomento 3
;   ESI = argomento 4
;   EDI = argomento 5
;   EAX = valore di ritorno (modificato da syscall_handler)
; =============================================================================
syscall_stub:
    pushad

    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax

    ; Chiama handler C syscall
    push esp
    call syscall_handler
    add esp, 4

    ; EAX dal frame contiene il valore di ritorno
    ; Lo copiamo nel frame pushad così popad lo ripristina correttamente
    ; (syscall_handler modifica frame->eax con il return value)

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax

    popad
    add esp, 8
    iret

; Marca stack come non-eseguibile
section .note.GNU-stack noalloc noexec nowrite progbits
