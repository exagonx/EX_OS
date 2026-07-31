; =============================================================================
; bootloader/stage2/entry.asm
; EX-OS — Extensible Operating System
;
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; This file is part of EX-OS, distributed under the GNU GPL v2.
; See the LICENSE file in the project root for the full license text.
;
; FIX BUG #4: Aggiunta routine enable_unreal_mode, chiamata prima di
;   loader_main(), che abilita il "Big Real Mode" (Unreal Mode). Questo
;   permette di accedere a indirizzi fisici > 1MB (es. 0x100000) tramite
;   istruzioni rep movsd con registri a 32 bit, necessario per caricare
;   il kernel in memoria alta.
;
; FIX BUG #5: jump_to_kernel_asm implementata interamente in assembly puro
;   (16-bit + 32-bit), eliminando il codice C con inline .code32 che
;   produceva byte errati nel binario flat 16-bit. La funzione C
;   jump_to_kernel() in loader.c ora chiama questa stub ASM.
; =============================================================================

extern loader_main
[BITS 16]

; =============================================================================
; Entry point — prima istruzione eseguita da Stage 1
; =============================================================================
global _start

section .text._start
_start:
    cli

    ; Normalizza segmenti a base 0
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7000      ; Stack Stage 2: 0x7000 (cresce verso il basso)

    sti

    ; Salva numero drive passato da Stage 1 in DL
    mov [boot_drive], dl

    ; Stampa messaggio Stage 2 attivo
    mov si, msg_stage2
    call print16

    ; -----------------------------------------------------------------
    ; FIX BUG #4: Abilita Unreal Mode (Big Real Mode) PRIMA di
    ; chiamare loader_main(), in modo che fat12_read_sectors() possa
    ; usare rep movsd per copiare dati a indirizzi > 1MB (0x100000).
    ;
    ; Unreal Mode:
    ;   1. Carica GDT temporanea con segmento flat 4GB
    ;   2. Entra brevemente in Protected Mode (CR0.PE=1)
    ;   3. Carica FS e GS con il selettore flat 4GB (limite 0xFFFFFFFF)
    ;   4. Torna in Real Mode (CR0.PE=0)
    ;   5. I segmenti FS/GS mantengono il limite 4GB in modalità reale
    ;      → rep movsd con prefisso FS:/GS: accede a tutto lo spazio fisico
    ; -----------------------------------------------------------------
    call enable_unreal_mode

    mov si, msg_unreal
    call print16

    ; Chiama la funzione principale C di Stage 2
    push word [boot_drive]  ; Argomento: numero drive
    call loader_main
    add sp, 2               ; Pulizia stack (cdecl)

    ; loader_main non dovrebbe mai tornare
    mov si, msg_returned
    call print16
    jmp halt16

; =============================================================================
; enable_unreal_mode — Abilita Big Real Mode
;
; Carica una GDT temporanea con un segmento dati flat 4GB,
; passa brevemente in Protected Mode per espandere i descrittori
; di segmento di FS e GS, poi torna in Real Mode.
; FS e GS mantengono il limite 4GB: le istruzioni movsd con prefisso
; FS:/GS: possono ora accedere a indirizzi fisici fino a 4GB.
;
; Questo è necessario per copiare il kernel a 0x100000 (1MB) da Real Mode.
; =============================================================================
enable_unreal_mode:
    push eax
    push ebx

    cli

    ; Carica GDT temporanea (definita in .data sotto)
    lgdt [gdt_temp_ptr]

    ; Entra in Protected Mode: CR0.PE = 1
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump per svuotare pipeline (necessario dopo cambio di modo)
    jmp 0x08:.in_pm

[BITS 32]
.in_pm:
    ; Carica FS e GS con selettore flat 4GB (indice 1, GDT_TEMP)
    mov ax, 0x10        ; selettore dati flat (descrittore 2 della GDT temp)
    mov fs, ax
    mov gs, ax

    ; Torna in Real Mode: CR0.PE = 0
    mov eax, cr0
    and eax, ~1
    mov cr0, eax

[BITS 16]
    ; Far jump per tornare in Real Mode con CS=0
    jmp 0x0000:.back_to_rm

.back_to_rm:
    ; Ripristina segmenti Real Mode (tranne FS e GS che mantengono limite 4GB)
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    ; NON toccare FS e GS: mantengono il descrittore con limite 4GB

    sti

    pop ebx
    pop eax
    ret

; =============================================================================
; print16 — Stampa stringa ASCIIZ in Real Mode via INT 10h
; Input: SI = puntatore stringa (terminata da 0)
; =============================================================================
global print16
print16:
    push ax
    push bx
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; =============================================================================
; halt16 — Halt in Real Mode
; =============================================================================
global halt16
halt16:
    mov si, msg_halt
    call print16
    cli
    hlt
    jmp halt16

; =============================================================================
; jump_to_kernel_asm — Transizione a Protected Mode e salto al kernel
;
; FIX BUG #5: Tutta la logica di switch a PM è in assembly puro.
; Il codice C loader.c::jump_to_kernel() era sbagliato perché usava
; inline .code32 dentro un file compilato come 16-bit, producendo
; byte x86-32 dove il processore si aspettava x86-16.
;
; Firma C: void jump_to_kernel_asm(uint32_t kernel_addr, uint32_t info_addr)
; Convenzione cdecl:
;   [sp+2] = kernel_addr (indirizzo entry point kernel = 0x100000)
;   [sp+6] = info_addr   (indirizzo BootInfo)
;
; Questa funzione NON ritorna.
; =============================================================================
global jump_to_kernel_asm
jump_to_kernel_asm:
    ; Leggi argomenti dallo stack (siamo ancora in 16-bit)
    mov  eax, [esp+2]       ; kernel_addr
    mov  [s_kernel_addr], eax
    mov  eax, [esp+6]       ; info_addr
    mov  [s_info_addr], eax

    cli

    ; Carica GDT definitiva (uguale a quella usata da loader.c,
    ; ma la usiamo da qui in ASM per la transizione finale)
    lgdt [gdt_temp_ptr]

    ; Abilita Protected Mode: CR0.PE = 1
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump 32-bit: carica CS con selettore code segment 0x08
    ; Questo svuota la pipeline e attiva la CPU in 32-bit PM.
    ; La destinazione è pm_entry32 (etichetta nel segmento .text a 0x08)
    jmp dword 0x08:pm_entry32

[BITS 32]
pm_entry32:
    ; Carica registri data segment con selettore 0x10 (dati flat 4GB)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Imposta stack kernel temporaneo (sotto 1MB, sopra il nostro codice)
    mov esp, 0x9F000

    ; Prepara argomento per kernel_main: push BootInfo*
    mov eax, [s_info_addr]
    push eax

    ; Salta all'entry point del kernel (indirizzo fisico 0x100000)
    mov eax, [s_kernel_addr]
    jmp eax

    ; Non raggiunto
.halt:
    cli
    hlt
    jmp .halt

[BITS 16]

; =============================================================================
; read_disk_sectors(uint16_t lba, uint8_t count, uint16_t segment, uint16_t offset)
; Legge 'count' settori dal LBA in segment:offset via INT 13h
; Ritorna 0=successo, 1=errore
; =============================================================================
global read_disk_sectors
read_disk_sectors:
    push bp
    mov bp, sp
    push ax
    push bx
    push cx
    push dx
    push es

    mov ax, [bp+8]          ; LBA
    mov cl, [bp+6]          ; count
    mov bx, [bp+4]          ; segment
    mov es, bx
    mov bx, [bp+2]          ; offset

.read_loop:
    push cx

    xor dx, dx
    mov cx, 18
    div cx
    inc dx
    mov cl, dl              ; settore (1-based)

    xor dx, dx
    mov cx, 2
    div cx
    mov dh, dl              ; testina
    mov ch, al              ; cilindro

    mov ah, 0x02
    mov al, 0x01
    mov dl, [boot_drive]
    int 0x13
    jc .error

    add bx, 512
    jnc .no_wrap
    mov ax, es
    add ax, 0x20
    mov es, ax
.no_wrap:
    mov ax, [bp+8]
    inc ax
    mov [bp+8], ax

    pop cx
    dec cx
    jnz .read_loop

    xor ax, ax
    jmp .done

.error:
    pop cx
    mov ax, 1

.done:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    pop bp
    ret

; =============================================================================
; write_disk_sectors(uint16_t lba, uint8_t count, uint16_t segment, uint16_t offset)
; =============================================================================
global write_disk_sectors
write_disk_sectors:
    push bp
    mov bp, sp
    push ax
    push bx
    push cx
    push dx
    push es

    mov ax, [bp+8]
    mov cl, [bp+6]
    mov bx, [bp+4]
    mov es, bx
    mov bx, [bp+2]

.write_loop:
    push cx

    xor dx, dx
    mov cx, 18
    div cx
    inc dx
    mov cl, dl

    xor dx, dx
    mov cx, 2
    div cx
    mov dh, dl
    mov ch, al

    mov ah, 0x03
    mov al, 0x01
    mov dl, [boot_drive]
    int 0x13
    jc .error

    add bx, 512
    jnc .no_wrap
    mov ax, es
    add ax, 0x20
    mov es, ax
.no_wrap:
    mov ax, [bp+8]
    inc ax
    mov [bp+8], ax

    pop cx
    dec cx
    jnz .write_loop

    xor ax, ax
    jmp .done

.error:
    pop cx
    mov ax, 1

.done:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    pop bp
    ret

; =============================================================================
; get_memory_size — Rileva RAM disponibile (fallback INT 15h AH=88)
; Ritorna: KB totali in AX
; =============================================================================
global get_memory_size
get_memory_size:
    push bx
    push cx
    push dx
    push es

    mov ah, 0x88
    int 0x15
    jc .use_default
    add ax, 1024
    jmp .done
.use_default:
    mov ax, 4096
.done:
    pop es
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; Sezione dati
; =============================================================================
section .data

global boot_drive
boot_drive      db 0x00

msg_stage2      db 'Stage 2 attivo', 0x0D, 0x0A, 0
msg_unreal      db 'Unreal mode attivo', 0x0D, 0x0A, 0
msg_returned    db 'ERRORE: loader_main() tornato!', 0x0D, 0x0A, 0
msg_halt        db 'Halt.', 0x0D, 0x0A, 0

; Variabili per jump_to_kernel_asm
s_kernel_addr   dd 0
s_info_addr     dd 0

; =============================================================================
; GDT temporanea per Unreal Mode e transizione PM
;
; Descrittore 0: Null
; Descrittore 1: Code flat 4GB (base=0, limit=4GB, 32-bit, exec/read)
; Descrittore 2: Data flat 4GB (base=0, limit=4GB, 32-bit, read/write)
; =============================================================================
align 8
gdt_temp:
    ; Null descriptor
    dq 0

    ; Code segment: 0x08 — base=0, limit=4GB, 32-bit, ring0, executable
    dw 0xFFFF       ; limit[15:0]
    dw 0x0000       ; base[15:0]
    db 0x00         ; base[23:16]
    db 0x9A         ; access: P=1 DPL=0 S=1 E=1 DC=0 RW=1 A=0
    db 0xCF         ; flags: G=1 D=1 L=0 + limit[19:16]=F
    db 0x00         ; base[31:24]

    ; Data segment: 0x10 — base=0, limit=4GB, 32-bit, ring0, writable
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92         ; access: P=1 DPL=0 S=1 E=0 DC=0 RW=1 A=0
    db 0xCF
    db 0x00

gdt_temp_end:

gdt_temp_ptr:
    dw gdt_temp_end - gdt_temp - 1   ; limit
    dd gdt_temp                        ; base (indirizzo fisico)

; Marca stack come non-eseguibile
section .note.GNU-stack noalloc noexec nowrite progbits
