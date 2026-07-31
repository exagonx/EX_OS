; =============================================================================
; kernel/sched/context_switch.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
; =============================================================================

[BITS 32]

; Ridefiniamo correttamente:
global context_switch
context_switch:
    ; Leggi parametri PRIMA di modificare lo stack
    mov eax, [esp + 4]      ; EAX = old_esp* (dove salvare il nostro ESP)
    mov ecx, [esp + 8]      ; ECX = new_esp
    mov edx, [esp + 12]     ; EDX = new_cr3

    ; Salva contesto corrente sullo stack corrente
    pushfd                  ; EFLAGS
    pushad                  ; EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI (32 byte)

    push ds                 ; Salva segmenti
    push es
    push fs
    push gs

    ; Salva ESP corrente nel PCB del processo corrente
    mov [eax], esp

    ; -------------------------------------------------------------------------
    ; Da qui in poi non usiamo più lo stack del processo corrente
    ; -------------------------------------------------------------------------

    ; Cambia Page Directory se diversa da quella corrente
    ; Confronta con CR3 attuale
    mov eax, cr3
    cmp eax, edx
    je  .no_cr3_switch
    mov cr3, edx            ; Carica nuova PD (invalida TLB)
.no_cr3_switch:

    ; Carica lo stack del nuovo processo
    mov esp, ecx

    ; Ripristina contesto del nuovo processo
    pop gs
    pop fs
    pop es
    pop ds

    popad                   ; Ripristina registri general-purpose
    popfd                   ; Ripristina EFLAGS (incluso IF - interrupt flag)

    ; Ritorna al codice del nuovo processo
    ; L'indirizzo di ritorno è già sullo stack (push ret di context_switch precedente)
    ret

; =============================================================================
; proc_entry_stub_user — Trampolino per il PRIMO avvio di un processo utente
;
; Era menzionato nei commenti di proc_create ma mai implementato: lo stack
; costruito da proc_create termina con un "ret address" che punta qui
; (invece che direttamente all'entry point utente). Senza questo, il
; semplice "ret" di context_switch eseguiva il codice utente in ring0
; (CPL invariato dal contesto kernel precedente), senza alcun cambio di
; privilegio/stack/selettori richiesto per ring3 — causa del crash
; imprevedibile osservato al primo avvio della shell.
;
; Convenzione: proc_create mette in EAX l'entry point utente e in ECX lo
; user_esp finale; questi valori sono ripristinati da popad subito prima
; del ret che salta qui, quindi disponibili in EAX/ECX appena entriamo.
; ============================================================================= */
global proc_entry_stub_user
proc_entry_stub_user:
    cli
    mov edx, eax             ; EDX = entry point (libera EAX per il selettore)

    mov ax, 0x23             ; User data selector | RPL=3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23          ; User SS
    push ecx                 ; User ESP
    push dword 0x200         ; EFLAGS: IF=1
    push dword 0x1B          ; User CS | RPL=3
    push edx                 ; EIP (entry point)
    iret
    ; Non ritorna

; =============================================================================
; proc_entry_stub_kernel — Trampolino per il PRIMO avvio di un task kernel
;
; Per i task kernel (is_kernel_task=1) non serve alcun cambio di privilegio:
; l'entry point va semplicemente raggiunto in ring0. EAX contiene l'entry
; point (stessa convenzione dello stub utente).
; ============================================================================= */
global proc_entry_stub_kernel
proc_entry_stub_kernel:
    jmp eax                  ; Salta direttamente all'entry point, ring0
    ; Non ritorna

; =============================================================================
; sched_enter_usermode — Salta a un processo utente in ring3
;
; Usato da exec() per avviare il primo processo utente.
; Costruisce un frame iret artificiale e ritorna in ring3.
;
; Firma C:
;   void sched_enter_usermode(uint32_t entry, uint32_t user_esp)
;
; entry:    indirizzo virtuale entry point del programma ELF
; user_esp: indirizzo stack utente (top, allineato a 16 byte)
; =============================================================================
global sched_enter_usermode
sched_enter_usermode:
    mov eax, [esp + 4]      ; EAX = entry point
    mov ecx, [esp + 8]      ; ECX = user_esp

    ; Disabilita interrupt durante il setup
    cli

    ; Selettore segmento utente: GDT_USER_DATA_SEL | RPL3 = 0x23
    ; Selettore codice  utente: GDT_USER_CODE_SEL | RPL3 = 0x1B
    mov dx, 0x23            ; User data selector | RPL=3
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    ; SS lo imposta iret

    ; Costruisci frame iret sullo stack KERNEL corrente:
    ; La CPU durante iret si aspetta (in ordine su stack, top = primo):
    ;   [ESP+0]  = EIP  (entry point)
    ;   [ESP+4]  = CS   (user code selector | RPL3)
    ;   [ESP+8]  = EFLAGS (IF=1 per abilitare interrupt in user mode)
    ;   [ESP+12] = ESP  (user stack pointer)  ← solo ring3→ring0
    ;   [ESP+16] = SS   (user stack selector)

    push dword 0x23         ; User SS (stack segment | RPL=3)
    push ecx                ; User ESP
    push dword 0x200        ; EFLAGS: IF=1 (interrupt enable), tutto il resto 0
    push dword 0x1B         ; User CS (code segment | RPL=3)
    push eax                ; EIP (entry point)

    ; Salta a ring3 tramite iret
    iret
    ; Non ritorna

; =============================================================================
; pit_configure — Configura il PIT 8253/8254 per generare IRQ0 a 100Hz
;
; Firma C: void pit_configure(uint32_t frequency_hz)
;
; Il PIT ha un clock di 1.193182 MHz.
; Divisore = 1193182 / frequency_hz
; Per 100 Hz: divisore = 11931 (≈ 11932)
; =============================================================================
global pit_configure
pit_configure:
    mov eax, [esp + 4]      ; EAX = frequenza desiderata in Hz

    ; Calcola divisore: 1193182 / freq
    mov ecx, eax
    mov eax, 1193182
    xor edx, edx
    div ecx                 ; EAX = divisore, EDX = resto
    ; EAX = divisore

    ; Comando PIT: canale 0, lobyte/hibyte, square wave generator (mode 3)
    ; Control word: 0x36 = 0011 0110b
    ;   Bit 7-6: 00 = canale 0
    ;   Bit 5-4: 11 = lobyte/hibyte (16 bit)
    ;   Bit 3-1: 011 = mode 3 (square wave)
    ;   Bit 0:   0 = binary counting
    push eax                ; Salva divisore
    mov al, 0x36
    out 0x43, al            ; Scrivi control word al command register PIT

    ; Scrivi divisore: prima low byte, poi high byte
    pop eax
    out 0x40, al            ; Low byte divisore → canale 0
    shr eax, 8
    out 0x40, al            ; High byte divisore → canale 0

    ret

; Marca stack come non-eseguibile
section .note.GNU-stack noalloc noexec nowrite progbits
