; =============================================================================
; tools/iso/prova-nasm16.asm — sedici bit, `org`, e nessun linker
; EX-OS — Extensible Operating System
;
; Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
;
; SPDX-License-Identifier: GPL-2.0-or-later
; =============================================================================
;
; ! QUESTO E' IL MOTIVO PER CUI NASM STA SUL CD, e prova-nasm.asm — che fa un
; eseguibile ELF come lo farebbe `as` — non lo dimostra. Qui non c'e' nessun
; linker, nessun formato oggetto e nessun sistema operativo sotto: c'e' un
; settore di avvio, cioe' 512 byte che il BIOS carica a 0x7c00 e a cui salta.
; E' il primo programma che scrive chi impara a scrivere un sistema operativo,
; ed e' scritto in NASM praticamente sempre.
;
;     nasm -f bin /cdrom/prova-nasm16.asm -o /avvio.bin
;     ndisasm -b 16 -o 0x7c00 /avvio.bin
;
; La seconda riga e' il giro completo dentro EX-OS: dai byte si torna alle
; istruzioni, e si vede che sono quelle che si erano scritte.
;
; ! `org 0x7c00` E' LA COSA CHE `ld` NON PUO' FARE PER TE. In prova-nasm.asm
; l'indirizzo lo decide il collegatore; qui non c'e' collegatore, e le
; etichette devono gia' nascere con l'indirizzo giusto dentro — se `msg`
; valesse 0x0010 invece di 0x7c10, il BIOS stamperebbe quel che si trova a
; 0x0010, che e' la tavola dei vettori d'interruzione.
;
; ! E NON E' UN SETTORE DI AVVIO DI EX-OS. EX-OS ne ha uno vero, in
; boot/stage1.asm, che carica lo stage 2 e passa in modo protetto. Questo
; stampa una riga e si ferma: serve a provare NASM, non ad avviare niente.
; =============================================================================

            bits 16
            org  0x7c00

avvio:      xor  ax, ax                 ; segmenti a zero: non si eredita
            mov  ds, ax                 ; niente da chi ci ha caricati
            mov  es, ax
            mov  ss, ax
            mov  sp, 0x7c00             ; la pila cresce all'ingiu' da qui

            mov  si, msg
scrivi:     lodsb                       ; al = [ds:si], si++
            test al, al
            jz   fermo
            mov  ah, 0x0e               ; BIOS: scrivi un carattere in teletype
            mov  bx, 0x0007             ; pagina 0, attributo normale
            int  0x10
            jmp  scrivi

fermo:      cli
            hlt
            jmp  fermo

msg:        db   "NASM: settore di avvio assemblato dentro EX-OS.", 13, 10, 0

            times 510 - ($ - $$) db 0   ; riempi fino al byte 510
            dw   0xaa55                 ; la firma che il BIOS cerca
