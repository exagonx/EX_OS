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
; The modes the card offers, for the kernel and the Video panel (17 October
; 2026, @RISOLUZIONI): up to MODI_MAX entries of 8 bytes - width, height
; (words), bits per pixel, 0, the VBE mode number - after the E820 map
; (which ends at 0xD280). BootInfo +46 says where, +50 how many.
%define MODITAB 0xD400
%define MODI_MAX 48
; Which card and which screen (22 October 2026, @SCHEDA-VIDEO): the strings
; the video BIOS gives of itself and the screen's EDID, for svga.drv -lista
; and the Video panel. 304 bytes after the modes (0xD400 + 48*8 = 0xD580);
; BootInfo +54 says where. Layout in kernel/include/kernel.h (VidInfoBoot).
%define VIDINFO 0xD600
%define VIDINFO_N 304

; --- Il disco in RAM -------------------------------------------------------
; Dove finisce il volume, quanto e' grande, e da dove si passa.
; ! 12 MB E NON PIU' 32 (@RDBOOT, 22 ottobre 2026): una macchina da 16 MB deve
; partire. Sotto stanno il sistema di base compresso (RBDEST, 10 MB) e quello
; espanso dal kernel (RD_ESPANSO in kernel/include/rdlz4.h, 6 MB).
%define RDDEST  0x00C00000  ; 12 MB, al piu' 2,88 MB: fino a 15 MB
%define RBDEST  0x00A00000  ; 10 MB: rdboot.lz4 cosi' com'e'
%define RBMAX   (RDDEST - RBDEST)   ; 2 MB: oltre toccherebbe il volume intero
%define RDM_MAX 64          ; le voci di rdmappa (RD_MAX_EXT in bootinst.c)
%define RDSEG   0x8000      ; il rimbalzo, a 512 KB: sotto c'e' il kernel
; La misura del dischetto, come in bootloader/stage1/boot.asm (-DDISCHETTO=288
; per il 2,88 MB: 36 settori per traccia, l'area dei dati piu' avanti). Di
; serie e' 1,44 MB e niente cambia.
%ifndef DISCHETTO
%define DISCHETTO 144
%endif
%if DISCHETTO = 288
%define F_SPT   36
%define F_DATA  39
%else
%define F_SPT   18
%define F_DATA  33
%endif
; ! G_SPT / G_TESTE: the geometry reads are done with. As a floppy they are the
; floppy's constants; with GEOMETRIA_BIOS (the USB key images) they are what
; the BIOS says when it boots the key as a HARD DISK - see geometria below.
%ifdef GEOMETRIA_BIOS
%define G_SPT   [g_spt]
%define G_TESTE [g_teste]
%else
%define G_SPT   F_SPT
%define G_TESTE 2
%endif
%define RDTRACK 160         ; 80 cilindri x 2 testine
%define RDSPT   F_SPT       ; settori per traccia
%define RDBYTE  (RDTRACK * RDSPT * 512)

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
%ifdef GEOMETRIA_BIOS
    call geometria
%endif

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
    mov  byte [da_disco], 1           ; per rdboot_carica, piu' sotto
%ifdef GEOMETRIA_BIOS
    ; Booted from an installed disk (its partition boot sector carries
    ; 'EXHD'): there is nothing to copy into RAM - see ramdisco.
    mov  byte [avvio_hd], 1
%endif

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
    ;
    ; ! SI LEGGE A TRATTI, NON A SETTORI, dal 10 settembre 2026, e su un
    ; lettore USB e' la differenza fra minuti e secondi.
    ;
    ; Un cluster di un floppy e' UN settore: la prima stesura faceva una
    ; INT 13h per ognuno, cioe' oltre cinquecento chiamate per un kernel di
    ; 260 KB. Su un controller vero non si sente; dietro l'emulazione di un
    ; floppy USB ogni chiamata e' un giro completo sul bus, e il risultato e'
    ; un minuto abbondante di motore che gira senza che la testina si muova —
    ; cioe' una macchina che sembra piantata mentre sta lavorando.
    ;
    ; La catena FAT sta gia' tutta in memoria a 0xA000, quindi guardare avanti
    ; non costa niente: si contano i cluster CONSECUTIVI e si leggono in un
    ; colpo. Un file appena scritto e' quasi sempre contiguo, quindi in pratica
    ; si legge una traccia per volta.
    ;
    ; ! TRE LIMITI, E NESSUNO E' FACOLTATIVO:
    ;   - non oltre la fine della TRACCIA: una lettura CHS non la attraversa;
    ;   - non oltre i 64 KB del segmento di destinazione, o l'offset a 16 bit
    ;     ricomincerebbe da capo in mezzo al trasferimento;
    ;   - non piu' di 18 settori, che e' una traccia intera.
.kload:
    cmp  ax, 0xFF8
    jae  .kdone
    cmp  ax, 2
    jb   .kdone

    ; ---- quanti cluster consecutivi seguono ----------------------------
    mov  [kprimo], ax
    mov  [kult], ax
    mov  word [kcnt], 1
.krun:
    cmp  word [kcnt], F_SPT
    jae  .krun_fine
    mov  ax, [kult]
    call fat_next               ; AX = il cluster dopo [kult]
    mov  bx, [kult]
    inc  bx
    cmp  ax, bx                 ; e' proprio quello subito dopo?
    jne  .krun_fine
    mov  [kult], ax
    inc  word [kcnt]
    jmp  .krun
.krun_fine:

    ; ---- LBA del primo settore, e CHS ----------------------------------
    mov  ax, [kprimo]
    sub  ax, 2
    add  ax, F_DATA               ; LBA = (cluster - 2) + 33
    xor  dx, dx
    mov  cx, G_SPT
    div  cx                     ; AX = lba/18, DX = settore 0-based
    mov  [ksett], dx            ; da qui in poi serve solo il resto
    xor  dx, dx
    mov  cx, G_TESTE
    div  cx                     ; AX = cilindro, DX = testina
    mov  [kcil], ax
    mov  [ktesta], dx

    ; ---- primo limite: la fine della traccia ---------------------------
    mov  ax, G_SPT
    sub  ax, [ksett]
    cmp  ax, [kcnt]
    jae  .klim2
    mov  [kcnt], ax
.klim2:
    ; ---- secondo limite: i 64 KB del segmento --------------------------
    ; (0x10000 - koff) / 512, con koff = 0 che vuol dire «ci sta tutto»
    mov  ax, [koff]
    or   ax, ax
    jz   .kleggi                ; segmento appena cominciato: nessun limite
    neg  ax                     ; 0x10000 - koff, in aritmetica a 16 bit
    shr  ax, 9
    cmp  ax, [kcnt]
    jae  .kleggi
    mov  [kcnt], ax

.kleggi:
%ifdef GEOMETRIA_BIOS
    cmp  byte [usa_lba], 0
    je   .kchs
    push bx
    push es
    mov  ax, [kseg]
    mov  es, ax
    mov  bx, [koff]
    mov  ax, [kprimo]
    sub  ax, 2
    add  ax, F_DATA
    mov  [l42_lba], ax
    mov  al, byte [kcnt]
    call leggi42
    pop  es
    pop  bx
    jc   .halt
    jmp  .kletto
.kchs:
%endif
    push bx
    push cx
    push dx
    push es
    mov  ax, [kseg]
    mov  es, ax
    mov  bx, [koff]
    mov  ch, byte [kcil]
    mov  cl, byte [ksett]
    inc  cl                     ; i settori si contano da uno
    mov  dh, byte [ktesta]
    mov  dl, [drv]
    mov  ah, 0x02
    mov  al, byte [kcnt]
    int  0x13
    pop  es
    pop  dx
    pop  cx
    pop  bx
    jc   .halt
.kletto:

    ; ---- avanza destinazione e catena ----------------------------------
    mov  ax, [kcnt]
    shl  ax, 9                  ; byte letti = settori * 512
    add  [koff], ax
    jnc  .nw2
    add  word [kseg], 0x1000
.nw2:
    ; ! SI AVANZA DI QUANTI SE NE SONO LETTI, NON FINO ALL'ULTIMO DEL TRATTO.
    ;
    ; Il tratto contato e' quello dei cluster CONSECUTIVI; quanti se ne leggono
    ; davvero lo decidono i limiti qui sopra, e possono essere meno. Ripartendo
    ; dal successore dell'ultimo CONTATO si saltavano i cluster in mezzo: con
    ; un tratto di 18 e un limite di traccia a 10, otto settori per giro non
    ; venivano mai letti. Il kernel si caricava «senza errori» e non partiva —
    ; Stage 2 arrivava a saltarci dentro e li' finiva tutto.
    mov  cx, [kcnt]
    mov  ax, [kprimo]
.kavanza:
    call fat_next
    loop .kavanza
    jmp  .kload

    ; --- la vecchia strada, un settore per volta, non si usa piu' -------
.kvecchio:
    push ax
    sub  ax, 2
    add  ax, F_DATA          ; LBA = (cluster-2)+33
    push ax
    push bx
    push cx
    push dx
    push es
    xor  dx, dx
    mov  cx, F_SPT
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
    mov  dword [es:di+38], 0      ; rd_addr: nessun disco in RAM
    mov  dword [es:di+42], 0      ; rd_byte
    mov  dword [es:di+46], MODITAB ; modi_addr: the table of modes
    mov  dword [es:di+50], 0      ; modi_n: none yet
    mov  dword [es:di+54], 0      ; vid_addr: nothing read yet
    mov  dword [es:di+58], 0      ; rb_firma: niente sistema di base (ancora)
    mov  dword [es:di+62], 0      ; rb_addr
    mov  dword [es:di+66], 0      ; rb_byte
    xor  ax, ax
    mov  es, ax

    ; ---- Il volume in RAM, se qualcuno l'ha chiesto --------------------
    call ramdisco

    ; ---- Il sistema di base, se il disco ne ha uno ----------------------
    call rdboot_carica

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
    ; Il byte lo scrive /dev/svga.drv, che lo trova cercando la firma
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

    ; ! IL BIT 7 DICE «PREFERISCI 16 BPP», e non e' una preferenza estetica:
    ; e' meta' dei byte da spostare a ogni scorrimento della console. Il
    ; perche' per esteso sta piu' sotto, accanto alla scelta della
    ; profondita'. I bit 0..1 restano la risoluzione, come sono sempre stati:
    ; qui si toglie di mezzo il bit 7 prima di guardare la tabella, o un
    ; 0x82 sembrerebbe «valore fuori tabella» e si finirebbe in testo.
    mov  ah, al
    and  ah, 0x80
    mov  [want16], ah
    and  al, 0x7F

    cmp  al, 4                    ; 4 = svgacust: any mode the card lists
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

    call vid_leggi                ; which card, which screen: see VIDINFO

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
    inc  word [vmodi]             ; quanti modi la scheda ha elencato

    mov  ax, [es:0]
    test ax, 0x0001
    jz   .mloop
    test ax, 0x0010
    jz   .mloop
    call modo_annota              ; every usable mode goes in the table

    ; ! LA RISOLUZIONE SI CONFRONTA PRIMA DEL FRAMEBUFFER LINEARE, e l'ordine
    ; conta solo per una ragione: sapere PERCHE' si e' rinunciato. Scartando
    ; per attributi prima di guardare la misura, «questa scheda non ha
    ; 800x600» e «ce l'ha ma solo a banchi» diventano lo stesso silenzio — e
    ; sono due problemi con due rimedi diversi.
    mov  ax, [es:0x12]            ; XResolution
    cmp  ax, [vwant]
    jne  .mloop
    mov  ax, [es:0x14]            ; YResolution
    cmp  ax, [hwant]
    jne  .mloop

    mov  byte [vres], 1           ; la misura c'e'

    mov  ax, [es:0]
    test ax, 0x0080               ; framebuffer lineare
    jz   .mloop

    mov  byte [vlfb], 1           ; ...e anche lineare

    ; ! NON SI PRENDE IL PRIMO CHE COMBACIA, si tiene il migliore.
    ; La stessa risoluzione viene offerta a piu' profondita' di colore, e
    ; nella lista di QEMU quella a 16 bpp viene prima: prendendo il primo
    ; ci si ritrovava a 800x600x16 con un 32 bpp disponibile due voci piu'
    ; in la'. A 32 bpp un pixel e' una scrittura sola; a 16 va impacchettato
    ; in 5-6-5 e a 24 sono tre byte a cavallo delle parole. Meno codice nel
    ; kernel, e piu' veloce, sulla via che si percorre sempre.
    ;
    ; ! E QUELLA MISURA GUARDAVA META' DEL LAVORO, ed e' la correzione del 14
    ; settembre 2026. «Un pixel e' una scrittura sola» conta il costo di
    ; DISEGNARE un carattere, che si fa una volta per cella; la via che si
    ; percorre davvero sempre e' lo SCORRIMENTO, che sposta lo schermo intero.
    ;
    ; A 800x600 sono 1,8 MB di memoria video per ogni riga scorsa — non cache,
    ; sul bus, su una macchina del 2004. A 16 bpp sono 900 KB: meta'. E anche
    ; disegnare costa meno, non di piu': due pixel adiacenti stanno in una
    ; scrittura da 32 bit, e impacchettarli e' uno spostamento e un or fra
    ; registri, contro una scrittura in memoria video che vale cento volte.
    ;
    ; ! MA NON SI SCEGLIE QUI PER TUTTI. Su una macchina veloce 32 bpp e' piu'
    ; fedele — 5-6-5 butta i bit bassi di ogni componente — e la lentezza non
    ; si sente. Chi ce l'ha lenta lo sa, e adesso puo' dirlo:
    ; `svga.drv 800x600 -16` accende il bit 7 del byte di modo. Il
    ; predefinito non cambia.
    mov  al, [es:0x19]            ; BitsPerPixel
    mov  ah, [fbbpp]              ; il migliore trovato finora (0 = nessuno)
    cmp  al, 32
    je   .cand
    cmp  al, 24
    je   .cand
    cmp  al, 16
    jne  .mloop
.cand:
    cmp  byte [want16], 0
    je   .piubit                  ; predefinito: piu' bit = meglio

    ; Con -16: si vuole il PIU' PICCOLO fra quelli buoni, e 16 e' il minimo
    ; che la console sa disegnare. Il primo 16 trovato e' quello giusto.
    cmp  al, 16
    jne  .mloop                   ; qualunque altra profondita' non interessa
    cmp  ah, 0
    jne  .mloop                   ; ne abbiamo gia' uno: basta
    jmp  .prendi
.piubit:
    cmp  al, ah                   ; piu' bit = meglio
    jbe  .mloop
.prendi:
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
    ; ! THE LIST IS READ TO ITS END (17 October 2026): it stopped at the
    ; first 32 bpp (or 16 with -16), and the table of modes for the Video
    ; panel came out cut. The choice is the same: with -16 only the first 16
    ; is taken (.cand), otherwise only a deeper one replaces it (.piubit).
    jmp  .mloop

.fine_lista:
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    movzx eax, word [nmodi]
    mov  dword [es:50], eax       ; modi_n
    pop  es
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

    ; ! LA GRAFICA E' PARTITA: NIENTE SPIEGAZIONE DEL PERCHE' NON E' PARTITA
    ; (8 ottobre 2026). Da qui si cadeva dritti nel pezzo qui sotto, che
    ; stampa la ragione della rinuncia ogni volta che la grafica era stata
    ; chiesta: e a modo impostato, con la misura trovata e lineare, la
    ; ragione che gli restava da dire era «c e ma non a 16, 24 o 32 bit».
    ; Su un PC vero quella riga restava in cima allo schermo grafico, a dire
    ; il falso su una grafica che funzionava. Il byte e' quello in memoria:
    ; quello nel file, che svga.drv legge, non si tocca.
    mov  byte [svgamodo], 0

.novesa:
    xor  ax, ax
    mov  es, ax

    ; ! SI DICE PERCHE', o «lo schermo e' rimasto in testo» resta un mistero.
    ; Le tre risposte hanno tre rimedi diversi: nessun VBE vuol dire che la
    ; scheda non lo offre affatto; nessuna misura vuol dire chiedere un'altra
    ; risoluzione; solo a banchi vuol dire che quella misura c'e' ma senza
    ; framebuffer lineare, e li' il rimedio e' un'altra risoluzione ancora.
    cmp  byte [svgamodo], 0
    je   .vfine                   ; nessuno aveva chiesto la grafica

    mov  si, msg_v1
    call print
    mov  ax, [vmodi]
    call numero
    mov  si, msg_v2
    call print

    cmp  byte [vres], 0
    jne  .vlfb
    mov  si, msg_vnores
    jmp  .vdimmi
.vlfb:
    cmp  byte [vlfb], 0
    jne  .vaddr
    mov  si, msg_vnolfb
    jmp  .vdimmi
.vaddr:
    mov  si, msg_vnobpp
.vdimmi:
    call print
.vfine:

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

; =============================================================================
; IL VOLUME IN RAM — l'unica strada per una macchina il cui lettore sta sull'USB
;
; ! IL PROBLEMA, IN UNA RIGA: il BIOS sa leggere un lettore USB, il kernel no.
; Stage 1 e Stage 2 lavorano con INT 13h e si caricano benissimo da un floppy o
; da un CD attaccati all'USB; poi il kernel passa in modo protetto e va a
; cercare quel supporto dove i supporti stanno da sempre — il controller
; dell'FDC, il bus IDE — e li' non c'e' niente. Errori di filesystem, nessuna
; shell, e una macchina che ha caricato tutto e non puo' fare niente.
;
; Qui il volume intero si copia in RAM FINCHE' IL BIOS E' ANCORA DISPONIBILE, e
; il kernel ci monta sopra la radice. Da quel momento non gli importa piu' di
; come sia fatto il lettore: qualunque cosa il BIOS sappia leggere diventa un
; sistema che si avvia.
;
; ! E' SPENTO DI SUO. Costa 1,44 MB di memoria e qualche secondo di
; caricamento, e su una macchina che il suo lettore ce l'ha per davvero non
; serve a niente. Si accende a costruzione, come la risoluzione: il byte lo
; trova la firma 'RAMDISCO' qui sotto.
;
; ! IL MODO REALE NON ARRIVA A 32 MB, e questa e' la parte che va spiegata.
; Un segmento reale vede 64 KB, e la destinazione sta a trentadue milioni di
; byte. Si usa il «modo reale grande»: si entra un istante in modo protetto,
; si carica ES con un descrittore che copre 4 GB, si esce — e la CPU tiene il
; limite nuovo nella cache del segmento anche in modo reale. Da li' `movsd`
; con indirizzi a 32 bit ci arriva.
;
; ! MA INT 13h VUOLE ES:BX, e ES ce l'ha grande. Percio' a ogni traccia si fa
; il giro due volte: ES normale per leggere nel rimbalzo a 512 KB, ES grande
; per copiare in alto. Sono centosessanta giri, e ognuno costa dieci
; istruzioni: meno di quanto costi la lettura stessa.
;
; ! E SE UNA TRACCIA NON SI LEGGE, IL DISCO IN RAM NON SI FA. Meglio un avvio
; che si ferma dicendo perche', che una radice con un buco in mezzo: quella
; darebbe file troncati e errori che sembrano di tutt'altro.
; =============================================================================
ramdisco:
    mov  al, [rdflag]
    or   al, al
    jz   .no                      ; 0 = non richiesto

    ; Da disco rigido non serve: quello il kernel lo legge da se'.
    ; (Con RAMDISCO=2 invece si': e' una chiavetta, vedi .disco piu' sotto.)
    mov  al, [drv]
    cmp  al, 0x80
%ifdef RAMDISCO
%if RAMDISCO = 2
    jae  .disco
%elifdef GEOMETRIA_BIOS
    ; ! A USB KEY BOOTED AS A HARD DISK IS STILL A FLOPPY IMAGE TO COPY: the
    ; kernel cannot read USB at boot, so "from a hard disk it is not needed"
    ; was exactly wrong here - the copy was skipped and the root was looked
    ; for on a device nobody could read (14 October 2026).
    ; ! BUT NOT FROM AN INSTALLED DISK: `install` puts this same Stage 2 on
    ; the disk, and there it copied the disk's first 2.88 MB into RAM (the
    ; kernel found no FAT in it and went on to the disk - seen on the MCP77
    ; laptop, the same evening). The 'EXHD' path sets avvio_hd.
    cmp  byte [avvio_hd], 0
    jne  .no
%else
    jae  .no
%endif
%else
    jae  .no
%endif

    ; ! LA MEMORIA SI CONTA PRIMA. Il volume va a 32 MB: su una macchina che
    ; ne ha meno si scriverebbe nel nulla, e il sintomo sarebbe una radice
    ; piena di zeri. mem_upper e' in KB oltre il primo MB.
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    mov  eax, [es:9]
    pop  es
    cmp  eax, 14 * 1024           ; finisce a 15 MB: ne servono 14 oltre il primo
    jb   .pocamem

    mov  si, msg_rd
    call print

    ; A20: senza, l'indirizzo 0x2000000 si ripiegherebbe su se stesso.
    in   al, 0x92
    or   al, 2
    and  al, 0xFE
    out  0x92, al

    call gdt_prepara

    mov  word [rdtrk], 0
    mov  dword [rddst], RDDEST

.giro:
    mov  ax, [rdtrk]
    cmp  ax, RDTRACK
    jae  .fatto

    ; cilindro = traccia / 2, testina = traccia & 1
    mov  bx, ax
    shr  ax, 1
    mov  ch, al                   ; CH = cilindro
    and  bl, 1
    mov  dh, bl                   ; DH = testina

    mov  cl, 1                    ; settore 1 (i settori partono da uno)
    mov  ax, RDSEG
    mov  es, ax
    xor  bx, bx
%ifdef GEOMETRIA_BIOS
    ; The floppy track becomes an LBA, read with the BIOS geometry: as a
    ; floppy it is one read as before, as a hard disk it is split where the
    ; BIOS's tracks end.
    mov  al, '.'
    call carattere
    mov  ax, [rdtrk]
    mov  dx, RDSPT
    mul  dx
    mov  [rdlba], ax
    call leggi_lba
    jc   .guasto
    xor  bx, bx
    jmp  .letta
%endif

    ; ! UN PUNTO PER TRACCIA, e non e' decorazione. Su un lettore USB la
    ; lettura del volume dura decine di secondi: senza un segno, lo schermo
    ; resta fermo con un cursore che lampeggia, ed e' indistinguibile da una
    ; macchina piantata. Chi guarda deve poter dire «sta andando avanti» o
    ; «e' fermo», e la differenza sono centosessanta puntini.
    mov  al, '.'
    call carattere

    ; --- prima si prova la traccia intera ------------------------------
    mov  al, RDSPT
    call leggi_tratto
    jnc  .letta

    ; --- ripiego: un settore per volta ---------------------------------
    ;
    ; ! NON TUTTI I BIOS LEGGONO DICIOTTO SETTORI IN UN COLPO da un floppy
    ; EMULATO sull'USB. Quando non ce la fanno rispondono errore, e con i soli
    ; tentativi ripetuti si perde un secondo per traccia in reset del
    ; controller — cioe' minuti buoni, con lo schermo fermo. Sceso a un settore
    ; per volta il giro e' piu' lungo ma FUNZIONA, ed e' meglio di un avvio che
    ; non arriva. La 's' a schermo dice che si e' passati di qui.
    mov  al, 's'
    call carattere

    mov  byte [rdsett], 1         ; settore corrente, 1..18
.uno:
    mov  cl, [rdsett]
    mov  al, 1
    call leggi_tratto
    jc   .guasto

    add  bx, 512                  ; il prossimo settore dopo, nel rimbalzo
    inc  byte [rdsett]
    cmp  byte [rdsett], RDSPT
    jbe  .uno

    xor  bx, bx                   ; la copia riparte dall'inizio del rimbalzo

.letta:
    call unreal_es                ; ES = 4 GB

    ; ! LA DESTINAZIONE SI LEGGE PRIMA DI CAMBIARE DS. `rddst` e' una variabile
    ; di Stage 2, cioe' sta nel segmento dati di qui; un istante dopo DS punta
    ; al rimbalzo, e la stessa riga leggerebbe due parole a caso dentro i dati
    ; appena letti dal disco. Con un EDI cosi' la copia va a finire ovunque, e
    ; la macchina si ferma prima di dire qualunque cosa.
    mov  edi, [rddst]

    push ds
    mov  ax, RDSEG
    mov  ds, ax                   ; sorgente: il rimbalzo
    xor  esi, esi
    mov  ecx, (RDSPT * 512) / 4
    a32 rep movsd
    pop  ds

    add  dword [rddst], RDSPT * 512
    inc  word [rdtrk]
    jmp  .giro

.fatto:
    ; Il kernel lo saprà da qui.
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    mov  dword [es:38], RDDEST
    mov  dword [es:42], RDBYTE
    pop  es

    mov  si, msg_rdok
    call print
    xor  ax, ax
    mov  es, ax
    ret

.guasto:
    mov  si, msg_rderr
    call print
    xor  ax, ax
    mov  es, ax
    ret

.pocamem:
    mov  si, msg_rdmem
    call print
.no:
    ret

%ifdef RAMDISCO
%if RAMDISCO = 2
; =============================================================================
; IL VOLUME IN RAM DA UN DISCO — `RAMDISCO=2`, la chiavetta USB (10 ott. 2026)
;
; La stessa idea di qui sopra, per un supporto che il BIOS presenta come DISCO
; (0x80): una chiavetta. Il kernel i dischi USB non li sa leggere finche' i
; suoi driver non girano - e i driver stanno sul disco. Qui, finche' il BIOS
; c'e', si copia in RAM la PARTIZIONE ATTIVA intera: un FAT piccolo con dentro
; il sistema. Il kernel ci monta la radice, la shell parte, e sono poi i driver
; USB caricati da li' a rendere visibile la chiavetta vera (in /USB).
;
; ! QUALE PARTIZIONE LO DICE LA TABELLA, NON UN NUMERO SCRITTO QUI: si legge il
; settore 0 dell'unita' e si prende la voce marcata attiva (0x80) - la stessa
; da cui l'MBR ha appena avviato. Niente da ricalcolare se la chiavetta viene
; rifatta piu' grande o piu' piccola.
;
; ! AL PIU' RDMAXSET SETTORI (24 MB), e il limite non e' la memoria della
; macchina - una macchina a 64 bit parte da 512 MB. E' che il volume sta a
; 32 MB, dentro la fascia dei primi 64 MB che il kernel vede da OGNI processo,
; e il resto della fascia serve al kernel. Una partizione piu' grande si
; carica troncata: chi costruisce la chiavetta (tools/mkchiave.sh) la fa
; della misura giusta.
;
; ! SI LEGGE CON INT 13h/42h, 64 settori per volta, nel rimbalzo a 512 KB, e
; da li' si copia in alto col «modo reale grande» come per il dischetto.
; =============================================================================
%define RDMAXSET (24 * 2048)
.disco:
    mov  si, msg_rd
    call print

    ; --- il settore 0 dell'unita': la tabella delle partizioni ---------------
    mov  dword [rdap_lba], 0
    mov  word  [rdap_cnt], 1
    call .leggi_dap
    jc   .dguasto
    push ds
    mov  ax, RDSEG
    mov  ds, ax
    mov  si, 0x1BE
    mov  cx, 4
.dvoce:
    cmp  byte [si], 0x80          ; attiva?
    je   .dtrovata
    add  si, 16
    loop .dvoce
    pop  ds
    jmp  .dguasto
.dtrovata:
    mov  eax, [si + 8]            ; primo settore
    mov  ebx, [si + 12]           ; quanti
    pop  ds
    mov  [rdap_lba], eax
    cmp  ebx, RDMAXSET
    jbe  .dmisura
    mov  ebx, RDMAXSET
.dmisura:
    mov  [rdrest], ebx
    mov  eax, ebx
    shl  eax, 9
    mov  [rdtot], eax

    ; --- la memoria: 32 MB sotto il volume, il volume, e 4 MB sopra ---------
    ; (mem_upper arriva al piu' a 63 MB anche su una macchina che ne ha
    ; quattro giga: e' il numero del BIOS vecchio. Per questo il conto si fa
    ; in modo che 24 MB di volume ci stiano.)
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    mov  eax, [es:9]              ; KB di memoria estesa
    pop  es
    mov  edx, [rdtot]
    shr  edx, 10
    add  edx, 36 * 1024
    cmp  eax, edx
    jb   .pocamem

    in   al, 0x92                 ; A20, come per il dischetto
    or   al, 2
    and  al, 0xFE
    out  0x92, al
    call gdt_prepara
    mov  dword [rddst], RDDEST
    mov  byte [rdpunti], 0

.dgiro:
    mov  ebx, [rdrest]
    or   ebx, ebx
    jz   .dfatto
    movzx eax, word [rdpasso]
    cmp  ebx, eax
    jae  .dquanti
    mov  eax, ebx
.dquanti:
    mov  [rdap_cnt], ax
    call .leggi_dap
    jnc  .dletta
    ; ! NON TUTTI I BIOS LEGGONO 64 SETTORI IN UNA CHIAMATA da una chiavetta.
    ; Se cinque tentativi non bastano si scende - 64, poi 8, poi UNO, che e'
    ; il modo in cui questo stesso file legge il kernel e che funziona
    ; ovunque - e si riprova lo stesso punto. Si rinuncia solo quando non si
    ; legge nemmeno un settore. Una barra a schermo per ogni discesa.
    cmp  word [rdpasso], 1
    jbe  .dguasto
    shr  word [rdpasso], 3
    mov  al, '/'
    call carattere
    jmp  .dgiro
.dletta:

    inc  byte [rdpunti]           ; un punto ogni megabyte
    test byte [rdpunti], 31
    jnz  .dcopia
    mov  al, '.'
    call carattere
.dcopia:
    ; ! LA COPIA SI FA A INTERRUPT SPENTI, e il limite di ES si allarga QUI
    ; dentro e non con unreal_es (che finisce con `sti`). Il limite a 4 GB
    ; vive nella cache del segmento, e un interrupt servito dal BIOS in mezzo
    ; alla copia puo' rimetterlo a 64 KB: il BIOS di una chiavetta lavora a
    ; sua volta in modo protetto. Da li' `rep movsd` continua con un indirizzo
    ; che ES non copre piu'. Visto il 10 ottobre 2026 con la prima versione:
    ; un avvio su sette, a volte fermo, a volte con la radice letta sbagliata
    ; («ELF: magic non valido» su /bin/sh). Trentadue KB per giro a interrupt
    ; spenti sono microsecondi.
    cli
    lgdt [GDTB+0x1E]
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    mov  ax, 0x08
    mov  es, ax                   ; ES = 4 GB
    mov  eax, cr0
    and  al, 0xFE
    mov  cr0, eax
    mov  edi, [rddst]
    movzx ecx, word [rdap_cnt]
    shl  ecx, 7                   ; settori * 512 / 4
    push ds
    mov  ax, RDSEG
    mov  ds, ax
    xor  esi, esi
    a32 rep movsd
    pop  ds
    sti

    movzx eax, word [rdap_cnt]
    sub  [rdrest], eax
    add  [rdap_lba], eax
    shl  eax, 9
    add  [rddst], eax
    jmp  .dgiro

.dfatto:
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    mov  dword [es:38], RDDEST
    mov  eax, [rdtot]
    mov  [es:42], eax
    pop  es
    mov  si, msg_rdok
    call print
    xor  ax, ax
    mov  es, ax
    ret

; La lettura non e' riuscita: lo si lascia SCRITTO per il kernel (rd_addr a
; zero, rd_byte a tutti uno), perche' il messaggio qui sotto sparisce appena
; lo schermo passa in grafica e chi guarda vede solo «la shell non carica».
.dguasto:
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    mov  dword [es:38], 0
    mov  dword [es:42], 0xFFFFFFFF
    pop  es
    jmp  .guasto

; Legge [rdap_cnt] settori da [rdap_lba] nel rimbalzo. CF = non riuscita.
;
; ! SI RIPROVA, FINO A TRE VOLTE, rimettendo a posto l'unita' fra una e
; l'altra: una lettura da una chiavetta attraverso il BIOS ogni tanto fallisce
; senza che la chiavetta abbia niente (visto in QEMU: un avvio su otto si
; fermava con «Lettura fallita» dopo qualche megabyte). E' la stessa cura che
; leggi_tratto ha per il dischetto. Il pacchetto resta com'e': il BIOS non
; deve toccarlo, ma il conto dei settori lo riscriviamo lo stesso a ogni giro.
.leggi_dap:
    mov  byte [rdprove], 3
    mov  ax, [rdap_cnt]
    mov  [rdcnt_s], ax
.ld_prova:
    mov  ax, [rdcnt_s]
    mov  [rdap_cnt], ax
    mov  ah, 0x42
    mov  dl, [drv]
    mov  si, rdap
    int  0x13
    jnc  .ld_ok
    xor  ah, ah                   ; rimette a posto l'unita'
    mov  dl, [drv]
    int  0x13
    dec  byte [rdprove]
    jnz  .ld_prova
    stc
    ret
.ld_ok:
    mov  ax, [rdcnt_s]
    mov  [rdap_cnt], ax
    clc
    ret
%endif
%endif

; Una riga a schermo. Stage 2 e' silenzioso di suo — un avvio riuscito non ha
; niente da dire — ma il caricamento del volume dura qualche secondo, e uno
; schermo fermo senza spiegazioni e' il modo in cui si presenta una macchina
; piantata.
print:
    push ax
.pl:
    lodsb
    or   al, al
    jz   .pfine
    call carattere
    jmp  .pl
.pfine:
    pop  ax
    ret

; Il cluster che segue AX, letto dalla FAT gia' in memoria a 0xA000.
; Rende il prossimo in AX. Non tocca altro.
;
; ! LA FAT12 IMPACCHETTA UNA VOCE E MEZZA OGNI TRE BYTE, ed e' il motivo per
; cui questa funzione esiste invece di essere una lettura: la voce di un
; cluster PARI sta nei dodici bit bassi della parola, quella di un DISPARI nei
; dodici alti. Sbagliare meta' vuol dire seguire una catena che esiste ma non
; e' quella del file.
fat_next:
    push bx
    push cx
    push si
    mov  si, ax
    mov  cx, ax
    shr  cx, 1
    add  si, cx
    add  si, FAT
    mov  cx, [si]
    test ax, 1
    jz   .fn_pari
    shr  cx, 4
    jmp  .fn_fine
.fn_pari:
    and  cx, 0x0FFF
.fn_fine:
    mov  ax, cx
    pop  si
    pop  cx
    pop  bx
    ret

; Un numero decimale (AX) a schermo e sulla seriale.
numero:
    push ax
    push bx
    push cx
    push dx
    mov  bx, 10
    xor  cx, cx
.n_div:
    xor  dx, dx
    div  bx
    push dx
    inc  cx
    or   ax, ax
    jnz  .n_div
.n_out:
    pop  ax
    add  al, '0'
    call carattere
    loop .n_out
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    ret

%ifdef GEOMETRIA_BIOS
; =============================================================================
; geometria - how the BIOS presents the boot drive (14 October 2026)
;
; As a floppy (DL < 0x80) the floppy's numbers stay. As a hard disk the BIOS
; is asked (INT 13h AH=08h) and the answer is written on screen, so a key
; that does not boot tells which way the BIOS took it:
;     Avvio: disco BIOS 128, 63 settori per traccia, 255 testine
; =============================================================================
geometria:
    cmp  byte [drv], 0x80
    jb   .g_floppy
    ; ! LBA FIRST (see bootloader/stage1/boot.asm): the BIOS's CHS numbers
    ; for a USB stick may not fit AH=08h at all (SeaBIOS: 72 sectors per
    ; track, answered as 8).
    pusha
    mov  ah, 0x41
    mov  bx, 0x55AA
    mov  dl, [drv]
    int  0x13
    jc   .g_chs
    cmp  bx, 0xAA55
    jne  .g_chs
    test cl, 1
    jz   .g_chs
    mov  byte [usa_lba], 1
    popa
    mov  si, msg_geo1
    call print
    xor  ax, ax
    mov  al, [drv]
    call numero
    mov  si, msg_geolba
    call print
    ret
.g_chs:
    popa
    push es
    pusha
    mov  ah, 0x08
    mov  dl, [drv]
    xor  di, di
    mov  es, di
    int  0x13
    jc   .g_no
    and  cx, 0x3F
    jz   .g_no
    mov  [g_spt], cx
    mov  dl, dh
    xor  dh, dh
    inc  dx
    mov  [g_teste], dx
.g_no:
    popa
    pop  es
    mov  si, msg_geo1
    call print
    xor  ax, ax
    mov  al, [drv]
    call numero
    mov  si, msg_geo2
    call print
    mov  ax, [g_spt]
    call numero
    mov  si, msg_geo3
    call print
    mov  ax, [g_teste]
    call numero
    mov  si, msg_geo4
    call print
.g_floppy:
    ret

; leggi42 - AL sectors from LBA [l42_lba] into ES:BX with INT 13h AH=42h,
; three tries with a reset in between. CF=1 = failed. Touches nothing else.
leggi42:
    pusha
    xor  ah, ah
    mov  [l42_n], ax
    mov  [dapu_off], bx
    mov  [dapu_seg], es
    mov  ax, [l42_lba]
    mov  [dapu_lba], ax
    mov  di, 3
.l42_prova:
    mov  ax, [l42_n]
    mov  [dapu_n], ax              ; a failed call may have changed it
    mov  si, dapu
    mov  ah, 0x42
    mov  dl, [drv]
    int  0x13
    jnc  .l42_ok
    xor  ah, ah
    mov  dl, [drv]
    int  0x13
    dec  di
    jnz  .l42_prova
    popa
    stc
    ret
.l42_ok:
    popa
    clc
    ret

; leggi_lba - RDSPT sectors from LBA [rdlba] into RDSEG:0, with the BIOS
; geometry: one read per BIOS track piece, and one sector at a time once a
; piece has failed (the 's' on screen, as in the floppy loop). CF=1 = failed.
leggi_lba:
    pusha
    mov  word [rdfatti], 0
.ll_giro:
    mov  ax, [rdfatti]
    cmp  ax, RDSPT
    jae  .ll_ok
    cmp  byte [usa_lba], 0
    je   .ll_chs
    add  ax, [rdlba]
    mov  [l42_lba], ax
    mov  cx, RDSPT
    sub  cx, [rdfatti]
    cmp  cx, 18                    ; at most 18 sectors per read: 64 at a time
    jbe  .l4n                      ; failed on the real PC (10 October)
    mov  cx, 18
.l4n:
    cmp  byte [rduno], 0
    je   .l4t
    mov  cx, 1
.l4t:
    mov  [rdquanti], cx
    mov  bx, [rdfatti]
    shl  bx, 9
    mov  al, cl
    call leggi42
    jc   .ll_male
    mov  ax, [rdquanti]
    add  [rdfatti], ax
    jmp  .ll_giro
.ll_chs:
    mov  ax, [rdfatti]
    add  ax, [rdlba]               ; current LBA (< 5760)
    xor  dx, dx
    div  word [g_spt]              ; AX = BIOS track, DX = sector, 0-based
    mov  si, dx
    mov  cx, [g_spt]
    sub  cx, dx                    ; sectors left in this BIOS track
    mov  di, RDSPT
    sub  di, [rdfatti]             ; sectors still wanted
    cmp  cx, di
    jbe  .ll_n
    mov  cx, di
.ll_n:
    cmp  byte [rduno], 0
    je   .ll_tanti
    mov  cx, 1
.ll_tanti:
    mov  [rdquanti], cx
    xor  dx, dx
    div  word [g_teste]            ; AX = cylinder, DX = head
    mov  ch, al
    mov  cl, ah
    shl  cl, 6                     ; cylinder bits 8-9
    mov  bx, si
    inc  bx
    or   cl, bl                    ; sector, 1-based
    mov  dh, dl
    mov  bx, [rdfatti]
    shl  bx, 9                     ; offset in the bounce buffer
    mov  al, [rdquanti]
    call leggi_tratto
    jc   .ll_male
    mov  ax, [rdquanti]
    add  [rdfatti], ax
    jmp  .ll_giro
.ll_male:
    cmp  byte [rduno], 0
    jne  .ll_fallita
    mov  byte [rduno], 1           ; from here on, one sector per read
    mov  al, 's'
    call carattere
    jmp  .ll_giro
.ll_fallita:
    popa
    stc
    ret
.ll_ok:
    popa
    clc
    ret
%endif

; Legge AL settori da CH/CL/DH in ES:BX, con tre tentativi e un reset del
; controller in mezzo. CF=1 se non ce l'ha fatta. Non tocca BX.
leggi_tratto:
    push cx
    push dx
    push di
    mov  di, 3
    mov  ah, 0x02
.lt_prova:
    push ax
    push cx
    push dx
    mov  dl, [drv]
    int  0x13
    pop  dx
    pop  cx
    pop  ax
    jnc  .lt_ok

    ; Il reset rimette a posto un lettore che ha perso il passo. Su un
    ; lettore USB costa parecchio: e' il motivo per cui non ci si appoggia.
    push ax
    xor  ah, ah
    mov  dl, [drv]
    int  0x13
    pop  ax
    mov  ah, 0x02
    dec  di
    jnz  .lt_prova

    pop  di
    pop  dx
    pop  cx
    stc
    ret
.lt_ok:
    pop  di
    pop  dx
    pop  cx
    clc
    ret

; Un carattere a schermo E sulla seriale, senza toccare niente.
;
; ! ANCHE SULLA SERIALE, perche' e' l'unico modo di sapere cosa fa Stage 2
; quando lo schermo non basta: qui non c'e' nessun registro da rileggere dopo,
; e un avvio che si ferma a meta' non lascia altra traccia. Stage 1 la porta
; l'ha gia' accesa (vedi bootloader/stage1/boot.asm), quindi costa due
; istruzioni.
carattere:
    push ax
    push bx
    push dx
    mov  ah, 0x0E
    mov  bx, 0x0007
    int  0x10
    pop  dx
    pop  bx
    pop  ax

    push ax
    push dx
    mov  dx, 0x3F8
    out  dx, al
    pop  dx
    pop  ax
    ret

; =============================================================================
; IL SISTEMA DI BASE: rdboot.lz4 in RAM (@RDBOOT, 22 ottobre 2026)
;
; ! DUE STRADE PER LA STESSA MAPPA. La mappa e' `rdmappa` in fondo al file:
;   - da DISCO (`da_disco`) la scrive bootinstall (kernel/boot/bootinst.c)
;     quando installa l'avvio: il settore di avvio della partizione e' pieno
;     e il sistema di base su ext2 e' spezzato in parecchi tratti;
;   - da FLOPPY, da CD (l'emulazione floppy di El Torito) e da chiavetta la
;     si ricava qui, dalla catena FAT12 di /RDBOOT.LZ4 (rdboot_dal_fat): la
;     directory radice e la FAT le ha gia' in memoria Stage 1.
; Zero voci = il supporto non ne ha uno, e si parte come prima.
;
; ! DAL FLOPPY E' IL SISTEMA, NON UN DI PIU'. Il dischetto porta solo
; LOADER.BIN, KERNEL.BIN, RDBOOT.LZ4 e /boot/KERNEL.CFG: programmi e driver
; stanno in /rdboot (tools/mkfloppy.sh). Con il volume intero in RAM
; (ramdisco) le due cose convivono: il volume e' rd_addr, questo rb_addr.
;
; ! COMPRESSO O NO, QUI NON IMPORTA: si porta in memoria il file com'e', a
; RBDEST (10 MB). Se e' 'EXLZ' lo espande il kernel a 6 MB
; (kernel/boot/rdlz4.c), che ha lo spazio e il tempo che qui non ci sono.
;
; ! LA STESSA STRADA DELLA CHIAVETTA: si legge nel rimbalzo a 512 KB - da
; disco INT 13h/42h al piu' 64 settori per volta, dal floppy INT 13h/02h al
; piu' fino alla fine della traccia - poi si copia in alto col modo reale
; grande A INTERRUPT SPENTI (il perche' e' accanto a .dcopia). Il kernel lo
; trova da BootInfo: rb_firma 'RDBT', rb_addr, rb_byte.
;
; ! SE NON SI LEGGE, SI PARTE SENZA, e lo si dice a schermo: da disco il
; sistema c'e' ancora tutto; dal floppy si arriva a un kernel senza shell,
; che lo scrive.
; =============================================================================
rdboot_carica:
    mov  byte [rb_lba], 1
    cmp  byte [da_disco], 0
    jne  .mappa
    mov  byte [rb_lba], 0
%ifdef GEOMETRIA_BIOS
    mov  al, [usa_lba]            ; la chiavetta letta come disco: LBA se c'e'
    mov  [rb_lba], al
%endif
    call rdboot_dal_fat
    jc   .no
.mappa:
    cmp  word [rdm_n], 0
    je   .no

    ; ! LA MISURA E LA MEMORIA SI CONTANO PRIMA. Il file va a RBDEST e non
    ; deve arrivare al volume intero (RDDEST); e deve finire dentro la RAM
    ; che c'e': mem_upper e' in KB oltre il primo MB.
    mov  eax, [rdm_byte]
    cmp  eax, RBMAX
    ja   .pocamem
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    mov  eax, [es:9]              ; KB oltre il primo MB
    pop  es
    shl  eax, 10
    add  eax, 0x100000            ; dove finisce la RAM
    mov  edx, [rdm_byte]
    add  edx, RBDEST              ; dove finirebbe il file
    cmp  eax, edx
    jb   .pocamem

    mov  si, msg_rdb
    call print

    in   al, 0x92                 ; A20
    or   al, 2
    and  al, 0xFE
    out  0x92, al
    call gdt_prepara
    mov  dword [rbdst], RBDEST
    mov  word [rbidx], 0

.voce:
    mov  ax, [rbidx]
    cmp  ax, [rdm_n]
    jae  .fatto
    mov  bx, 6
    mul  bx
    mov  si, rdm_ext
    add  si, ax
    mov  eax, [si]
    mov  [rbdap_lba], eax
    movzx eax, word [si + 4]
    mov  [rbrest], eax
    inc  word [rbidx]

.pezzo:
    mov  eax, [rbrest]
    or   eax, eax
    jz   .voce
    cmp  eax, 64
    jbe  .quanti
    mov  eax, 64
.quanti:
    mov  [rbcnt], ax
    cmp  byte [rb_lba], 0
    jne  .lba
    ; CHS: non oltre la fine della traccia, che una lettura non attraversa
    mov  eax, [rbdap_lba]
    xor  edx, edx
    mov  cx, G_SPT
    movzx ecx, cx
    div  ecx                      ; EAX = traccia, EDX = settore da 0
    mov  ax, G_SPT
    sub  ax, dx
    cmp  ax, [rbcnt]
    jae  .lba
    mov  [rbcnt], ax
.lba:
    mov  byte [rbprove], 3
.prova:
    call rb_leggi
    jnc  .letta
    xor  ah, ah                   ; rimette a posto l'unita' e riprova
    mov  dl, [drv]
    int  0x13
    dec  byte [rbprove]
    jnz  .prova
    mov  si, msg_rdbno
    call print
    ret

.letta:
    cli
    lgdt [GDTB+0x1E]
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    mov  ax, 0x08
    mov  es, ax                   ; ES = 4 GB
    mov  eax, cr0
    and  al, 0xFE
    mov  cr0, eax
    mov  edi, [rbdst]
    movzx ecx, word [rbcnt]
    shl  ecx, 7                   ; settori * 512 / 4
    push ds
    mov  ax, RDSEG
    mov  ds, ax
    xor  esi, esi
    a32 rep movsd
    pop  ds
    xor  ax, ax
    mov  es, ax
    sti

    movzx eax, word [rbcnt]
    sub  [rbrest], eax
    add  [rbdap_lba], eax
    shl  eax, 9
    add  [rbdst], eax
    jmp  .pezzo

.fatto:
    push es
    mov  ax, BINFO >> 4
    mov  es, ax
    mov  dword [es:58], 0x54424452   ; 'RDBT'
    mov  dword [es:62], RBDEST
    mov  eax, [rdm_byte]
    mov  [es:66], eax
    pop  es
    mov  si, msg_rdbok
    call print
.no:
    ret
.pocamem:
    mov  si, msg_rdbmem
    call print
    ret

; rb_leggi - [rbcnt] settori dal LBA [rbdap_lba] nel rimbalzo RDSEG:0, un
; tentativo. Con [rb_lba] INT 13h/42h, senza INT 13h/02h con la geometria
; del dischetto (o quella del BIOS, GEOMETRIA_BIOS). CF=1 = fallita.
rb_leggi:
    pusha
    push es
    cmp  byte [rb_lba], 0
    je   .chs
    mov  ax, [rbcnt]
    mov  [rbdap_cnt], ax          ; il BIOS puo' riscriverlo: si rimette
    mov  ah, 0x42
    mov  dl, [drv]
    mov  si, rbdap
    int  0x13
    jmp  .fine
.chs:
    mov  eax, [rbdap_lba]
    xor  edx, edx
    mov  cx, G_SPT
    movzx ecx, cx
    div  ecx                      ; EAX = traccia, EDX = settore da 0
    mov  bx, dx
    inc  bx                       ; BX = settore da 1
    xor  edx, edx
    mov  cx, G_TESTE
    movzx ecx, cx
    div  ecx                      ; EAX = cilindro, EDX = testina
    mov  ch, al
    mov  cl, ah
    shl  cl, 6                    ; bit 8-9 del cilindro
    or   cl, bl
    mov  dh, dl
    mov  dl, [drv]
    mov  ax, RDSEG
    mov  es, ax
    xor  bx, bx
    mov  ah, 0x02
    mov  al, [rbcnt]
    int  0x13
.fine:
    pop  es
    popa
    ret

; rdboot_dal_fat - riempie rdmappa dalla catena FAT12 di RDBOOT.LZ4 nella
; directory radice (0x7E00) e nella FAT (FAT) che Stage 1 ha caricato. I
; cluster consecutivi diventano una voce sola. CF=1 = non c'e', o la catena
; e' rotta o troppo spezzata: in quel caso rdm_n resta 0.
rdboot_dal_fat:
    pusha
    cld
    mov  di, 0x7E00
    mov  cx, 224
.cerca:
    mov  al, [di]
    test al, al
    jz   .non
    cmp  al, 0xE5
    je   .salta
    test byte [di+11], 0x18       ; non etichette e non directory
    jnz  .salta
    push cx
    push di
    mov  si, rbnome
    mov  cx, 11
    repe cmpsb
    pop  di
    pop  cx
    jz   .trovato
.salta:
    add  di, 32
    loop .cerca
.non:
    mov  word [rdm_n], 0
    popa
    stc
    ret

.trovato:
    mov  eax, [di+28]
    or   eax, eax
    jz   .non
    mov  [rdm_byte], eax
    mov  word [rdm_n], 0
    mov  ax, [di+26]              ; primo cluster
    mov  bx, rdm_ext - 6          ; l'ultima voce scritta
    mov  bp, 6000                 ; una catena che gira in tondo finisce qui
.giro:
    cmp  ax, 0xFF8
    jae  .fine
    cmp  ax, 2
    jb   .non
    dec  bp
    jz   .non
    mov  dx, ax
    sub  dx, 2
    add  dx, F_DATA               ; LBA del cluster (un settore)
    cmp  word [rdm_n], 0
    je   .nuova
    mov  cx, [bx]
    add  cx, [bx+4]
    cmp  cx, dx                   ; continua la voce precedente?
    jne  .nuova
    inc  word [bx+4]
    jmp  .avanti
.nuova:
    cmp  word [rdm_n], RDM_MAX
    jae  .non
    add  bx, 6
    mov  [bx], dx
    mov  word [bx+2], 0
    mov  word [bx+4], 1
    inc  word [rdm_n]
.avanti:
    call fat_next
    jmp  .giro
.fine:
    popa
    clc
    ret

; Costruisce la GDT a GDTB. Serve al modo reale grande, e la rifara' anche il
; passaggio a modo protetto piu' sotto: farla due volte non costa niente e
; toglie di mezzo la dipendenza fra i due pezzi.
gdt_prepara:
    push ax
    xor  ax, ax
    mov  [GDTB+0x00], ax
    mov  [GDTB+0x02], ax
    mov  [GDTB+0x04], ax
    mov  [GDTB+0x06], ax
    mov  word  [GDTB+0x08], 0xFFFF
    mov  word  [GDTB+0x0A], 0x0000
    mov  byte  [GDTB+0x0C], 0x00
    mov  byte  [GDTB+0x0D], 0x92
    mov  byte  [GDTB+0x0E], 0xCF
    mov  byte  [GDTB+0x0F], 0x00
    mov  word  [GDTB+0x1E], 23
    mov  dword [GDTB+0x20], GDTB
    pop  ax
    ret

; ES = un descrittore che copre 4 GB, e si resta in modo reale.
;
; ! NON C'E' NESSUN SALTO LONTANO, ed e' voluto: CS non cambia, quindi il
; codice continua a girare esattamente dov'era. L'unica cosa che cambia e' cio'
; che la CPU si ricorda del segmento ES.
unreal_es:
    push ax
    cli
    lgdt [GDTB+0x1E]
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    mov  ax, 0x08
    mov  es, ax
    mov  eax, cr0
    and  al, 0xFE
    mov  cr0, eax
    sti
    pop  ax
    ret

; -----------------------------------------------------------------------------
; La modalita' grafica voluta, e la firma con cui /dev/svga.drv la trova.
;
; ! LA FIRMA NON E' DECORAZIONE: e' l'unico modo che ha il comando di
; sapere DOVE scrivere. Un offset fisso dentro il binario cambierebbe a
; ogni riga aggiunta qui sopra, e il comando finirebbe a scrivere in mezzo
; al codice — su un file che serve ad avviare la macchina.
; modo_annota: ES:0 is the ModeInfoBlock of mode [mcur], supported and
; graphic. If it has a linear framebuffer and 16, 24 or 32 bpp it goes in
; MODITAB (the first MODI_MAX). Registers and ES are kept.
modo_annota:
    push ax
    push bx
    mov  ax, [es:0]
    test ax, 0x0080
    jz   .esci
    mov  al, [es:0x19]
    cmp  al, 16
    je   .ok
    cmp  al, 24
    je   .ok
    cmp  al, 32
    jne  .esci
.ok:
    mov  bx, [nmodi]
    cmp  bx, MODI_MAX
    jae  .esci
    shl  bx, 3
    mov  ax, [es:0x12]
    mov  [bx+MODITAB], ax
    mov  ax, [es:0x14]
    mov  [bx+MODITAB+2], ax
    mov  al, [es:0x19]
    mov  [bx+MODITAB+4], al
    mov  byte [bx+MODITAB+5], 0
    mov  ax, [mcur]
    mov  [bx+MODITAB+6], ax
    inc  word [nmodi]
.esci:
    pop  bx
    pop  ax
    ret

; -----------------------------------------------------------------------------
; vid_leggi — the card's names and the screen's EDID into VIDINFO (22 October
; 2026). Called with the VbeInfoBlock just filled; keeps every register.
;
; ! THE MODES DO NOT SAY WHICH SCREEN IS ATTACHED. The laptop's NVIDIA BIOS
; lists 1280x720 at most and its panel is 1366x768: only the EDID (VBE 4F15,
; the DDC of the screen) says what the panel really is. A BIOS that cannot
; read it answers something other than 004Fh, and then edid_ok stays 0.
;
; ! THE EXTENDED NAMES EXIST FROM VBE 2.0 ON: on a 1.x card the pointers at
; +16h..+1Eh are not filled, and following them would copy whatever is there.
; -----------------------------------------------------------------------------
vid_leggi:
    push es
    pusha
    xor  ax, ax
    mov  es, ax
    mov  di, VIDINFO
    mov  cx, VIDINFO_N / 2
    rep  stosw
    mov  dword [es:VIDINFO], 'VIDI'

    mov  ax, VBEINFO >> 4
    mov  es, ax
    mov  ax, [es:4]               ; VbeVersion
    mov  bx, [es:18]              ; TotalMemory, in 64 KB
    xor  dx, dx
    mov  es, dx
    mov  [es:VIDINFO+6], ax
    mov  [es:VIDINFO+8], bx

    mov  bx, 6                    ; OemStringPtr: every VBE has it
    mov  di, VIDINFO+128
    mov  cx, 47
    call vid_str
    cmp  ax, 0x0200
    jb   .edid
    mov  bx, 0x16                 ; OemVendorNamePtr
    mov  di, VIDINFO+16
    mov  cx, 47
    call vid_str
    mov  bx, 0x1A                 ; OemProductNamePtr
    mov  di, VIDINFO+64
    mov  cx, 47
    call vid_str
    mov  bx, 0x1E                 ; OemProductRevPtr
    mov  di, VIDINFO+112
    mov  cx, 15
    call vid_str

.edid:
    xor  ax, ax
    mov  es, ax
    mov  di, VIDINFO+176
    mov  ax, 0x4F15               ; VBE/DDC
    mov  bl, 1                    ; read EDID
    xor  cx, cx                   ; controller 0
    xor  dx, dx                   ; block 0
    int  0x10
    cmp  ax, 0x004F
    jne  .fine
    xor  ax, ax
    mov  es, ax
    ; the EDID header, 00 FF FF FF FF FF FF 00: anything else is not one
    cmp  dword [es:VIDINFO+176], 0xFFFFFF00
    jne  .fine
    cmp  dword [es:VIDINFO+180], 0x00FFFFFF
    jne  .fine
    mov  byte [es:VIDINFO+4], 1   ; edid_ok

.fine:
    xor  ax, ax
    mov  es, ax
    mov  dword [es:BINFO+54], VIDINFO
    popa
    pop  es
    ret

; vid_str — copies the string at the FAR pointer at VbeInfoBlock+BX into
; 0:DI, at most CX characters, then a 0. Unprintable bytes become '?'.
; Keeps every register.
vid_str:
    pusha
    push ds
    push es
    mov  ax, VBEINFO >> 4
    mov  es, ax
    mov  si, [es:bx]
    mov  ax, [es:bx+2]
    xor  dx, dx
    mov  es, dx
    mov  dx, ax
    or   dx, si
    jz   .zero                    ; a null pointer: nothing to copy
    mov  ds, ax
.car:
    lodsb
    or   al, al
    jz   .zero
    cmp  al, 0x20
    jb   .strano
    cmp  al, 0x7E
    jbe  .metti
.strano:
    mov  al, '?'
.metti:
    stosb
    loop .car
.zero:
    mov  byte [es:di], 0
    pop  es
    pop  ds
    popa
    ret

want16   db 0                 ; bit 7 del byte di modo: preferisci 16 bpp
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
; ! MODE 4: ANY RESOLUTION, written here by svga.drv (17 October 2026). It
; follows the table so that the index arithmetic finds it, and its place -
; signature + 21 - is part of the contract with svga.drv.
svgacust  dw 0, 0
nmodi     dw 0                ; entries in MODITAB

; ! LA FIRMA, come per SVGAMODE e per la stessa ragione: un offset fisso
; dentro il binario cambierebbe a ogni riga aggiunta qui sopra.
ramdiscomagic db 'RAMDISCO'
%ifndef RAMDISCO
%define RAMDISCO 0
%endif
rdflag    db RAMDISCO   ; 0 = niente, 1 = il volume si carica in RAM

rdtrk     dw 0
rdsett    db 1
%if RAMDISCO = 2
; Il pacchetto di INT 13h/42h per il volume letto da un disco (vedi .disco)
rdap:
    db   0x10
    db   0
rdap_cnt:
    dw   1
    dw   0                ; offset nel rimbalzo
    dw   RDSEG            ; il rimbalzo, a 512 KB
rdap_lba:
    dd   0
    dd   0
rdrest    dd 0            ; settori ancora da leggere
rdtot     dd 0            ; byte del volume
rdpunti   db 0
rdprove   db 0            ; tentativi rimasti per la lettura in corso
rdpasso   dw 64           ; settori per chiamata: 64, poi 8, poi 1
rdcnt_s   dw 0            ; i settori chiesti (il BIOS puo' riscrivere il conto)
%endif

; Il tratto di kernel che si sta leggendo: primo cluster, ultimo, quanti
; settori, e il suo CHS.
kprimo    dw 0
kult      dw 0
kcnt      dw 0
kcil      dw 0
ktesta     dw 0
ksett     dw 0

; Perche' la grafica non e' partita: tre risposte, tre rimedi diversi.
vmodi     dw 0        ; quanti modi la scheda ha elencato
vres      db 0        ; 1 = la risoluzione voluta esiste
vlfb      db 0        ; 1 = ...e con framebuffer lineare

msg_v1    db 13, 10, 'VESA: ', 0
msg_v2    db ' modi elencati, ', 0
msg_vnores  db 'nessuno alla risoluzione chiesta.', 13, 10, 0
msg_vnolfb  db 'quella risoluzione c e ma solo a banchi.', 13, 10, 0
msg_vnobpp  db 'c e ma non a 16, 24 o 32 bit.', 13, 10, 0
rddst     dd 0

msg_rd    db 'Carico il volume in RAM...', 13, 10, 0
msg_rdok  db 'Volume in RAM: la radice non dipende piu dal lettore.', 13, 10, 0
msg_rderr db 'Lettura fallita: niente disco in RAM.', 13, 10, 0
msg_rdmem db 'Memoria insufficiente per il disco in RAM.', 13, 10, 0
msg_rdb    db 'Carico il sistema di base...', 13, 10, 0
msg_rdbok  db 'Sistema di base in RAM.', 13, 10, 0
msg_rdbno  db 'rdboot illeggibile: parto senza.', 13, 10, 0
msg_rdbmem db 'Memoria insufficiente per rdboot: parto senza.', 13, 10, 0

da_disco  db 0            ; 1 = avvio dal settore 'EXHD' di un disco
rb_lba    db 0            ; 1 = rdboot si legge con INT 13h/42h
rbnome    db 'RDBOOT  LZ4'
rbidx     dw 0            ; la voce di rdmappa che si sta leggendo
rbrest    dd 0            ; settori ancora da leggere nella voce
rbdst     dd 0            ; dove va il prossimo pezzo
rbcnt     dw 0            ; settori del pezzo
rbprove   db 0
rbdap:                    ; il pacchetto di INT 13h/42h
    db   0x10
    db   0
rbdap_cnt:
    dw   0
    dw   0                ; offset nel rimbalzo
    dw   RDSEG
rbdap_lba:
    dd   0
    dd   0

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
%ifdef GEOMETRIA_BIOS
g_spt    dw F_SPT
g_teste  dw 2
rdlba    dw 0
rdfatti  dw 0
rdquanti dw 0
rduno    db 0
usa_lba  db 0
avvio_hd db 0
l42_lba  dw 0
l42_n    dw 0
dapu     db 16, 0
dapu_n   dw 0
dapu_off dw 0
dapu_seg dw 0
dapu_lba dd 0, 0
msg_geolba db ', letture LBA', 13, 10, 0
msg_geo1 db 'Avvio: disco BIOS ', 0
msg_geo2 db ', ', 0
msg_geo3 db ' settori per traccia, ', 0
msg_geo4 db ' testine', 13, 10, 0
%endif
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
; ! LA TABELLA DI rdboot, ALLINEATA A 512 BYTE nel file: bootinstall la cerca
; all'inizio di un settore di Stage 2 e la riscrive li', un settore solo.
; Contratto con kernel/boot/bootinst.c (RD_FIRMA, RD_MAX_EXT): RDM_MAX in testa.
    times (512 - (($ - $$) % 512)) % 512 db 0
rdmappa   db 'RDMAPPA', 0
rdm_byte  dd 0            ; byte di /boot/rdboot.img
rdm_n     dw 0            ; quante voci valgono (0 = nessun archivio)
rdm_ext   times RDM_MAX * 6 db 0   ; {LBA assoluto 32 bit, settori 16 bit}

%if ($ - $$) > (0x7C00 - 0x0500)
  %error "Stage2 supera 0x7700 byte: il codice invaderebbe la mappa dei settori a 0x7C00"
%endif
