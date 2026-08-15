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
; LA GDT NON STA PIU' A 0x0A00, e il motivo e' scritto anche in fondo al file.
;
; Stage2 vive a 0x0500. Con la GDT a 0x0A00 il codice aveva 1280 byte in tutto,
; e ne usava gia' 1095: il sondaggio VESA non ci stava. Spostarla e' esattamente
; il rimedio che il commento sul limite indicava, ed e' senza conseguenze —
; questo indirizzo lo conosce solo questo file, che la costruisce e la carica.
;
; 0xE400 e' libero: la mappa E820 finisce a 0xD280 e il kernel comincia a
; 0x10000. Sotto ci mettiamo anche i due blocchi che il BIOS riempie per VBE.
; -----------------------------------------------------------------------------
%define VBEINFO 0xE000  ; VbeInfoBlock,  512 byte  -> 0xE200
%define VBEMODE 0xE200  ; ModeInfoBlock, 256 byte  -> 0xE300
%define GDTB    0xE400  ; GDT: 3 descrittori + puntatore

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
    ; ! I CAMPI DEL FRAMEBUFFER SI AZZERANO QUI, prima di provarci. Cosi'
    ; ogni via d'uscita del sondaggio VESA — compresa quella che non lo
    ; comincia nemmeno — lascia fb_addr = 0, che per il kernel vuol dire
    ; "modo testo". Riempirli solo in caso di successo e non azzerarli
    ; qui darebbe al kernel la spazzatura che c'era a 0xC019.
    mov  dword [es:di+25], 0      ; fb_addr
    mov  dword [es:di+29], 0      ; fb_pitch
    mov  word  [es:di+33], 0      ; fb_width
    mov  word  [es:di+35], 0      ; fb_height
    mov  byte  [es:di+37], 0      ; fb_bpp
    xor  ax, ax
    mov  es, ax

    ; =====================================================================
    ; MODALITA' GRAFICA VESA — l'unico posto da cui si puo' fare
    ;
    ; Impostare una modalita' VBE vuol dire INT 10h, cioe' il BIOS, cioe'
    ; il MODO REALE. Da qui in avanti non c'e' piu' occasione: sotto si
    ; entra in modo protetto e non si torna. Il kernel, che pure sa quale
    ; risoluzione l'utente vuole, non potrebbe impostarla nemmeno volendo.
    ;
    ; ! QUALE MODALITA' LO DICE UN BYTE DENTRO QUESTO BINARIO, non un
    ; file. Stage2 non ha un lettore FAT: riceve da stage1 una mappa di
    ; settori gia' pronta per KERNEL.BIN, e da disco rigido nemmeno
    ; quella — c'e' la mappa che scrive l'installatore. Leggere
    ; /boot/kernel.cfg da qui vorrebbe dire un lettore FAT12 in
    ; assembly che funzionerebbe da floppy e non da disco.
    ;
    ; Il byte lo scrive /bin/svga, che lo trova cercando la firma
    ; 'SVGAMODE' dentro LOADER.BIN o STAGE2.BIN. E' lo stesso patto della
    ; mappa di settori: il programma che gira DENTRO EX-OS, e che il
    ; filesystem ce l'ha, prepara qui cio' che serve a chi si avvia.
    ;
    ; ! OGNI FALLIMENTO RIPIEGA SUL TESTO. Scheda senza VBE, modalita'
    ; non offerta, INT 10h che risponde male: si esce di qui con
    ; fb_addr = 0 e il kernel accende la console di testo di sempre. Uno
    ; schermo nero sarebbe il modo peggiore di dire "non ce la faccio",
    ; perche' e' anche il modo in cui si presenta un kernel che non parte.
    ; =====================================================================
    mov  al, [svgamodo]
    or   al, al
    jz   .novesa                  ; 0 = modo testo: non si tocca niente
    cmp  al, 3
    ja   .novesa                  ; valore fuori tabella: idem

    ; Risoluzione voluta, dalla tabella (1=640x480 2=800x600 3=1024x768)
    movzx bx, al
    dec  bx
    shl  bx, 2
    mov  ax, [svgatab + bx]
    mov  [vwant], ax
    mov  ax, [svgatab + bx + 2]
    mov  [hwant], ax

    ; ---- VbeInfoBlock: c'e' il VBE su questa scheda? --------------------
    mov  ax, VBEINFO >> 4
    mov  es, ax
    xor  di, di
    mov  dword [es:di], 'VBE2'    ; chiede la struttura estesa (VBE 2.0)
    mov  ax, 0x4F00
    int  0x10
    cmp  ax, 0x004F
    jne  .novesa
    cmp  dword [es:0], 'VESA'
    jne  .novesa

    ; +14 puntatore FAR alla lista dei modi, terminata da 0xFFFF
    mov  ax, [es:16]
    mov  [mlseg], ax
    mov  ax, [es:14]
    mov  [mloff], ax

.mloop:
    push ds
    mov  ds, [mlseg]
    mov  si, [cs:mloff]
    mov  cx, [si]
    pop  ds
    cmp  cx, 0xFFFF
    je   .fine_lista              ; lista finita: si usa il migliore trovato
    add  word [mloff], 2
    mov  [mcur], cx

    ; ---- ModeInfoBlock di questo modo ----------------------------------
    mov  ax, VBEMODE >> 4
    mov  es, ax
    xor  di, di
    mov  ax, 0x4F01
    int  0x10
    cmp  ax, 0x004F
    jne  .mloop

    ; +0 attributi: bit0 supportato, bit4 grafico, bit7 framebuffer lineare
    ;
    ; ! IL BIT 7 NON E' UN DI PIU'. Senza framebuffer lineare la memoria
    ; video si raggiunge solo a finestre di 64 KB da commutare a ogni
    ; banco, e il kernel dovrebbe cambiare banco per disegnare: qui si
    ; scartano quei modi e basta.
    mov  ax, [es:0]
    test ax, 0x0001
    jz   .mloop
    test ax, 0x0010
    jz   .mloop
    test ax, 0x0080
    jz   .mloop

    mov  ax, [es:0x12]            ; XResolution
    cmp  ax, [vwant]
    jne  .mloop
    mov  ax, [es:0x14]            ; YResolution
    cmp  ax, [hwant]
    jne  .mloop

    ; ! NON SI PRENDE IL PRIMO CHE COMBACIA, si tiene il migliore.
    ; La stessa risoluzione viene offerta a piu' profondita' di colore, e
    ; nella lista di QEMU quella a 16 bpp viene prima: prendendo il primo
    ; ci si ritrovava a 800x600x16 con un 32 bpp disponibile due voci piu'
    ; in la'. A 32 bpp un pixel e' una scrittura sola; a 16 va impacchettato
    ; in 5-6-5 e a 24 sono tre byte a cavallo delle parole. Meno codice nel
    ; kernel, e piu' veloce, sulla via che si percorre sempre.
    mov  al, [es:0x19]            ; BitsPerPixel
    mov  ah, [fbbpp]              ; il migliore trovato finora (0 = nessuno)
    cmp  al, 32
    je   .cand
    cmp  al, 24
    je   .cand
    cmp  al, 16
    jne  .mloop
.cand:
    cmp  al, ah                   ; piu' bit = meglio
    jbe  .mloop
    mov  [fbbpp], al
    mov  eax, [es:0x28]           ; PhysBasePtr
    mov  [fbaddr], eax
    movzx eax, word [es:0x10]     ; BytesPerScanLine
    mov  [fbpitch], eax
    mov  ax, [es:0x12]
    mov  [fbw], ax
    mov  ax, [es:0x14]
    mov  [fbh], ax
    mov  ax, [mcur]
    mov  [mbest], ax
    cmp  byte [fbbpp], 32
    jne  .mloop                   ; meglio di 32 non c'e': si smette di cercare

.fine_lista:
    cmp  byte [fbbpp], 0
    je   .novesa                  ; nessun modo utilizzabile: resta il testo
    mov  ax, [mbest]
    mov  [mcur], ax

    ; ---- Imposta: da qui lo schermo di testo del BIOS non c'e' piu' -----
    ; Sotto si scrive solo sulla porta seriale, quindi va bene cosi'.
    mov  bx, [mcur]
    or   bx, 0x4000               ; bit 14 = usa il framebuffer lineare
    mov  ax, 0x4F02
    int  0x10
    cmp  ax, 0x004F
    jne  .novesa

    ; ---- Consegna al kernel, in coda a BootInfo ------------------------
    mov  ax, BINFO >> 4
    mov  es, ax
    xor  di, di
    mov  eax, [fbaddr]
    mov  dword [es:di+25], eax
    mov  eax, [fbpitch]
    mov  dword [es:di+29], eax
    mov  ax, [fbw]
    mov  word [es:di+33], ax
    mov  ax, [fbh]
    mov  word [es:di+35], ax
    mov  al, [fbbpp]
    mov  byte [es:di+37], al

.novesa:
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

    ; ---- GDT a GDTB: null / data flat (0x08) / code flat (0x10) -------
    cli
    xor  ax, ax
    mov  [GDTB+0x00], ax
    mov  [GDTB+0x02], ax
    mov  [GDTB+0x04], ax
    mov  [GDTB+0x06], ax
    mov  word  [GDTB+0x08], 0xFFFF
    mov  word  [GDTB+0x0A], 0x0000
    mov  word  [GDTB+0x0C], 0x0000
    mov  byte  [GDTB+0x0D], 0x92
    mov  byte  [GDTB+0x0E], 0xCF
    mov  byte  [GDTB+0x0F], 0x00
    mov  word  [GDTB+0x10], 0xFFFF
    mov  word  [GDTB+0x12], 0x0000
    mov  word  [GDTB+0x14], 0x0000
    mov  byte  [GDTB+0x15], 0x9A
    mov  byte  [GDTB+0x16], 0xCF
    mov  byte  [GDTB+0x17], 0x00
    mov  word  [GDTB+0x1E], 23        ; GDT limit = 3*8-1
    mov  dword [GDTB+0x20], GDTB    ; GDT base

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
    lgdt [GDTB+0x1E]
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    db   0x66, 0xEA      ; far jmp dword ptr (operand size 32)
    dd   pm32_entry
    dw   0x10
    jmp  .pmloop         ; non dovrebbe mai arrivare qui

; -----------------------------------------------------------------------------
; La modalita' grafica voluta, e la firma con cui /bin/svga la trova.
;
; ! LA FIRMA NON E' DECORAZIONE: e' l'unico modo che ha il comando di
; sapere DOVE scrivere. Un offset fisso dentro il binario cambierebbe a
; ogni riga aggiunta qui sopra, e il comando finirebbe a scrivere in mezzo
; al codice — su un file che serve ad avviare la macchina.
svgamagic db 'SVGAMODE'
; ! IL PREDEFINITO RESTA 0 = TESTO, e si sceglie a costruzione con
; `make SVGA=800x600`. Cambiare il predefinito vorrebbe dire che chiunque
; costruisce EX-OS si ritrova in grafica senza averlo chiesto — e chi lavora
; sulla seriale non se ne accorgerebbe nemmeno.
;
; ! SERVE PERCHE' L'IMPOSTAZIONE VIVE DENTRO L'IMMAGINE, NON NEL REPOSITORY:
; la scrive /dev/svga.drv dentro LOADER.BIN, quindi ogni floppy ricostruito
; ripartiva in testo e il server grafico moriva dicendo «lo schermo e' in modo
; TESTO». Due volte in un giorno.
%ifndef SVGAMODO
%define SVGAMODO 0
%endif
svgamodo  db SVGAMODO ; 0=testo 1=640x480 2=800x600 3=1024x768
svgatab   dw 640, 480
          dw 800, 600
          dw 1024, 768

vwant dw 0
hwant dw 0
mlseg dw 0
mloff dw 0
mcur  dw 0
mbest dw 0
fbaddr  dd 0
fbpitch dd 0
fbw   dw 0
fbh   dw 0
fbbpp db 0

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
; Stage2 viene caricato a 0x0500. Il primo indirizzo occupato che segue e'
; 0x7C00: li' c'e' ancora il settore di stage1, e non e' un residuo — a
; 0x7C00+0x1A0 c'e' la mappa dei settori del kernel, che questo file legge.
; Sovrascriverla vorrebbe dire caricare il kernel da settori a caso.
;
; STORIA DI QUESTO LIMITE, che vale la pena avere sott'occhio:
;
;   fino ad agosto 2026 la soglia era 0x0A00, perche' li' si costruiva la
;   GDT. Con 1095 byte gia' occupati restavano ~185 byte, e il sondaggio
;   VESA non ci stava. La GDT e' stata spostata a GDTB (0xE400, vedi in
;   testa al file): la conosce solo questo file, che la scrive e la carica,
;   quindi spostarla non tocca nient'altro — ed era il rimedio che questo
;   stesso commento indicava.
;
; Il margine ora e' di ~28 KB. Se un giorno servisse superarlo, si sposta
; PIU' IN ALTO il caricamento del kernel (0x10000), non questo limite.
; =============================================================================
%if ($ - $$) > (0x7C00 - 0x0500)
  %error "Stage2 supera 0x7700 byte: il codice invaderebbe la mappa dei settori a 0x7C00"
%endif
