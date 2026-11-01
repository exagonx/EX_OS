; =============================================================================
; kernel/arch/x86_64/tramp64.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
; =============================================================================
;
; THE TRAMPOLINE OF THE EXTRA PROCESSORS AT 64 BIT
;
; The same job as kernel/arch/x86/smp_tramp.asm, with the same symbols, so that
; smp.c stays one file: a processor wakes up as an 8086 at SMP_TRAMP_BASE and
; has to reach long mode with the kernel's tables.
;
; ! IT CARRIES A SMALL GDT OF ITS OWN. The kernel's has no 32-bit code segment,
; and the way to long mode goes through one. The kernel's GDT is loaded at the
; end, and a far return puts CS on it: selector 0x18, the one this file uses
; for 64-bit code, is the programs' data in the kernel's table.
;
; ! IT DOES NOT RUN WHERE IT IS LINKED: smp.c copies it below the megabyte, so
; every address is SMP_TRAMP_BASE + offset, as in the 32-bit one.
; =============================================================================

SMP_TRAMP_BASE equ 0x8000
MSR_EFER       equ 0xC0000080

%define R(x) ((x) - smp_tramp_inizio)
%define A(x) (SMP_TRAMP_BASE + R(x))

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
global smp_tramp_efer
global smp_ap_salta

; smp_ap_salta(pila, funzione): off the trampoline's stack, onto the idle
; task's, and `funzione`, which does not return.
[BITS 64]
smp_ap_salta:
    mov     rsp, rdi
    and     rsp, -16
    xor     ebp, ebp
    call    rsi
.fermo:
    cli
    hlt
    jmp     .fermo

align 16
[BITS 16]
smp_tramp_inizio:
    cli
    cld
    mov     ax, cs
    mov     ds, ax
    o32 lgdt [R(gdt_piccola_reg)]

    mov     eax, cr0
    or      al, 1
    mov     cr0, eax
    jmp     dword 0x08:A(protetto)

[BITS 32]
protetto:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     ss, ax

    ; The order the processor demands: PAE, the tables, LME, then paging.
    mov     eax, [A(smp_tramp_cr4)]
    mov     cr4, eax
    mov     eax, [A(smp_tramp_cr3)]
    mov     cr3, eax
    mov     ecx, MSR_EFER
    mov     eax, [A(smp_tramp_efer)]
    xor     edx, edx
    wrmsr
    mov     eax, [A(smp_tramp_cr0)]
    mov     cr0, eax
    jmp     0x18:A(lungo)

[BITS 64]
lungo:
    lgdt    [abs A(smp_tramp_gdt)]
    lidt    [abs A(smp_tramp_idt)]
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    xor     eax, eax
    mov     fs, ax
    mov     gs, ax

    mov     esp, [abs A(smp_tramp_esp)]
    mov     eax, [abs A(smp_tramp_entra)]
    sub     rsp, 8                  ; as after a call: the C wants it so
    push    qword 0x08              ; the kernel's code
    push    rax
    o64 retf

align 8
gdt_piccola:
    dq 0
    dq 0x00CF9A000000FFFF           ; 0x08 code, 32 bit
    dq 0x00CF92000000FFFF           ; 0x10 data
    dq 0x00209A0000000000           ; 0x18 code, 64 bit
gdt_piccola_reg:
    dw 4 * 8 - 1
    dd A(gdt_piccola)

align 8
smp_tramp_gdt:      dw 0                ; sgdt: limit, then 8 bytes of base
                    dq 0
                    dw 0
smp_tramp_idt:      dw 0
                    dq 0
                    dw 0
smp_tramp_cr0:      dd 0
smp_tramp_cr3:      dd 0
smp_tramp_cr4:      dd 0
smp_tramp_efer:     dd 0
smp_tramp_esp:      dd 0
smp_tramp_entra:    dd 0
smp_tramp_fine:

section .note.GNU-stack noalloc noexec nowrite progbits
