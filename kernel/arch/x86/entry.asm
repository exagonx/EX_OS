; =============================================================================
; kernel/arch/x86/entry.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
;
; FIX BUG #3 (lato kernel): BOOTINFO_PHYS_ADDR corretto a 0xC000.
;   Il codice precedente leggeva BootInfo* da [0x8FFFC], cioè dallo
;   stack di Stage 2 impostato a 0x9F000 da pm_entry32. Questo è
;   fragile e dipende dall'indirizzo esatto di push in entry.asm Stage 2.
;   Ora leggiamo direttamente da BOOTINFO_PHYS_ADDR = 0xC000, dove
;   Stage 2 ha scritto la struttura BootInfo prima di saltare al kernel.
;   Questo è il metodo corretto e robusto: indirizzo fisso concordato.
; =============================================================================

[BITS 32]                       ; Protected Mode 32-bit

; Simboli dal linker script kernel.ld
extern _bss_start
extern _bss_end
extern _stack_top

; Funzione C principale del kernel
extern kernel_main

; Esporta entry point
global kernel_entry

; Indirizzo fisico dove Stage 2 ha scritto la struttura BootInfo.
; DEVE corrispondere a BOOTINFO_ADDR in stage2.h e BOOTINFO_PHYS_ADDR in kernel.h.
; FIX BUG #3: era 0xB000 (= DISK_BUFFER_ADDR, sovrascritta durante il caricamento).
%define BOOTINFO_PHYS_ADDR  0xC000

; =============================================================================
; Sezione .text.kernel_entry — DEVE essere la prima nel binario finale
; Garantito dal linker script: *(.text.kernel_entry) prima di *(.text)
; Quando Stage 2 fa objcopy --output-target binary, il primo byte del
; flat binary corrisponde esattamente a kernel_entry.
; =============================================================================
section .text.kernel_entry

kernel_entry:
    ; FIX: disabilita interrupt immediatamente. Senza questo, IRQ pendenti
    ; nel PIC (mappato di default su vettori 0x08-0x0F che collidono con
    ; le eccezioni CPU #DF,#GP,#PF) causano triple fault prima che la IDT
    ; sia installata.
    cli

    ; FIX: rimappa il PIC 8259 su vettori 0x20-0x2F PRIMA di ogni altra cosa.
    ; Sequenza standard ICW1-ICW4.
    mov  al, 0x11
    out  0x20, al
    out  0xA0, al
    mov  al, 0x20        ; master base = 0x20
    out  0x21, al
    mov  al, 0x28        ; slave base  = 0x28
    out  0xA1, al
    mov  al, 0x04        ; master: slave su IRQ2
    out  0x21, al
    mov  al, 0x02        ; slave: cascade identity 2
    out  0xA1, al
    mov  al, 0x01        ; 8086 mode
    out  0x21, al
    out  0xA1, al
    mov  al, 0xFF        ; maschera tutti gli IRQ — li sblocca sched_init
    out  0x21, al
    out  0xA1, al

    ; FIX: configura il PIT a 100Hz qui, in puro assembly, PRIMA che la
    ; paginazione sia attiva e prima di ogni chiamata C. Il PIT non genera
    ; interrupt finché IRQ0 resta mascherato (sopra), quindi è sicuro
    ; configurarlo ora — sched_init lo sbloccherà solo dopo aver impostato
    ; g_current, evitando che il primo tick trovi lo scheduler non pronto.
    mov  al, 0x36        ; ch0, lobyte/hibyte, mode2 (rate generator)
    out  0x43, al
    mov  al, 0xBB        ; divisore 11931 low byte  (1193182/100 ≈ 11931)
    out  0x40, al
    mov  al, 0x2E        ; divisore 11931 high byte
    out  0x40, al

    ; --- EARLY SERIAL MARKER: 'K' = kernel_entry raggiunto -------------------
    ; Stampato PRIMA di qualsiasi altra cosa (anche prima di impostare lo
    ; stack) per confermare che il salto da Stage2 a 0x100000 e' avvenuto
    ; e che il codice del kernel e' stato copiato correttamente in RAM.
    mov  dx, 0x3FD
.kearly_w:
    in   al, dx
    test al, 0x20
    jz   .kearly_w
    mov  dx, 0x3F8
    mov  al, 75          ; 'K'
    out  dx, al

    ; -------------------------------------------------------------------------
    ; Passo 1: Imposta stack kernel definitivo
    ; _stack_top è definito nel linker script (fine sezione .stack)
    ; -------------------------------------------------------------------------
    mov esp, _stack_top
    and esp, 0xFFFFFFF0         ; Allinea a 16 byte (ABI x86)

    ; -------------------------------------------------------------------------
    ; Passo 2: Recupera puntatore BootInfo
    ;
    ; FIX BUG #3: Stage 2 scrive BootInfo all'indirizzo fisso BOOTINFO_ADDR
    ; (0xC000, concordato in stage2.h). Leggiamo la struttura direttamente
    ; da quell'indirizzo, non più dallo stack volatile di Stage 2 (0x8FFFC).
    ;
    ; jump_to_kernel_asm() in entry.asm Stage 2 fa "push eax" (BootInfo*)
    ; prima di "jmp kernel_entry". Ma abbiamo appena cambiato ESP (passo 1),
    ; quindi quel valore sullo stack Stage 2 non è più accessibile tramite
    ; il nostro ESP. Usiamo invece l'indirizzo fisso BOOTINFO_PHYS_ADDR.
    ; -------------------------------------------------------------------------
    mov eax, BOOTINFO_PHYS_ADDR ; EAX = indirizzo fisico BootInfo (0xC000)
    push eax                    ; Passa BootInfo* come argomento a kernel_main

    ; -------------------------------------------------------------------------
    ; Passo 3: Azzera BSS
    ; Tutti i dati non inizializzati devono essere zero prima di usarli in C.
    ; -------------------------------------------------------------------------
    mov edi, _bss_start
    mov ecx, _bss_end
    sub ecx, edi                ; ECX = dimensione BSS in byte
    shr ecx, 2                  ; ECX = numero dword
    xor eax, eax
    xor eax, eax
    rep stosd

    ; -------------------------------------------------------------------------
    ; Passo 4: Chiama kernel_main(BootInfo*)
    ; -------------------------------------------------------------------------
    call kernel_main

    ; -------------------------------------------------------------------------
    ; Passo 5: kernel_main non dovrebbe mai tornare.
    ; -------------------------------------------------------------------------
    cli
.halt_loop:
    hlt
    jmp .halt_loop

; =============================================================================
; Funzioni ASM usate dal kernel C
; =============================================================================

; --- GDT flush ----------------------------------------------------------------
global gdt_flush
gdt_flush:
    mov eax, [esp+4]
    lgdt [eax]
    jmp 0x08:.reload_cs
.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

; --- IDT flush ----------------------------------------------------------------
global idt_flush
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    ret

; --- TSS flush ----------------------------------------------------------------
global tss_flush
tss_flush:
    mov ax, [esp+4]
    ltr ax
    ret

; --- Registro CR0 -------------------------------------------------------------
global read_cr0
read_cr0:
    mov eax, cr0
    ret

global write_cr0
write_cr0:
    mov eax, [esp+4]
    mov cr0, eax
    ret

; --- Registro CR4 -------------------------------------------------------------
; ⚠️ CR4 NON ESISTE SUL 386 E SUI PRIMI 486. Chi chiama deve gia' sapere
; che la CPU ce l'ha: qui non si puo' controllare, perche' l'istruzione
; stessa e' quella che mancherebbe. La discriminante e' CPUID, e CPUID a
; sua volta si rileva col bit ID di EFLAGS (vedi sotto).
global read_cr4
read_cr4:
    mov eax, cr4
    ret

global write_cr4
write_cr4:
    mov eax, [esp+4]
    mov cr4, eax
    ret

; --- CPUID --------------------------------------------------------------------
; cpuid_disponibile(): 1 se la CPU ha CPUID, 0 altrimenti.
;
; ⚠️ NON SI PUO' CHIEDERE A CPUID SE CPUID ESISTE. L'unico modo e' provare
; a invertire il bit 21 (ID) di EFLAGS: sul 386 e sui 486 piu' vecchi quel
; bit non si lascia cambiare. E' il test che usa ogni sistema che debba
; girare anche su quelle macchine.
global cpuid_disponibile
cpuid_disponibile:
    pushfd                          ; salva EFLAGS originali
    pushfd                          ; copia su cui lavorare
    xor dword [esp], 0x00200000     ; inverte il bit ID
    popfd                           ; prova a rimetterlo in EFLAGS
    pushfd
    pop eax                         ; rileggi: il bit e' cambiato davvero?
    xor eax, [esp]                  ; confronta con l'originale
    popfd                           ; ripristina EFLAGS come li abbiamo trovati
    and eax, 0x00200000
    shr eax, 21
    ret

; cpuid_edx1() / cpuid_ecx1(): i registri EDX ed ECX della funzione 1.
; EDX bit 23 = MMX, 24 = FXSR, 25 = SSE, 26 = SSE2.  ECX bit 0 = SSE3.
;
; EBX va salvato: e' callee-saved nella nostra ABI e CPUID lo distrugge.
global cpuid_edx1
cpuid_edx1:
    push ebx
    mov eax, 1
    cpuid
    mov eax, edx
    pop ebx
    ret

global cpuid_ecx1
cpuid_ecx1:
    push ebx
    mov eax, 1
    cpuid
    mov eax, ecx
    pop ebx
    ret

; cpuid_max(): la funzione piu' alta supportata (foglia 0, EAX). Serve a
; sapere se la foglia 1 si puo' chiedere: su una CPU che si ferma a 0,
; leggere la 1 restituisce valori di un'altra foglia.
global cpuid_max
cpuid_max:
    push ebx
    xor eax, eax
    cpuid
    pop ebx
    ret

; --- Registro CR2 (indirizzo Page Fault) --------------------------------------
global read_cr2
read_cr2:
    mov eax, cr2
    ret

; --- Registro CR3 (Page Directory base) --------------------------------------
global read_cr3
read_cr3:
    mov eax, cr3
    ret

global write_cr3
write_cr3:
    mov eax, [esp+4]
    mov cr3, eax
    ret

; --- Interrupt ----------------------------------------------------------------
global interrupts_enable
interrupts_enable:
    sti
    ret

global interrupts_disable
interrupts_disable:
    cli
    ret

; --- EFLAGS -------------------------------------------------------------------
global read_eflags
read_eflags:
    pushfd
    pop eax
    ret

; --- I/O port: byte -----------------------------------------------------------
global port_inb
port_inb:
    mov dx, [esp+4]
    in al, dx
    ret

global port_outb
port_outb:
    mov dx, [esp+4]
    mov al, [esp+8]
    out dx, al
    ret

; --- I/O port: word -----------------------------------------------------------
global port_inw
port_inw:
    mov dx, [esp+4]
    in ax, dx
    ret

global port_outw
port_outw:
    mov dx, [esp+4]
    mov ax, [esp+8]
    out dx, ax
    ret

; --- I/O port: BLOCCHI di word (rep insw / rep outsw) -------------------------
;
; ⚠️ QUESTE DUE RIGHE DI ASSEMBLY VALGONO PIU' DI TUTTO IL RESTO DEL
; DRIVER DEL DISCO, e il perche' si vede solo contando.
;
; Prima il trasferimento di un settore era un ciclo C che chiamava
; port_inw 256 volte: 256 CALL, 256 RET, e in mezzo il montaggio a mano
; dei due byte di ogni parola. Duemila e passa istruzioni per 512 byte —
; ed e' da li' che venivano gli 0,75 MB/s misurati, non dal controller e
; non dal disco.
;
; `rep insw` fa lo stesso lavoro con UNA istruzione. L'ordine dei byte in
; memoria e' identico a quello che il ciclo componeva a mano (basso, poi
; alto): su x86 e' cosi' per costruzione, quindi i dati sul disco non
; cambiano di una virgola.
;
; ⚠️ IL BUFFER DEVE ESSERE GIA' PRESENTE IN RAM. rep insw non e'
; interrompibile a meta' in modo utile: se la pagina di destinazione non
; c'e', il fault avviene DENTRO il trasferimento. Per le letture verso un
; buffer utente ci pensa vm_precarica_utente() prima di entrare nel VFS.
global port_insw
port_insw:                      ; (uint16_t porta, void *dst, uint32_t n_word)
    push edi
    mov dx,  [esp+8]
    mov edi, [esp+12]
    mov ecx, [esp+16]
    cld
    rep insw
    pop edi
    ret

global port_outsw
port_outsw:                     ; (uint16_t porta, const void *src, uint32_t n_word)
    push esi
    mov dx,  [esp+8]
    mov esi, [esp+12]
    mov ecx, [esp+16]
    cld
    rep outsw
    pop esi
    ret

; --- I/O port: dword ----------------------------------------------------------
global port_inl
port_inl:
    mov dx, [esp+4]
    in eax, dx
    ret

global port_outl
port_outl:
    mov dx, [esp+4]
    mov eax, [esp+8]
    out dx, eax
    ret

; --- I/O delay (porta 0x80 — POST diagnostic, ~1us) --------------------------
global io_delay
io_delay:
    out 0x80, al
    ret

; --- CPUID --------------------------------------------------------------------
; Firma C: void cpuid(uint32_t code, uint32_t *eax, uint32_t *ebx,
;                     uint32_t *ecx, uint32_t *edx)
global cpuid
cpuid:
    push ebx
    push esi

    mov  eax, [esp+12]
    cpuid

    mov  esi, [esp+16]
    test esi, esi
    jz   .skip_eax
    mov  [esi], eax
.skip_eax:

    mov  esi, [esp+20]
    test esi, esi
    jz   .skip_ebx
    mov  [esi], ebx
.skip_ebx:

    mov  esi, [esp+24]
    test esi, esi
    jz   .skip_ecx
    mov  [esi], ecx
.skip_ecx:

    mov  esi, [esp+28]
    test esi, esi
    jz   .skip_edx
    mov  [esi], edx
.skip_edx:

    pop esi
    pop ebx
    ret

; Marca stack come non-eseguibile
section .note.GNU-stack noalloc noexec nowrite progbits
