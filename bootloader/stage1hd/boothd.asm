; =============================================================================
; bootloader/stage1hd/boothd.asm — EX-OS, settore di avvio di partizione
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
; SPDX-License-Identifier: GPL-2.0-or-later
;
; Va nel settore 0 della partizione. L'MBR lo carica a 0x7C00 con DL =
; unita' e ci salta dentro.
;
; Carica Stage 2 a 0x0500 e ci salta, esattamente come fa il settore di
; avvio del floppy (bootloader/stage1/boot.asm): da li' in poi il percorso
; di avvio e' lo stesso, e questa e' la ragione per cui e' stato fatto
; cosi'.
;
; -----------------------------------------------------------------------
; PERCHE' UNA MAPPA DI SETTORI E NON UN LETTORE FAT
;
; L'alternativa sarebbe far leggere la FAT a questo settore per trovare
; STAGE2.BIN per nome. In 512 byte, meno l'area del BPB e la firma, restano
; circa 320 byte: un lettore FAT12 ci sta a fatica, uno FAT32 no. Un
; settore di avvio che funziona su FAT16 e non su FAT32 sarebbe una
; trappola, non una semplificazione.
;
; Qui invece l'installatore — che gira dentro EX-OS e ha il driver FAT
; completo — trova il file, VERIFICA CHE SIA CONTIGUO e scrive qui il suo
; LBA di partenza e la lunghezza. Questo codice legge dei settori e basta,
; e funziona identico su qualunque FAT.
;
; ! IL PREZZO, ed e' bene sia scritto qui: la mappa vale finche' i file
; non si spostano. Riscrivere il kernel o Stage 2 sul disco OBBLIGA a
; rieseguire l'installazione. E' lo stesso patto di LILO, ed e' il motivo
; per cui l'installatore rifiuta i file frammentati invece di seguirne la
; catena: una catena spezzata qui sarebbe un sistema che non si avvia con
; un messaggio incomprensibile.
;
; -----------------------------------------------------------------------
; IL BPB NON E' IN QUESTO FILE
;
; I byte 3..89 sono lasciati a zero: l'installatore li riempie con quelli
; del filesystem gia' presente sulla partizione. Scriverci un BPB inventato
; renderebbe illeggibile il volume — l'installazione dell'avvio
; distruggerebbe i dati che sta cercando di rendere avviabili.
; =============================================================================
[BITS 16]
[ORG 0x7C00]

        jmp short _start
        nop

; ---- BPB: 87 byte lasciati a ZERO, riempiti dall'installatore ------------
; (byte 3..89: OEM name, BytesPerSector ... FileSystemType)
        times 87 db 0

_start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti

    mov  [unita], dl

    ; --- La mappa e' stata scritta davvero? ---
    ; La magia e' gia' nel binario (serve a Stage 2 per riconoscere l'avvio
    ; da disco), quindi non dice nulla su questo: cio' che distingue un
    ; settore installato da uno patchato e' il CONTEGGIO. A zero si
    ; leggerebbero zero settori e si salterebbe in memoria vuota.
    cmp  word [s2_cnt], 0
    je   errore_nonpatch

    ; --- Estensioni INT 13h ---
    mov  ah, 0x41
    mov  bx, 0x55AA
    mov  dl, [unita]
    int  0x13
    jc   errore_noext
    cmp  bx, 0xAA55
    jne  errore_noext

    ; --- Carica Stage 2 a 0x0000:0x0500 ---
    ;
    ; Un settore per volta: il buffer di destinazione e' basso e piccolo, e
    ; una lettura multi-settore che attraversa un confine di traccia non e'
    ; garantita su ogni BIOS. Stage 2 sono pochi settori: la lentezza non
    ; si vede, un avvio che fallisce su una macchina su dieci si'.
    mov  eax, [s2_lba]
    mov  cx,  [s2_cnt]
    mov  di,  0x0500

.prossimo:
    test cx, cx
    jz   .fatto

    mov  [dap_lba], eax
    mov  [dap_off], di
    push cx
    push eax
    mov  ah, 0x42
    mov  dl, [unita]
    mov  si, dap
    int  0x13
    pop  eax
    pop  cx
    jc   errore_lettura

    inc  eax
    add  di, 512
    dec  cx
    jmp  .prossimo

.fatto:
    ; Stage 2 trova i parametri del kernel leggendoli da qui: questo
    ; settore resta a 0x7C00, intatto, per tutta la sua durata.
    mov  dl, [unita]
    jmp  0x0000:0x0500

errore_nonpatch:
    mov  si, msg_nonpatch
    jmp  errore
errore_noext:
    mov  si, msg_noext
    jmp  errore
errore_lettura:
    mov  si, msg_lettura

errore:
    call stampa
    mov  si, msg_stop
    call stampa
fermo:
    hlt
    jmp  fermo

stampa:
    lodsb
    or   al, al
    jz   .fine
    mov  ah, 0x0E
    mov  bx, 0x0007
    int  0x10
    jmp  stampa
.fine:
    ret

dap:
    db   0x10
    db   0
    dw   1
dap_off:
    dw   0x0500
    dw   0x0000
dap_lba:
    dd   0
    dd   0

unita:   db 0

msg_nonpatch: db 'Avvio: mappa assente, rilanciare install', 13, 10, 0
msg_noext:    db 'Avvio: BIOS senza estensioni INT13h', 13, 10, 0
msg_lettura:  db 'Avvio: lettura di Stage 2 fallita', 13, 10, 0
msg_stop:     db 'Sistema fermo.', 13, 10, 0

; Riempimento fino all'area di patch. Se il codice la superasse, NASM
; fallirebbe qui con "TIMES value ... is negative": e' il controllo, e
; scatta al momento giusto — l'assemblaggio, non l'avvio.
    times 0x1A0 - ($ - $$) db 0

; =============================================================================
; AREA DI PATCH — offset 0x1A0. La scrive l'installatore, e la legge anche
; Stage 2 (che trova questo settore ancora a 0x7C00).
;
; L'offset e' fisso e dichiarato qui una volta sola: e' un contratto fra
; tre pezzi di codice scritti in linguaggi diversi (questo assembly,
; bootloader/stage2/loader.asm e kernel/boot/bootinst.c). Se cambia qui e
; non la', l'unico sintomo e' un sistema che non parte.
;
; -----------------------------------------------------------------------
; PERCHE' IL KERNEL HA UNA LISTA DI INTERVALLI E STAGE 2 NO
;
; Su FAT un file contiguo e' un intervallo solo. Su ext2 non lo e' quasi
; mai, e non per frammentazione: il blocco di PUNTATORI viene allocato in
; mezzo ai dati, perche' serve prima del tredicesimo blocco. Un kernel da
; 147 KB appena copiato su un volume vergine sta cosi':
;
;     (0-11):74-85, (IND):86, (12-144):87-219
;
; cioe' due tratti contigui separati dall'indiretto. Con un intervallo
; solo non sarebbe caricabile NESSUN kernel da ext2.
;
; Stage 2 invece resta un intervallo solo, e non e' una svista: sta in
; ~1 KB, cioe' dentro i primi 12 blocchi diretti, dove nessun indiretto
; esiste ancora. E' il pezzo che DEVE essere trovato senza saper leggere
; niente, quindi la sua mappa deve poter stare in una manciata di byte.
; =============================================================================
magia:   dd 0x44485845     ; +0  'EXHD' — presente gia' nel binario: e' il
                           ;     segno con cui Stage 2 riconosce di essere
                           ;     stato avviato da disco e non da floppy
s2_lba:  dd 0              ; +4  LBA ASSOLUTO di Stage 2 sul disco
s2_cnt:  dw 0              ; +8  quanti settori (un intervallo solo)
k_size:  dd 0              ; +10 dimensione ESATTA del kernel in byte.
                           ;     Serve perche' i settori arrotondano per
                           ;     eccesso: Stage 2 copia a 0x100000 usando
                           ;     questo numero, e copiarne 511 in piu'
                           ;     sporcherebbe cio' che segue l'immagine.
k_next:  dw 0              ; +14 quanti intervalli del kernel seguono
k_ext:                     ; +16 { dd lba_assoluto; dw settori } x K_MAX_EXT
K_MAX_EXT equ 12           ;     12 x 6 = 72 byte, fino a 0x1F8.
                           ;     Bastano per un kernel di qualche MB: gli
                           ;     intervalli non crescono con la dimensione
                           ;     ma con il numero di blocchi indiretti
                           ;     attraversati, cioe' uno ogni 256 blocchi.
    times K_MAX_EXT db 0, 0, 0, 0, 0, 0

    times 510 - ($ - $$) db 0
    dw 0xAA55
