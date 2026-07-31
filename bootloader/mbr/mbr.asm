; =============================================================================
; bootloader/mbr/mbr.asm — EX-OS Master Boot Record (512 byte esatti)
; Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
; SPDX-License-Identifier: GPL-2.0-or-later
;
; Il primo settore di un disco rigido. Il BIOS lo carica a 0x7C00 e ci
; salta dentro con DL = numero dell'unita'.
;
; COMPITO UNICO: trovare la partizione attiva, caricarne il primo settore
; a 0x7C00 e saltarci. Niente filesystem, niente kernel: quelli sono
; problemi del settore di avvio della partizione.
;
; -----------------------------------------------------------------------
; PERCHE' SI RILOCA A 0x0600
;
; Il settore di avvio della partizione va caricato a 0x7C00 — e' li' che
; si aspetta di girare, perche' e' li' che lo metterebbe il BIOS se il
; disco non fosse partizionato. Ma a 0x7C00 c'e' questo codice. O si
; sposta lui, o si sovrascrive da solo a meta' esecuzione.
;
; 0x0600 e' la scelta convenzionale: e' sopra la BIOS Data Area (che
; finisce a 0x0500) e sotto tutto il resto.
;
; -----------------------------------------------------------------------
; PERCHE' I 64 BYTE DELLA TABELLA NON SONO IN QUESTO FILE
;
; Questo binario contiene SOLO il codice (byte 0-445) e la firma. La
; tabella delle partizioni (446-509) resta quella che c'e' gia' sul
; disco: l'installatore sovrascrive il codice e lascia la tabella intatta.
;
; Metterla qui, magari azzerata, significherebbe che installare l'avvio
; CANCELLA le partizioni — e con esse ogni dato del disco. La separazione
; non e' una comodita': e' cio' che rende l'operazione non distruttiva.
; =============================================================================
[BITS 16]
[ORG 0x0600]

start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00           ; lo stack cresce verso il basso, sotto di noi

    ; --- riloca 0x7C00 -> 0x0600 e prosegui laggiu' ---
    mov  si, 0x7C00
    mov  di, 0x0600
    mov  cx, 512
    cld                        ; movsb va in avanti: DF non e' garantito
    rep  movsb
    jmp  0x0000:riavvia

riavvia:
    sti
    mov  [unita], dl

    ; --- Le estensioni INT 13h ci sono? ---
    ; Servono per leggere con un LBA a 32 bit. Senza, una partizione oltre
    ; gli 8 GB non e' raggiungibile in CHS, e fallire in silenzio darebbe
    ; un sistema che "non si avvia" senza dire perche'.
    mov  ah, 0x41
    mov  bx, 0x55AA
    mov  dl, [unita]
    int  0x13
    jc   no_ext
    cmp  bx, 0xAA55
    jne  no_ext

    ; --- cerca la partizione attiva ---
    mov  si, 0x0600 + 446
    mov  cx, 4
cerca:
    cmp  byte [si], 0x80
    je   trovata
    add  si, 16
    loop cerca

    mov  si, msg_noattiva
    jmp  errore

trovata:
    mov  [voce], si            ; la convenzione vuole DS:SI = voce, a fine corsa

    ; LBA di partenza: dword a offset 8 della voce
    mov  eax, [si + 8]
    mov  [dap_lba], eax

    mov  ah, 0x42
    mov  dl, [unita]
    mov  si, dap
    int  0x13
    jc   errore_lettura

    ; --- il settore caricato e' avviabile? ---
    ; Senza questo controllo si salterebbe dentro 512 byte di dati a caso:
    ; il sintomo sarebbe un blocco o un riavvio a ciclo, senza alcun
    ; messaggio, e con l'aria di un guasto hardware.
    cmp  word [0x7C00 + 510], 0xAA55
    jne  errore_firma

    mov  dl, [unita]
    mov  si, [voce]
    jmp  0x0000:0x7C00

no_ext:
    mov  si, msg_noext
    jmp  errore

errore_lettura:
    mov  si, msg_lettura
    jmp  errore

errore_firma:
    mov  si, msg_firma

errore:
    call stampa
    mov  si, msg_stop
    call stampa
fermo:
    hlt
    jmp  fermo

; --- stampa la stringa NUL-terminata in DS:SI ---
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

; --- Disk Address Packet per INT 13h/42h ---
dap:
    db   0x10                  ; dimensione del pacchetto
    db   0
    dw   1                     ; settori da leggere
    dw   0x7C00                ; offset di destinazione
    dw   0x0000                ; segmento
dap_lba:
    dd   0
    dd   0

unita:      db 0
voce:       dw 0

msg_noattiva: db 'MBR: nessuna partizione attiva', 13, 10, 0
msg_noext:    db 'MBR: BIOS senza estensioni INT13h', 13, 10, 0
msg_lettura:  db 'MBR: lettura del settore di avvio fallita', 13, 10, 0
msg_firma:    db 'MBR: la partizione attiva non e avviabile', 13, 10, 0
msg_stop:     db 'Sistema fermo.', 13, 10, 0

; Il codice deve stare nei primi 446 byte: da 446 comincia la tabella
; delle partizioni, che questo file NON contiene e non deve toccare.
; Se lo superasse, NASM fallirebbe qui con "TIMES value ... is negative".
    times 446 - ($ - $$) db 0

; 64 byte di tabella: azzerati QUI, ma l'installatore non li scrive mai —
; copia solo i primi 446 byte di questo binario. Ci sono perche' il file
; risulti un settore da 512 byte ispezionabile con gli stessi strumenti
; usati per un MBR vero.
    times 64 db 0
    dw 0xAA55
