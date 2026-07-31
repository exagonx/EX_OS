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

; -----------------------------------------------------------------------------
; Mappa di memoria E820, costruita qui e letta dal kernel (pmm_init).
;
; ATTENZIONE ALL'INDIRIZZO. stage2.h definisce E820_MAP_ADDR = 0x0A800, ma
; quel file appartiene al vecchio Stage2 in C (fat12.c/loader.c/print.c) che
; NON viene piu' compilato — vedi il Makefile. Quel valore e' sbagliato per
; il layout attuale: 0xA800 cade in mezzo alla FAT1, che stage1 carica a
; 0xA000 e che occupa 9*512 = 4608 byte fino a 0xB200. Usarlo significherebbe
; distruggere la FAT proprio mentre serve a seguire la catena del kernel.
;
; 0xD000 e' invece libero: BootInfo occupa 25 byte da 0xC000, il kernel viene
; caricato a 0x10000, e nulla fra i due e' usato. 32 entry * 20 byte = 640
; byte, quindi si arriva a 0xD280 — con ampio margine.
; -----------------------------------------------------------------------------
%define E820MAP 0xD000
%define E820SEG (E820MAP >> 4)
%define E820MAX 32      ; tetto sul numero di entry accettate

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

    ; =====================================================================
    ; AVVIO DA DISCO? — il bivio fra le due strade
    ;
    ; Il settore di avvio della partizione (bootloader/stage1hd/boothd.asm)
    ; e' ancora a 0x7C00 e contiene la mappa dei settori del kernel. Se
    ; c'e' la magia, si legge da li'.
    ;
    ; PERCHE' UN BIVIO E NON DUE STAGE 2 DIVERSI: la parte difficile —
    ; E820, A20, GDT, passaggio a modo protetto, copia a 0x100000 — e'
    ; identica nei due casi. Duplicarla vorrebbe dire correggere ogni bug
    ; futuro in due posti, e scoprire di averne corretto uno solo al
    ; prossimo avvio da floppy.
    ;
    ; Sul floppy la magia non c'e': a 0x7C00 c'e' il settore di avvio del
    ; floppy, che li' dentro ha altro. Il ramo vecchio resta intatto.
    ; =====================================================================
    cmp  dword [0x7C00 + 0x1A0], 0x44485845     ; 'EXHD'
    jne  .da_floppy

    ; ---- 'D' = avvio da disco ------------------------------------------
    mov  dx, 0x3FD
.wd: in   al, dx
    test al, 0x20
    jz   .wd
    mov  dx, 0x3F8
    mov  al, 68
    out  dx, al

    mov  eax, [0x7C00 + 0x1A0 + 10]   ; dimensione esatta del kernel
    mov  [ksiz], eax
    mov  word [dseg], 0x1000          ; destinazione: 0x10000, come da floppy

    ; --- La mappa del kernel e' una LISTA di intervalli ------------------
    ; Su ext2 il blocco di puntatori sta in mezzo ai dati, quindi un
    ; kernel e' spezzato in due o piu' tratti contigui. Vedi il commento
    ; esteso in bootloader/stage1hd/boothd.asm.
    ;
    ; Il contatore e il puntatore stanno in MEMORIA e non in registri:
    ; DX serve alla lettura (numero di unita' BIOS) e i pochi registri
    ; liberi a 16 bit finiscono subito.
    mov  ax, [0x7C00 + 0x1A0 + 14]    ; quanti intervalli
    mov  [n_ext], ax
    mov  word [p_ext], 0x7C00 + 0x1A0 + 16

.dnext:
    cmp  word [n_ext], 0
    jz   .kdone

    mov  si, [p_ext]
    mov  eax, [si]                    ; LBA assoluto dell'intervallo
    mov  cx,  [si + 4]                ; quanti settori
    add  word [p_ext], 6
    dec  word [n_ext]

.dload:
    test cx, cx
    jz   .dnext                       ; intervallo finito: al prossimo

    mov  [dap_lba], eax
    mov  bx, [dseg]
    mov  [dap_seg], bx

    push cx
    push eax
    mov  ah, 0x42
    mov  dl, [drv]
    mov  si, dap
    int  0x13
    pop  eax
    pop  cx
    jc   .halt

    inc  eax
    ; 512 byte = 0x20 paragrafi. Si avanza il SEGMENTO e si tiene l'offset
    ; a zero: cosi' non esiste il caso di overflow dell'offset a 16 bit,
    ; che sul ramo floppy e' gia' costato un bug (vedi il FIX piu' sotto).
    add  word [dseg], 0x20
    dec  cx
    jmp  .dload

.da_floppy:
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
    ; FIX: quando koff (offset 16-bit) va in overflow, l'indirizzo lineare
    ; è avanzato di un intero segmento di 64KB (65536 byte). Il segmento
    ; kseg deve quindi avanzare di 65536/16 = 0x1000, non di 0x20 (che
    ; corrisponde a soli 512 byte). Con 0x20 qualunque kernel.bin più
    ; grande di 64KB veniva scritto in indirizzi sbagliati oltre il primo
    ; wrap, corrompendo silenziosamente la coda del file.
    add  word [kseg], 0x1000
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

    ; ---- Mappa di memoria E820 -> 0xD000 --------------------------------
    ; INT 15h AX=E820 e' l'unico modo di sapere quanta RAM c'e' davvero.
    ; Il ripiego storico AH=88h resta piu' sotto, ma restituisce i KB di
    ; memoria estesa in AX — un registro a 16 bit — quindi ha un tetto
    ; strutturale a 65535 KB: su una macchina da 192 MB ne dichiarava 64.
    ;
    ; Si chiedono 20 byte per entry (ECX=20), non 24: e' esattamente la
    ; dimensione di E820Entry nel kernel, cosi' la tabella si legge come un
    ; array senza conversioni. I BIOS che scrivono comunque 24 byte non
    ; fanno danno — i 4 byte in piu' (attributi ACPI 3.0) finiscono
    ; nell'entry successiva, che viene riscritta al giro dopo.
    ;
    ; EBX e' l'indice di continuazione: parte da 0 e torna a 0 sull'ultima
    ; entry. CF alzato al PRIMO giro significa "E820 non supportato": in tal
    ; caso il contatore resta 0 e pmm_init usa da solo il ripiego.
    mov  ax, E820SEG
    mov  es, ax
    xor  di, di
    xor  ebx, ebx
    xor  bp, bp                 ; BP = entry raccolte (libero in stage2)
.e820lp:
    mov  eax, 0x0000E820
    mov  edx, 0x534D4150        ; 'SMAP'
    mov  ecx, 20
    int  0x15
    jc   .e820end               ; fine lista (o non supportato al 1o giro)
    cmp  eax, 0x534D4150        ; il BIOS deve rispondere 'SMAP'
    jne  .e820end
    jcxz .e820nx                ; zero byte scritti: entry da ignorare
    inc  bp
    add  di, 20
.e820nx:
    test ebx, ebx               ; EBX=0 -> era l'ultima
    jz   .e820end
    cmp  bp, E820MAX
    jb   .e820lp
.e820end:
    mov  [e8cnt], bp
    xor  ax, ax
    mov  es, ax

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
    ; +13 e +17 non sono piu' "reserved": sono e820_count ed e820_addr,
    ; come nella BootInfo di kernel/include/kernel.h. Se il conteggio e' 0
    ; pmm_init ignora l'indirizzo e ricade su mem_lower+mem_upper.
    movzx eax, word [e8cnt]
    mov  dword [es:di+13], eax
    mov  dword [es:di+17], E820MAP
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
e8cnt dw 0
ksiz  dd 0
kseg  dw 0x1000
koff  dw 0
; --- Disk Address Packet per l'avvio da disco (INT 13h/42h) ---
dap:
    db   0x10
    db   0
    dw   1
    dw   0                ; offset: sempre 0, si avanza il segmento
dap_seg:
    dw   0x1000
dap_lba:
    dd   0
    dd   0
dseg  dw 0x1000

; --- Percorrenza della lista di intervalli del kernel (avvio da disco) ---
; In memoria e non in registri: DX serve alla lettura BIOS e a 16 bit i
; registri liberi finiscono subito.
n_ext dw 0                ; intervalli ancora da leggere
p_ext dw 0                ; puntatore al prossimo, dentro l'area di patch

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

; =============================================================================
; LIMITE DI DIMENSIONE — non rimuovere questo controllo
;
; Stage2 viene caricato a 0x0500 e la GDT viene costruita a 0x0A00: ci sono
; quindi 0x0A00-0x0500 = 1280 byte in tutto. Oltre quella soglia il codice
; stesso finirebbe sotto la GDT, che lo sovrascriverebbe subito prima di
; entrare in Protected Mode — un guasto che si manifesterebbe come un salto
; nel vuoto, senza alcun messaggio, e solo sulla macchina vera.
;
; L'aggiunta della mappa E820 ha portato il file da 583 a 1095 byte (da 2 a
; 3 settori, che stage1 carica comunque perche' segue la catena FAT). Il
; margine residuo e' di ~185 byte: se serve piu' spazio, spostare la GDT
; piu' in alto (e' referenziata solo qui dentro) invece di alzare questo
; limite.
; =============================================================================
%if ($ - $$) > (0x0A00 - 0x0500)
  %error "Stage2 supera 1280 byte: il codice invaderebbe la GDT a 0x0A00"
%endif
