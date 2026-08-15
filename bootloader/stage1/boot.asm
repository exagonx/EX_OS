; =============================================================================
; bootloader/stage1/boot.asm — EX-OS Stage 1 Boot Sector (512 byte esatti)
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
; SPDX-License-Identifier: GPL-2.0-or-later
;
; Layout di caricamento (NO overlap):
;   - Root dir : 14 settori, LBA 19  -> 0x7E00 .. 0x9A00
;   - FAT1     :  9 settori, LBA  1  -> 0xA000 .. 0xB200  (caricata DOPO la
;                  root dir, quindi non viene sovrascritta)
;   - LOADER.BIN -> 0x0500 (segue la catena FAT a 0xA000)
;
; Prima del far jump a 0x0000:0x0500 viene inizializzata la porta seriale
; COM1 (0x3F8, 115200 8N1) cosi' Stage 2 puo' usarla immediatamente per il
; debug.
; =============================================================================
[BITS 16]
[ORG 0x7C00]

        jmp short _start
        nop

; ---- BPB FAT12 1.44MB --------------------------------------------------
        db 'EXOS    '   ; OEM name (8 byte)
        dw 512          ; BytesPerSector
        db 1            ; SectorsPerCluster
        dw 1            ; ReservedSectors
        db 2            ; NumberOfFATs
        dw 224          ; RootEntryCount
        dw 2880         ; TotalSectors16
        db 0xF0         ; MediaType
        dw 9            ; SectorsPerFAT
        dw 18           ; SectorsPerTrack
        dw 2            ; NumberOfHeads
        dd 0            ; HiddenSectors
        dd 0            ; TotalSectors32
        db 0x00         ; DriveNumber
        db 0            ; Reserved1
        db 0x29         ; BootSignature (extended BPB)
        dd 0xEA05EDA7   ; VolumeID
        db 'EX-OS      '; VolumeLabel (11 byte)
        db 'FAT12   '   ; FileSystemType (8 byte)

; ---- Entry point ---------------------------------------------------------
_start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti
    mov  [drv], dl

    ; 1) Root dir a 0x7E00 (14 settori, LBA 19) — PRIMA
    mov  ax, 19
    mov  bx, 0x7E00
    mov  cx, 14
    call rsect

    ; 2) FAT1 a 0xA000 (9 settori, LBA 1) — DOPO, nessun overlap
    mov  ax, 1
    mov  bx, 0xA000
    mov  cx, 9
    call rsect

    ; Cerca 'LOADER  BIN' nella root directory (224 entry da 32 byte)
    cld                  ; repe cmpsb richiede DF=0; int 13h non garantisce DF
    mov  di, 0x7E00
    mov  cx, 224
.s:
    cmp  byte [di], 0
    je   halt
    cmp  byte [di], 0xE5
    je   .sk
    push cx
    push di
    mov  si, fname
    mov  cx, 11
    repe cmpsb
    pop  di
    pop  cx
    jz   .found
.sk:
    add  di, 32
    loop .s
    jmp  halt

.found:
    mov  ax, [di+26]    ; primo cluster di LOADER.BIN
    mov  bx, 0x0500     ; buffer destinazione (deve coincidere con ORG di
                         ; stage2 e con il jmp 0x0000:0x0500 piu' sotto)

.lp:
    cmp  ax, 0xFF8
    jae  .done
    cmp  ax, 2
    jb   .done
    push ax
    sub  ax, 2
    add  ax, 33          ; LBA = (cluster-2) + 33  (data area FAT12 1.44MB)
    mov  cx, 1
    call rsect           ; rsect (pusha/popa) ripristina BX al ritorno
    add  bx, 512         ; quindi il main loop avanza BX di 512 ogni cluster
    pop  ax
    ; Prossimo cluster dalla FAT a 0xA000 (formula FAT12: offset = c + c/2)
    mov  si, ax
    mov  cx, ax
    shr  cx, 1
    add  si, cx
    add  si, 0xA000
    mov  cx, [si]
    test ax, 1
    jz   .ev
    shr  cx, 4
    jmp  .nx
.ev:
    and  cx, 0x0FFF
.nx:
    mov  ax, cx
    jmp  .lp

.done:
    ; ---- Init COM1 (0x3F8, 115200 8N1, FIFO, RTS/DTR) per Stage 2 -------
    mov  dx, 0x3F9
    xor  al, al
    out  dx, al          ; IER = 0
    mov  dx, 0x3FB
    mov  al, 0x80
    out  dx, al          ; DLAB = 1
    mov  dx, 0x3F8
    mov  al, 1
    out  dx, al          ; divisor low  -> 115200 baud
    mov  dx, 0x3F9
    xor  al, al
    out  dx, al          ; divisor high
    mov  dx, 0x3FB
    mov  al, 3
    out  dx, al          ; 8N1, DLAB = 0
    mov  dx, 0x3FA
    mov  al, 0xC7
    out  dx, al          ; FIFO enable, clear, 14-byte threshold
    mov  dx, 0x3FC
    mov  al, 0x0B
    out  dx, al          ; RTS, DTR, OUT2

    mov  dl, [drv]
    jmp  0x0000:0x0500   ; salta a Stage 2

; ---------------------------------------------------------------------------
; rsect: legge CX settori da LBA AX a ES:BX (CHS via INT 13h/AH=02)
; pusha/popa: tutti i registri (incluso BX) sono ripristinati al ritorno.
; ---------------------------------------------------------------------------
rsect:
    pusha
.rl:
    push cx
    push ax
    xor  dx, dx
    mov  cx, 18
    div  cx              ; AX = LBA/18 (traccia), DX = LBA%18
    inc  dx              ; DX = settore (1-based)
    mov  si, dx          ; salva il settore in SI (la prossima div lo
                         ; sovrascriverebbe se restasse in DX/CX)
    xor  dx, dx
    mov  cx, 2
    div  cx              ; AX = cilindro, DX = testina
    mov  ch, al          ; CH = cilindro
    mov  dh, dl          ; DH = testina
    mov  ax, si          ; AX = settore (salvato)
    mov  cl, al          ; CL = settore (1-based)
    mov  ah, 0x02
    mov  al, 1
    mov  dl, [drv]
    int  0x13
    jc   .err
    add  bx, 512
    pop  ax
    inc  ax
    pop  cx
    loop .rl
    popa
    ret
.err:
    pop  ax
    pop  cx
    popa
halt:
    cli
    hlt
    jmp  halt

fname  db 'LOADER  BIN'
drv    db 0

times 510-($ - $$) db 0
dw 0xAA55
