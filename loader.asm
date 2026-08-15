; =============================================================================
; bootloader/stage2/loader.asm — EX-OS Stage 2 Loader (flat binary, ORG 0x0500)
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
; SPDX-License-Identifier: GPL-2.0-or-later
;
; Compilato con `nasm -f bin` (NON con gcc): e' codice 16-bit puro che gira
; in Real Mode subito dopo il far jump di Stage 1 a 0x0000:0x0500.
;
; Markers seriali (COM1, gia' inizializzata da Stage 1) per debug:
;   S = Stage2 avviato
;   F = KERNEL.BIN trovato in root dir
;   K = kernel caricato in RAM a 0x10000
;   P = GDT pronta, sto entrare in Protected Mode
;   J = in PM32, segmenti flat impostati, sto per saltare al kernel a 0x100000
;
; Layout memoria usato:
;   0x7E00          root dir   (caricata da Stage1)
;   0xA000          FAT1       (caricata da Stage1)
;   0x10000         kernel.bin caricato qui via INT 13h (segmento 0x1000)
;   0xC000          BootInfo struct
;   0x0A00          GDT (3 descrittori: null, data flat, code flat)
;   0x100000        destinazione finale del kernel (copiata in Real Mode
;                    con il trucco A20 ES=0xFFFF/DI=0x10)
; =============================================================================
[BITS 16]
[ORG 0x0500]

%define FAT    0xA000   ; FAT caricata da stage1
%define BINFO  0xC000   ; BootInfo struct

_start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7000
    sti
    mov  [drv], dl

    ; ---- 'S' = Stage2 avviato (COM1 gia' inizializzata da stage1) --------
    mov  dx, 0x3FD
.ws: in   al, dx
    test al, 0x20
    jz   .ws
    mov  dx, 0x3F8
    mov  al, 83
    out  dx, al

    ; ---- Cerca KERNEL.BIN nella root dir (0x7E00, 224 entry * 32) --------
    cld                  ; repe cmpsb richiede DF=0 (incremento SI/DI)
    mov  di, 0x7E00
    mov  cx, 224
.find:
    mov  al, [di]
    test al, al
    jz   .halt
    cmp  al, 0xE5
    je   .skip
    mov  al, [di+11]
    test al, 0x18        ; salta volume-label (0x08) e directory (0x10)
    jnz  .skip
    push cx
    push di
    mov  si, kname
    mov  cx, 11
    repe cmpsb
    pop  di
    pop  cx
    jz   .found
.skip:
    add  di, 32
    loop .find
.halt:
    cli
    hlt

.found:
    ; ---- 'F' = trovato -----------------------------------------------
    mov  dx, 0x3FD
.wf: in   al, dx
    test al, 0x20
    jz   .wf
    mov  dx, 0x3F8
    mov  al, 70
    out  dx, al

    mov  eax, [di+28]      ; dimensione file (32-bit)
    mov  [ksiz], eax
    mov  ax, [di+26]       ; primo cluster
    mov  word [kseg], 0x1000
    mov  word [koff], 0

    ; ---- Carica catena cluster -> 0x10000 (segmento kseg:koff) --------
.kload:
    cmp  ax, 0xFF8
    jae  .kdone
    cmp  ax, 2
    jb   .kdone
    push ax
    sub  ax, 2
    add  ax, 33            ; LBA = (cluster-2)+33
    push ax
    push bx
    push cx
    push dx
    push es
    xor  dx, dx
    mov  cx, 18
    div  cx
    inc  dx              ; DX = settore (1-based)
    mov  si, dx          ; salva il settore in SI (la prossima div lo
                         ; sovrascriverebbe se restasse in DX/CX)
    xor  dx, dx
    mov  cx, 2
    div  cx
    mov  ch, al          ; CH = cilindro
    mov  dh, dl          ; DH = testina
    mov  ax, si          ; AX = settore (salvato)
    mov  cl, al          ; CL = settore (1-based)
    mov  ax, [kseg]
    mov  es, ax
    mov  bx, [koff]
    mov  ax, 0x0201        ; AH=02 (read), AL=1 settore
    mov  dl, [drv]
    int  0x13
    pop  es
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    add  word [koff], 512
    jnc  .nw
    add  word [kseg], 0x20 ; avanza segmento di 512 byte quando koff overflow
.nw:
    pop  ax
    ; prossimo cluster dalla FAT a 0xA000
    mov  si, ax
    mov  cx, ax
    shr  cx, 1
    add  si, cx
    add  si, FAT
    mov  cx, [si]
    test ax, 1
    jz   .ev
    shr  cx, 4
    jmp  .gn
.ev:
    and  cx, 0x0FFF
.gn:
    mov  ax, cx
    jmp  .kload

.kdone:
    ; ---- 'K' = kernel caricato a 0x10000 -------------------------------
    mov  dx, 0x3FD
.wk: in   al, dx
    test al, 0x20
    jz   .wk
    mov  dx, 0x3F8
    mov  al, 75
    out  dx, al

    ; ---- Costruisci BootInfo a 0xC000 ----------------------------------
    ; layout (tutto little-endian):
    ;   +0  dword magic   = 'SOYM' (0x4D594F53)
    ;   +4  byte  boot_drive
    ;   +5  dword mem_lower (KB)
    ;   +9  dword mem_upper (KB)
    ;  +13  dword reserved0
    ;  +17  dword reserved1
    ;  +21  dword kernel_size
    mov  ax, BINFO >> 4
    mov  es, ax
    xor  di, di
    mov  dword [es:di+0],  0x4D594F53
    mov  al, [drv]
    mov  byte [es:di+4], al
    mov  dword [es:di+5],  640      ; mem_lower fisso a 640 KB
    mov  ah, 0x88                   ; INT 15h/88h -> mem_upper (KB, oltre 1MB)
    int  0x15
    jc   .memfb
    movzx eax, ax
    jmp  .memok
.memfb:
    mov  eax, 3072                  ; fallback: assume 32MB - 1MB = 31MB
.memok:
    mov  dword [es:di+9],  eax
    mov  dword [es:di+13], 0
    mov  dword [es:di+17], 0
    mov  eax, [ksiz]
    mov  dword [es:di+21], eax
    xor  ax, ax
    mov  es, ax

    ; ---- A20 fast gate --------------------------------------------------
    in   al, 0x92
    or   al, 2
    and  al, 0xFE
    out  0x92, al

    ; NOTA: la copia kernel 0x10000 -> 0x100000 e' stata SPOSTATA in
    ; pm32_entry sotto, dove si dispone di registri a 32 bit.
    ; Il vecchio metodo (rep movsw con DI=0x0010 in real mode) era
    ; limitato a ~57.8 KB per il wrapping a 16 bit di DI — superata
    ; quella soglia il kernel veniva troncato silenziosamente (schermo
    ; nero). In PM32 usiamo ESI/EDI/ECX a 32 bit e copiamo ksiz byte
    ; esatti senza alcun limite pratico (fino a ~600 KB di kernel).

    ; ---- GDT a 0x0A00: null / data flat (0x08) / code flat (0x10) -----
    cli
    xor  ax, ax
    mov  [0x0A00], ax
    mov  [0x0A02], ax
    mov  [0x0A04], ax
    mov  [0x0A06], ax
    mov  word  [0x0A08], 0xFFFF
    mov  word  [0x0A0A], 0x0000
    mov  word  [0x0A0C], 0x0000
    mov  byte  [0x0A0D], 0x92
    mov  byte  [0x0A0E], 0xCF
    mov  byte  [0x0A0F], 0x00
    mov  word  [0x0A10], 0xFFFF
    mov  word  [0x0A12], 0x0000
    mov  word  [0x0A14], 0x0000
    mov  byte  [0x0A15], 0x9A
    mov  byte  [0x0A16], 0xCF
    mov  byte  [0x0A17], 0x00
    mov  word  [0x0A1E], 23        ; GDT limit = 3*8-1
    mov  dword [0x0A20], 0x0A00    ; GDT base

    ; ---- 'P' = pronto per Protected Mode -------------------------------
    mov  dx, 0x3FD
.wp: in   al, dx
    test al, 0x20
    jz   .wp
    mov  dx, 0x3F8
    mov  al, 80
    out  dx, al

    ; ---- Maschera tutti gli IRQ hardware (master + slave PIC 8259) ------
    ; cli e' gia' attivo, ma un IRQ gia' pending nel PIC potrebbe attivarsi
    ; nella finestra fra CR0.PE=1 e il far jump. 0xFF = maschera tutti.
    mov  al, 0xFF
    out  0x21, al        ; maschera IRQ 0-7  (master PIC)
    out  0xA1, al        ; maschera IRQ 8-15 (slave PIC)

.pmloop:
    lgdt [0x0A1E]
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    db   0x66, 0xEA      ; far jmp dword ptr (operand size 32)
    dd   pm32_entry
    dw   0x10
    jmp  .pmloop         ; non dovrebbe mai arrivare qui

drv   db 0
ksiz  dd 0
kseg  dw 0x1000
koff  dw 0
kname db 'KERNEL  BIN'

; =============================================================================
; Padding esplicito: garantisce che [BITS 32] / pm32_entry inizi esattamente
; a file offset 512 = fisico 0x700 (inizio del cluster 2), evitando che una
; istruzione attraversi il confine 0x6FF/0x700 fra i due cluster caricati
; separatamente da Stage1.
; =============================================================================
times (0x200 - (($-$$) % 0x200)) % 0x200 db 0x90
; =============================================================================
; Codice 32-bit: setup segmenti flat, copia kernel, salto a 0x100000
; =============================================================================
[BITS 32]
pm32_entry:
    mov  ax, 0x08
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x9F000

    ; ---- Copia kernel 0x10000 -> 0x100000 in PM32 (32-bit, nessun limite 64KB)
    ; In PM32 i segmenti sono flat (base=0, limit=4GB): gli indirizzi fisici
    ; corrispondono direttamente agli indirizzi virtuali. Copiamo esattamente
    ; ksiz byte (dimensione reale del file KERNEL.BIN dalla directory FAT),
    ; arrotondati per eccesso a multiplo di 4 per usare movsd.
    mov  esi, 0x10000       ; sorgente: dove stage2 ha caricato il kernel
    mov  edi, 0x100000      ; destinazione: 1 MB fisico (inizio kernel)
    mov  ecx, [ksiz]        ; dimensione esatta del kernel (byte)
    add  ecx, 3
    shr  ecx, 2             ; arrotonda a dword: ecx = (ksiz+3)/4
    cld
    rep  movsd              ; copia ECX dword (ECX*4 byte)

    ; 'J' = in PM32, segmenti flat ok, kernel copiato, sto per saltare
    mov  edx, 0x3FD
.wj: in   al, dx
    test al, 0x20
    jz   .wj
    mov  edx, 0x3F8
    mov  al, 74
    out  dx, al

    jmp  dword 0x100000

.dead:
    cli
    hlt
    jmp  .dead
