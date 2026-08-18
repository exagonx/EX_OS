# =============================================================================
# Makefile
# EX-OS — Extensible Operating System
#
# Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
#
# FIX BUG #1: Il kernel è linkato come ELF32 (necessario per GDB, simboli,
# e relocation) ma Stage 2 lo carica come flat binary a 0x100000.
# Aggiunto step objcopy: kernel.elf → kernel.bin (binario flat).
# Stage 2 salta a KERNEL_LOAD_ADDR = 0x100000 che ora è il primo byte
# eseguibile (kernel_entry in entry.asm), non l'header ELF.
# =============================================================================

# --- Toolchain ----------------------------------------------------------------
# CROSS := i686-linux-gnu  # cross-compiler (opzionale)
CROSS_LD_EMU := elf_i386

CC      := gcc
LD      := ld
OBJCOPY := objcopy
OBJDUMP := objdump
AS      := nasm

# --- Flag di compilazione -----------------------------------------------------
# =============================================================================
# ! LA CPU DI BASE E' IL PENTIUM 133 MMX, E IL COMPILATORE DEVE SAPERLO.
#
# Senza -march, GCC usa il suo default — i686 — ed emette `cmov`, che e' del
# Pentium PRO. Misurato il 17 agosto 2026: 591 cmov sparse nei binari. Su un
# Pentium MMX quelle istruzioni non esistono, e il sistema si sarebbe fermato
# con un'eccezione di istruzione non valida al primo programma.
#
# ! ERA UNA SCELTA DICHIARATA E NON APPLICATA. La CPU di base si era decisa;
# il compilatore continuava a produrre per un'altra. Un requisito che nessuno
# verifica non e' un requisito — per questo c'e' anche il bersaglio
# `verifica-cpu`, che rilegge i binari e ferma la costruzione se ci ritrova
# istruzioni oltre la base.
#
# -mtune=pentium-mmx e non solo -march: `march` dice COSA si puo' usare,
# `mtune` dice PER CHI ordinare le istruzioni. Sulla stessa CPU la seconda si
# paga in prestazioni, non in compatibilita'.
# =============================================================================
CPU_BASE := -march=pentium-mmx -mtune=pentium-mmx

CFLAGS := -m32 $(CPU_BASE) \
           -ffreestanding \
           -fno-builtin \
           -fno-stack-protector \
           -fno-pic \
           -Wall \
           -Wextra \
           -O2 \
           -g \
           -std=c11

CFLAGS += -fno-pie -fno-PIE

# -I drivers/kbd serve al TTY in-kernel: include kbd_proto.h, il protocollo
# IPC condiviso con il driver tastiera ring3 (drivers/kbd/kbd.c).
CFLAGS += -I kernel/include \
          -I lib/include \
          -I drivers/tty \
          -I drivers/kbd

LDFLAGS := -m $(CROSS_LD_EMU) \
            -nostdlib \
            --orphan-handling=discard

ASFLAGS_BIN   := -f bin
ASFLAGS_ELF   := -f elf32 -g

# --- Directory ----------------------------------------------------------------
BUILD_DIR   := build
DIST_DIR    := dist
BOOT_DIR    := bootloader
KERNEL_DIR  := kernel
DRIVER_DIR  := drivers
LIB_DIR     := lib
BIN_DIR     := bin
TOOLS_DIR   := tools
BOOT_DIR2   := bootloader

BUILD_STAGE1  := $(BUILD_DIR)/stage1
BUILD_STAGE2  := $(BUILD_DIR)/stage2
BUILD_KERNEL  := $(BUILD_DIR)/kernel
BUILD_DRIVERS := $(BUILD_DIR)/drivers
# Driver che NON vanno sul floppy ma solo sul CD EX-OS. La separazione è
# una directory e non un filtro nella regola del floppy perché
# tools/mkfloppy.sh prende tutto quello che trova in build/drivers/: un
# elenco di esclusioni lì dentro sarebbe una lista da ricordarsi di
# aggiornare ogni volta, e il floppy si riempirebbe il giorno che
# qualcuno se ne dimentica.
BUILD_DRIVERS_CD := $(BUILD_DIR)/drivers-cd
BUILD_BIN     := $(BUILD_DIR)/bin
# Programmi che vanno solo sul CD, stessa ragione di BUILD_DRIVERS_CD.
BUILD_BIN_CD  := $(BUILD_DIR)/bin-cd

# --- Header di protocollo condivisi -------------------------------------------
# ! DEFINITI QUI E NON ACCANTO ALLA REGOLA CHE LI PRODUCE. Le prerequisite
# di una regola vengono espanse quando make LEGGE la riga: una variabile
# definita piu' sotto risulta vuota, la dipendenza sparisce senza un
# errore, e modificare il protocollo smette di ricompilare i client. Ci
# sono gia' cascato con questi due — netdetect e nettest includono
# pci_proto.h e net_proto.h ma le loro regole stanno prima di quelle dei
# driver che li definivano.
# Tutto cio' che finisce dentro l'immagine floppy. Serve come PREREQUISITA
# della regola `floppy`, quindi va definita QUI e non accanto a quella
# regola: le prerequisite si espandono quando make legge la riga, e una
# variabile definita piu' sotto risulterebbe vuota — che e' esattamente il
# difetto che questa riga esiste per chiudere.
#
# =============================================================================
# ! COSA VA SUL FLOPPY, E COSA NO
#
# Il floppy porta il SISTEMA: avviarsi, preparare un disco, installarsi,
# leggere e scrivere file. Partizionatore (fdisk), formattatore (mkfs),
# controllore (chkdsk), montaggio, installatore, editor. Il driver del
# floppy e quello della tastiera, che servono a partire.
#
# ! I DRIVER AGGIUNTIVI NON CI VANNO — vanno sul CD di EX-OS. Rete
# (pci, ne2k, pcnet, ip) e tutto cio' che verra' dopo si costruiscono in
# $(BUILD_DRIVERS_CD), che il floppy non guarda nemmeno.
#
# Non e' una preferenza: in 1.44 MB non ci stanno, e il modo in cui non ci
# stanno e' il peggiore. mcopy fallisce a meta' dell'elenco, l'immagine
# resta priva di qualche file scelto dall'ordine alfabetico, e il sistema
# si avvia fino al punto in cui gli serve quello che manca. Per questo
# `make verify` controlla la regola invece di fidarsi.
#
# Il CD-ROM non ha un driver in /dev: ATAPI e ISO 9660 stanno DENTRO il
# kernel (kernel/block/atapi.c, kernel/fs/iso9660.c), perche' il kernel
# deve poterci montare la radice prima che esista un processo.
# =============================================================================
PROGRAMMI_FLOPPY := shell hello id chmod ls mem stack disk libctest fdisk mkfs trunc chkdsk rename rm_prog mv_prog uname_prog mount_prog cp_prog install_prog textline gfedit mkdir_prog rmdir_prog delete_prog hwconfig hwinfo cmp_prog shmtest polltest toolinst login help_prog keymap libc testo mouse_prog floppy_drv kbd_drv svga_drv vgaprova_drv \
                    pci_drv mouseser_drv uhci_drv xhci_drv

# =============================================================================
# ! DOVE VA OGNI PROGRAMMA — SI DICHIARA QUI, E NON C'E' UN ALTROVE
#
# Tre destinazioni, e ognuna risponde a una domanda diversa:
#
#   PROGRAMMI_FLOPPY   il SISTEMA DI BASE. Ci sta in 1.44 MB e serve a
#                      partire, preparare un disco, installarsi, leggere e
#                      scrivere file. Finisce sul floppy E sul CD di EX-OS:
#                      il CD e' un superinsieme del floppy, non un'altra
#                      cosa.
#
#   PROGRAMMI_CD       il resto del SISTEMA OPERATIVO: rete (netdetect,
#                      ping, ftp, telnet, dhcp, host...) e utilita' che sul
#                      floppy non ci stanno o non avrebbero senso senza i
#                      driver che sul floppy non ci stanno. Solo su
#                      dist/exos.iso.
#
#   il CD degli strumenti  i LINGUAGGI: gcc, g++, cpp, cc1, fbc, as, ld,
#                      libstdc++, OpenSSL. Non si dichiarano qui perche'
#                      non si costruiscono qui — sono binari per i386-exos
#                      prodotti fuori da questo Makefile, e la loro regola
#                      sta nel blocco di dist/exos-tools.iso.
#
# ! UN PROGRAMMA NUOVO VA AGGIUNTO A UNA DELLE DUE LISTE, SEMPRE. Non
# esiste la terza possibilita' «per adesso lo lascio fuori»: un sorgente
# sotto bin/ che non e' in nessuna lista non viene compilato e non finisce
# su nessuna immagine, e nessuno se ne accorge finche' non lo cerca sulla
# macchina. E' successo con bin/xcp/, rimasto fuori da tutto per mesi.
# `make verifica-programmi` — che le due ISO eseguono da sole — confronta
# le liste con il contenuto di bin/ e si ferma dicendo quali mancano.
# =============================================================================
PROGRAMMI_CD := netdetect nettest ping ipcfg dhcp host tcptest ftp telnet xcp winprova exwincmd
# Le applicazioni grafiche non stanno in PROGRAMMI_CD: hanno un albero loro.
PROGRAMMI_EXWIN := exwin_so exdlg_so eximg_so pm filemgr edit term

# I driver, con la stessa regola dei programmi. Quelli di base stanno gia'
# dentro PROGRAMMI_FLOPPY (floppy_drv, kbd_drv): sul floppy servono a
# partire. Gli altri sono il resto del sistema e vanno solo sul CD.
#
# ! pcnet_drv NON ERA IN NESSUNA LISTA fino ad agosto 2026: si costruiva
# solo di rimbalzo, perche' la ricetta di exos.iso lo nominava fra le
# proprie prerequisite. `make all` non lo faceva, e chi costruiva senza
# passare dalla ISO si ritrovava un driver in meno senza un messaggio.
DRIVER_CD := ne2k_drv pcnet_drv e1000_drv ip_drv wserver_drv

# I driver che sul floppy NON devono comparire. Serve a `make verify`.
DRIVER_SOLO_CD := pci.drv ne2k.drv pcnet.drv ip.drv

# Directory di drivers/ che NON producono un .drv, con il perche'. Serve a
# verifica-programmi, che senza le segnalerebbe come driver dimenticati.
#
#   net   sono solo header di protocollo (net_proto.h, ip_proto.h): li
#         includono i client, non c'e' niente da caricare.
#   tty   e' compilato DENTRO il kernel ($(BUILD_KERNEL)/tty.o) e
#         inizializzato al PASSO 14; non e' mai stato un modulo ring3.
#   usb   e' la META' COMUNE dello stack USB — descrittori, configurazioni,
#         HID «boot», hub — compilata dentro uhci.drv e xhci.drv. Non e' un
#         driver e non lo diventera': non guida nessun hardware, e infatti
#         non nomina nessun controller.
NON_DRIVER := net tty usb

PCI_DRV_PROTO := drivers/pci/pci_proto.h
NET_PROTO     := drivers/net/net_proto.h
IP_PROTO      := drivers/net/ip_proto.h
# Risolutore DNS: e' un MODULO compilato dentro i programmi, non un
# servizio — DNS sta sopra UDP come DHCP, e un errore li' dentro deve far
# fallire un comando, non spegnere la rete. Vedi lib/include/dns.h.
DNS_SRC       := lib/dns.c
DNS_HDR       := lib/include/dns.h
# «Cosa manca per accendere la rete», in un posto solo: prima ogni comando
# se n'era scritta una versione, e chi si fermava a meta' leggeva
# istruzioni diverse a seconda del comando con cui ci aveva provato.
RETE_SRC      := lib/rete.c
RETE_HDR      := lib/include/rete.h
BUILD_OBJ     := $(BUILD_DIR)/obj
BUILD_LIB     := $(BUILD_DIR)/lib

# --- File output --------------------------------------------------------------
STAGE1_BIN  := $(BUILD_DIR)/stage1.bin
STAGE2_BIN  := $(BUILD_DIR)/stage2.bin

# FIX BUG #1: distinguiamo kernel ELF (per debug/simboli) da kernel flat binary
# (per il boot). Stage 2 carica e salta al flat binary.
KERNEL_ELF  := $(BUILD_DIR)/kernel.elf
KERNEL_BIN  := $(BUILD_DIR)/kernel.bin

FLOPPY_IMG  := $(DIST_DIR)/floppy.img

# --- Colori output ------------------------------------------------------------
RED    := \033[0;31m
GREEN  := \033[0;32m
YELLOW := \033[1;33m
BLUE   := \033[0;34m
CYAN   := \033[0;36m
NC     := \033[0m

define log_phase
	@printf "%s=== %s ===%s\n" "" "$(1)" ""
endef

define log_ok
	@echo "[OK] $(1)"
endef

define log_info
	@echo "[..] $(1)"
endef

# =============================================================================
# TARGET PRINCIPALE
# =============================================================================

# --- Shell utente (/bin/sh) ---------------------------------------------------
SHELL_SRC   := bin/sh/shell.c
SHELL_START := bin/sh/start.S
SHELL_BIN   := $(BUILD_BIN)/sh
SHELL_LD    := bin/sh/shell.ld

# -ffunction-sections / -fdata-sections mettono ogni funzione e ogni dato
# nella propria sezione, e il link con --gc-sections butta quelle che
# nessuno raggiunge. Serve da quando la libc ha stdio, allocatore e
# conversioni: senza, OGNI programma di /bin si porta dentro l'intera
# libreria — `ls` cresceva da 12 a 25 KB per funzioni che non chiama, e su
# un floppy da 1.44 MB quel raddoppio si sente. I linker script
# raccoglievano gia' .text.* / .rodata.* / .data.* / .bss.*, quindi non e'
# servito toccarli.
# ! IL CANARINO E' ACCESO NEI PROGRAMMI UTENTE dal 17 agosto 2026, e nel
# kernel no: il kernel non ha ancora un __stack_chk_fail suo, e accenderlo
# senza darebbe un simbolo mancante al collegamento — o peggio, uno preso dalla
# libc utente, che nel kernel non c'e'.
#
# -fstack-protector-strong e non -all: «all» protegge OGNI funzione, comprese
# quelle senza un solo array locale, e su un floppy da 1.44 MB si paga in
# spazio quello che non si guadagna in difesa. «strong» protegge quelle che un
# overflow puo' davvero toccare. Vedi lib/libc_avvio.c per il perche' serve.
# ! -mstack-protector-guard=global, E SENZA NON PARTE NIENTE. Su i386 GCC
# cerca il canarino in %gs:0x14 — lo slot TLS di Linux — non nella variabile
# __stack_chk_guard. Su EX-OS quel segmento non e' impostato per i programmi, e
# ogni funzione protetta faceva
#
#     [FAULT] PID 6 'sh2': page fault a 0x00000014 (lettura)
#
# cioe' l'offset 0x14 letto da un segmento che non c'e'. Il numero lo diceva.
#
# ! E NON ERA UN PROBLEMA DI CPU, benche' fosse arrivato insieme al cambio di
# architettura: era la CONVENZIONE su dove sta il canarino. Le due cose si
# somigliano solo perche' capitano nello stesso momento — ed e' proprio per
# questo che conviene guardare l'indirizzo prima della teoria.
CFLAGS_USER := -m32 $(CPU_BASE) -ffreestanding -fno-builtin \
               -fstack-protector-strong -mstack-protector-guard=global \
               -fno-pic -fno-pie -Wall -O2 -std=c11 -nostdlib \
               -ffunction-sections -fdata-sections

# =============================================================================
# ! IL SEGNAPOSTO DEI FLAG — un'uscita che sa di essere scaduta
#
# Cambiare CFLAGS_USER non ricostruisce niente: i programmi non dipendono dal
# Makefile, e make non ha modo di sapere che le OPZIONI sono cambiate. Il 17
# agosto 2026 e' costato mezz'ora: la CPU di base era stata portata a
# pentium-mmx, la costruzione era passata senza errori, e nei binari restavano
# 273 `cmov` — istruzioni che su quella CPU non esistono.
#
# Il nome del segnaposto contiene l'IMPRONTA dei flag: cambiandoli, il file
# cambia nome, non esiste, e tutto cio' che ci dipende si rifa'. E' lo stesso
# schema del segnaposto della risoluzione SVGA, e per la stessa ragione.
#
# ! E' LA SETTIMA VOLTA CON QUESTA FORMA in questo progetto. Le altre sei sono
# elencate in RIPRENDERE.md. Ogni volta il sintomo e' lo stesso: una
# costruzione che riesce e non fa quello che si e' chiesto.
# =============================================================================
# ! I DRIVER SI COLLEGANO DA SOLI, SENZA LA libc, quindi non hanno
# __stack_chk_fail e il canarino li' non si puo' accendere. Non e' una
# dimenticanza: un driver e' un modulo ET_DYN collegato per conto suo, e dargli
# il canarino vuol dire dargli anche chi lo definisce — cioe' una copia di quel
# codice per ogni driver. Vale la pena il giorno che un driver leggera' dati da
# fuori; oggi leggono registri.
CFLAGS_DRV := $(CFLAGS_USER) -fno-stack-protector

IMPRONTA_FLAG := $(shell echo "$(CFLAGS_USER) $(CFLAGS)" | md5sum | cut -c1-10)
SEGNO_FLAG    := $(BUILD_DIR)/.flag-$(IMPRONTA_FLAG)

$(SEGNO_FLAG):
	@mkdir -p $(BUILD_DIR) $(BUILD_OBJ)
	@rm -f $(BUILD_DIR)/.flag-*
	@# ! GLI OGGETTI SI BUTTANO, non basta il segnaposto. Il segnaposto viene
	@# creato PRIMA che i suoi dipendenti si ricostruiscano: se la costruzione
	@# fallisce a meta', al giro dopo il segnaposto c'e' gia' e make considera
	@# tutto in ordine — con meta' dei binari fatti coi flag vecchi. E'
	@# successo il 17 agosto 2026, e il sintomo era 272 `cmov` rimaste in
	@# binari che nessuno aveva ricompilato.
	@#
	@# Buttando gli oggetti la ricostruzione riparte comunque, perche' cio'
	@# che manca non torna a esistere da solo.
	@# ! ANCHE GLI OGGETTI DEL KERNEL, che non stanno in build/obj. Dimenticarli
	@# ha lasciato 207 `cmov` dentro kernel.elf mentre tutti i programmi erano
	@# gia' a posto — e il kernel e' proprio quello che non puo' permettersi
	@# un'istruzione che la CPU non ha.
	@rm -f $(BUILD_OBJ)/*.o
	@find $(BUILD_DIR)/kernel -name '*.o' -delete 2>/dev/null || true
	@rm -f $(BUILD_DIR)/kernel.elf $(BUILD_DIR)/kernel.bin
	@touch $@
	@echo "[flag] opzioni del compilatore cambiate: oggetti buttati, si rifa' tutto"


# ! start.S NON E' OPZIONALE E NON E' lib/start.S. La shell non collega
# la libc — parla col kernel attraverso i propri involucri sh_* — quindi
# non puo' usare l'ingresso comune, che chiama _libc_start. Questo prende
# argc e argv dallo stack e li passa a shell_main: senza, `sh -c` non
# esiste, e senza quello non esistono system() e popen() nella libc.
$(SHELL_BIN): $(SHELL_SRC) $(SHELL_START) $(SHELL_LD) lib/include/spawn_abi.h $(SEGNO_FLAG)
	@echo "=== Compilazione Shell utente /bin/sh ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -I drivers/pci -I drivers/net -c $(SHELL_SRC) -o $(BUILD_OBJ)/shell.o
	$(CC) -m32 -c $(SHELL_START) -o $(BUILD_OBJ)/sh_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(SHELL_LD) $(BUILD_OBJ)/sh_start.o $(BUILD_OBJ)/shell.o -o $@
	@echo "[OK] Shell compilata: $@"

.PHONY: shell
shell: dirs $(SHELL_BIN)

# --- Programma utente di esempio (/bin/hello) ---------------------------------
HELLO_SRC := bin/hello/hello.c
HELLO_BIN := $(BUILD_BIN)/hello
HELLO_LD  := bin/hello/hello.ld

# ! hello NON COLLEGA LA libc — ha un _start suo — quindi non ha
# __stack_chk_guard, e il canarino li' non si puo' accendere senza dargli anche
# chi lo definisce. E' un programma dimostrativo che non legge niente da fuori:
# non e' il posto dove serve. La shell, che invece legge righe scritte da una
# persona, il canarino se lo definisce da se'.
$(HELLO_BIN): $(HELLO_SRC) $(HELLO_LD) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/hello ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -fno-stack-protector -c $(HELLO_SRC) -o $(BUILD_OBJ)/hello.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(HELLO_LD) $(BUILD_OBJ)/hello.o -o $@
	@echo "[OK] hello compilato: $@"

.PHONY: hello
hello: dirs $(HELLO_BIN)

# --- Sorgenti libc ------------------------------------------------------------
# ATTENZIONE: queste variabili devono restare definite PRIMA di qualunque
# regola che le usi come prerequisito. I prerequisiti sono espansi da make nel
# momento in cui legge la regola: fino a luglio 2026 LIBC_SRC era definita
# più in basso, dopo la regola di $(LS_BIN), quindi $(LIBC_SRC) in quella
# lista si espandeva a stringa vuota. Risultato: /bin/ls NON veniva
# ricompilato quando si modificava lib/libc.c, e sul floppy finiva un
# binario vecchio (un fix a printf() sembrava "non avere effetto").
LIBC_SRC   := lib/libc.c
# Gli header sono una dipendenza vera: modificarli cambia i prototipi e i
# tipi visti da ogni programma. Senza questa riga, cambiare libc.h non
# ricompilava niente — la stessa trappola gia' documentata per version.h.
#
# E' un wildcard e non l'elenco di libc.h soltanto, perche' da agosto 2026
# in lib/include ci sono anche gli header con i nomi standard (<stdio.h>,
# <stdint.h>...). Il CD degli strumenti li copia TUTTI con lib/include/*.h:
# nominarne uno solo qui vorrebbe dire un CD che non si rifa' quando cambia
# uno degli altri, cioe' header vecchi consegnati a chi compila su EX-OS.
LIBC_HDR   := $(wildcard lib/include/*.h) $(wildcard lib/include/sys/*.h)
LIBC_SO    := $(BUILD_LIB)/libc.so
LIBC_LD    := lib/libc.ld
LIBC_START := lib/start.S

# =============================================================================
# I NOMI DELLA LIBC CONDIVISA — dichiarati QUI, prima di ogni regola che li usa
#
# ! STAVANO ACCANTO ALLE LORO REGOLE, seicento righe piu' in basso, e per
# questo il primo tentativo su /bin/libctest E' PASSATO SENZA RICOLLEGARE
# NIENTE: make espande l'elenco dei prerequisiti mentre LEGGE il file, quindi
# li' `$(LIBC_PONTI_OBJ)` valeva stringa vuota. La ricetta era giusta e non e'
# mai stata eseguita — il binario vecchio e' rimasto, e le 294 prove che
# credevo di aver fatto sulla libc condivisa le avevo fatte su quella statica.
#
# ! E' LA QUINTA VOLTA CHE UN'USCITA NON SA DI ESSERE SCADUTA. Le altre
# quattro: le dipendenze finte del bersaglio floppy, uhci.drv mancante fra
# quelle dell'ISO, SVGA non prerequisito di Stage 2, e le immagini che non
# dipendevano dal Makefile. Un prerequisito scritto con una variabile vuota non
# e' un prerequisito debole: non esiste, e nessuno lo dice.
# =============================================================================
LIBC_AVVIO   := lib/libc_avvio.c
LIBC_PONTI_C := lib/libc_ponti.c
GEN_DIR      := $(BUILD_DIR)/gen

LIBC_SO_OBJ  := $(BUILD_OBJ)/libc_per_so.o
GEN_ESPORTA  := $(GEN_DIR)/libc_esporta.S
GEN_PONTI    := $(GEN_DIR)/libc_ponti.S

# I quattro oggetti che un programma collega AL POSTO della libc statica:
# i ponti, il risolutore, l'avvio e il cercatore di simboli.
LIBC_PONTI_OBJ := $(BUILD_OBJ)/libc_ponti_asm.o $(BUILD_OBJ)/libc_ponti_c.o \
                  $(BUILD_OBJ)/libc_ponti_avvio.o $(BUILD_OBJ)/libc_ponti_exlib.o

# --- Programma utente /bin/ls --------------------------------------------------
# A differenza di hello e shell, ls usa la libc del progetto invece di
# reimplementare le syscall a mano. Il loader ELF del kernel non supporta
# ancora il link dinamico per programmi normali (solo per i driver, via
# dynlink.c/drvmgr.c) — quindi qui compiliamo ls.c E lib/libc.c insieme,
# senza -shared/-fPIC, e li linkiamo in un unico binario statico
# autosufficiente, come hello e shell. Stesso schema andrà riusato per
# qualunque futuro programma in /bin che voglia usare la libc senza
# attendere il vero link dinamico runtime.
LS_SRC    := bin/ls/ls.c
LS_BIN    := $(BUILD_BIN)/ls
LS_LD     := bin/ls/ls.ld
LS_START  := $(LIBC_START)

$(LS_BIN): $(LS_SRC) $(LS_LD) $(LIBC_SRC) $(LS_START) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/ls ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(LS_SRC)   -o $(BUILD_OBJ)/ls_main.o
	$(CC) -m32 -c $(LS_START)                          -o $(BUILD_OBJ)/ls_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(LS_LD) \
	    $(BUILD_OBJ)/ls_start.o \
	    $(BUILD_OBJ)/ls_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] ls compilato: $@"

.PHONY: ls
ls: dirs $(LS_BIN)

# --- Programma utente /bin/mem ------------------------------------------------
# Stato della memoria fisica per fascia (convenzionale/superiore/estesa).
# Stesso schema di /bin/ls: mem.c + lib/libc.c + lib/start.S in un unico
# binario statico. tools/mkfloppy.sh copia da solo tutto build/bin/*, quindi
# non serve aggiungerlo altrove: basta che compaia in "all".
MEM_SRC   := bin/mem/mem.c
MEM_BIN   := $(BUILD_BIN)/mem
MEM_LD    := bin/mem/mem.ld
MEM_START := $(LIBC_START)

$(MEM_BIN): $(MEM_SRC) $(MEM_LD) $(LIBC_SRC) $(MEM_START) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/mem ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(MEM_SRC)  -o $(BUILD_OBJ)/mem_main.o
	$(CC) -m32 -c $(MEM_START)                         -o $(BUILD_OBJ)/mem_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MEM_LD) \
	    $(BUILD_OBJ)/mem_start.o \
	    $(BUILD_OBJ)/mem_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] mem compilato: $@"

# --- Programma utente /bin/stack ----------------------------------------------
# Mostra come sono allocati gli stack di ogni processo: impegnato (RAM reale)
# contro riservato (solo spazio di indirizzamento). Stesso schema di /bin/ls.
STACK_SRC   := bin/stack/stack.c
STACK_BIN   := $(BUILD_BIN)/stack
STACK_LD    := bin/stack/stack.ld

$(STACK_BIN): $(STACK_SRC) $(STACK_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/stack ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(STACK_SRC) -o $(BUILD_OBJ)/stack_main.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/stack_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(STACK_LD) \
	    $(BUILD_OBJ)/stack_start.o \
	    $(BUILD_OBJ)/stack_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] stack compilato: $@"

# --- Programma utente /bin/libctest -------------------------------------------
# Prova la libc DENTRO EX-OS: allocatore, flussi bufferizzati, printf,
# setjmp/longjmp, conversioni. Esce con 0 se tutto passa, quindi vale anche
# come prova automatica e non solo come stampa da guardare.
#
# Perche' sta nel floppy e non fra gli strumenti opzionali: e' la rete di
# sicurezza della libreria su cui verranno costruiti gli altri programmi,
# e deve poter girare sulla macchina che ha il problema — che magari e'
# proprio quella senza lettore CD.
LIBCTEST_SRC := bin/libctest/libctest.c
LIBCTEST_BIN := $(BUILD_BIN)/libctest
LIBCTEST_LD  := bin/libctest/libctest.ld

# ! COLLEGATO ALLA LIBC CONDIVISA, ed e' la prova che conta piu' di tutte: 294
# prove che toccano stringhe, stdio, allocatore, directory, pipe e conversioni.
# Se i ponti sbagliassero anche un solo argomento, qui si vedrebbe — e si
# vedrebbe COME, invece che come un fault in un programma qualunque.
#
# ! E NON SI COLLEGA --gc-sections IN MODO DIVERSO dagli altri: libctest chiama
# quasi tutta la libc, quindi tiene quasi tutti i ponti. E' il caso peggiore
# per la dimensione, ed e' giusto misurarlo su di lui.
$(LIBCTEST_BIN): $(LIBCTEST_SRC) $(LIBCTEST_LD) $(LIBC_PONTI_OBJ) $(LIBC_START) \
                 $(LIBC_HDR) $(LIBC_SO) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/libctest (libc CONDIVISA) ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(LIBCTEST_SRC) -o $(BUILD_OBJ)/libctest_main.o
	$(CC) -m32 -c $(LIBC_START)                            -o $(BUILD_OBJ)/libctest_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(LIBCTEST_LD) \
	    $(BUILD_OBJ)/libctest_start.o $(BUILD_OBJ)/libctest_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] libctest compilato: $@"

.PHONY: libctest
libctest: dirs $(LIBCTEST_BIN)

# --- Programma utente /bin/disk -----------------------------------------------
# Mostra dischi e partizioni. SOLA LETTURA. Stesso schema di /bin/ls.
DISK_SRC   := bin/disk/disk.c
DISK_BIN   := $(BUILD_BIN)/disk
DISK_LD    := bin/disk/disk.ld

$(DISK_BIN): $(DISK_SRC) $(DISK_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/disk ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(DISK_SRC) -o $(BUILD_OBJ)/disk_main.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/disk_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(DISK_LD) \
	    $(BUILD_OBJ)/disk_start.o $(BUILD_OBJ)/disk_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] disk compilato: $@"

.PHONY: disk
disk: dirs $(DISK_BIN)

# --- Programma utente /bin/fdisk ----------------------------------------------
# Partizionatore MBR interattivo. SCRIVE: a differenza di /bin/disk, che
# resta in sola lettura di proposito. Stesso schema di /bin/ls.
FDISK_SRC  := bin/fdisk/fdisk.c
FDISK_BIN  := $(BUILD_BIN)/fdisk
FDISK_LD   := bin/fdisk/fdisk.ld

$(FDISK_BIN): $(FDISK_SRC) $(FDISK_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/fdisk ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(FDISK_SRC) -o $(BUILD_OBJ)/fdisk_main.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/fdisk_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(FDISK_LD) \
	    $(BUILD_OBJ)/fdisk_start.o $(BUILD_OBJ)/fdisk_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] fdisk compilato: $@"

.PHONY: fdisk
fdisk: dirs $(FDISK_BIN)

# --- Programma utente /bin/mkfs -----------------------------------------------
# Formattatore FAT16/FAT32. Scrive dentro una partizione tramite
# SYS_BLKWRITE, che accetta solo dispositivi di tipo partizione: la tabella
# delle partizioni resta irraggiungibile da qui. Stesso schema di /bin/ls.
# Due sorgenti: mkfs.c (contorno + ramo FAT) e ext2.c (ramo ext2). Sono
# separati perche' i due formati non hanno niente in comune, non per
# dimensione del file.
MKFS_SRC   := bin/mkfs/mkfs.c
MKFS_EXT2  := bin/mkfs/ext2.c
MKFS_HDR   := bin/mkfs/ext2.h
MKFS_BIN   := $(BUILD_BIN)/mkfs
MKFS_LD    := bin/mkfs/mkfs.ld

$(MKFS_BIN): $(MKFS_SRC) $(MKFS_EXT2) $(MKFS_HDR) $(MKFS_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/mkfs ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I bin/mkfs -c $(MKFS_SRC)  -o $(BUILD_OBJ)/mkfs_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I bin/mkfs -c $(MKFS_EXT2) -o $(BUILD_OBJ)/mkfs_ext2.o
	$(CC) -m32 -c $(LIBC_START)                                     -o $(BUILD_OBJ)/mkfs_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MKFS_LD) \
	    $(BUILD_OBJ)/mkfs_start.o $(BUILD_OBJ)/mkfs_main.o \
	    $(BUILD_OBJ)/mkfs_ext2.o  $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] mkfs compilato: $@"

# --- /bin/hwconfig: guarda la macchina e scrive la configurazione ------------
# Sta sul FLOPPY perche' e' uno strumento di sistema come fdisk e install:
# serve proprio quando si prepara una macchina, cioe' quando il CD magari
# non c'e' ancora. Senza /dev/pci.drv (che sta sul CD) configura montaggi e
# moduli e dice che la parte di rete non ha potuto verificarla.
HWCONFIG_SRC := bin/hwconfig/hwconfig.c
HWCONFIG_BIN := $(BUILD_BIN)/hwconfig
HWCONFIG_LD  := bin/hwconfig/hwconfig.ld

$(HWCONFIG_BIN): $(HWCONFIG_SRC) $(PCI_DRV_PROTO) $(HWCONFIG_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/hwconfig ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -I drivers/kbd -c $(HWCONFIG_SRC) -o $(BUILD_OBJ)/hwconfig_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/hwconfig_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(HWCONFIG_LD) \
	    $(BUILD_OBJ)/hwconfig_start.o $(BUILD_OBJ)/hwconfig_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] hwconfig compilato: $@"

.PHONY: hwconfig
hwconfig: dirs $(HWCONFIG_BIN)

# --- /bin/hwinfo: inventario dell'hardware, per chi scrive un driver ---------
#
# ! NON E' UN DOPPIONE DI hwconfig, e la differenza sta nella domanda a cui
# rispondono. hwconfig chiede «come accendo questa macchina» e per deciderlo
# gli basta sapere SE una scheda c'e'. hwinfo chiede «cosa devo scrivere per
# pilotarla», e allora servono gli identificatori su cui agganciarsi, la
# classe, i BAR con lo spazio in cui rispondono, l'IRQ e il registro comando.
# Sono dati diversi, e metterli nello stesso programma vorrebbe dire un
# hwconfig che stampa mezza scheda tecnica mentre chiede se scrivere
# kernel.cfg.
#
# Sta sul FLOPPY come hwconfig: serve quando si prepara una macchina nuova,
# che e' esattamente quando il CD magari non c'e' ancora e ci si accorge che
# una scheda non ha driver.
HWINFO_SRC := bin/hwinfo/hwinfo.c
HWINFO_BIN := $(BUILD_BIN)/hwinfo
HWINFO_LD  := bin/hwinfo/hwinfo.ld

$(HWINFO_BIN): $(HWINFO_SRC) $(PCI_DRV_PROTO) $(HWINFO_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/hwinfo ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -c $(HWINFO_SRC) -o $(BUILD_OBJ)/hwinfo_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/hwinfo_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(HWINFO_LD) \
	    $(BUILD_OBJ)/hwinfo_start.o $(BUILD_OBJ)/hwinfo_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] hwinfo compilato: $@"

.PHONY: hwinfo
hwinfo: dirs $(HWINFO_BIN)

# --- /bin/cmp: confronto byte per byte --------------------------------------
#
# ! NON E' UN DOPPIONE DI NIENTE: EX-OS non aveva nessun modo di dire se due
# file sono uguali. E' servito per chiudere il PUNTO FISSO di fbc — costruire
# il compilatore con se' stesso finche' due generazioni non danno lo stesso
# binario — e confrontare le DIMENSIONI non basta: due binari possono pesare
# uguale e differire, ed e' proprio il caso che il punto fisso deve escludere.
#
# Serve anche ai makefile: `cmp -s nuovo vecchio || cp nuovo vecchio` e' il
# modo classico di non ritoccare la data di un file che non e' cambiato.
#
# Sta sul floppy perche' e' minuscolo e perche' serve quando si verifica una
# costruzione, cioe' quando il CD magari non c'e'.
CMP_SRC := bin/cmp/cmp.c
CMP_BIN := $(BUILD_BIN)/cmp
CMP_LD  := bin/cmp/cmp.ld

$(CMP_BIN): $(CMP_SRC) $(CMP_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/cmp ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(CMP_SRC) -o $(BUILD_OBJ)/cmp_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/cmp_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(CMP_LD) \
	    $(BUILD_OBJ)/cmp_start.o $(BUILD_OBJ)/cmp_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] cmp compilato: $@"

.PHONY: cmp_prog
cmp_prog: dirs $(CMP_BIN)

# --- /bin/shmtest: la prova della memoria condivisa fra processi -------------
#
# Si rilancia da solo con -f per avere un secondo processo: senza fork() e'
# l'unico modo di averne due che si conoscono. I valori attesi sono funzioni
# dei soli indici, quindi ogni byte ha una risposta giusta nota in anticipo.
SHMTEST_SRC := bin/shmtest/shmtest.c
SHMTEST_BIN := $(BUILD_BIN)/shmtest
SHMTEST_LD  := bin/shmtest/shmtest.ld

$(SHMTEST_BIN): $(SHMTEST_SRC) $(SHMTEST_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/shmtest ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(SHMTEST_SRC) -o $(BUILD_OBJ)/shmtest_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/shmtest_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(SHMTEST_LD) \
	    $(BUILD_OBJ)/shmtest_start.o $(BUILD_OBJ)/shmtest_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] shmtest compilato: $@"

.PHONY: shmtest
shmtest: dirs $(SHMTEST_BIN)

# --- /bin/polltest: la prova di poll() e select() ----------------------------
#
# Si rilancia da solo con -f per avere un secondo processo che lo svegli. La
# prova che conta e' che il figlio veda il padre BLOCKED: senza quella, ogni
# altra riga passerebbe identica anche con un'attesa attiva.
POLLTEST_SRC := bin/polltest/polltest.c
POLLTEST_BIN := $(BUILD_BIN)/polltest
POLLTEST_LD  := bin/polltest/polltest.ld

$(POLLTEST_BIN): $(POLLTEST_SRC) $(POLLTEST_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/polltest ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(POLLTEST_SRC) -o $(BUILD_OBJ)/polltest_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/polltest_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(POLLTEST_LD) \
	    $(BUILD_OBJ)/polltest_start.o $(BUILD_OBJ)/polltest_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] polltest compilato: $@"

.PHONY: polltest
polltest: dirs $(POLLTEST_BIN)

# --- /bin/toolinst: installa gli strumenti del CD tools ----------------------
#
# ! STA QUI E NON SUL CD TOOLS. Il CD degli strumenti non e' avviabile: e'
# un contenitore di binari per i386-exos. Chi lo installa ha gia' un EX-OS
# acceso — da floppy, dal CD di sistema o da un disco — e il programma che
# fa l'installazione deve stare LI'. Sul CD tools sarebbe un eseguibile che
# nessuno puo' raggiungere finche' non ha gia' fatto a mano il lavoro che
# serviva a lui.
TOOLINST_SRC := bin/toolinst/toolinst.c
TOOLINST_BIN := $(BUILD_BIN)/toolinst
TOOLINST_LD  := bin/toolinst/toolinst.ld

$(TOOLINST_BIN): $(TOOLINST_SRC) $(TOOLINST_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/toolinst ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(TOOLINST_SRC) -o $(BUILD_OBJ)/toolinst_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/toolinst_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TOOLINST_LD) \
	    $(BUILD_OBJ)/toolinst_start.o $(BUILD_OBJ)/toolinst_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] toolinst compilato: $@"

.PHONY: toolinst
toolinst: dirs $(TOOLINST_BIN)

# --- /bin/login: chiede chi sei e lancia la shell -----------------------------
# Sistema di base: senza, una macchina con l'autenticazione accesa non si
# avvia. Va quindi sul floppy insieme alla shell.
LOGIN_SRC := bin/login/login.c
LOGIN_BIN := $(BUILD_BIN)/login
LOGIN_LD  := bin/login/login.ld

$(LOGIN_BIN): $(LOGIN_SRC) $(KBD_DRV_PROTO) $(LOGIN_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/login ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -c $(LOGIN_SRC) -o $(BUILD_OBJ)/login_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)  -o $(BUILD_OBJ)/login_libc.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/login_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(LOGIN_LD) \
	    $(BUILD_OBJ)/login_start.o $(BUILD_OBJ)/login_main.o \
	    $(BUILD_OBJ)/login_libc.o -o $@
	@echo "[OK] login compilato: $@"

.PHONY: login
login: dirs $(LOGIN_BIN)

# --- /bin/help: l'aiuto, sfogliato da /boot/help.txt --------------------------
# Sta sul floppy con il testo che legge: un sistema che si avvia deve poter
# dire cosa sa fare, e il banner della shell rimanda proprio qui.
HELP_SRC := bin/help/help.c
HELP_BIN := $(BUILD_BIN)/help
HELP_LD  := bin/help/help.ld

$(HELP_BIN): $(HELP_SRC) $(KBD_DRV_PROTO) $(HELP_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/help ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -c $(HELP_SRC) -o $(BUILD_OBJ)/help_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/help_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(HELP_LD) \
	    $(BUILD_OBJ)/help_start.o $(BUILD_OBJ)/help_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] help compilato: $@"

.PHONY: help_prog
help_prog: dirs $(HELP_BIN)

# --- /dev/svga.drv: sceglie la risoluzione della console ----------------------
#
# ! E' UN DRIVER, E STA FRA I DRIVER. Il nome e la collocazione non sono
# un'etichetta: entrando nel catalogo /drivers viene sondato con `-i` come
# tutti gli altri, e `hwconfig -d` lo installa da solo sul disco insieme a
# kbd.drv. Tenerlo in /bin avrebbe voluto dire ricordarsi di copiarlo a
# mano su ogni sistema installato.
#
# Sul floppy e sul CD: la risoluzione si cambia da una macchina appena
# installata, che il CD degli strumenti potrebbe non averlo.
SVGA_DRV_SRC := drivers/svga/svga.c
SVGA_DRV_OUT := $(BUILD_DRIVERS)/svga.drv
SVGA_DRV_LD  := drivers/svga/svga.ld

$(SVGA_DRV_OUT): $(SVGA_DRV_SRC) $(SVGA_DRV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 svga.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(SVGA_DRV_SRC) -o $(BUILD_DRIVERS)/svga_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS)/svga_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS)/svga_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(SVGA_DRV_LD) \
	    $(BUILD_DRIVERS)/svga_start.o \
	    $(BUILD_DRIVERS)/svga_main.o  \
	    $(BUILD_DRIVERS)/svga_libc.o  \
	    -o $@
	@echo "[OK] svga.drv compilato: $@"

.PHONY: svga_drv
svga_drv: dirs $(SVGA_DRV_OUT)

# --- /dev/vgaprova.drv: rompe lo schermo APPOSTA -----------------------------
#
# Non e' un driver: e' lo strumento di misura della rete di sicurezza. Si
# chiama .drv perche' dal 13 agosto 2026 e' l'unico modo di ottenere le porte
# I/O, e senza quelle non puo' mettere la scheda in modalita' grafica.
#
# Serve perche' vga_ripristina_testo() nel kernel e' codice di emergenza, e un
# codice di emergenza mai eseguito e' un codice che non si sa se funziona.
VGAPROVA_SRC := drivers/vgaprova/vgaprova.c
VGAPROVA_OUT := $(BUILD_DRIVERS)/vgaprova.drv
VGAPROVA_LD  := drivers/vgaprova/vgaprova.ld

$(VGAPROVA_OUT): $(VGAPROVA_SRC) $(VGAPROVA_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione strumento di prova vgaprova.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(VGAPROVA_SRC) -o $(BUILD_DRIVERS)/vgaprova_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS)/vgaprova_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS)/vgaprova_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(VGAPROVA_LD) \
	    $(BUILD_DRIVERS)/vgaprova_start.o \
	    $(BUILD_DRIVERS)/vgaprova_main.o  \
	    $(BUILD_DRIVERS)/vgaprova_libc.o  \
	    -o $@
	@echo "[OK] vgaprova.drv compilato: $@"

.PHONY: vgaprova_drv
vgaprova_drv: dirs $(VGAPROVA_OUT)

# --- /dev/mouseser.drv: mouse seriale, protocollo Microsoft ------------------
#
# Hardware separato dalla tastiera, quindi driver separato — al contrario del
# mouse PS/2, che condivide l'8042 con la tastiera e sta dentro kbd.drv.
# Registra il servizio "mouse" e parla lo stesso protocollo (kbd_proto.h).
#
# Sul CD e non sul floppy: un mouse seriale e' raro, e il floppy e' pieno.
MOUSESER_SRC := drivers/mouseser/mouseser.c
MOUSESER_OUT := $(BUILD_DRIVERS)/mouseser.drv
MOUSESER_LD  := drivers/mouseser/mouseser.ld

$(MOUSESER_OUT): $(MOUSESER_SRC) $(KBD_DRV_PROTO) $(MOUSESER_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 mouseser.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -c $(MOUSESER_SRC) -o $(BUILD_DRIVERS)/mouseser_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS)/mouseser_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS)/mouseser_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MOUSESER_LD) \
	    $(BUILD_DRIVERS)/mouseser_start.o \
	    $(BUILD_DRIVERS)/mouseser_main.o  \
	    $(BUILD_DRIVERS)/mouseser_libc.o  \
	    -o $@
	@echo "[OK] mouseser.drv compilato: $@"

.PHONY: mouseser_drv
mouseser_drv: dirs $(MOUSESER_OUT)

# --- /dev/uhci.drv: USB (controller UHCI + enumerazione + HID «boot») -------
#
# Una scheda madre senza supporto legacy non ha l'8042: tastiera e mouse sono
# USB e basta. UHCI e' il controller piu' semplice, ed e' il posto giusto dove
# far nascere lo stack — enumerazione, descrittori e HID sono identici su
# qualunque controller, e cambieranno solo di casa quando arrivera' xHCI.
# ! LA META' COMUNE DELLO STACK USB, COMPILATA DENTRO TUTT'E DUE I DRIVER.
# Non e' una libreria e non c'e' un .a: ogni driver e' un eseguibile statico a
# se', quindi il file si compila una volta per driver con un nome di oggetto
# diverso. E' lo stesso trattamento che ha gia' libc.c, per la stessa ragione.
USB_COMUNE_SRC := drivers/usb/usb_comune.c
USB_COMUNE_HDR := drivers/usb/usb_comune.h

UHCI_SRC := drivers/uhci/uhci.c
UHCI_OUT := $(BUILD_DRIVERS)/uhci.drv
UHCI_LD  := drivers/uhci/uhci.ld

$(UHCI_OUT): $(UHCI_SRC) $(USB_COMUNE_SRC) $(USB_COMUNE_HDR) $(PCI_DRV_PROTO) $(KBD_DRV_PROTO) $(UHCI_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 uhci.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -I drivers/kbd -I drivers/usb -c $(UHCI_SRC) -o $(BUILD_DRIVERS)/uhci_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -I drivers/usb -c $(USB_COMUNE_SRC) -o $(BUILD_DRIVERS)/uhci_usb.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS)/uhci_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS)/uhci_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(UHCI_LD) \
	    $(BUILD_DRIVERS)/uhci_start.o \
	    $(BUILD_DRIVERS)/uhci_main.o  \
	    $(BUILD_DRIVERS)/uhci_usb.o   \
	    $(BUILD_DRIVERS)/uhci_libc.o  \
	    -o $@
	@echo "[OK] uhci.drv compilato: $@"

.PHONY: uhci_drv
uhci_drv: dirs $(UHCI_OUT)

# --- /dev/xhci.drv: il controller di una macchina senza legacy ---------------
#
# ! VA SUL FLOPPY ACCANTO A pci E uhci, e per la stessa ragione: una macchina
# senza 8042 deve trovare sul supporto di avvio tutto cio' che le serve per
# avere una tastiera. Su quelle macchine il controller e' questo, non l'UHCI.
XHCI_SRC := drivers/xhci/xhci.c
XHCI_OUT := $(BUILD_DRIVERS)/xhci.drv
XHCI_LD  := drivers/xhci/xhci.ld

$(XHCI_OUT): $(XHCI_SRC) $(USB_COMUNE_SRC) $(USB_COMUNE_HDR) $(PCI_DRV_PROTO) $(KBD_DRV_PROTO) $(XHCI_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 xhci.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -I drivers/kbd -I drivers/usb -c $(XHCI_SRC) -o $(BUILD_DRIVERS)/xhci_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -I drivers/usb -c $(USB_COMUNE_SRC) -o $(BUILD_DRIVERS)/xhci_usb.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS)/xhci_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS)/xhci_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(XHCI_LD) \
	    $(BUILD_DRIVERS)/xhci_start.o \
	    $(BUILD_DRIVERS)/xhci_main.o  \
	    $(BUILD_DRIVERS)/xhci_usb.o   \
	    $(BUILD_DRIVERS)/xhci_libc.o  \
	    -o $@
	@echo "[OK] xhci.drv compilato: $@"

.PHONY: xhci_drv
xhci_drv: dirs $(XHCI_OUT)

# --- /dev/wserver.drv: il server a finestre, gradino 1 -----------------------
#
# ! SI CHIAMA .drv PER UNA RAGIONE SOLA: mappare il framebuffer. mmio_map() e'
# riservata agli eseguibili caricati da un file *.drv. Non guida nessuna
# periferica: il nome e' il lasciapassare, non una descrizione.
#
# ! E VA SUL CD, NON SUL FLOPPY, ed e' una scelta di spazio dichiarata: il
# floppy e' il supporto che deve bastare da solo per AVERE UNA TASTIERA, e li'
# ci stanno kbd, pci, uhci, xhci e mouseser. Un server grafico non serve a
# far ripartire una macchina muta — serve dopo.
WSERVER_SRC := drivers/wserver/wserver.c
WSERVER_OUT := $(BUILD_DRIVERS_CD)/wserver.drv
WSERVER_LD  := drivers/wserver/wserver.ld
WIN_PROTO   := drivers/wserver/win_proto.h
FONT_SRC    := kernel/arch/x86/font8x16.c

$(WSERVER_OUT): $(WSERVER_SRC) $(WIN_PROTO) $(KBD_DRV_PROTO) $(WSERVER_LD) \
                $(FONT_SRC) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 wserver.drv ==="
	@mkdir -p $(BUILD_DRIVERS_CD)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -I drivers/wserver -c $(WSERVER_SRC) -o $(BUILD_DRIVERS_CD)/wserver_main.o
	@# ! LO STESSO CARATTERE DELLA CONSOLE, non una copia sua. Cosi' una
	@# scritta dentro una finestra e una sulla console di testo hanno la
	@# stessa forma, ed e' la stessa ragione per cui vga_modo3.c ricarica
	@# proprio font8x16 quando rimette il modo testo.
	$(CC) $(CFLAGS_USER) -I lib/include -I kernel/include -c $(FONT_SRC) -o $(BUILD_DRIVERS_CD)/wserver_font.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS_CD)/wserver_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS_CD)/wserver_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(WSERVER_LD) \
	    $(BUILD_DRIVERS_CD)/wserver_start.o \
	    $(BUILD_DRIVERS_CD)/wserver_main.o  \
	    $(BUILD_DRIVERS_CD)/wserver_font.o  \
	    $(BUILD_DRIVERS_CD)/wserver_libc.o  \
	    -o $@
	@echo "[OK] wserver.drv compilato: $@"

.PHONY: wserver_drv
wserver_drv: dirs $(WSERVER_OUT)

# --- /bin/winprova: la prova del toolkit ExWin -------------------------------
EXWIN_SRC := lib/exwin/exwin.c
# ! TRE HEADER PER TRE LINGUAGGI, UNA LIBRERIA SOLA. exwin.hpp e exwin.bi non
# sono due toolkit in piu': avvolgono le stesse funzioni C. Se ex_crea()
# cambia, il C++ non compila piu' e lo si scopre subito.
EXWIN_HDR := lib/exwin/exwin.h lib/exwin/exwin.hpp lib/exwin/exwin.bi

# ! LE DIRECTORY DI /exwin SI DICHIARANO QUI, PRIMA DI CHI LE USA. Stavano piu'
# in basso, accanto alle applicazioni, e la libreria condivisa — che viene
# prima — le trovava VUOTE: con `:=` make espande subito, quindi il link
# scriveva in «/exwin.so», cioe' nella radice del disco. Non un errore di
# sintassi: un percorso sbagliato, e il permesso negato come unico indizio.
BUILD_EXWIN     := $(BUILD_DIR)/exwin
BUILD_EXWIN_BIN := $(BUILD_EXWIN)/bin
BUILD_EXWIN_LIB := $(BUILD_EXWIN)/lib

# =============================================================================
# LA LIBRERIA CONDIVISA — /exwin/lib/exwin.so
#
# ! IL TOOLKIT NON SI COLLEGA PIU' DENTRO OGNI APPLICAZIONE. Prima ogni
# programma grafico si portava 13 KB di exwin.c piu' 4 KB di font: tre
# applicazioni erano tre copie identiche. Adesso c'e' una libreria sola,
# caricata una volta dal kernel e mappata in tutti (kernel/loader/lib.c), e
# nell'applicazione entra soltanto exwin_stub.c — i ponti verso di lei.
#
# ! LA LIBRERIA SI PORTA DENTRO LA PROPRIA libc, come una DLL con il CRT
# statico. Non e' spreco: quella copia sta in un posto solo, mentre prima ce
# n'era una per applicazione. La libc condivisa e' il passo dopo.
#
# ! NIENTE start.S QUI. Una libreria non parte: non ha un _start, il suo punto
# d'ingresso e' la TABELLA dei nomi. Vedi lib/exwin/exwin.ld.
# =============================================================================
EXWIN_ESPORTA := lib/exwin/exwin_esporta.c
EXWIN_STUB    := lib/exwin/exwin_stub.c
EXWIN_LD      := lib/exwin/exwin.ld
EXLIB_SRC     := lib/exlib/exlib.c
EXLIB_HDR     := lib/include/exlib.h

EXWIN_SO := $(BUILD_EXWIN_LIB)/exwin.so

$(EXWIN_SO): $(EXWIN_SRC) $(EXWIN_ESPORTA) $(EXWIN_HDR) $(EXWIN_LD) \
             $(EXLIB_HDR) $(WIN_PROTO) $(FONT_SRC) $(EXIMG_HDR) \
             $(LIBC_PONTI_OBJ) $(LIBC_SO) $(SEGNO_FLAG)
	@echo "=== Compilazione libreria condivisa /exwin/lib/exwin.so ==="
	@mkdir -p $(BUILD_EXWIN_LIB) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I lib/eximg -I drivers/wserver -I drivers/kbd -c $(EXWIN_SRC) -o $(BUILD_OBJ)/so_exwin.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(EXWIN_ESPORTA) -o $(BUILD_OBJ)/so_esporta.o
	$(CC) $(CFLAGS_USER) -I lib/include -I kernel/include -c $(FONT_SRC) -o $(BUILD_OBJ)/so_font.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(EXWIN_LD) \
	    $(BUILD_OBJ)/so_esporta.o $(BUILD_OBJ)/so_exwin.o \
	    $(BUILD_OBJ)/so_font.o $(LIBC_PONTI_OBJ) -o $@
	@# ! LA TABELLA DEVE STARE ESATTAMENTE ALLA BASE. Se una modifica al linker
	@# script la spostasse, il kernel renderebbe un indirizzo che non e' la
	@# tabella e ogni applicazione grafica salterebbe nel vuoto: e' il genere di
	@# guasto che non somiglia alla sua causa, quindi si controlla qui.
	@# LC_ALL=C perche' readelf traduce le etichette: su un sistema italiano
	@# «Entry point» diventa «Indirizzo punto d'ingresso» e il controllo non
	@# trovava niente — passando per un confronto con la stringa vuota, che
	@# fallisce sempre. Un controllo che dipende dalla lingua non e' un
	@# controllo.
	@ent=$$(LC_ALL=C readelf -h $@ | awk '/Entry point/ {print $$4}'); \
	 if [ "$$ent" != "0x4000000" ]; then \
	     echo "[ERRORE] la tabella di exwin.so e' a $$ent invece che a 0x4000000"; \
	     exit 1; \
	 fi; \
	 echo "[OK] exwin.so compilata: $@ (tabella a $$ent)"

.PHONY: exwin_so
exwin_so: dirs $(EXWIN_SO)

# --- /exwin/lib/exdlg.so: i dialoghi, in una libreria a parte ----------------
#
# ! SEPARATA DA exwin.so PERCHE' NON TUTTI LA VOGLIONO. La barra delle
# applicazioni, un orologio, un pannello non aprono file mai: tenerli separati
# vuol dire che chi non apre file non paga il dialogo. Ed e' la prova che il
# meccanismo regge con PIU' di una libreria — che era il vero dubbio.
#
# ! exdlg USA exwin, e la usa come un'applicazione qualunque: si collega lo
# STUB, non il toolkit. Portarsene dentro una copia darebbe due tabelle di
# finestre nello stesso processo, e una finestra creata dall'una sarebbe
# invisibile all'altra.
EXDLG_SRC     := lib/exdlg/exdlg.c
EXDLG_ESPORTA := lib/exdlg/exdlg_esporta.c
EXDLG_STUB    := lib/exdlg/exdlg_stub.c
EXDLG_HDR     := lib/exdlg/exdlg.h
EXDLG_LD      := lib/exdlg/exdlg.ld

EXDLG_SO := $(BUILD_EXWIN_LIB)/exdlg.so

$(EXDLG_SO): $(EXDLG_SRC) $(EXDLG_ESPORTA) $(EXDLG_HDR) $(EXDLG_LD) \
             $(EXWIN_STUB) $(EXWIN_HDR) $(EXLIB_SRC) $(EXLIB_HDR) \
             $(LIBC_PONTI_OBJ) $(LIBC_SO) $(SEGNO_FLAG)
	@echo "=== Compilazione libreria condivisa /exwin/lib/exdlg.so ==="
	@mkdir -p $(BUILD_EXWIN_LIB) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I lib/exdlg -c $(EXDLG_SRC) -o $(BUILD_OBJ)/sodlg_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I lib/exdlg -c $(EXDLG_ESPORTA) -o $(BUILD_OBJ)/sodlg_esporta.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -c $(EXWIN_STUB) -o $(BUILD_OBJ)/sodlg_exwin.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(EXDLG_LD) \
	    $(BUILD_OBJ)/sodlg_esporta.o $(BUILD_OBJ)/sodlg_main.o \
	    $(BUILD_OBJ)/sodlg_exwin.o $(LIBC_PONTI_OBJ) -o $@
	@ent=$$(LC_ALL=C readelf -h $@ | awk '/Entry point/ {print $$4}'); \
	 if [ "$$ent" != "0x4400000" ]; then \
	     echo "[ERRORE] la tabella di exdlg.so e' a $$ent invece che a 0x4400000"; \
	     exit 1; \
	 fi; \
	 echo "[OK] exdlg.so compilata: $@ (tabella a $$ent)"

.PHONY: exdlg_so
exdlg_so: dirs $(EXDLG_SO)

# --- verifica-testi: niente UTF-8 in cio' che va a schermo -------------------
#
# ! IL FONT DEL SERVER E' A 256 CARATTERI, UNO PER BYTE. Una stringa scritta in
# UTF-8 non viene «resa male»: esce come i suoi byte, e un trattino lungo
# diventa tre caratteri accentati. E' successo nel titolo del terminale, dove
# «Terminale — /bin/sh» si leggeva «Terminale ZCO /bin/sh» — trovato guardando
# una fotografia, non compilando.
#
# ! SI CONTROLLANO LE STRINGHE, NON I FILE. Nei commenti si scrive in italiano
# vero, con le virgolette basse e gli accenti, e cosi' dev'essere: sono per chi
# legge il codice. La regola vale solo per cio' che finisce dentro le
# virgolette, che e' cio' che finisce a schermo.
#
# ! E VALE PER LE APPLICAZIONI GRAFICHE, NON PER TUTTO IL SISTEMA. I messaggi
# che vanno sulla console di testo e sulla seriale usano «» e i trattini lunghi
# in 45 file: e' la convenzione del progetto, e la seriale la rende bene. Qui
# si guarda dove il font e' quello del server a finestre, che di caratteri ne
# ha 256 e non sa niente di UTF-8 — cioe' dove il difetto si e' visto.
.PHONY: verifica-testi
verifica-testi:
	@fuori=""; \
	for f in $$(find lib/exwin lib/exdlg lib/eximg exwin/bin \
	              -name '*.c' -o -name '*.h' 2>/dev/null); do \
	    if LC_ALL=C grep -nP '^[^*/]*"[^"]*[^\x00-\x7F][^"]*"' $$f >/dev/null 2>&1; then \
	        fuori="$$fuori $$f"; \
	    fi; \
	done; \
	if [ -n "$$fuori" ]; then \
	    echo "[ERRORE] stringhe non ASCII in cio' che va a schermo:$$fuori"; \
	    for f in $$fuori; do LC_ALL=C grep -nP '^[^*/]*"[^"]*[^\x00-\x7F][^"]*"' $$f | head -3; done; \
	    exit 1; \
	fi; \
	echo "[OK] nessuna stringa non ASCII nelle applicazioni grafiche"

# --- L'immagine con cui si prova il lettore PNG ------------------------------
#
# ! SENZA UN'IMMAGINE SUL SUPPORTO IL LETTORE NON E' PROVABILE DA DENTRO, e
# `winprova -s` e' un'opzione che non e' mai stata eseguita per mancanza di un
# file da darle. 1,6 KB sul CD contro un decodificatore che nessuno ha mai
# visto lavorare sulla macchina vera: e' un buon cambio.
#
# ! IL FILE SI GENERA, NON SI TIENE NEL REPOSITORY. Un PNG committato e' un
# blob di cui nessuno sa piu' com'e' fatto; questo lo produce tools/mkimg.py da
# una formula, che dice a chi legge sia il pattern sia quali filtri esercita —
# e accanto scrive i pixel attesi, con cui la foto si confronta byte per byte.
PROVA_IMG_GEN := tools/mkimg.py
PROVE_IMG_DIR := $(BUILD_DIR)/prove-img
PROVA_PNG     := $(PROVE_IMG_DIR)/rgb.png
PROVA_ICO     := $(PROVE_IMG_DIR)/icona.ico
PROVA_JPG     := $(PROVE_IMG_DIR)/jpg444.jpg

# ! UNA SENTINELLA PERCHE' UN COLPO SOLO PRODUCE OTTO FILE. Scrivere una regola
# per ciascuno vorrebbe dire lanciare il generatore otto volte, e con `make -j`
# significa otto processi che scrivono gli stessi file insieme.
$(PROVE_IMG_DIR)/.fatte: $(PROVA_IMG_GEN)
	@mkdir -p $(PROVE_IMG_DIR)
	@python3 $(PROVA_IMG_GEN) $(PROVE_IMG_DIR) >/dev/null
	@touch $@
	@echo "[OK] immagini di prova (PNG e ICO) in $(PROVE_IMG_DIR)"

$(PROVA_PNG) $(PROVA_ICO) $(PROVA_JPG): $(PROVE_IMG_DIR)/.fatte

# --- /exwin/lib/eximg.so: i formati d'immagine che costano -------------------
#
# ! NON SI COLLEGA A NESSUNO, SI APRE QUANDO SERVE. exdlg ha uno stub perche'
# chi apre file lo sa gia' quando lo si compila; qui no: e' ex_immagine() del
# toolkit che se ne accorge davanti ai byte di un file, e apre la libreria con
# exlib_apri_fra(). Per questo non c'e' un eximg_stub.c — non ci sarebbe
# nessuno a cui darlo.
#
# ! E NON DIPENDE DA exwin.so. Un decodificatore prende byte e rende pixel: non
# sa cosa sia una finestra. Se dipendesse, non si potrebbe usarlo per leggere
# un'immagine senza avere lo schermo — per esempio in un programma che converte
# un file dalla riga di comando.
EXIMG_SRC     := lib/eximg/eximg.c
EXIMG_PNG     := lib/eximg/png.c
EXIMG_ICO     := lib/eximg/ico.c
EXIMG_JPG     := lib/eximg/jpg.c
EXIMG_INFLATE := lib/eximg/inflate.c
EXIMG_ESPORTA := lib/eximg/eximg_esporta.c
EXIMG_HDR     := lib/eximg/eximg.h lib/eximg/eximg_interno.h lib/eximg/inflate.h
EXIMG_LD      := lib/eximg/eximg.ld

EXIMG_SO := $(BUILD_EXWIN_LIB)/eximg.so

$(EXIMG_SO): $(EXIMG_SRC) $(EXIMG_PNG) $(EXIMG_ICO) $(EXIMG_JPG) \
             $(EXIMG_INFLATE) $(EXIMG_ESPORTA) \
             $(EXIMG_HDR) $(EXIMG_LD) $(EXLIB_SRC) $(EXLIB_HDR) \
             $(LIBC_PONTI_OBJ) $(LIBC_SO) $(SEGNO_FLAG)
	@echo "=== Compilazione libreria condivisa /exwin/lib/eximg.so ==="
	@mkdir -p $(BUILD_EXWIN_LIB) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/eximg -c $(EXIMG_SRC) -o $(BUILD_OBJ)/soimg_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/eximg -c $(EXIMG_PNG) -o $(BUILD_OBJ)/soimg_png.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/eximg -c $(EXIMG_ICO) -o $(BUILD_OBJ)/soimg_ico.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/eximg -c $(EXIMG_JPG) -o $(BUILD_OBJ)/soimg_jpg.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/eximg -c $(EXIMG_INFLATE) -o $(BUILD_OBJ)/soimg_inflate.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/eximg -c $(EXIMG_ESPORTA) -o $(BUILD_OBJ)/soimg_esporta.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(EXIMG_LD) \
	    $(BUILD_OBJ)/soimg_esporta.o $(BUILD_OBJ)/soimg_main.o \
	    $(BUILD_OBJ)/soimg_png.o $(BUILD_OBJ)/soimg_ico.o \
	    $(BUILD_OBJ)/soimg_jpg.o \
	    $(BUILD_OBJ)/soimg_inflate.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@ent=$$(LC_ALL=C readelf -h $@ | awk '/Entry point/ {print $$4}'); \
	 if [ "$$ent" != "0x4c00000" ]; then \
	     echo "[ERRORE] la tabella di eximg.so e' a $$ent invece che a 0x4c00000"; \
	     exit 1; \
	 fi; \
	 echo "[OK] eximg.so compilata: $@ (tabella a $$ent)"

.PHONY: eximg_so
eximg_so: dirs $(EXIMG_SO)

# =============================================================================
# LA LIBC CONDIVISA — /lib/libc.so
#
# ! E' IL PEZZO PIU' GROSSO E QUELLO PIU' DELICATO. exwin.so la usano tre
# programmi; questa la usano TUTTI, shell e init compresi. Per questo si
# costruisce accanto a quella statica invece che al posto suo: un programma si
# collega all'una o all'altra, e i due modi convivono. L'interruttore e' il
# collegamento — $(LIBC_PONTI_OBJ) al posto di $(BUILD_OBJ)/<prog>_libc.o —
# non un #ifdef sparso nei sorgenti.
#
# ! I DUE LATI SI GENERANO DAI SIMBOLI VERI, con tools/genlibc.py: un elenco
# scritto a mano sarebbe rimasto indietro alla prima funzione aggiunta, e una
# funzione presente da un lato solo non da' un errore di compilazione — da' un
# salto a zero al primo programma che la chiama.
#
# ! -DEXOS_LIBC_SO toglie l'avvio (lib/libc_avvio.c): dentro la libreria
# `main` non esiste e il collegamento fallirebbe. Il perche' per esteso sta in
# cima a quel file.
# =============================================================================

$(LIBC_SO_OBJ): $(LIBC_SRC) $(LIBC_AVVIO) $(SEGNO_FLAG)
	@mkdir -p $(BUILD_OBJ)
	@# ! LA LIBRERIA CONDIVISA SI COMPILA SENZA CANARINO, e va detto perche'.
	@# Il canarino e' una VARIABILE, e una variabile dentro la libreria non
	@# puo' essere quella del programma — e' la stessa trappola di errno. La
	@# .data della libreria e' privata per processo, quindi un canarino li'
	@# sarebbe possibile, ma non c'e' nessuno che lo inizializzi: resterebbe
	@# la costante scritta nel binario, cioe' un valore che chi attacca
	@# conosce. Meglio nessuna difesa che una difesa che si sa aggirare.
	@# Nella coda: dare a libc.so un canarino suo, acceso da __lib_avvio.
	$(CC) $(CFLAGS_USER) -fno-stack-protector -DEXOS_LIBC_SO -c $(LIBC_SRC) -o $@

$(GEN_ESPORTA) $(GEN_PONTI): $(LIBC_SO_OBJ) tools/genlibc.py
	@mkdir -p $(GEN_DIR)
	python3 tools/genlibc.py $(LIBC_SO_OBJ) $(GEN_DIR)

$(LIBC_SO): $(LIBC_SO_OBJ) $(GEN_ESPORTA) $(LIBC_LD) $(SEGNO_FLAG)
	@echo "=== Compilazione libc condivisa /lib/libc.so ==="
	@mkdir -p $(BUILD_LIB) $(BUILD_OBJ)
	$(CC) -m32 -c $(GEN_ESPORTA) -o $(BUILD_OBJ)/libc_esporta.o
	@# ! SENZA --gc-sections, e sta scritto in lib/libc.ld: una libreria
	@# condivisa non sa chi la usera' domani.
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(LIBC_LD) \
	    $(BUILD_OBJ)/libc_esporta.o $(LIBC_SO_OBJ) -o $@
	@ent=$$(LC_ALL=C readelf -h $@ | awk '/Entry point/ {print $$4}'); \
	 if [ "$$ent" != "0x4800000" ]; then \
	     echo "[ERRORE] la tabella di libc.so e' a $$ent invece che a 0x4800000"; \
	     exit 1; \
	 fi; \
	 echo "[OK] libc.so compilata: $@ (tabella a $$ent)"

$(BUILD_OBJ)/libc_ponti_asm.o: $(SEGNO_FLAG) $(GEN_PONTI)
	@mkdir -p $(BUILD_OBJ)
	$(CC) -m32 -c $(GEN_PONTI) -o $@

$(BUILD_OBJ)/libc_ponti_c.o: $(SEGNO_FLAG) $(LIBC_PONTI_C) $(EXLIB_HDR)
	@mkdir -p $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(LIBC_PONTI_C) -o $@

$(BUILD_OBJ)/libc_ponti_avvio.o: $(SEGNO_FLAG) $(LIBC_AVVIO)
	@mkdir -p $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(LIBC_AVVIO) -o $@

$(BUILD_OBJ)/libc_ponti_exlib.o: $(SEGNO_FLAG) $(EXLIB_SRC) $(EXLIB_HDR)
	@mkdir -p $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(EXLIB_SRC) -o $@

.PHONY: libc_so libc
libc_so: dirs $(LIBC_SO)

# ! `libc` RESTA UN BERSAGLIO, e non e' cortesia verso chi lo digita: `all`
# dipende da lui. Toglierlo insieme alla regola vecchia ha fermato la
# costruzione con «Nessuna regola per generare l'obiettivo libc» — un
# messaggio che parla di make e non dice che cosa e' stato tolto.
libc: dirs $(LIBC_SO)

WINPROVA_SRC := bin/winprova/winprova.c
WINPROVA_BIN := $(BUILD_BIN_CD)/winprova
WINPROVA_LD  := bin/winprova/winprova.ld

$(WINPROVA_BIN): $(WINPROVA_SRC) $(WINPROVA_LD) $(EXWIN_STUB) $(EXLIB_SRC) $(EXLIB_HDR) $(EXWIN_HDR) \
                 $(WIN_PROTO) $(FONT_SRC) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/winprova ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(WINPROVA_SRC) -o $(BUILD_OBJ)/winprova_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(EXWIN_STUB) -o $(BUILD_OBJ)/winprova_exwin.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_OBJ)/winprova_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(WINPROVA_LD) \
	    $(BUILD_OBJ)/winprova_start.o $(BUILD_OBJ)/winprova_main.o \
	    $(BUILD_OBJ)/winprova_exwin.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] winprova compilato: $@"

.PHONY: winprova
winprova: dirs $(WINPROVA_BIN)

# =============================================================================
# /exwin — LE APPLICAZIONI GRAFICHE, SEPARATE DA QUELLE DA SHELL
#
# ! NON STANNO IN /bin, ED E' UNA DECISIONE. I programmi di /bin si lanciano da
# una shell e parlano con un terminale; questi vogliono il server a finestre, e
# lanciati da una shell senza server non fanno niente. Mescolarli vorrebbe dire
# un `ls /bin` in cui meta' dei nomi non si puo' usare li' dove si sta
# guardando. E' la stessa ragione per cui i driver stanno in /dev.
#
#   /exwin/bin    le applicazioni
#   /exwin/lib    l'elenco delle applicazioni, e cio' che condividono
#   /exwin/dev    i pezzi grafici che vogliono il varco dei driver
# =============================================================================
EXWIN_APPLIST := exwin/lib/applicazioni.txt

PM_SRC := exwin/bin/pm/pm.c
PM_BIN := $(BUILD_EXWIN_BIN)/pm
PM_LD  := exwin/bin/pm/pm.ld

$(PM_BIN): $(PM_SRC) $(PM_LD) $(EXWIN_STUB) $(EXLIB_SRC) $(EXLIB_HDR) $(EXWIN_HDR) $(WIN_PROTO) \
           $(FONT_SRC) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /exwin/bin/pm ==="
	@mkdir -p $(BUILD_EXWIN_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(PM_SRC) -o $(BUILD_OBJ)/pm_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(EXWIN_STUB) -o $(BUILD_OBJ)/pm_exwin.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_OBJ)/pm_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(PM_LD) \
	    $(BUILD_OBJ)/pm_start.o $(BUILD_OBJ)/pm_main.o \
	    $(BUILD_OBJ)/pm_exwin.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] pm compilato: $@"

.PHONY: pm
pm: dirs $(PM_BIN)

# --- /bin/exwin: accende la grafica -----------------------------------------
#
# ! STA IN /bin E NON IN /exwin/bin. Lo si digita da una shell, PRIMA che la
# grafica esista: metterlo insieme alle applicazioni grafiche vorrebbe dire
# cercarlo dove si arriva solo dopo averlo eseguito.
EXWINCMD_SRC := bin/exwin/exwin.c
EXWINCMD_BIN := $(BUILD_BIN_CD)/exwin
EXWINCMD_LD  := bin/exwin/exwin.ld

$(EXWINCMD_BIN): $(EXWINCMD_SRC) $(EXWINCMD_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/exwin ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(EXWINCMD_SRC) -o $(BUILD_OBJ)/exwincmd_main.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_OBJ)/exwincmd_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(EXWINCMD_LD) \
	    $(BUILD_OBJ)/exwincmd_start.o $(BUILD_OBJ)/exwincmd_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] exwin compilato: $@"

.PHONY: exwincmd
exwincmd: dirs $(EXWINCMD_BIN)


# --- /exwin/bin/filemgr: la prima applicazione grafica ----------------------
FILEMGR_SRC := exwin/bin/filemgr/filemgr.c
FILEMGR_BIN := $(BUILD_EXWIN_BIN)/filemgr
FILEMGR_LD  := exwin/bin/filemgr/filemgr.ld

$(FILEMGR_BIN): $(FILEMGR_SRC) $(FILEMGR_LD) $(EXWIN_STUB) $(EXLIB_SRC) $(EXLIB_HDR) $(EXWIN_HDR) \
                $(WIN_PROTO) $(FONT_SRC) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /exwin/bin/filemgr ==="
	@mkdir -p $(BUILD_EXWIN_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(FILEMGR_SRC) -o $(BUILD_OBJ)/filemgr_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(EXWIN_STUB) -o $(BUILD_OBJ)/filemgr_exwin.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_OBJ)/filemgr_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(FILEMGR_LD) \
	    $(BUILD_OBJ)/filemgr_start.o $(BUILD_OBJ)/filemgr_main.o \
	    $(BUILD_OBJ)/filemgr_exwin.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] filemgr compilato: $@"

.PHONY: filemgr
filemgr: dirs $(FILEMGR_BIN)

# --- /exwin/bin/edit: l'editor di testo grafico ------------------------------
EDIT_SRC := exwin/bin/edit/edit.c
EDIT_BIN := $(BUILD_EXWIN_BIN)/edit
EDIT_LD  := exwin/bin/edit/edit.ld

$(EDIT_BIN): $(EDIT_SRC) $(EDIT_LD) $(EXWIN_STUB) $(EXLIB_SRC) $(EXLIB_HDR) $(EXWIN_HDR) \
             $(EXDLG_STUB) $(EXDLG_HDR) \
             $(WIN_PROTO) $(FONT_SRC) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /exwin/bin/edit ==="
	@mkdir -p $(BUILD_EXWIN_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I lib/exdlg -I drivers/wserver -I drivers/kbd -c $(EDIT_SRC) -o $(BUILD_OBJ)/edit_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(EXWIN_STUB) -o $(BUILD_OBJ)/edit_exwin.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exdlg -c $(EXDLG_STUB) -o $(BUILD_OBJ)/edit_exdlg.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_OBJ)/edit_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(EDIT_LD) \
	    $(BUILD_OBJ)/edit_start.o $(BUILD_OBJ)/edit_main.o \
	    $(BUILD_OBJ)/edit_exwin.o \
	    $(BUILD_OBJ)/edit_exdlg.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] edit compilato: $@"

.PHONY: edit
edit: dirs $(EDIT_BIN)

# --- /exwin/bin/term: il terminale in finestra -------------------------------
#
# ! E' QUASI TUTTO NEL TOOLKIT. Il controllo «terminale» apre le due pipe,
# avvia il programma e disegna la griglia: questo binario e' la finestra che ci
# sta intorno. Per questo non si collega a exdlg — non apre file.
TERM_SRC := exwin/bin/term/term.c
TERM_BIN := $(BUILD_EXWIN_BIN)/term
TERM_LD  := exwin/bin/term/term.ld

$(TERM_BIN): $(TERM_SRC) $(TERM_LD) $(EXWIN_STUB) $(EXLIB_SRC) $(EXLIB_HDR) \
             $(EXWIN_HDR) $(WIN_PROTO) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /exwin/bin/term ==="
	@mkdir -p $(BUILD_EXWIN_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(TERM_SRC) -o $(BUILD_OBJ)/term_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd -c $(EXWIN_STUB) -o $(BUILD_OBJ)/term_exwin.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_OBJ)/term_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TERM_LD) \
	    $(BUILD_OBJ)/term_start.o $(BUILD_OBJ)/term_main.o \
	    $(BUILD_OBJ)/term_exwin.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] term compilato: $@"

.PHONY: term
term: dirs $(TERM_BIN)

# ! L'ELENCO E' UN FILE E VA COPIATO, non compilato dentro: aggiungere
# un'applicazione dev'essere una riga, non una ricostruzione.
# ! LA LIBRERIA CONDIVISA FA PARTE DI CIO' CHE SI INSTALLA, e va messa qui
# dentro: senza, le applicazioni finirebbero sull'immagine e la libreria no —
# tre programmi che partono e si fermano subito dicendo che non la trovano.
EXWIN_OUT := $(PM_BIN) $(FILEMGR_BIN) $(EDIT_BIN) $(TERM_BIN) $(EXWIN_SO) $(EXDLG_SO) \
             $(EXIMG_SO)

# --- /bin/testo: rimette lo schermo, si digita alla cieca --------------------
#
# Sul FLOPPY e non e' negoziabile, per la stessa ragione di keymap: chi ne ha
# bisogno ha lo schermo illeggibile, e il comando che rimedia deve esserci
# sempre e chiamarsi in un modo che si batta senza vederlo.
TESTO_SRC := bin/testo/testo.c
TESTO_BIN := $(BUILD_BIN)/testo
TESTO_LD  := bin/testo/testo.ld

$(TESTO_BIN): $(TESTO_SRC) $(TESTO_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/testo ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(TESTO_SRC) -o $(BUILD_OBJ)/testo_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/testo_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TESTO_LD) \
	    $(BUILD_OBJ)/testo_start.o $(BUILD_OBJ)/testo_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] testo compilato: $@"

.PHONY: testo
testo: dirs $(TESTO_BIN)

# --- /bin/mouse: segue il mouse PS/2 -----------------------------------------
#
# Il mouse lo serve /dev/kbd.drv, perche' e' la seconda porta dello stesso
# 8042: due processi che leggono 0x60 si rubano i byte. Vedi kbd_proto.h.
MOUSE_SRC := bin/mouse/mouse.c
MOUSE_BIN := $(BUILD_BIN)/mouse
MOUSE_LD  := bin/mouse/mouse.ld

$(MOUSE_BIN): $(MOUSE_SRC) $(KBD_DRV_PROTO) $(MOUSE_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/mouse ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -c $(MOUSE_SRC) -o $(BUILD_OBJ)/mouse_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/mouse_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MOUSE_LD) \
	    $(BUILD_OBJ)/mouse_start.o $(BUILD_OBJ)/mouse_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] mouse compilato: $@"

.PHONY: mouse_prog
mouse_prog: dirs $(MOUSE_BIN)

# --- /bin/keymap: sceglie la disposizione della tastiera ---------------------
# Sul FLOPPY, e non e' negoziabile: chi ha la disposizione sbagliata se ne
# accorge digitando, e in quel momento ha una tastiera che scrive i
# caratteri sbagliati. Il comando che rimedia deve esserci sempre.
KEYMAP_BIN := $(BUILD_BIN)/keymap
KEYMAP_LD  := bin/keymap/keymap.ld

$(KEYMAP_BIN): bin/keymap/keymap.c drivers/kbd/kbd_proto.h $(KEYMAP_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/keymap ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -c bin/keymap/keymap.c -o $(BUILD_OBJ)/keymap_main.o
	$(CC) -m32 -c $(LIBC_START)          -o $(BUILD_OBJ)/keymap_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(KEYMAP_LD) \
	    $(BUILD_OBJ)/keymap_start.o $(BUILD_OBJ)/keymap_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] keymap compilato: $@"

.PHONY: keymap
keymap: dirs $(KEYMAP_BIN)

.PHONY: mkfs
mkfs: dirs $(MKFS_BIN)

# --- Programma utente /bin/trunc ----------------------------------------------
# Cambia la dimensione di un file (SYS_TRUNCATE). Stesso schema di /bin/ls.
TRUNC_SRC  := bin/trunc/trunc.c
TRUNC_BIN  := $(BUILD_BIN)/trunc
TRUNC_LD   := bin/trunc/trunc.ld

$(TRUNC_BIN): $(TRUNC_SRC) $(TRUNC_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/trunc ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(TRUNC_SRC) -o $(BUILD_OBJ)/trunc_main.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/trunc_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TRUNC_LD) \
	    $(BUILD_OBJ)/trunc_start.o $(BUILD_OBJ)/trunc_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] trunc compilato: $@"

.PHONY: trunc
trunc: dirs $(TRUNC_BIN)

# --- Programma utente /bin/chkdsk ---------------------------------------------
# Controllo e riparazione di un volume FAT12/16/32. Lavora sui SETTORI
# GREZZI di una partizione NON montata (blkread/blkwrite): sopra un volume
# montato c'e' una cache write-back, e un controllo li' non direbbe niente
# di vero. Stesso schema di /bin/trunc.
CHKDSK_SRC := bin/chkdsk/chkdsk.c
CHKDSK_BIN := $(BUILD_BIN)/chkdsk
CHKDSK_LD  := bin/chkdsk/chkdsk.ld

$(CHKDSK_BIN): $(CHKDSK_SRC) $(CHKDSK_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/chkdsk ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(CHKDSK_SRC) -o $(BUILD_OBJ)/chkdsk_main.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/chkdsk_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(CHKDSK_LD) \
	    $(BUILD_OBJ)/chkdsk_start.o $(BUILD_OBJ)/chkdsk_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] chkdsk compilato: $@"

.PHONY: chkdsk
chkdsk: dirs $(CHKDSK_BIN)

# --- Programma utente /bin/rename ---------------------------------------------
# Cambia il nome di un file (SYS_RENAME). ! NON si chiama `mv` di
# proposito: mv su Unix sposta anche fra filesystem, copiando e
# cancellando; qui i dati non si muovono mai. Il nome dice cosa fa.
RENAME_SRC := bin/rename/rename.c
RENAME_BIN := $(BUILD_BIN)/rename
RENAME_LD  := bin/rename/rename.ld

$(RENAME_BIN): $(RENAME_SRC) $(RENAME_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/rename ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(RENAME_SRC) -o $(BUILD_OBJ)/rename_main.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/rename_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(RENAME_LD) \
	    $(BUILD_OBJ)/rename_start.o $(BUILD_OBJ)/rename_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] rename compilato: $@"

.PHONY: rename
rename: dirs $(RENAME_BIN)

# --- Programmi utente /bin/rm e /bin/mv ---------------------------------------
#
# ! SONO I GEMELLI POSIX DI `delete` E `rename`, E NON SONO DOPPIONI. La
# differenza sta in chi scrive il comando:
#
#   delete / rename   li scrive una PERSONA al prompt. `delete` espande i
#                     jolly e dice quanti file ha preso; `rename` dichiara
#                     di non attraversare directory. Sono i modi giusti per
#                     chi guarda lo schermo.
#
#   rm / mv           li scrive un MAKEFILE, e devono comportarsi come su
#                     ogni altro sistema: `rm -f` che TACE su un file che
#                     non c'e' (centoquarantasei volte nel makefile di
#                     FreeBASIC, e una sola lamentela ferma la costruzione),
#                     `rm -r` che scende, `mv` che sposta davvero fra
#                     directory copiando e cancellando.
#
# Rinominare i primi due avrebbe voluto dire promettere flag che non hanno;
# dare ai secondi i modi dei primi avrebbe voluto dire ricette che si
# fermano. Il perche' per esteso sta in testa ai due sorgenti.
#
# Sul FLOPPY e non solo sul CD: `make` sta sul CD degli strumenti, ma i
# comandi che le sue ricette chiamano devono esserci sul sistema di base —
# altrimenti si potrebbe costruire solo con il CD inserito.
RM_SRC := bin/rm/rm.c
RM_BIN := $(BUILD_BIN)/rm
RM_LD  := bin/rm/rm.ld

$(RM_BIN): $(RM_SRC) $(RM_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/rm ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(RM_SRC) -o $(BUILD_OBJ)/rm_main.o
	$(CC) -m32 -c $(LIBC_START)                      -o $(BUILD_OBJ)/rm_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(RM_LD) \
	    $(BUILD_OBJ)/rm_start.o $(BUILD_OBJ)/rm_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] rm compilato: $@"

.PHONY: rm_prog
rm_prog: dirs $(RM_BIN)

# --- Programma utente /bin/uname ----------------------------------------------
#
# ! C'ERA GIA' COME BUILT-IN DELLA SHELL, e non bastava per due motivi. Il
# primo: stampava una frase per una persona («EX-OS version 0.176 ...»),
# mentre un makefile fa `ifeq ($(shell uname),Linux)` e confronta con UNA
# PAROLA. Il secondo, che si e' visto solo eseguendo: GNU make NON passa
# dalla shell per un comando senza metacaratteri — lo lancia diretto — quindi
# cercava un PROGRAMMA di nome `uname` e rispondeva «fork: file o directory
# inesistente», un errore che parla di fork mentre a mancare e' un eseguibile.
#
# Il built-in e' stato tolto dalla shell: due risposte diverse sotto lo stesso
# nome, a seconda di chi chiama, sono peggio di una risposta scomoda. La frase
# per le persone si chiama `ver`, e c'era gia'.
UNAME_SRC := bin/uname/uname.c
UNAME_BIN := $(BUILD_BIN)/uname
UNAME_LD  := bin/uname/uname.ld

# ! IL PRIMO PROGRAMMA COLLEGATO ALLA LIBC CONDIVISA (17 agosto 2026), ed e'
# stato scelto piccolo apposta: stampa una riga e finisce. Se l'aggancio a
# libc.so avesse un difetto, si vedrebbe qui invece che dentro qualcosa di
# complicato. Al posto di uname_libc.o ci sono i $(LIBC_PONTI_OBJ) — i ponti,
# il risolutore, l'avvio e il cercatore di simboli.
$(UNAME_BIN): $(UNAME_SRC) $(UNAME_LD) $(LIBC_PONTI_OBJ) $(LIBC_START) $(LIBC_SO) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/uname (libc CONDIVISA) ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(UNAME_SRC) -o $(BUILD_OBJ)/uname_main.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/uname_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(UNAME_LD) \
	    $(BUILD_OBJ)/uname_start.o $(BUILD_OBJ)/uname_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] uname compilato: $@"

# --- /bin/id e /bin/whoami: guardare la propria identita' --------------------
#
# ! DUE NOMI, UN BINARIO, come su Unix: whoami e' id che guarda argv[0]. E
# whoami e' un COLLEGAMENTO fatto copiando il file — EX-OS non ha i link — che
# costa 5 KB e vale la chiarezza di poter battere il nome che tutti conoscono.
ID_SRC := bin/id/id.c
ID_BIN := $(BUILD_BIN)/id
ID_LD  := bin/id/id.ld

$(ID_BIN): $(ID_SRC) $(ID_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/id (e /bin/whoami) ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(ID_SRC) -o $(BUILD_OBJ)/id_main.o
	$(CC) -m32 -c $(LIBC_START)                      -o $(BUILD_OBJ)/id_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(ID_LD) \
	    $(BUILD_OBJ)/id_start.o $(BUILD_OBJ)/id_main.o $(LIBC_PONTI_OBJ) -o $@
	@cp $@ $(BUILD_BIN)/whoami
	@echo "[OK] id compilato: $@ (e whoami)"

.PHONY: id
id: dirs $(ID_BIN)

# --- /bin/chmod e /bin/chown: governare i permessi ---------------------------
#
# ! UN BINARIO CON DUE NOMI, come id/whoami. Le due funzioni condividono la
# lettura di /boot/utenti e il ciclo sui file: separarle vorrebbe dire due
# copie che divergono al primo cambiamento.
PERM_SRC := bin/chmod/chmod.c
PERM_BIN := $(BUILD_BIN)/chmod
PERM_LD  := bin/chmod/chmod.ld

$(PERM_BIN): $(PERM_SRC) $(PERM_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/chmod (e /bin/chown) ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(PERM_SRC) -o $(BUILD_OBJ)/perm_main.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/perm_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(PERM_LD) \
	    $(BUILD_OBJ)/perm_start.o $(BUILD_OBJ)/perm_main.o $(LIBC_PONTI_OBJ) -o $@
	@cp $@ $(BUILD_BIN)/chown
	@echo "[OK] chmod compilato: $@ (e chown)"

.PHONY: chmod
chmod: dirs $(PERM_BIN)

.PHONY: uname_prog
uname_prog: dirs $(UNAME_BIN)

MV_SRC := bin/mv/mv.c
MV_BIN := $(BUILD_BIN)/mv
MV_LD  := bin/mv/mv.ld

$(MV_BIN): $(MV_SRC) $(MV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/mv ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(MV_SRC) -o $(BUILD_OBJ)/mv_main.o
	$(CC) -m32 -c $(LIBC_START)                      -o $(BUILD_OBJ)/mv_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MV_LD) \
	    $(BUILD_OBJ)/mv_start.o $(BUILD_OBJ)/mv_main.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] mv compilato: $@"

.PHONY: mv_prog
mv_prog: dirs $(MV_BIN)

# --- Programma /bin/xcp (solo CD) ---------------------------------------------
# Copia ricorsiva di un albero. Fino ad agosto 2026 questo programma NON
# aveva una regola: il sorgente stava in bin/xcp/ e non veniva compilato
# ne' messo su nessuna immagine. E' il buco che si chiude dichiarando ogni
# programma qui dentro — vedi PROGRAMMI_CD e `make verifica-programmi`.
#
# Sul CD e non sul floppy perche' `cp -r` fa gia' lo stesso lavoro e sul
# floppy c'e' gia': xcp resta per gli alberi profondi, dove ha una
# ricorsione sua invece di appoggiarsi alle opzioni di cp.
XCP_SRC := bin/xcp/xcp.c
XCP_BIN := $(BUILD_BIN_CD)/xcp
XCP_LD  := bin/xcp/xcp.ld

$(XCP_BIN): $(XCP_SRC) $(XCP_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/xcp ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(XCP_SRC) -o $(BUILD_OBJ)/xcp_main.o
	$(CC) -m32 -c $(LIBC_START)                      -o $(BUILD_OBJ)/xcp_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(XCP_LD) \
	    $(BUILD_OBJ)/xcp_start.o $(BUILD_OBJ)/xcp_main.o \
	    $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] xcp compilato: $@"

.PHONY: xcp
xcp: dirs $(XCP_BIN)

# --- Programma /bin/netdetect (solo CD) ---------------------------------------
# Riconosce le schede di rete chiedendo al server PCI e dice quale driver
# serve. Va in $(BUILD_BIN_CD) e non in $(BUILD_BIN): sul floppy ci
# starebbe (ce ne sono 731 KB liberi), ma il floppy serve ad avviare e a
# installare, e uno strumento di rete senza i driver di rete — che sul
# floppy non ci stanno — non servirebbe a niente una volta li'.
NETDETECT_SRC := bin/netdetect/netdetect.c
NETDETECT_BIN := $(BUILD_BIN_CD)/netdetect
NETDETECT_LD  := bin/netdetect/netdetect.ld

$(NETDETECT_BIN): $(NETDETECT_SRC) $(NETDETECT_LD) $(PCI_DRV_PROTO) $(NET_PROTO) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/netdetect ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(NETDETECT_SRC) -o $(BUILD_OBJ)/netdetect_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/netdetect_rete.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/netdetect_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(NETDETECT_LD) \
	    $(BUILD_OBJ)/netdetect_start.o $(BUILD_OBJ)/netdetect_main.o $(BUILD_OBJ)/netdetect_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] netdetect compilato: $@"

.PHONY: netdetect
netdetect: dirs $(NETDETECT_BIN)

# --- Programma /bin/nettest (solo CD) -----------------------------------------
# Prova un driver di rete mandando un frame vero. Usa ARP e non ping
# perche' ARP e' il primo scambio possibile senza avere uno stack: se
# arriva la risposta, scheda, driver e IPC funzionano in entrambi i sensi.
NETTEST_SRC := bin/nettest/nettest.c
NETTEST_BIN := $(BUILD_BIN_CD)/nettest
NETTEST_LD  := bin/nettest/nettest.ld

$(NETTEST_BIN): $(NETTEST_SRC) $(NETTEST_LD) $(NET_PROTO) $(DNS_SRC) $(DNS_HDR) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/nettest ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(NETTEST_SRC) -o $(BUILD_OBJ)/nettest_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/nettest_rete.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DNS_SRC)     -o $(BUILD_OBJ)/nettest_dns.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/nettest_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(NETTEST_LD) \
	    $(BUILD_OBJ)/nettest_start.o $(BUILD_OBJ)/nettest_main.o $(BUILD_OBJ)/nettest_dns.o $(BUILD_OBJ)/nettest_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] nettest compilato: $@"

.PHONY: nettest
nettest: dirs $(NETTEST_BIN)

# --- /bin/ping e /bin/ipcfg (solo CD) -----------------------------------------
# Client sottili dello stack IP: non conoscono ICMP ne' ARP, mandano un
# messaggio a /dev/ip.drv e stampano la risposta. Il protocollo sta tutto
# nello stack, che e' un processo a se'.
PING_SRC  := bin/ping/ping.c
PING_BIN  := $(BUILD_BIN_CD)/ping
PING_LD   := bin/ping/ping.ld

$(PING_BIN): $(PING_SRC) $(PING_LD) $(IP_PROTO) $(DNS_SRC) $(DNS_HDR) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/ping ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(PING_SRC) -o $(BUILD_OBJ)/ping_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/ping_rete.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DNS_SRC)  -o $(BUILD_OBJ)/ping_dns.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/ping_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(PING_LD) \
	    $(BUILD_OBJ)/ping_start.o $(BUILD_OBJ)/ping_main.o $(BUILD_OBJ)/ping_dns.o $(BUILD_OBJ)/ping_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] ping compilato: $@"

.PHONY: ping
ping: dirs $(PING_BIN)

IPCFG_SRC := bin/ipcfg/ipcfg.c
IPCFG_BIN := $(BUILD_BIN_CD)/ipcfg
IPCFG_LD  := bin/ipcfg/ipcfg.ld

$(IPCFG_BIN): $(IPCFG_SRC) $(IPCFG_LD) $(IP_PROTO) $(DNS_SRC) $(DNS_HDR) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/ipcfg ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(IPCFG_SRC) -o $(BUILD_OBJ)/ipcfg_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/ipcfg_rete.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DNS_SRC)   -o $(BUILD_OBJ)/ipcfg_dns.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/ipcfg_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(IPCFG_LD) \
	    $(BUILD_OBJ)/ipcfg_start.o $(BUILD_OBJ)/ipcfg_main.o $(BUILD_OBJ)/ipcfg_dns.o $(BUILD_OBJ)/ipcfg_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] ipcfg compilato: $@"

.PHONY: ipcfg
ipcfg: dirs $(IPCFG_BIN)

# --- /bin/dhcp (solo CD) ------------------------------------------------------
# Client DHCP. E' un PROGRAMMA e non un pezzo dello stack: DHCP sta sopra
# UDP come un client DNS, e un errore qui fa fallire un comando invece di
# spegnere la rete.
DHCP_SRC := bin/dhcp/dhcp.c
DHCP_BIN := $(BUILD_BIN_CD)/dhcp
DHCP_LD  := bin/dhcp/dhcp.ld

$(DHCP_BIN): $(DHCP_SRC) $(DHCP_LD) $(IP_PROTO) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/dhcp ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DHCP_SRC) -o $(BUILD_OBJ)/dhcp_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/dhcp_rete.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/dhcp_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(DHCP_LD) \
	    $(BUILD_OBJ)/dhcp_start.o $(BUILD_OBJ)/dhcp_main.o $(BUILD_OBJ)/dhcp_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] dhcp compilato: $@"

.PHONY: dhcp
dhcp: dirs $(DHCP_BIN)

# --- /bin/host (solo CD) ------------------------------------------------------
# Interroga il DNS. Esiste per poter provare il risolutore DA SOLO: quando
# `ping nome` non funziona, la domanda e' se sia rotto il ping o il DNS.
HOST_SRC := bin/host/host.c
HOST_BIN := $(BUILD_BIN_CD)/host
HOST_LD  := bin/host/host.ld

$(HOST_BIN): $(HOST_SRC) $(HOST_LD) $(IP_PROTO) $(DNS_SRC) $(DNS_HDR) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/host ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(HOST_SRC) -o $(BUILD_OBJ)/host_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/host_rete.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DNS_SRC)  -o $(BUILD_OBJ)/host_dns.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/host_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(HOST_LD) \
	    $(BUILD_OBJ)/host_start.o $(BUILD_OBJ)/host_main.o $(BUILD_OBJ)/host_dns.o $(BUILD_OBJ)/host_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] host compilato: $@"

.PHONY: host
host: dirs $(HOST_BIN)

# --- /bin/tcptest (solo CD) ---------------------------------------------------
# Sta a TCP come nettest sta al driver: prova UN livello per volta. Quando
# il client FTP non funzionera', la domanda sara' se sia rotto FTP o TCP.
TCPTEST_SRC := bin/tcptest/tcptest.c
TCPTEST_BIN := $(BUILD_BIN_CD)/tcptest
TCPTEST_LD  := bin/tcptest/tcptest.ld

$(TCPTEST_BIN): $(TCPTEST_SRC) $(TCPTEST_LD) $(IP_PROTO) $(DNS_SRC) $(DNS_HDR) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/tcptest ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(TCPTEST_SRC) -o $(BUILD_OBJ)/tcptest_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DNS_SRC)  -o $(BUILD_OBJ)/tcptest_dns.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/tcptest_rete.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/tcptest_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TCPTEST_LD) \
	    $(BUILD_OBJ)/tcptest_start.o $(BUILD_OBJ)/tcptest_main.o $(BUILD_OBJ)/tcptest_dns.o \
	    $(BUILD_OBJ)/tcptest_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] tcptest compilato: $@"

.PHONY: tcptest
tcptest: dirs $(TCPTEST_BIN)

# --- /bin/ftp (solo CD) -------------------------------------------------------
# Client FTP, modo PASSIVO soltanto: il modo attivo vuole che il client si
# metta in ASCOLTO, e il nostro TCP non sa farlo (ne' funzionerebbe dietro
# un NAT). Vedi drivers/net/ip_proto.h.
FTP_SRC := bin/ftp/ftp.c
FTP_BIN := $(BUILD_BIN_CD)/ftp
FTP_LD  := bin/ftp/ftp.ld

$(FTP_BIN): $(FTP_SRC) $(FTP_LD) $(IP_PROTO) $(DNS_SRC) $(DNS_HDR) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/ftp ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(FTP_SRC)  -o $(BUILD_OBJ)/ftp_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DNS_SRC)  -o $(BUILD_OBJ)/ftp_dns.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/ftp_rete.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/ftp_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(FTP_LD) \
	    $(BUILD_OBJ)/ftp_start.o $(BUILD_OBJ)/ftp_main.o $(BUILD_OBJ)/ftp_dns.o \
	    $(BUILD_OBJ)/ftp_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] ftp compilato: $@"

.PHONY: ftp
ftp: dirs $(FTP_BIN)

# --- /bin/telnet: sessione interattiva su TCP (solo CD) ----------------------
# Sta con gli altri strumenti di rete. A differenza di ftp ha bisogno anche
# del servizio TASTIERA, perche' una sessione interattiva vuole i tasti uno
# per uno: aspetta rete e tastiera sulla stessa cassetta postale IPC.
TELNET_SRC := bin/telnet/telnet.c
TELNET_BIN := $(BUILD_BIN_CD)/telnet
TELNET_LD  := bin/telnet/telnet.ld

$(TELNET_BIN): $(TELNET_SRC) $(TELNET_LD) $(IP_PROTO) drivers/kbd/kbd_proto.h $(DNS_SRC) $(DNS_HDR) $(RETE_SRC) $(RETE_HDR) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/telnet ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -I drivers/kbd -c $(TELNET_SRC) -o $(BUILD_OBJ)/telnet_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(DNS_SRC)  -o $(BUILD_OBJ)/telnet_dns.o
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -I drivers/pci -c $(RETE_SRC) -o $(BUILD_OBJ)/telnet_rete.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/telnet_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TELNET_LD) \
	    $(BUILD_OBJ)/telnet_start.o $(BUILD_OBJ)/telnet_main.o $(BUILD_OBJ)/telnet_dns.o \
	    $(BUILD_OBJ)/telnet_rete.o $(LIBC_PONTI_OBJ) -o $@
	@echo "[OK] telnet compilato: $@"

.PHONY: telnet
telnet: dirs $(TELNET_BIN)

.PHONY: stack
stack: dirs $(STACK_BIN)

.PHONY: mem
mem: dirs $(MEM_BIN)

# --- Programma utente /bin/textline -------------------------------------------
# Editor di testo lineare (stile edlin). Stesso schema di /bin/ls: usa la
# libc del progetto, compilata e linkata insieme al sorgente.
TEXTLINE_SRC := bin/textline/textline.c
TEXTLINE_BIN := $(BUILD_BIN)/textline
TEXTLINE_LD  := bin/textline/textline.ld

$(TEXTLINE_BIN): $(TEXTLINE_SRC) $(TEXTLINE_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/textline ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(TEXTLINE_SRC) -o $(BUILD_OBJ)/textline_main.o
	$(CC) -m32 -c $(LIBC_START)                            -o $(BUILD_OBJ)/textline_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TEXTLINE_LD) \
	    $(BUILD_OBJ)/textline_start.o \
	    $(BUILD_OBJ)/textline_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] textline compilato: $@"

.PHONY: textline
textline: dirs $(TEXTLINE_BIN)

# --- Programma utente /bin/gfedit ---------------------------------------------
# Editor di testo a schermo intero, riscrittura per EX-OS di GF_TEXTEDITOR.
#
# Unico programma utente compilato da PIU' unita' di traduzione: sono 3000
# righe divise per compito (terminale, buffer, disco, sintassi, interfaccia,
# editing, avvio) e tenerle in un file solo renderebbe illeggibile ognuna.
# Lo schema di link resta quello di tutti gli altri — start.S, gli oggetti
# del programma, la libc del progetto — solo con piu' oggetti in mezzo.
#
# -I drivers/kbd serve per kbd_proto.h: l'editor parla DIRETTAMENTE al
# servizio tastiera via IPC per avere i tasti uno per uno invece che una
# riga alla volta, ed e' quell'header il contratto fra le due parti. E' lo
# stesso motivo per cui la stessa -I sta gia' nelle CFLAGS del kernel (la
# usa il TTY, che di quel servizio e' l'altro cliente).
GFEDIT_DIR  := bin/gfedit
GFEDIT_SRC  := $(GFEDIT_DIR)/gf_main.c   \
               $(GFEDIT_DIR)/gf_term.c   \
               $(GFEDIT_DIR)/gf_buffer.c \
               $(GFEDIT_DIR)/gf_fileio.c \
               $(GFEDIT_DIR)/gf_syntax.c \
               $(GFEDIT_DIR)/gf_ui.c     \
               $(GFEDIT_DIR)/gf_edit.c
GFEDIT_HDR  := $(GFEDIT_DIR)/gfedit.h drivers/kbd/kbd_proto.h lib/include/libc.h
GFEDIT_OBJ  := $(patsubst $(GFEDIT_DIR)/%.c,$(BUILD_OBJ)/gfedit_%.o,$(GFEDIT_SRC))
GFEDIT_BIN  := $(BUILD_BIN)/gfedit
GFEDIT_LD   := $(GFEDIT_DIR)/gfedit.ld

$(BUILD_OBJ)/gfedit_%.o: $(GFEDIT_DIR)/%.c $(GFEDIT_HDR)
	@mkdir -p $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I $(GFEDIT_DIR) -I drivers/kbd -c $< -o $@

$(GFEDIT_BIN): $(GFEDIT_OBJ) $(GFEDIT_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/gfedit ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) -m32 -c $(LIBC_START)         -o $(BUILD_OBJ)/gfedit_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(GFEDIT_LD) \
	    $(BUILD_OBJ)/gfedit_start.o \
	    $(GFEDIT_OBJ)               \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] gfedit compilato: $@"

.PHONY: gfedit
gfedit: dirs $(GFEDIT_BIN)

# --- Programma utente /bin/install --------------------------------------------
INSTALL_SRC := bin/install/install.c
INSTALL_BIN := $(BUILD_BIN)/install
INSTALL_LD  := bin/install/install.ld

$(INSTALL_BIN): $(INSTALL_SRC) $(INSTALL_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/install ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(INSTALL_SRC) -o $(BUILD_OBJ)/install_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                   -o $(BUILD_OBJ)/install_libc.o
	$(CC) -m32 -c $(LIBC_START)                           -o $(BUILD_OBJ)/install_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(INSTALL_LD) \
	    $(BUILD_OBJ)/install_start.o \
	    $(BUILD_OBJ)/install_main.o  \
	    $(BUILD_OBJ)/install_libc.o  \
	    -o $@
	@echo "[OK] install compilato: $@"

.PHONY: install_prog
install_prog: dirs $(INSTALL_BIN)

# --- Programma utente /bin/cp -------------------------------------------------
CP_SRC := bin/cp/cp.c
CP_BIN := $(BUILD_BIN)/cp
CP_LD  := bin/cp/cp.ld

$(CP_BIN): $(CP_SRC) $(CP_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/cp ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(CP_SRC) -o $(BUILD_OBJ)/cp_main.o
	$(CC) -m32 -c $(LIBC_START)                      -o $(BUILD_OBJ)/cp_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(CP_LD) \
	    $(BUILD_OBJ)/cp_start.o \
	    $(BUILD_OBJ)/cp_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] cp compilato: $@"

.PHONY: cp_prog
cp_prog: dirs $(CP_BIN)
# --- Programma utente /bin/mount ----------------------------------------------
# Un solo binario per mount e umount: il comportamento dipende da argv[0],
# e su un floppy da 1.44 MB due binari che condividono tutto tranne tre
# righe non valgono i ~12 KB in piu'. mkfloppy.sh copia lo stesso file
# come /bin/mount e /bin/umount.
MOUNT_SRC := bin/mount/mount.c
MOUNT_BIN := $(BUILD_BIN)/mount
MOUNT_LD  := bin/mount/mount.ld

$(MOUNT_BIN): $(MOUNT_SRC) $(MOUNT_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/mount ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(MOUNT_SRC) -o $(BUILD_OBJ)/mount_main.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/mount_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MOUNT_LD) \
	    $(BUILD_OBJ)/mount_start.o \
	    $(BUILD_OBJ)/mount_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] mount compilato: $@"

.PHONY: mount_prog
mount_prog: dirs $(MOUNT_BIN)

# --- Programma utente /bin/mkdir ----------------------------------------------
MKDIR_SRC := bin/mkdir/mkdir.c
MKDIR_BIN := $(BUILD_BIN)/mkdir
MKDIR_LD  := bin/mkdir/mkdir.ld

$(MKDIR_BIN): $(MKDIR_SRC) $(MKDIR_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/mkdir ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(MKDIR_SRC) -o $(BUILD_OBJ)/mkdir_main.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/mkdir_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MKDIR_LD) \
	    $(BUILD_OBJ)/mkdir_start.o \
	    $(BUILD_OBJ)/mkdir_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] mkdir compilato: $@"

.PHONY: mkdir_prog
mkdir_prog: dirs $(MKDIR_BIN)

# --- Programma utente /bin/rmdir ----------------------------------------------
RMDIR_SRC := bin/rmdir/rmdir.c
RMDIR_BIN := $(BUILD_BIN)/rmdir
RMDIR_LD  := bin/rmdir/rmdir.ld

$(RMDIR_BIN): $(RMDIR_SRC) $(RMDIR_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/rmdir ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(RMDIR_SRC) -o $(BUILD_OBJ)/rmdir_main.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/rmdir_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(RMDIR_LD) \
	    $(BUILD_OBJ)/rmdir_start.o \
	    $(BUILD_OBJ)/rmdir_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] rmdir compilato: $@"

.PHONY: rmdir_prog
rmdir_prog: dirs $(RMDIR_BIN)

# --- Programma utente /bin/delete ---------------------------------------------
DELETE_SRC := bin/delete/delete.c
DELETE_BIN := $(BUILD_BIN)/delete
DELETE_LD  := bin/delete/delete.ld

$(DELETE_BIN): $(DELETE_SRC) $(DELETE_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione /bin/delete ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(DELETE_SRC) -o $(BUILD_OBJ)/delete_main.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/delete_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(DELETE_LD) \
	    $(BUILD_OBJ)/delete_start.o \
	    $(BUILD_OBJ)/delete_main.o  \
	    $(LIBC_PONTI_OBJ)  \
	    -o $@
	@echo "[OK] delete compilato: $@"

.PHONY: delete_prog
delete_prog: dirs $(DELETE_BIN)

# --- libc.so: la regola sta piu' in alto ---------------------------------------
#
# ! QUI C'ERA UN TENTATIVO MORTO, tolto il 17 agosto 2026: costruiva libc.so
# con -shared -fPIC, cioe' un ET_DYN. Il caricatore ELF del kernel accetta solo
# ET_EXEC (kernel/loader/elf.c), quindi quella libreria non e' mai stata
# caricata da nessuno: si costruiva, finiva sul floppy, e occupava spazio.
# Adesso al suo posto c'e' la libc condivisa vera — ET_EXEC a base fissa, con
# risoluzione per nome — e la regola sta accanto a quelle di exwin.so ed
# exdlg.so.


# --- Driver ELF: floppy.drv --------------------------------------------------
FLOPPY_DRV_SRC := drivers/floppy/floppy.c
FLOPPY_DRV_OUT := $(BUILD_DRIVERS)/floppy.drv
FLOPPY_DRV_LD  := drivers/floppy/floppy.ld

$(FLOPPY_DRV_OUT): $(FLOPPY_DRV_SRC) $(FLOPPY_DRV_LD) $(SEGNO_FLAG)
	@echo "=== Compilazione driver floppy.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_DRV) -shared -fPIC -c $(FLOPPY_DRV_SRC) -o $(BUILD_DRIVERS)/floppy.o
	$(LD) -m $(CROSS_LD_EMU) -shared --allow-shlib-undefined --hash-style=sysv -T $(FLOPPY_DRV_LD) $(BUILD_DRIVERS)/floppy.o -o $@
	@echo "[OK] floppy.drv compilato: $@"

.PHONY: floppy_drv
floppy_drv: dirs $(FLOPPY_DRV_OUT)

# --- Driver ring3: kbd.drv ---------------------------------------------------
# ATTENZIONE: questo NON è più un modulo condiviso.
#
# Fino a luglio 2026 kbd.drv era compilato con -shared -fPIC e linkato
# ET_DYN, per essere mappato nello spazio del kernel da drvmgr.c/dynlink.c.
# elf_load(), che carica i processi ring3, accetta solo ET_EXEC e lo
# rifiutava con "tipo non supportato (type=3)".
#
# Ora è un normale eseguibile utente statico, costruito con lo stesso
# schema di /bin/ls: kbd.c + lib/libc.c + lib/start.S linkati insieme,
# senza -shared e senza -fPIC. L'unica differenza rispetto a un programma
# di /bin è la destinazione sul floppy (/dev invece di /bin), decisa da
# tools/mkfloppy.sh in base alla directory build/drivers/.
KBD_DRV_SRC   := drivers/kbd/kbd.c
KBD_DRV_PROTO := drivers/kbd/kbd_proto.h
KBD_DRV_OUT   := $(BUILD_DRIVERS)/kbd.drv
KBD_DRV_LD    := drivers/kbd/kbd.ld

$(KBD_DRV_OUT): $(KBD_DRV_SRC) $(KBD_DRV_PROTO) $(KBD_DRV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 kbd.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/kbd -c $(KBD_DRV_SRC) -o $(BUILD_DRIVERS)/kbd_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS)/kbd_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS)/kbd_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(KBD_DRV_LD) \
	    $(BUILD_DRIVERS)/kbd_start.o \
	    $(BUILD_DRIVERS)/kbd_main.o  \
	    $(BUILD_DRIVERS)/kbd_libc.o  \
	    -o $@
	@echo "[OK] kbd.drv compilato: $@"

.PHONY: kbd_drv
kbd_drv: dirs $(KBD_DRV_OUT)

# --- Server ring3: pci.drv ---------------------------------------------------
# Enumerazione del bus PCI in userspace. Stesso schema di kbd.drv.
#
# NON entra nel floppy: ci sta, ma il floppy serve ad avviare e a
# installare, e il PCI serve alla rete — che sul floppy non ci starebbe
# comunque. Va sul CD EX-OS (target iso-exos), che è la sua destinazione
# dichiarata. Vedi la regola $(ISOX_ROOT) più sotto.
PCI_DRV_SRC   := drivers/pci/pci.c
PCI_DRV_OUT   := $(BUILD_DRIVERS)/pci.drv
PCI_DRV_LD    := drivers/pci/pci.ld

$(PCI_DRV_OUT): $(PCI_DRV_SRC) $(PCI_DRV_PROTO) $(PCI_DRV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione server ring3 pci.drv ==="
	@mkdir -p $(BUILD_DRIVERS_CD)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -c $(PCI_DRV_SRC) -o $(BUILD_DRIVERS)/pci_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS)/pci_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS)/pci_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(PCI_DRV_LD) \
	    $(BUILD_DRIVERS)/pci_start.o \
	    $(BUILD_DRIVERS)/pci_main.o  \
	    $(BUILD_DRIVERS)/pci_libc.o  \
	    -o $@
	@echo "[OK] pci.drv compilato: $@"

.PHONY: pci_drv
pci_drv: dirs $(PCI_DRV_OUT)

# --- Driver ring3: ne2k.drv (solo CD) ----------------------------------------
# NE2000/DP8390. Sta in userspace perche' la RAM dei pacchetti e' SULLA
# scheda e ci si arriva da una porta di I/O: nessun DMA verso la memoria
# di sistema, quindi nessuna necessita' che il kernel sappia di indirizzi
# fisici o pagine bloccate.
NE2K_DRV_SRC   := drivers/ne2k/ne2k.c
NE2K_DRV_OUT   := $(BUILD_DRIVERS_CD)/ne2k.drv
NE2K_DRV_LD    := drivers/ne2k/ne2k.ld

$(NE2K_DRV_OUT): $(NE2K_DRV_SRC) $(NET_PROTO) $(PCI_DRV_PROTO) $(NE2K_DRV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 ne2k.drv ==="
	@mkdir -p $(BUILD_DRIVERS_CD)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -I drivers/net -c $(NE2K_DRV_SRC) -o $(BUILD_DRIVERS_CD)/ne2k_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS_CD)/ne2k_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS_CD)/ne2k_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(NE2K_DRV_LD) \
	    $(BUILD_DRIVERS_CD)/ne2k_start.o \
	    $(BUILD_DRIVERS_CD)/ne2k_main.o  \
	    $(BUILD_DRIVERS_CD)/ne2k_libc.o  \
	    -o $@
	@echo "[OK] ne2k.drv compilato: $@"

.PHONY: ne2k_drv
ne2k_drv: dirs $(NE2K_DRV_OUT)

# --- Driver ring3: pcnet.drv (solo CD) ---------------------------------------
# AMD PCnet-PCI II / FAST III (Am79C970/C973). A differenza del ne2k questa
# scheda e' un BUS MASTER: legge e scrive la RAM di sistema da sola, agli
# indirizzi FISICI che le si danno. Da qui le due cose che il ne2k non
# chiedeva — il bit bus master nel comando PCI (via /dev/pci.drv) e
# SYS_DMA_ALLOC nel kernel, che e' l'unico modo di avere memoria
# fisicamente contigua di cui si conosca l'indirizzo fisico.
PCNET_DRV_SRC  := drivers/pcnet/pcnet.c
PCNET_DRV_OUT  := $(BUILD_DRIVERS_CD)/pcnet.drv
PCNET_DRV_LD   := drivers/pcnet/pcnet.ld

$(PCNET_DRV_OUT): $(PCNET_DRV_SRC) $(NET_PROTO) $(PCI_DRV_PROTO) $(PCNET_DRV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 pcnet.drv ==="
	@mkdir -p $(BUILD_DRIVERS_CD)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -I drivers/net -c $(PCNET_DRV_SRC) -o $(BUILD_DRIVERS_CD)/pcnet_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS_CD)/pcnet_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS_CD)/pcnet_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(PCNET_DRV_LD) \
	    $(BUILD_DRIVERS_CD)/pcnet_start.o \
	    $(BUILD_DRIVERS_CD)/pcnet_main.o  \
	    $(BUILD_DRIVERS_CD)/pcnet_libc.o  \
	    -o $@
	@echo "[OK] pcnet.drv compilato: $@"

.PHONY: pcnet_drv
pcnet_drv: dirs $(PCNET_DRV_OUT)

# --- Driver ring3 e1000.drv: la scheda PREDEFINITA di QEMU (solo CD) ---------
#
# ! QUESTA SCHEDA NON SI GUIDA COME LE ALTRE DUE. NE2000 e PCnet rispondono
# nello spazio I/O; l'e1000 ha i registri in MEMORIA (BAR0), e mappare
# memoria fisica in spazio utente e' una syscall che EX-OS non ha ancora —
# punto 1 del gradino 0 di DIREZIONE.md.
#
# Si usa la FINESTRA A PORTE della BAR1 (IOADDR/IODATA): l'intero spazio dei
# registri raggiunto due accessi per volta. E' documentata dal costruttore,
# non e' un trucco, ed e' l'unica strada finche' non c'e' la mappatura MMIO.
E1000_DRV_SRC  := drivers/e1000/e1000.c
E1000_DRV_OUT  := $(BUILD_DRIVERS_CD)/e1000.drv
E1000_DRV_LD   := drivers/e1000/e1000.ld

$(E1000_DRV_OUT): $(E1000_DRV_SRC) $(NET_PROTO) $(PCI_DRV_PROTO) $(E1000_DRV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione driver ring3 e1000.drv ==="
	@mkdir -p $(BUILD_DRIVERS_CD)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -I drivers/net -c $(E1000_DRV_SRC) -o $(BUILD_DRIVERS_CD)/e1000_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS_CD)/e1000_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS_CD)/e1000_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(E1000_DRV_LD) \
	    $(BUILD_DRIVERS_CD)/e1000_start.o \
	    $(BUILD_DRIVERS_CD)/e1000_main.o  \
	    $(BUILD_DRIVERS_CD)/e1000_libc.o  \
	    -o $@
	@echo "[OK] e1000.drv compilato: $@"

.PHONY: e1000_drv
e1000_drv: dirs $(E1000_DRV_OUT)

# --- Stack IPv4 ring3: ip.drv (solo CD) ---------------------------------------
# ARP + IPv4 + ICMP in un PROCESSO A SE'. Non tocca porte: parla col driver
# di scheda via IPC come qualunque altro programma. Sta fuori dal driver
# perche' questi protocolli sono uguali su ogni scheda, hanno tempi propri
# (scadenze ARP, attese di risposta) che il driver non deve gestire, e
# perche' se sbaglia lo stack si riavvia lo stack — la scheda resta accesa.
IP_DRV_SRC := drivers/ip/ip.c
IP_DRV_OUT := $(BUILD_DRIVERS_CD)/ip.drv
IP_DRV_LD  := drivers/ip/ip.ld

$(IP_DRV_OUT): $(IP_DRV_SRC) $(NET_PROTO) $(IP_PROTO) $(IP_DRV_LD) $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(SEGNO_FLAG)
	@echo "=== Compilazione stack ring3 ip.drv ==="
	@mkdir -p $(BUILD_DRIVERS_CD)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -c $(IP_DRV_SRC) -o $(BUILD_DRIVERS_CD)/ip_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS_CD)/ip_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS_CD)/ip_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(IP_DRV_LD) \
	    $(BUILD_DRIVERS_CD)/ip_start.o \
	    $(BUILD_DRIVERS_CD)/ip_main.o  \
	    $(BUILD_DRIVERS_CD)/ip_libc.o  \
	    -o $@
	@echo "[OK] ip.drv compilato: $@"

.PHONY: ip_drv
ip_drv: dirs $(IP_DRV_OUT)

.PHONY: all
# ! SI COSTRUISCE DALLE LISTE, non da un elenco scritto qui. Fino ad
# agosto 2026 questa riga ripeteva a mano i nomi dei programmi di rete e
# dei driver, e mancavano pcnet_drv e xcp: due elenchi della stessa cosa
# divergono al primo che si dimentica di aggiornarne uno.
all: dirs stage1 stage2 kernel $(PROGRAMMI_FLOPPY) $(PROGRAMMI_CD) $(PROGRAMMI_EXWIN) $(DRIVER_CD) verifica-programmi floppy
	@echo ""
	@echo "============================================"
	@echo " EX-OS build completata!"
	@echo " Immagine: $(FLOPPY_IMG)"
	@echo "============================================"
	@echo ""
	@echo "Test con QEMU:"
	@echo "  make run"
	@echo "Debug con GDB:"
	@echo "  make debug"

# =============================================================================
# FASE 1a — BOOTLOADER
# =============================================================================

.PHONY: stage1
stage1: dirs $(STAGE1_BIN)
	@echo "[OK] Stage 1 compilato: $(STAGE1_BIN)"

$(STAGE1_BIN): $(BOOT_DIR)/stage1/boot.asm $(SEGNO_FLAG)
	@echo "=== FASE 1a — Stage 1 Boot Sector ==="
	@echo "[..] Assemblo Stage 1: $<"
	@mkdir -p $(BUILD_STAGE1)
	$(AS) $(ASFLAGS_BIN) $< -o $@
	@SIZE=$$(stat -c%s $@); \
	if [ "$$SIZE" -ne 512 ]; then \
		echo "[ERRORE] Stage 1 deve essere 512 byte, trovato: $$SIZE byte"; \
		exit 1; \
	fi
	@SIG=$$(dd if=$@ bs=2 skip=255 count=1 status=none | od -A n -t x2 | tr -d " \n"); \
	if [ "$$SIG" != "aa55" ]; then \
		echo "[ERRORE] Firma 0x55AA mancante (trovato: $$SIG)!"; \
		exit 1; \
	fi
	@echo "[OK] Stage 1: 512 byte, firma 0x55AA verificata"

.PHONY: stage2
stage2: dirs stage1 $(STAGE2_BIN)
	@echo "[OK] Stage 2 compilato: $(STAGE2_BIN)"

# Stage2 e' interamente in assembly puro (loader.asm), compilato come
# flat binary a 16 bit con NASM. NON usa piu' i vecchi file C
# (fat12.c/loader.c/print.c), che producevano codice 32-bit ELF non
# eseguibile in Real Mode 16-bit.
STAGE2_ASM_SRC := $(BOOT_DIR)/stage2/loader.asm

# ! LA RISOLUZIONE SI PUO' SCEGLIERE A COSTRUZIONE: `make SVGA=800x600`.
#
# Serve perche' l'impostazione vive DENTRO l'immagine — la scrive
# /dev/svga.drv dentro LOADER.BIN — quindi ogni floppy ricostruito ripartiva
# in modo testo, e chi stava provando il server grafico se ne accorgeva solo
# dal messaggio «lo schermo e' in modo TESTO».
#
# ! IL PREDEFINITO RESTA IL TESTO, e non e' pigrizia: chi costruisce EX-OS
# non deve ritrovarsi in grafica senza averlo chiesto, e chi lavora sulla
# seriale non se ne accorgerebbe nemmeno.
# ! LA SCELTA SI RICORDA, NON SI RIPETE A OGNI COMANDO. Alla prima stesura
# bisognava passare SVGA= a ogni invocazione, e bastava un `make iso-exos`
# senza per ricostruire Stage 2 in modo testo e disfare tutto — in silenzio,
# perche' nessuno dei due comandi sbagliava. Adesso `make SVGA=800x600` la
# scrive in .svga e da li' in poi vale per tutti.
#
# ! FUORI DA build/, o un `make clean` se la porterebbe via: e' una scelta di
# chi costruisce, non un prodotto della costruzione. Stessa ragione per cui i
# dischi non stanno in dist/.
SVGA_FILE := .svga
SVGA ?= $(strip $(shell cat $(SVGA_FILE) 2>/dev/null))
SVGA_MODO := $(strip \
  $(if $(filter 640x480,$(SVGA)),1, \
  $(if $(filter 800x600,$(SVGA)),2, \
  $(if $(filter 1024x768,$(SVGA)),3, \
  $(if $(SVGA),ERRORE,0)))))

# ! E LA MODALITA' DEVE STARE FRA LE PREREQUISITE, O NON SI RICOSTRUISCE.
# Alla prima stesura no, e il difetto e' venuto fuori subito: `make
# SVGA=800x600` e poi `make` da' ancora l'800x600, perche' loader.asm non e'
# cambiato e make non ha modo di sapere che e' cambiata una VARIABILE. E' lo
# stesso modo di sbagliare del floppy e delle due ISO — un'uscita che dipende
# da qualcosa che non le si e' dichiarato.
#
# Il segno sta nel NOME del file: cambiando la modalita' cambia il nome, il
# file non c'e', e Stage 2 si rifa'. I vecchi si tolgono, o resterebbero li'
# a dire che un tempo si era costruito in un altro modo.
STAGE2_SVGA_SEGNO := $(BUILD_STAGE2)/.svga-$(SVGA_MODO)

$(STAGE2_SVGA_SEGNO):
	@mkdir -p $(BUILD_STAGE2)
	@rm -f $(BUILD_STAGE2)/.svga-*
	@touch $@
	@printf '%s\n' "$(SVGA)" > $(SVGA_FILE)

$(STAGE2_BIN): $(STAGE2_ASM_SRC) $(STAGE2_SVGA_SEGNO) $(SEGNO_FLAG)
	@echo "=== Assemblo Stage 2 (flat binary 16-bit) ==="
	@mkdir -p $(BUILD_STAGE2)
	@if [ "$(SVGA_MODO)" = "ERRORE" ]; then \
	    echo "[ERRORE] SVGA=$(SVGA) non e' una risoluzione nota."; \
	    echo "         Ammesse: 640x480, 800x600, 1024x768, oppure niente"; \
	    echo "         (niente = console di testo 80x25, il predefinito)."; \
	    exit 1; \
	fi
	$(AS) -f bin -DSVGAMODO=$(SVGA_MODO) $(STAGE2_ASM_SRC) -o $@
	@if [ "$(SVGA_MODO)" != "0" ]; then \
	    echo "     risoluzione all'avvio: $(SVGA) (modo $(SVGA_MODO))"; \
	fi
	@SIZE=$$(stat -c%s $@); echo "[..] Stage 2: $$SIZE byte"

# =============================================================================
# FASE 1b — KERNEL BASE
# =============================================================================

.PHONY: kernel
kernel: dirs stage2 $(KERNEL_BIN)
	@echo "[OK] Kernel compilato: $(KERNEL_BIN) (flat binary per boot)"
	@echo "[OK] Kernel ELF debug: $(KERNEL_ELF)"

KERNEL_ASM_SRC := $(KERNEL_DIR)/arch/x86/entry.asm \
                  $(KERNEL_DIR)/arch/x86/isr_stubs.asm \
                  $(KERNEL_DIR)/sched/context_switch.asm

# --- Settori di avvio per disco rigido ----------------------------------------
# MBR e settore di avvio della partizione. Vengono assemblati con NASM e poi
# INCORPORATI nel kernel come array C (tools/bin2c.py): l'installazione non
# deve dipendere dalla presenza di due file sul supporto di avvio, o un
# floppy incompleto darebbe un errore a meta' installazione, con il disco
# gia' modificato.
BOOTHD_DIR   := $(BUILD_DIR)/boot
MBR_ASM      := $(BOOT_DIR2)/mbr/mbr.asm
BOOTHD_ASM   := $(BOOT_DIR2)/stage1hd/boothd.asm
MBR_BIN      := $(BOOTHD_DIR)/mbr.bin
BOOTHD_BIN   := $(BOOTHD_DIR)/boothd.bin
MBR_C        := $(BOOTHD_DIR)/mbr_bin.c
BOOTHD_C     := $(BOOTHD_DIR)/boothd_bin.c

$(MBR_BIN): $(MBR_ASM) $(SEGNO_FLAG)
	@mkdir -p $(BOOTHD_DIR)
	@echo "[..] Assemblo MBR: $<"
	$(AS) $(ASFLAGS_BIN) $< -o $@

$(BOOTHD_BIN): $(BOOTHD_ASM) $(SEGNO_FLAG)
	@mkdir -p $(BOOTHD_DIR)
	@echo "[..] Assemblo settore di avvio partizione: $<"
	$(AS) $(ASFLAGS_BIN) $< -o $@

$(MBR_C): $(MBR_BIN) $(TOOLS_DIR)/bin2c.py
	@python3 $(TOOLS_DIR)/bin2c.py $< $@ boot_mbr_bin

$(BOOTHD_C): $(BOOTHD_BIN) $(TOOLS_DIR)/bin2c.py
	@python3 $(TOOLS_DIR)/bin2c.py $< $@ boot_hd_bin

$(BUILD_KERNEL)/mbr_bin.o: $(MBR_C)
	@mkdir -p $(BUILD_KERNEL)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_KERNEL)/boothd_bin.o: $(BOOTHD_C)
	@mkdir -p $(BUILD_KERNEL)
	$(CC) $(CFLAGS) -c $< -o $@

KERNEL_C_SRC   := $(KERNEL_DIR)/arch/x86/gdt.c \
                  $(KERNEL_DIR)/arch/x86/idt.c \
                  $(KERNEL_DIR)/arch/x86/isr.c \
                  $(KERNEL_DIR)/arch/x86/fpu.c \
                  $(KERNEL_DIR)/arch/x86/entropia.c \
                  $(KERNEL_DIR)/arch/x86/vga.c \
                  $(KERNEL_DIR)/arch/x86/vga_modo3.c \
                  $(KERNEL_DIR)/arch/x86/font8x16.c \
                  $(KERNEL_DIR)/arch/x86/rtc.c \
                  $(KERNEL_DIR)/arch/x86/kprintf.c \
                  $(KERNEL_DIR)/arch/x86/memfun.c \
                  $(KERNEL_DIR)/arch/x86/tsc.c \
                  $(KERNEL_DIR)/mm/pmm.c \
                  $(KERNEL_DIR)/mm/paging.c \
                  $(KERNEL_DIR)/mm/kmalloc.c \
                  $(KERNEL_DIR)/mm/shm.c \
                  $(KERNEL_DIR)/sched/sched.c \
                  $(KERNEL_DIR)/ipc/ipc.c \
                  $(KERNEL_DIR)/ipc/pipe.c \
                  $(KERNEL_DIR)/ipc/pty.c \
                  $(KERNEL_DIR)/syscall/syscall.c \
                  $(KERNEL_DIR)/syscall/syscall_impl.c \
                  $(KERNEL_DIR)/fs/fat12.c \
                  $(KERNEL_DIR)/fs/fat.c \
                  $(KERNEL_DIR)/fs/ext2.c \
                  $(KERNEL_DIR)/fs/iso9660.c \
                  $(KERNEL_DIR)/fs/vfs.c \
                  $(KERNEL_DIR)/boot/bootinst.c \
                  $(KERNEL_DIR)/block/ata.c \
                  $(KERNEL_DIR)/block/atapi.c \
                  $(KERNEL_DIR)/block/mbr.c \
                  $(KERNEL_DIR)/block/vol.c \
                  $(KERNEL_DIR)/block/blk.c \
                  $(KERNEL_DIR)/fs/cfg.c \
                  $(KERNEL_DIR)/loader/elf.c \
                  $(KERNEL_DIR)/loader/lib.c \
                  $(KERNEL_DIR)/loader/dynlink.c \
                  $(KERNEL_DIR)/loader/drvmgr.c \
                  $(KERNEL_DIR)/arch/x86/power.c \
                  $(KERNEL_DIR)/version.c \
                  $(KERNEL_DIR)/kernel_main.c

KERNEL_ASM_OBJ := $(patsubst $(KERNEL_DIR)/%.asm, $(BUILD_KERNEL)/%.o, $(KERNEL_ASM_SRC))
KERNEL_C_OBJ   := $(patsubst $(KERNEL_DIR)/%.c,   $(BUILD_KERNEL)/%.o, $(KERNEL_C_SRC))
KERNEL_OBJS    := $(KERNEL_ASM_OBJ) $(KERNEL_C_OBJ) $(BUILD_KERNEL)/tty.o \
                  $(BUILD_KERNEL)/mbr_bin.o $(BUILD_KERNEL)/boothd_bin.o

$(BUILD_KERNEL)/%.o: $(KERNEL_DIR)/%.asm
	@echo "[..] Assemblo: $<"
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS_ELF) $< -o $@

$(BUILD_KERNEL)/%.o: $(KERNEL_DIR)/%.c
	@echo "[..] Compilo: $<"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Dipendenze dagli header, generate da GCC (-MMD -MP).
#
# TRAPPOLA CORRETTA (luglio 2026): senza queste, modificare un header NON
# ricompilava nulla. Il caso concreto: dopo aver incrementato
# EXOS_VERSION in kernel/include/version.h il sistema continuava a
# mostrare la versione precedente, e sembrava che l'incremento "non
# avesse effetto" — esattamente il sintomo, e la perdita di tempo, gia'
# documentati per LIBC_SRC. Ora ogni .o dipende dagli header che include.
#
# -MP aggiunge target fasulli per gli header: senza, cancellare o
# rinominare un header fa fallire make con "No rule to make target".
-include $(KERNEL_C_OBJ:.o=.d)

# ! IL TTY AVEVA LA REGOLA MA NON LE DIPENDENZE (corretto 0.149)
#
# Questa regola nomina il solo .c, e tty.o non fa parte di KERNEL_C_OBJ:
# la -include qui sopra non lo copriva. Modificare un header non lo
# ricompilava — ed e' la stessa trappola gia' documentata per version.h,
# ricomparsa in un posto che l'elenco non toccava.
#
# Il guasto che ne e' seguito e' istruttivo perche' non assomiglia a un
# problema di build: aggiungendo campi a `struct Process` in sched.h,
# tty.o ha continuato a leggere `console` all'OFFSET VECCHIO. Tutte e
# quattro le shell chiedevano la riga sulla console 0, il driver tastiera
# annunciava «la richiesta di PID 5 sostituisce quella di PID 4», e i
# comandi digitati sparivano senza errori. Sembrava un difetto delle
# console virtuali; era un oggetto compilato contro un'altra struttura.
$(BUILD_KERNEL)/tty.o: drivers/tty/tty.c
	@echo "[..] Compilo TTY inline: $<"
	@mkdir -p $(BUILD_KERNEL)
	$(CC) $(CFLAGS) -I drivers/tty -MMD -MP -c $< -o $@

-include $(BUILD_KERNEL)/tty.d

# FIX BUG #1: Due step distinti:
#   1. Link ELF32 (kernel.elf) — per GDB, simboli di debug, analisi
#   2. objcopy --output-target binary (kernel.bin) — flat binary per il boot
#
# Stage 2 carica kernel.bin a 0x100000. Il primo byte di kernel.bin corrisponde
# a kernel_entry (grazie a *(.text.kernel_entry) per primo nel linker script).
# In precedenza kernel.bin era in formato ELF: a 0x100000 c'era l'header ELF
# (0x7F 'E' 'L' 'F') e la CPU andava in triple fault cercando di eseguirlo.

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_DIR)/kernel.ld
	@echo "=== Link Kernel ELF ==="
	@echo "[..] Link Kernel → $@"
	$(LD) $(LDFLAGS) -T $(KERNEL_DIR)/kernel.ld $(KERNEL_OBJS) -o $@
	@echo "Sezioni kernel ELF:"
	@size $@ || true

$(KERNEL_BIN): $(KERNEL_ELF) $(SEGNO_FLAG)
	@echo "=== Conversione Kernel ELF → flat binary ==="
	@echo "[..] objcopy: $< → $@"
	$(OBJCOPY) -O binary $< $@
	@KSIZE=$$(stat -c%s $@); \
	printf "  kernel.bin: %s byte\n" "$$KSIZE"
	@echo "[OK] kernel.bin pronto per il boot (flat binary a 0x100000)"

# =============================================================================
# FLOPPY IMAGE
# =============================================================================

# ! IL FLOPPY DIPENDE DA TUTTO CIO' CHE CI FINISCE DENTRO, e fino ad
# agosto 2026 dipendeva solo da stage1, stage2 e kernel.
#
# mkfloppy.sh copia quello che TROVA in build/bin nel momento in cui parte.
# Con `make -j` niente impediva a questa regola di partire mentre i
# programmi si stavano ancora compilando: l'immagine veniva costruita con i
# binari VECCHI, e la prova successiva girava su codice che non era quello
# appena scritto. Non dava nessun errore — dava un risultato sbagliato che
# sembrava giusto, e l'ho scoperto solo confrontando le date di
# dist/floppy.img e build/bin/ls: l'immagine era piu' VECCHIA del binario.
#
# Con -j1 funzionava per l'ordine in cui stanno scritte le prerequisite di
# `all`, cioe' per caso.
#
# ! MA DIPENDERE DAI BERSAGLI FINTI NON BASTAVA, ed e' il difetto chiuso il
# 13 agosto 2026. `shell`, `ls`, `kbd_drv` sono .PHONY: non hanno una data,
# quindi make li considera SEMPRE piu' recenti dell'immagine e la ricetta
# girava a ogni invocazione. Non e' pericoloso come il caso dell'ISO — un
# floppy rifatto senza motivo e' corretto, solo lento — ma vuol dire anche
# che l'immagine NON si sa dire scaduta: era `make` a non poter rispondere
# alla domanda, non la risposta a essere «sempre si'».
#
# Adesso il bersaglio e' IL FILE, e dipende dai file veri. `floppy` resta
# come nome comodo da digitare.
#
# ! E LE DUE LISTE VANNO TENUTE INSIEME. PROGRAMMI_FLOPPY dice COSA
# COSTRUIRE (nomi finti), PROGRAMMI_FLOPPY_OUT dice DA COSA DIPENDE
# L'IMMAGINE (nomi di file). E' la stessa coppia di DRIVER_CD e
# DRIVER_SOLO_CD_OUT, ed e' lo stesso modo di sbagliare: un programma
# aggiunto solo alla prima si costruisce e non entra mai in un floppy nuovo.
# La guardia `verifica-dipendenze-floppy` qui sotto confronta le due.
PROGRAMMI_FLOPPY_OUT := $(SHELL_BIN) $(HELLO_BIN) $(LS_BIN) $(MEM_BIN) \
                        $(STACK_BIN) $(DISK_BIN) $(LIBCTEST_BIN) $(FDISK_BIN) \
                        $(MKFS_BIN) $(TRUNC_BIN) $(CHKDSK_BIN) $(RENAME_BIN) \
                        $(RM_BIN) $(MV_BIN) $(UNAME_BIN) $(MOUNT_BIN) \
                        $(CP_BIN) $(INSTALL_BIN) $(TEXTLINE_BIN) $(GFEDIT_BIN) \
                        $(MKDIR_BIN) $(RMDIR_BIN) $(DELETE_BIN) $(HWCONFIG_BIN) \
                        $(HWINFO_BIN) $(CMP_BIN) $(SHMTEST_BIN) $(POLLTEST_BIN) \
                        $(TOOLINST_BIN) $(LOGIN_BIN) $(HELP_BIN) $(KEYMAP_BIN) \
                        $(TESTO_BIN) $(MOUSE_BIN) $(ID_BIN) $(BUILD_BIN)/whoami $(PERM_BIN) $(BUILD_BIN)/chown $(LIBC_SO) \
                        $(FLOPPY_DRV_OUT) $(KBD_DRV_OUT) $(SVGA_DRV_OUT) \
                        $(VGAPROVA_OUT) $(PCI_DRV_OUT) $(MOUSESER_OUT) \
                        $(UHCI_OUT) $(XHCI_OUT)

# ! LA GUARDIA GEMELLA DI QUELLA DEL CD. Guarda cosa mkfloppy.sh COPIEREBBE
# — tutto cio' che trova in build/bin, build/drivers/*.drv e build/lib/*.so —
# e si ferma se qualcosa di quello non e' fra le dipendenze dell'immagine.
#
# ! CONTROLLA IL PRODOTTO, NON L'ELENCO DEI SORGENTI, e qui sta la
# differenza con `verifica-programmi`. Quella parte dai sorgenti e chiede
# «finisce da qualche parte?»; questa parte da cio' che finira' DAVVERO
# sul floppy e chiede «l'immagine sa di dipenderne?». Un programma
# costruito da una regola che nessuno ha dichiarato passa la prima e cade
# qui.
.PHONY: verifica-dipendenze-floppy
verifica-dipendenze-floppy:
	@dichiarati=""; \
	for f in $(PROGRAMMI_FLOPPY_OUT); do \
	    dichiarati="$$dichiarati $$(basename $$f)"; \
	done; \
	manca=""; \
	copiati=""; \
	for f in $(BUILD_BIN)/*; do \
	    [ -f "$$f" ] || continue; \
	    n=$$(basename $$f); \
	    case "$$n" in *.o|*.d|*.a|*.so) continue ;; esac; \
	    [ -x "$$f" ] || continue; \
	    copiati="$$copiati $$n"; \
	done; \
	for f in $(BUILD_DRIVERS)/*.drv $(BUILD_LIB)/*.so; do \
	    [ -f "$$f" ] || continue; \
	    copiati="$$copiati $$(basename $$f)"; \
	done; \
	for n in $$copiati; do \
	    case " $$dichiarati " in *" $$n "*) ;; *) manca="$$manca $$n";; esac; \
	done; \
	if [ -n "$$manca" ]; then \
	    echo "[ERRORE] finisce sul floppy ma non e' fra le sue dipendenze:$$manca"; \
	    echo "         l'immagine non si rifa' quando cambia: resta VECCHIA,"; \
	    echo "         senza nessun errore. Aggiungilo a PROGRAMMI_FLOPPY_OUT"; \
	    echo "         nel Makefile, accanto al suo nome in PROGRAMMI_FLOPPY."; \
	    exit 1; \
	fi; \
	echo "[OK] ogni file del floppy e' fra le dipendenze dell'immagine"

# ! verifica-dipendenze-floppy E' UNA PREREQUISITA D'ORDINE (dopo la barra),
# per la stessa ragione dell'ISO: e' un bersaglio finto, non ha una data, e
# fra le prerequisite normali renderebbe l'immagine perennemente scaduta —
# cioe' rimetterebbe esattamente il difetto che questa regola chiude.
# ! LE IMMAGINI DIPENDONO ANCHE DAL Makefile, ED E' LA QUARTA VOLTA CHE
# QUESTA FAMIGLIA DI DIFETTI SI PRESENTA. Le prime tre erano il floppy che
# dipendeva da bersagli finti, l'ISO che non elencava un driver, e la
# modalita' SVGA che non era fra le prerequisite di Stage 2. Questa e' la
# stessa cosa un piano piu' su: la RICETTA e' descritta qui dentro, quindi
# cambiare qui dentro puo' cambiare il contenuto dell'immagine — e senza
# questa riga make non ha modo di saperlo.
#
# ! ED E' SUCCESSO: aggiunta la copia di /exwin/lib nella ricetta del CD,
# `make iso-exos` ha detto che era tutto aggiornato e il CD e' rimasto senza
# l'elenco delle applicazioni. Il sintomo era un menu vuoto.
#
# Costa una ricostruzione delle immagini a ogni modifica del Makefile. E' il
# prezzo giusto: sono minuti, contro un'immagine che mente.
$(FLOPPY_IMG): Makefile $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN) \
               $(PROGRAMMI_FLOPPY_OUT) \
               boot/kernel.cfg boot/kernel.txt boot/help.txt \
               $(TOOLS_DIR)/mkfloppy.sh \
               | dirs verifica-dipendenze-floppy
	@echo "=== Creazione Immagine Floppy FAT12 1.44MB ==="
	@chmod +x $(TOOLS_DIR)/mkfloppy.sh
	@$(TOOLS_DIR)/mkfloppy.sh
	@# ! SI DICE QUANTO SPAZIO RESTA. E' il numero che avvisa PRIMA che
	@# l'immagine smetta di contenere tutto, invece di scoprirlo da un
	@# sistema che si avvia e non trova un file.
	@# ! SI CERCA LA RIGA 'free', NON L'ULTIMA. mdir chiude l'elenco con
	@# una riga VUOTA: `tail -1` prendeva quella e il numero usciva vuoto —
	@# cioe' l'avviso che deve precedere un floppy pieno non diceva niente,
	@# e sembrava una riga di contorno invece di un controllo rotto.
	@echo "     spazio libero sul floppy: $$(mdir -i $(FLOPPY_IMG) :: 2>/dev/null | \
	    grep -i 'free' | tail -1 | tr -dc '0-9') byte"

# ! I PROGRAMMI RESTANO PREREQUISITE DEL NOME COMODO, non del file. Chi
# digita `make floppy` vuole che quello che manca si costruisca; il file, di
# suo, deve solo sapere quando e' scaduto. Sono due domande diverse, e prima
# avevano una risposta sola.
.PHONY: floppy
floppy: $(PROGRAMMI_FLOPPY) $(FLOPPY_IMG)

.PHONY: img
img: floppy

# =============================================================================
# CD DEGLI STRUMENTI
#
# Un secondo supporto, separato dal floppy e volutamente NON avviabile:
# ci va cio' che in 1.44 MB non entra e che non serve a tutti. Il primo
# inquilino previsto e' TCC, in /bin.
#
# PERCHE' UN CD E NON UN FLOPPY PIU' GRANDE. Il floppy e' il supporto di
# avvio e deve restare quello collaudato; gli strumenti cambiano spesso,
# pesano e non devono poter rompere l'avvio. Il CD e' anche in sola
# lettura per costruzione, il che e' esattamente cio' che si vuole da un
# disco di strumenti: nessuno puo' modificarli per sbaglio.
#
# L'immagine si costruisce con tools/mkiso.py (niente genisoimage in
# questo ambiente) e porta sia i nomi ISO 9660 sia quelli Joliet, cosi'
# `ls /cdrom` mostra i nomi veri.
#
# NON fa parte di `make all`: il floppy e' l'artefatto principale, e chi
# compila il sistema non deve aspettare un CD che magari non usera'.
# =============================================================================
ISO_ROOT    := $(BUILD_DIR)/iso
ISO_IMG     := $(DIST_DIR)/exos-tools.iso
# Dove la build dei binutils nativi ha lasciato as-new e ld-new. Fuori dal
# repository di proposito: sono binari di 7 MB costruiti da sorgenti di
# terzi. Si sovrascrive dalla riga di comando se sta altrove:
#     make iso BINUTILS_NATIVI=~/altro/build
BINUTILS_NATIVI ?= $(HOME)/exos-native/build-nativi
# Il sysroot del bersaglio, dove tools/gcclibs-exos/prepara-gcclibs.sh
# installa GMP, MPFR e MPC. Se ci sono, il CD porta anche /bin/provamp.
CROSS_SYSROOT   ?= $(HOME)/exos-cross/i386-exos

# Dove il canadian cross ha lasciato cc1, xgcc e cpp. Si preferisce
# l'albero costruito con --enable-checking=release; se non c'e' si ripiega
# su quello con i controlli di sviluppo, DICENDOLO — un cc1 di 40 MB e uno
# di 25 si comportano allo stesso modo e si distinguono solo dal peso, che
# e' esattamente cio' che conta su una macchina piccola.
# Dove sta la cross toolchain i386-exos. Si puo' sovrascrivere da riga
# di comando: make iso EXOS_CROSS=/altro/percorso
EXOS_CROSS ?= $(HOME)/exos-cross

# ! IL CROSS SE LO METTE NEL PATH IL MAKEFILE, non chi lancia make.
#
# Le ricette qui sotto chiamano i386-exos-gcc, -g++ e -strip per nome. Se
# non sono nel PATH la costruzione del CD muore a meta' con «i386-exos-gcc:
# not found» — dopo aver gia' copiato binutils, GCC e 129 MB di albero,
# cioe' al punto piu' tardo e piu' caro. E la cosa che manca non manca
# affatto: sta in $(EXOS_CROSS)/bin, che questo Makefile conosce gia'
# perche' e' da li' che prende libgcc.a e gli header del bersaglio.
#
# Non puo' oscurare niente: i binari del cross hanno tutti il prefisso
# i386-exos-, che nessun programma di sistema usa.
export PATH := $(EXOS_CROSS)/bin:$(PATH)

# Dove prepara-openssl.sh ha lasciato libcrypto.a.
OPENSSL_BUILD ?= $(HOME)/openssl-build-exos
# GNU make per EX-OS: lo costruisce tools/make-exos/prepara-make.sh
MAKE_NATIVO   ?= $(HOME)/exos-native/build-make

# =============================================================================
# FreeBASIC — UNA VERSIONE SOLA, DICHIARATA IN UN POSTO SOLO
#
# Dal 12 agosto 2026 il compilatore installato e' la 1.10.1, che dentro EX-OS
# ha chiuso il PUNTO FISSO: gen2 e gen3 identiche byte per byte, verificato
# con /bin/cmp. Prima era la 1.07.3.
#
# ! TRE COSE DEVONO MUOVERSI INSIEME, E STANNO IN DUE POSTI DIVERSI:
#
#     $(FB_NATIVO)/fbc                     il compilatore
#     $(FB_NATIVO)/libfb.a, fbrt0.o        la sua runtime
#     $(FB_SORGENTI)/inc/*.bi              i suoi header
#
# Un fbc 1.10.1 con gli header della 1.07 non e' «quasi giusto»: e' un
# compilatore che accetta dichiarazioni che la sua runtime non ha, e il
# guasto si vede al link su simboli mai visti — o non si vede affatto.
#
# ! PER QUESTO NON BASTA IL COMMENTO: C'E' UN CONTROLLO. La versione si
# LEGGE dal binario e dall'albero e si confrontano (vedi verifica-fb piu'
# sotto). Cambiare una variabile e dimenticare l'altra e' esattamente il
# genere di errore che un commento non ferma.
#
# PER PASSARE A UNA VERSIONE PIU' NUOVA si cambia SOLO questa riga, dopo
# aver costruito il compilatore con tools/freebasic-exos/prepara-fb.sh:
FB_VERSIONE   ?= 1.10.1

# L'albero si cerca dove si scompattano i pacchetti e, per compatibilita',
# anche nella radice — la 1.07.3 stava li'. Nessuno dei due e' tracciato in
# git: sono sorgenti di terzi, come quelli di GCC.
FB_SORGENTI   ?= $(patsubst %/.,%,$(firstword \
                   $(wildcard freebasic/FreeBASIC-$(FB_VERSIONE)-source-bootstrap/.) \
                   $(wildcard FreeBASIC-$(FB_VERSIONE)-source-bootstrap/.)))

# La costruzione incrociata da cui vengono fbc, libfb.a e fbrt0.o.
# La produce tools/freebasic-exos/prepara-fb.sh.
FB_NATIVO     ?= $(HOME)/fb-build-110

# Le due versioni LETTE, non dichiarate: una dal binario, una dall'albero.
# Se non combaciano la costruzione si ferma.
FB_VER_BIN     = $(shell grep -ao 'Version [0-9][0-9.]*' $(FB_NATIVO)/fbc \
                          2>/dev/null | head -1 | sed 's/^Version //')
FB_VER_SRC     = $(shell sed -n 's/^FBVERSION *:*= *//p' \
                          $(FB_SORGENTI)/version.mk 2>/dev/null | head -1)

# =============================================================================
# I sorgenti che il CD porta in /freebasic, per RICOSTRUIRE FreeBASIC dentro
# EX-OS.
#
# ! E' UNA VARIABILE A PARTE E NON $(FB_SORGENTI), e la separazione serve.
# FB_SORGENTI e' l'albero da cui e' stato costruito il `fbc` che va sul CD:
# da li' si prendono anche i suoi .bi, e i due DEVONO combaciare — un fbc
# 1.07 con gli header di un'altra versione e' un compilatore che accetta
# dichiarazioni che la sua runtime non ha.
#
# Questa invece dice solo «quale albero mettere sul CD perche' qualcuno provi
# a costruirlo». Puntarla a una versione NUOVA e' esattamente il modo di
# provare se EX-OS riesce a compilarla, senza toccare il compilatore che gia'
# funziona:
#
#     make iso FB_SORGENTI_CD=freebasic/FreeBASIC-1.10.1-source-bootstrap
#
# ! SI VUOLE IL PACCHETTO «source-bootstrap», non «linux-x86_64». Quello con
# il nome di una piattaforma e' la distribuzione BINARIA: porta bin/, lib/ e
# include/ gia' compilati per QUELLA macchina, e non ha nessun src/. Non c'e'
# niente da compilare e i binari non servono a EX-OS. La regola qui sotto lo
# controlla e lo dice, invece di mettere sul CD 47 MB inservibili.
FB_SORGENTI_CD ?= $(FB_SORGENTI)

# Un SECONDO albero, per provare a portare una versione piu' nuova senza
# perdere quella che si costruisce.
#
# ! NE SERVONO DUE SUL CD, e la ragione e' che fanno due mestieri diversi:
# /freebasic e' l'albero della versione INSTALLATA — quella con cui il punto
# fisso e' chiuso — e /freebasic-nuovo e' il bersaglio di lavoro. Sostituire
# il primo col secondo toglierebbe l'unico riferimento contro cui
# confrontarsi proprio mentre si porta il nuovo.
#
# ! QUALE SIA «L'INSTALLATA» CAMBIA NEL TEMPO, e il 12 agosto 2026 e'
# cambiata: era la 1.07.3, ora e' la 1.10.1, che qui dentro ha chiuso il
# punto fisso a tre generazioni. Questo commento parla di RUOLI, non di
# numeri di versione: il numero sta in FB_VERSIONE, in un posto solo.
#
# Prende da se' quello che c'e' sotto freebasic/: e' la directory in cui si
# scompattano i pacchetti (vedi .gitignore). Vuota = niente secondo albero.
#
# ! SI ESCLUDE L'ALBERO GIA' USATO COME RIFERIMENTO. Da quando FB_SORGENTI
# punta anch'esso sotto freebasic/, senza questo filtro lo stesso albero
# finirebbe sul CD DUE VOLTE — 9 MB in doppio e due copie che chi legge
# crede diverse.
FB_SORGENTI_NUOVI ?= $(firstword $(filter-out $(FB_SORGENTI)/., \
                                    $(wildcard freebasic/*/.)))

# ! DUE CANDIDATI, IN ORDINE: prima la build con C++ (cc1 E cc1plus), poi
# quella con il solo C. Serve mentre la prima si costruisce — sono ore a
# -j1 — cosi' il CD continua a portare un compilatore funzionante invece
# di restare senza.
GCC_NATIVO_REL  ?= $(HOME)/gcc-build-cpp/gcc
GCC_NATIVO_CHK  ?= $(HOME)/gcc-build-rel/gcc
ISO_LEGGIMI := $(TOOLS_DIR)/iso/leggimi.txt
ISO_MKISO   := $(TOOLS_DIR)/mkiso.py

# ! IL CD DIPENDE ANCHE DAI BINARI CHE IMPACCHETTA, e all'inizio no.
# binutils e GCC nativi stanno FUORI da questo albero (in $(HOME)), quindi
# make non poteva accorgersi che erano cambiati: dopo aver rilinkato cc1,
# `make iso` diceva che era tutto aggiornato e il CD continuava a portare
# la versione di ore prima. Ci sono cascato due volte in un giorno — la
# prima col floppy, vedi PROGRAMMI_FLOPPY.
#
# $(wildcard ...) e non il percorso nudo: se quei binari non ci sono, la
# lista e' vuota e il CD si fa lo stesso (dicendo che mancano), invece di
# fallire con "nessuna regola per costruire".
BINARI_ESTERNI := $(wildcard $(GCC_NATIVO_REL)/cc1) $(wildcard $(GCC_NATIVO_CHK)/cc1) \
                  $(wildcard $(BINUTILS_NATIVI)/gas/as-new) \
                  $(wildcard $(BINUTILS_NATIVI)/ld/ld-new) \
                  $(wildcard $(BINUTILS_NATIVI)/binutils/ar) \
                  $(wildcard $(OPENSSL_BUILD)/libcrypto.a) \
                  $(wildcard $(EXOS_CROSS)/i386-exos/lib/libc.a) \
                  $(wildcard $(FB_NATIVO)/fbc) \
                  $(wildcard $(MAKE_NATIVO)/make)

# ! TUTTO tools/iso/, NON UN ELENCO SCRITTO A MANO — 13 agosto 2026.
#
# Qui c'era la lista dei sorgenti di prova, battuta a mano, e ne mancavano
# CINQUE: prova-fb.bas, prova-fb2.bas, prova-gcc.c, prova-ssl.c e
# strumenti.txt finivano sul CD senza esserne dipendenze. Cambiarli non
# rifaceva l'immagine, cioe' esattamente il difetto che il 13 agosto e'
# costato tre corse con uhci.drv — qui trovato guardando invece che
# sbattendoci contro.
#
# ! LA CURA NON E' AGGIUNGERE I CINQUE CHE MANCANO. Quella lista si e'
# staccata dalla realta' una volta e si stacchera' ancora: ogni prova nuova
# e' un'occasione di dimenticarsene, e il sintomo non somiglia a un errore.
# `tools/iso/` esiste SOLO per riempire questo CD, quindi la lista giusta e'
# la directory: una sola, e non puo' divergere da se stessa.
#
# La sottodirectory si toglie e si riprende file per file: come prerequisita
# una directory porta la propria data, che cambia anche quando dentro non
# cambia niente di rilevante.
ISO_PROVE := $(filter-out %/prova-make,$(wildcard $(TOOLS_DIR)/iso/*)) \
             $(wildcard $(TOOLS_DIR)/iso/prova-make/*)

# ! UNA LISTA SOLA ANCHE PER I DOCUMENTI, usata sia qui che nella ricetta
# che li copia. KERNEL_CORE_NOTES.md finiva in doc/ senza essere una
# dipendenza, ed e' lo stesso modo di sbagliare in piccolo.
ISO_DOC := README.md README.en.md KERNEL_CORE_NOTES.md gpl-2.0.txt

# ! IL CD PORTA I SORGENTI DI FreeBASIC, QUINDI NE DIPENDE — e non ne
# dipendeva. Trovato in diretta il 13 agosto 2026: ripulito il porting
# EX-OS della runtime (src/rtlib/exos/, che e' roba nostra), `make iso`
# ha risposto «nessuna operazione da eseguire». Il CD avrebbe continuato a
# portare i sorgenti di prima senza dirlo — lo stesso difetto di uhci.drv,
# in un posto dove nessuno lo cercava.
#
# ! SI USA find E NON UN ELENCO. Sono 1100 file e la ricetta li imbarca con
# tar: qualunque elenco scritto a mano sarebbe sbagliato il giorno dopo.
# Costa 40 ms a invocazione di make, ed e' il prezzo di un'immagine che sa
# dire quando e' scaduta.
#
# ! E SI FILTRA obj/ COME FA IL tar DELLA RICETTA. Sono i prodotti di una
# costruzione: dipenderne vorrebbe dire rifare il CD dopo ogni compilazione
# fatta dentro quell'albero, senza che il contenuto del CD cambi.
FB_CD_FILE := $(if $(wildcard $(FB_SORGENTI_CD)/makefile), \
                $(shell find $(FB_SORGENTI_CD)/src -type f -not -path '*/obj/*' 2>/dev/null) \
                $(wildcard $(FB_SORGENTI_CD)/makefile) \
                $(wildcard $(FB_SORGENTI_CD)/version.mk) \
                $(wildcard $(FB_SORGENTI_CD)/lib/fbextra.x))

# I documenti del porting, copiati in /freebasic sul CD. Anche questi in una
# variabile sola, usata qui e nella ricetta: vedi ISO_DOC.
FB_CD_DOC := $(wildcard $(TOOLS_DIR)/freebasic-exos/MODIFICHE-FBC.md) \
             $(wildcard $(TOOLS_DIR)/freebasic-exos/MODIFICHE-FBC.en.md) \
             $(wildcard $(TOOLS_DIR)/freebasic-exos/PORTING-$(FB_VERSIONE).it.txt) \
             $(wildcard $(TOOLS_DIR)/freebasic-exos/PORTING-$(FB_VERSIONE).en.txt)

$(ISO_IMG): Makefile $(BINARI_ESTERNI) $(ISO_MKISO) $(ISO_PROVE) \
            $(LIBC_SRC) $(LIBC_HDR) $(LIBC_START) \
            $(EXWIN_HDR) $(EXWIN_SRC) $(WIN_PROTO) \
            $(ISO_DOC) $(FB_CD_FILE) $(FB_CD_DOC) $(LIBC_PONTI_OBJ) $(LIBC_SO)
	@echo "=== Creazione CD degli strumenti ==="
	@# ! IL CONTROLLO CHE SAREBBE SERVITO. Qui dentro finiscono binari
	@# per i386-exos costruiti FUORI da questo Makefile (cc1, as, ld,
	@# libcrypto): se la libc ha cambiato un tipo dopo che sono stati
	@# costruiti, si collegano lo stesso e si rompono a caso — vedi
	@# tools/ricostruisci-bersaglio.sh per il caso vero. E' un AVVISO e
	@# non un errore: un CD con dentro roba vecchia si fa comunque, purche'
	@# chi lo fa lo sappia.
	@tools/ricostruisci-bersaglio.sh --verifica || \
	    echo "     !  vedi sopra: il CD conterra' binari con l'ABI vecchia"
	@mkdir -p $(DIST_DIR)
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/exos/include $(ISO_ROOT)/exos/lib $(ISO_ROOT)/doc $(ISO_ROOT)/bin
	@cp $(ISO_LEGGIMI) $(ISO_ROOT)/leggimi.txt
	@cp $(TOOLS_DIR)/iso/prova.s $(ISO_ROOT)/prova.s
	@cp $(TOOLS_DIR)/iso/prova-mp.c $(ISO_ROOT)/prova-mp.c
	@cp $(TOOLS_DIR)/iso/prova-mat.c $(ISO_ROOT)/prova-mat.c
	@cp $(TOOLS_DIR)/iso/prova-cpp.cpp $(ISO_ROOT)/prova-cpp.cpp
	@cp -r lib/include/. $(ISO_ROOT)/exos/include/
	@# ! IL TOOLKIT VA SUL CD DEGLI STRUMENTI, o chi compila DENTRO EX-OS non
	@# lo trova. Ci vanno i tre header — C, C++ e FreeBASIC — e il sorgente
	@# della libreria: non c'e' un .a perche' ogni programma di EX-OS e' un
	@# eseguibile statico che si tira dietro cio' che usa, esattamente come
	@# fa gia' con libc.c.
	@cp $(EXWIN_HDR) $(EXWIN_SRC) $(ISO_ROOT)/exos/include/ 2>/dev/null || true
	@cp $(WIN_PROTO) $(ISO_ROOT)/exos/include/ 2>/dev/null || true
	@cp $(LIBC_PONTI_OBJ) $(LIBC_SO) $(LIBC_START) $(ISO_ROOT)/exos/
	@# Il catalogo degli strumenti: lo legge `toolinst` (cioe' anche
	@# `install -tools`) per sapere cosa c'e' sul CD e cosa si puo'
	@# scegliere. Sta DENTRO l'albero perche' descrive l'albero: un CD
	@# senza non e' rotto, si installa con il catalogo di scorta.
	@cp $(TOOLS_DIR)/iso/strumenti.txt $(ISO_ROOT)/exos/strumenti.txt
	@# --- Runtime del BERSAGLIO: quello che serve a COLLEGARE, non a compilare
	@#
	@# ! SONO OGGETTI i386-exos, NON DELLA MACCHINA CHE COSTRUISCE. Vengono
	@# dalla cross toolchain, ma sono gia' codice di EX-OS: `ld` nativo li
	@# legge qui dentro esattamente come li legge il cross su Linux.
	@#
	@# ! SENZA QUESTI IL DRIVER ARRIVA A META'. `gcc -c` ha bisogno solo di
	@# cpp, cc1 e as; `gcc -o programma` ha bisogno anche di crt0, di
	@# libgcc.a e di libc.a — e senza si ottiene un errore di `ld` su simboli
	@# che non c'entrano niente con il sorgente che si stava compilando.
	@set -e; \
	R=""; \
	for d in $(EXOS_CROSS) $(HOME)/exos-cross; do \
	    [ -f "$$d/i386-exos/lib/crt0.o" ] && R="$$d" && break; \
	done; \
	if [ -n "$$R" ]; then \
	    cp "$$R"/i386-exos/lib/crt0.o "$$R"/i386-exos/lib/libc.a $(ISO_ROOT)/exos/lib/ 2>/dev/null || true; \
	    for f in libm.a; do \
	        [ -f "$$R/i386-exos/lib/$$f" ] && cp "$$R/i386-exos/lib/$$f" $(ISO_ROOT)/exos/lib/; \
	    done; \
	    for f in libgcc.a crti.o crtn.o crtbegin.o crtend.o; do \
	        [ -f "$$R"/lib/gcc/i386-exos/*/$$f ] && cp "$$R"/lib/gcc/i386-exos/*/$$f $(ISO_ROOT)/exos/lib/ || true; \
	    done; \
	    echo "     runtime del bersaglio da $$R: $$(ls $(ISO_ROOT)/exos/lib | tr '\n' ' ')"; \
	else \
	    echo "     runtime del bersaglio assente: /exos/lib vuota, gcc potra' solo compilare (-c)"; \
	fi
	@# ! LA STESSA VARIABILE CHE STA FRA LE PREREQUISITE. Scritti due volte,
	@# i due elenchi divergono: e' cosi' che KERNEL_CORE_NOTES.md e' finito
	@# sul CD senza che il CD dipendesse da lui.
	@cp $(ISO_DOC) $(ISO_ROOT)/doc/
	@# provacpp si costruisce con UNA RIGA e con g++, come su qualunque
	@# altro bersaglio: libstdc++ per i386-exos c'e' (dal 3 agosto 2026,
	@# vedi HANDOFF). Prima serviva compilare con g++ e collegare con gcc,
	@# perche' -lstdc++ non esisteva.
	@#
	@# I binutils NATIVI, se ci sono. Non stanno nel repository — sono
	@# 7 MB l'uno e si costruiscono da sorgenti di terzi (vedi
	@# tools/binutils-exos/leggimi.md) — quindi si copiano da dove li ha
	@# messi la build, e se non ci sono il CD si fa lo stesso.
	@#
	@# ! NON e' un ripiego silenzioso: il messaggio dice quale delle due
	@# cose e' successa, perche' un CD senza `as` e uno con `as` si
	@# distinguono solo provandoli, e la differenza va detta qui.
	@# Si TOLGONO i simboli di debug: sono i cinque sesti del file (7,3 MB
	@# contro 1,4) e non li legge nessuno — il caricatore di EX-OS mappa
	@# solo i segmenti PT_LOAD, e un debugger che li usi qui non c'e'.
	@# ! NON SOLO as E ld. Fino ad agosto 2026 di tutti i binutils nativi —
	@# che la build produce INSIEME, in una volta sola — se ne copiavano due,
	@# e gli altri sei restavano nella directory di build. Bastava a
	@# compilare e collegare a mano; non bastava piu' dal giorno che dentro
	@# EX-OS gira `make`, perche' la prima ricetta della runtime di FreeBASIC
	@# e' `ar rcs libfb.a <200 oggetti>` — e `ar` era li', gia' compilato per
	@# i386-exos, e non lo prendeva nessuno.
	@#
	@# ! ranlib E' UN FILE A PARTE, non un collegamento ad ar. Sui sistemi
	@# veri e' lo stesso binario sotto due nomi; qui EX-OS non ha i
	@# collegamenti, quindi la build ne produce due e se ne copiano due. Sono
	@# 1,3 MB in piu' a testa dopo lo strip.
	@#
	@# I nomi di destinazione sono quelli SENZA suffisso: la build li chiama
	@# `as-new`, `ld-new`, `nm-new`, `strip-new` perche' li costruisce accanto
	@# a quelli gia' installati sulla macchina, e `-new` non e' parte del nome
	@# del programma.
	@if [ -x "$(BINUTILS_NATIVI)/gas/as-new" ]; then \
	    set -e; \
	    for coppia in gas/as-new:as ld/ld-new:ld \
	                  binutils/ar:ar binutils/ranlib:ranlib \
	                  binutils/nm-new:nm binutils/strip-new:strip \
	                  binutils/objcopy:objcopy binutils/objdump:objdump; do \
	        da=$(BINUTILS_NATIVI)/$${coppia%%:*}; a=$(ISO_ROOT)/bin/$${coppia##*:}; \
	        [ -x "$$da" ] || continue; \
	        if command -v i386-exos-strip >/dev/null 2>&1; then \
	            i386-exos-strip -o $$a $$da; \
	        else \
	            cp $$da $$a; \
	        fi; \
	    done; \
	    printf 'binutils nativi per EX-OS (2.44): as ld ar ranlib nm strip objcopy objdump\n' \
	        > $(ISO_ROOT)/bin/leggimi.txt; \
	    echo "     binutils nativi da $(BINUTILS_NATIVI):"; \
	    echo "       $$(cd $(ISO_ROOT)/bin && ls as ld ar ranlib nm strip objcopy objdump 2>/dev/null | tr '\n' ' ')"; \
	    if [ -f $(CROSS_SYSROOT)/lib/libmpc.a ] && command -v i386-exos-gcc >/dev/null 2>&1; then \
	        i386-exos-gcc -O2 -o $(ISO_ROOT)/bin/provamp.tmp \
	            $(TOOLS_DIR)/iso/prova-mp.c -lmpc -lmpfr -lgmp && \
	        i386-exos-strip -o $(ISO_ROOT)/bin/provamp $(ISO_ROOT)/bin/provamp.tmp && \
	        rm -f $(ISO_ROOT)/bin/provamp.tmp && \
	        echo "     provamp: GMP + MPFR + MPC"; \
	    else \
	        echo "     GMP/MPFR/MPC assenti dal sysroot: niente provamp"; \
	    fi; \
	    if [ -f $(CROSS_SYSROOT)/lib/libm.a ] && command -v i386-exos-gcc >/dev/null 2>&1; then \
	        i386-exos-gcc -O2 -o $(ISO_ROOT)/bin/provamat.tmp \
	            $(TOOLS_DIR)/iso/prova-mat.c -lm && \
	        i386-exos-strip -o $(ISO_ROOT)/bin/provamat $(ISO_ROOT)/bin/provamat.tmp && \
	        rm -f $(ISO_ROOT)/bin/provamat.tmp && \
	        echo "     provamat: openlibm"; \
	    else \
	        echo "     libm assente dal sysroot: niente provamat"; \
	    fi; \
	    if [ -f $(CROSS_SYSROOT)/lib/libstdc++.a ] && command -v i386-exos-g++ >/dev/null 2>&1; then \
	        i386-exos-g++ -O2 -o $(ISO_ROOT)/bin/provacpp.tmp \
	            $(TOOLS_DIR)/iso/prova-cpp.cpp -lm && \
	        i386-exos-strip -o $(ISO_ROOT)/bin/provacpp $(ISO_ROOT)/bin/provacpp.tmp && \
	        rm -f $(ISO_ROOT)/bin/provacpp.tmp && \
	        echo "     provacpp: libstdc++ (contenitori, string, eccezioni)"; \
	    else \
	        echo "     libstdc++ assente dal sysroot: niente provacpp"; \
	    fi; \
	else \
	    printf 'Qui arrivera il compilatore: vedi /leggimi.txt\n' \
	        > $(ISO_ROOT)/bin/leggimi.txt; \
	    echo "     binutils nativi assenti ($(BINUTILS_NATIVI)): CD senza /bin"; \
	fi
	@# --- GCC nativo: cc1, il driver e il preprocessore ---------------------
	@# Stessa regola dei binutils: non stanno nel repository, si copiano da
	@# dove li ha lasciati il canadian cross, e se non ci sono il CD si fa
	@# lo stesso dicendo che mancano.
	@#
	@# ! SI DICE QUANTO PESANO. cc1 e' il file piu' grande che EX-OS abbia
	@# mai dovuto caricare, e la differenza fra l'albero `release` e quello
	@# con i controlli di sviluppo si vede solo in megabyte. Stamparlo qui
	@# evita di dover andare a misurare il CD per sapere quale dei due c'e'.
	@# ! IL NOME DEL DRIVER NON E' UN'ETICHETTA: xg++ diventa `g++` e non
	@# `gcc` perche' il driver decide DA COME E' STATO CHIAMATO se compilare
	@# in C o in C++, quale cc1 lanciare e se collegare libstdc++. Lo stesso
	@# binario sotto due nomi fa due mestieri; copiarne uno solo vorrebbe
	@# dire avere cc1plus sul CD e nessun modo di arrivarci.
	@set -e; \
	G=""; \
	if [ -x "$(GCC_NATIVO_REL)/cc1" ]; then G="$(GCC_NATIVO_REL)"; M="release"; \
	elif [ -x "$(GCC_NATIVO_CHK)/cc1" ]; then G="$(GCC_NATIVO_CHK)"; M="controlli di sviluppo"; \
	fi; \
	if [ -n "$$G" ]; then \
	    for b in cpp xgcc xg++; do \
	        [ -x "$$G/$$b" ] || continue; \
	        i386-exos-strip -o $(ISO_ROOT)/bin/$$b "$$G/$$b"; \
	    done; \
	    if [ -f $(ISO_ROOT)/bin/xgcc ]; then \
	        mv $(ISO_ROOT)/bin/xgcc $(ISO_ROOT)/bin/gcc; \
	    fi; \
	    if [ -f $(ISO_ROOT)/bin/xg++ ]; then \
	        mv $(ISO_ROOT)/bin/xg++ $(ISO_ROOT)/bin/g++; \
	    fi; \
	    echo "     GCC nativo incluso da $$G ($$M):"; \
	    for b in cpp gcc g++; do \
	        [ -f $(ISO_ROOT)/bin/$$b ] || continue; \
	        echo "       $$b  $$(du -h $(ISO_ROOT)/bin/$$b | cut -f1)"; \
	    done; \
	else \
	    echo "     GCC nativo assente: si costruisce con tools/gcc-exos/prepara-cc1.sh"; \
	fi
	@# --- L'ALBERO /exos: i percorsi che il driver ha COMPILATI DENTRO ----
	@#
	@# ! QUESTA DISPOSIZIONE NON E' UNA SCELTA NOSTRA, e' quella che xgcc
	@# cerca a runtime perche' e' stato configurato con --prefix=/exos.
	@#
	@# ! E ALLA FINE gcc, g++ E cpp VENGONO TOLTI DA /bin. Ci arrivano
	@# perche' e' da li' che si copiano dentro /exos/bin, ma da /cdrom/bin
	@# NON POSSONO FUNZIONARE: il driver calcola il proprio prefisso come
	@# <dir del binario>/.., cioe' /cdrom, e li' non ci sono ne' libexec/
	@# ne' lib/gcc/. Lasciandoli, `gcc` trovato nel PATH era sempre quello
	@# rotto — /cdrom/bin viene prima di /exos/bin — e rispondeva
	@#
	@#     gcc: fatal error: cannot execute 'cc1': file o directory inesistente
	@#
	@# anche su un sistema dove gli strumenti erano installati e
	@# funzionanti. Un binario che non puo' che fallire, messo dove viene
	@# trovato per primo, e' peggio di un binario assente. as e ld restano:
	@# quelli funzionano da qualunque directory, non avendo un prefisso da
	@# ricostruire.
	@# Si legge dal binario stesso (`strings gcc/cc1 | grep /exos`):
	@#
	@#   /exos/libexec/gcc/i386-exos/17.0.0/     cc1, cc1plus, collect2
	@#   /exos/lib/gcc/i386-exos/17.0.0/         libgcc.a, crt*.o, specs
	@#   /exos/lib/gcc/i386-exos/17.0.0/include  gli header DEL COMPILATORE
	@#                                           (stddef.h, stdarg.h: senza
	@#                                           questi non compila niente)
	@#   /exos/i386-exos/include                 header di sistema del bersaglio
	@#                                           (LA LIBC, vedi sotto: e' QUI
	@#                                           che cc1 la cerca da solo)
	@#   /exos/include                           la nostra libc
	@#   /exos/include/c++/17.0.0[/i386-exos]    libstdc++
	@#   /exos/i386-exos/bin/                    as, ld
	@#   /exos/bin/                              il driver STESSO
	@#
	@# ! IL DRIVER VA DENTRO L'ALBERO, ed e' la chiave di tutto: GCC si
	@# calcola il prefisso da DOVE STA LUI. Lanciato come /cdrom/bin/gcc
	@# cerca gli header in /cdrom/bin/../lib/gcc/... = /cdrom/lib/gcc/, che
	@# non esiste, e risponde «no include path in which to search for
	@# stdio.h». Lanciato come /cdrom/exos/bin/gcc il prefisso diventa
	@# /cdrom/exos e TUTTO l'albero torna al suo posto — senza nessun -B.
	@#
	@# ! as e ld FINISCONO IN DUE POSTI, perche' i due compilatori li
	@# cercano in due posti diversi e finora uno dei due funzionava per
	@# ripiego. GCC li vuole in <prefisso>/i386-exos/bin/ — e' il
	@# gcc_tooldir del suo configure; FreeBASIC in <prefisso>/bin/, perche'
	@# calcola il prefisso come «directory dell'eseguibile meno bin».
	@# Con la sola copia sotto i386-exos, fbc non trovava `as`, ripiegava
	@# sul nome nudo e lo faceva cercare al PATH: funzionava, ma per
	@# coincidenza — e una coincidenza smette di funzionare il giorno che
	@# il PATH cambia, senza che nessuno colleghi le due cose.
	@#
	@# ! GLI ALTRI SEI (ar, ranlib, nm, strip, objcopy, objdump) VANNO SOLO
	@# IN /exos/bin, e la differenza e' la stessa vista dall'altro lato:
	@# i386-exos/bin/ e' il posto dove il DRIVER di GCC cerca gli strumenti
	@# del bersaglio, e ne cerca due — as e ld. Chi chiama `ar` non e' un
	@# compilatore, e' un makefile, e i makefile lo cercano nel PATH.
	@# Copiarli anche la' sarebbero 2,6 MB in piu' sul CD sotto un percorso
	@# che nessuno guarda.
	@#
	@# ! E libstdc++.a VA ACCANTO A libc.a in /exos/lib, non fra gli header.
	@# Gli header C++ da soli fanno arrivare `g++` fino in fondo — cc1plus
	@# compila, as assembla, collect2 chiama ld — e li' cade con
	@#
	@#     ld: cannot find -lstdc++
	@#     ld: have you installed the static version of the stdc++ library ?
	@#
	@# cioe' un errore di LINK dopo una compilazione riuscita, che e' il
	@# momento piu' tardi in cui potesse uscire. Sono 24 MB e vanno messi
	@# dove ld guarda gia' per libc.a e libgcc.a.
	@#
	@# ! LA LIBC STA IN DUE POSTI, E NON E' UNO SPRECO. In /exos/include
	@# perche' e' li' che la mette il prefisso e che il leggimi dice di
	@# cercarla con -I; e in /exos/i386-exos/include perche' e' li' che cc1
	@# la cerca DA SOLO, senza che nessuno glielo dica. Quel percorso e'
	@# TOOL_INCLUDE_DIR, «un altro posto dove potrebbero stare gli header
	@# del sistema bersaglio» (gcc/cppdefault.cc), ed e' l'unico dei due
	@# che la rilocazione con -iprefix sa raggiungere. Finche' e' rimasto
	@# una directory vuota, `gcc -c` dentro EX-OS rispondeva «no include
	@# path in which to search for stdio.h» con gli header a due passi di
	@# distanza. Sono quaranta file di testo: pesano meno del messaggio
	@# d'errore che facevano stampare.
	@#
	@# ! SONO PERCORSI ASSOLUTI: valgono quando il CD e' la radice, oppure
	@# quando questo albero viene INSTALLATO su /exos del disco rigido.
	@# Montando il CD su /cdrom non combaciano, e al driver va detto con
	@# -B/cdrom/exos/lib/gcc/i386-exos/17.0.0/ — vedi /leggimi.txt.
	@set -e; \
	G=""; \
	if [ -x "$(GCC_NATIVO_REL)/cc1" ]; then G="$(GCC_NATIVO_REL)"; \
	elif [ -x "$(GCC_NATIVO_CHK)/cc1" ]; then G="$(GCC_NATIVO_CHK)"; fi; \
	if [ -n "$$G" ]; then \
	    V=$$(basename $$(ls -d $(EXOS_CROSS)/lib/gcc/i386-exos/*/ | head -1)); \
	    LG=$(ISO_ROOT)/exos/lib/gcc/i386-exos/$$V; \
	    mkdir -p $(ISO_ROOT)/exos/libexec/gcc/i386-exos/$$V $$LG \
	             $(ISO_ROOT)/exos/i386-exos/include $(ISO_ROOT)/exos/i386-exos/bin; \
	    for b in cc1 cc1plus collect2; do \
	        [ -x "$$G/$$b" ] && i386-exos-strip -o \
	            $(ISO_ROOT)/exos/libexec/gcc/i386-exos/$$V/$$b "$$G/$$b"; \
	    done; \
	    cp $(EXOS_CROSS)/lib/gcc/i386-exos/$$V/libgcc.a $$LG/ 2>/dev/null || true; \
	    for o in crtbegin.o crtend.o crti.o crtn.o; do \
	        cp $(EXOS_CROSS)/lib/gcc/i386-exos/$$V/$$o $$LG/ 2>/dev/null || true; \
	    done; \
	    cp -r $(EXOS_CROSS)/lib/gcc/i386-exos/$$V/include $$LG/ 2>/dev/null || true; \
	    cp -r $(EXOS_CROSS)/i386-exos/include/c++ $(ISO_ROOT)/exos/include/ 2>/dev/null || true; \
	    cp $(CROSS_SYSROOT)/lib/libstdc++.a $(ISO_ROOT)/exos/lib/ 2>/dev/null || true; \
	    mkdir -p $(ISO_ROOT)/exos/bin; \
	    for b in gcc g++ cpp; do \
	        [ -f $(ISO_ROOT)/bin/$$b ] && cp $(ISO_ROOT)/bin/$$b $(ISO_ROOT)/exos/bin/; \
	    done; \
	    for b in as ld; do \
	        [ -f $(ISO_ROOT)/bin/$$b ] || continue; \
	        cp $(ISO_ROOT)/bin/$$b $(ISO_ROOT)/exos/i386-exos/bin/; \
	        cp $(ISO_ROOT)/bin/$$b $(ISO_ROOT)/exos/bin/; \
	    done; \
	    for b in ar ranlib nm strip objcopy objdump; do \
	        [ -f $(ISO_ROOT)/bin/$$b ] || continue; \
	        cp $(ISO_ROOT)/bin/$$b $(ISO_ROOT)/exos/bin/; \
	    done; \
	    cp -r lib/include/. $(ISO_ROOT)/exos/i386-exos/include/; \
	    echo "     albero /exos: libexec+lib/gcc+include/c++ ($$(du -sh $(ISO_ROOT)/exos | cut -f1))"; \
	    \
	    for b in gcc g++ cpp; do rm -f $(ISO_ROOT)/bin/$$b; done; \
	    echo "     gcc/g++/cpp TOLTI da /bin: da li' non potrebbero funzionare"; \
	fi
	@cp $(TOOLS_DIR)/iso/prova-cc1.c $(ISO_ROOT)/prova-cc1.c
	@# Il gemello C++ di prova-cc1.c: senza #include, per la stessa ragione
	@# scritta in testa a quel file. Serve a provare cc1plus DENTRO EX-OS —
	@# provacpp qui accanto e' compilato su Linux e dimostra un'altra cosa.
	@cp $(TOOLS_DIR)/iso/prova-cpp1.cpp $(ISO_ROOT)/prova-cpp1.cpp
	@cp $(TOOLS_DIR)/iso/prova-gcc.c $(ISO_ROOT)/prova-gcc.c
	@cp $(TOOLS_DIR)/iso/prova-ssl.c $(ISO_ROOT)/prova-ssl.c
	@# --- OpenSSL: la libreria e il programma che la prova -----------------
	@# Come per GCC e binutils: i sorgenti non stanno nel repository e la
	@# libreria si costruisce a parte (tools/openssl-exos/). Se c'e', il CD
	@# porta libcrypto.a in /exos/lib e il programma di prova in /bin.
	@if [ -f "$(OPENSSL_BUILD)/libcrypto.a" ]; then \
	    cp "$(OPENSSL_BUILD)/libcrypto.a" $(ISO_ROOT)/exos/lib/; \
	    mkdir -p $(ISO_ROOT)/exos/include/openssl; \
	    cp -r "$(OPENSSL_BUILD)/include/openssl/." $(ISO_ROOT)/exos/include/openssl/ 2>/dev/null || true; \
	    cp -r openssl/include/openssl/. $(ISO_ROOT)/exos/include/openssl/ 2>/dev/null || true; \
	    if command -v i386-exos-gcc >/dev/null 2>&1; then \
	        i386-exos-gcc -O2 -I "$(OPENSSL_BUILD)/include" -I openssl/include \
	            -o $(ISO_ROOT)/bin/provassl $(TOOLS_DIR)/iso/prova-ssl.c \
	            "$(OPENSSL_BUILD)/libcrypto.a" 2>/dev/null && \
	        i386-exos-strip $(ISO_ROOT)/bin/provassl && \
	        echo "     OpenSSL: libcrypto.a e /bin/provassl ($$(du -h $(ISO_ROOT)/bin/provassl | cut -f1))"; \
	    fi; \
	else \
	    echo "     OpenSSL assente: si costruisce con tools/openssl-exos/prepara-openssl.sh"; \
	fi
	@# --- FreeBASIC: il compilatore e la sua runtime ----------------------
	@# Stessa regola di GCC, binutils e OpenSSL: non sta nel repository, si
	@# copia da dove l'ha lasciato tools/freebasic-exos/prepara-fb.sh, e se
	@# non c'e' il CD si fa lo stesso dicendo che manca.
	@#
	@# ! libfb.a VA SUL CD INSIEME A fbc, non e' un di piu': fbc traduce il
	@# .bas in assembly e poi CHIAMA as e ld, e il link di un programma
	@# FreeBASIC vuole quella libreria. Senza, si ottiene un compilatore che
	@# compila e non riesce a produrre un eseguibile — e il messaggio parla
	@# di simboli che non c'entrano con il sorgente.
	@#
	@# ! FreeBASIC VUOLE LA STESSA STRUTTURA DEL C, e la vuole per conto
	@# proprio: non e' una scelta di stile. Il suo makefile prevede due
	@# disposizioni e la nostra fbc usa quella «non-standalone»:
	@#
	@#     <prefisso>/bin/fbc
	@#     <prefisso>/include/freebasic/       i .bi
	@#     <prefisso>/lib/freebasic/<target>/  libfb.a, fbrt0.o
	@#
	@# Si legge dal comportamento, non dai sorgenti: con fbc in /bin, `fbc -v`
	@# stampa «assembling: /cdrom/bin/../bin/as». Il prefisso e' la directory
	@# dell'eseguibile MENO bin — esattamente come GCC, ma calcolato ogni
	@# volta invece che compilato dentro.
	@#
	@# ! E' PER QUESTO CHE fbc STA IN /exos/bin E NON PIU' IN /bin. Da /bin
	@# il prefisso sarebbe `/` e cerchierebbe /include/freebasic e
	@# /lib/freebasic: non li troverebbe e non lo direbbe chiaramente. Da
	@# /exos/bin trova tutto da solo, ovunque sia montato il CD — che e' la
	@# cosa che a GCC riesce solo con GCC_EXEC_PREFIX o con -B.
	@#
	@# ! DEI 31 MB DI inc/ SE NE COPIANO 460 KB, e la scelta e' voluta: il
	@# resto sono binding ad allegro, GTK, SDL, X11, zlib — librerie che su
	@# EX-OS non ci sono. Portarli darebbe header che compilano e link che
	@# falliscono con simboli mai visti; peggio di un file assente e' un
	@# file che promette. Si copia cio' che corrisponde a cio' che c'e':
	@# crt.bi e crt/ (che mappano sulla nostra libc), fb*.bi e fbc-int/
	@# (la runtime di FreeBASIC).
	@#
	@# ! E SEI NE MANCAVANO, per una lettura sbagliata di quella stessa
	@# regola. `file.bi`, `datetime.bi`, `string.bi`, `dir.bi`, `vbcompat.bi`
	@# e `utf_conv.bi` NON sono binding a librerie di terzi: dichiarano
	@# funzioni che stanno dentro libfb.a, cioe' dentro una libreria che il
	@# CD porta gia'. Per la regola scritta qui sopra dovevano esserci dal
	@# primo giorno.
	@#
	@# L'ha trovato la costruzione di fbc dentro EX-OS, dopo venti file:
	@#
	@#     src/compiler/fbc.bas(13) error 23: File not found, "file.bi"
	@#
	@# perche' i sorgenti del compilatore non hanno TUTTI i propri .bi
	@# accanto: tre li prendono da qui, cioe' dagli header installati. Sono
	@# 7,7 KB in tutto, e senza di loro mancavano anche a chi scrive un
	@# programma proprio — `#include "file.bi"` e' la riga piu' comune di
	@# FreeBASIC dopo `print`.
	@# ! IL NOME DELLA DIRECTORY E' QUELLO DEL BERSAGLIO, e dal 10 agosto
	@# 2026 il bersaglio e' `exos`: fbc compone <prefisso>/lib/freebasic/
	@# <target>/ da solo, e con `exos-x86` cerca li'. Era `linux-x86`
	@# finche' l'fbc credeva di produrre per Linux — vedi
	@# tools/freebasic-exos/bootstrap-exos.md. Sbagliare questo nome da'
	@# un fbc che compila e poi non trova fbrt0.o, cioe' un errore di link
	@# su un file che sul CD c'e', un livello piu' in la'.
	@# --- ! IL COMPILATORE E I SUOI HEADER DEVONO ESSERE LA STESSA VERSIONE
	@#
	@# fbc, la sua runtime e i suoi .bi vengono da DUE variabili diverse
	@# ($(FB_NATIVO) e $(FB_SORGENTI)), e cambiarne una sola non da' nessun
	@# errore: da' un compilatore che accetta dichiarazioni che la sua
	@# runtime non ha. Il guasto arriva al link, su simboli mai visti, e
	@# accusa il programma di chi compila invece di questa riga.
	@#
	@# Percio' le versioni si LEGGONO — una dal binario, una da version.mk —
	@# e si confrontano. Un commento non avrebbe fermato nessuno.
	@set -e; \
	if [ -x "$(FB_NATIVO)/fbc" ]; then \
	    if [ -z "$(FB_SORGENTI)" ]; then \
	        echo "  ! FB_VERSIONE=$(FB_VERSIONE) ma non trovo l'albero dei sorgenti." >&2; \
	        echo "    Cercato in freebasic/ e nella radice. Scompattalo, o correggi" >&2; \
	        echo "    FB_VERSIONE nel Makefile." >&2; \
	        exit 1; \
	    fi; \
	    if [ "$(FB_VER_BIN)" != "$(FB_VER_SRC)" ]; then \
	        echo "  ! FreeBASIC INCOERENTE: il compilatore e i suoi header non" >&2; \
	        echo "    sono della stessa versione." >&2; \
	        echo "      $(FB_NATIVO)/fbc      dice $(FB_VER_BIN)" >&2; \
	        echo "      $(FB_SORGENTI)/version.mk  dice $(FB_VER_SRC)" >&2; \
	        echo "" >&2; \
	        echo "    Muovi FB_VERSIONE e FB_NATIVO INSIEME: un fbc con gli header" >&2; \
	        echo "    di un'altra versione compila e poi non collega." >&2; \
	        exit 1; \
	    fi; \
	    echo "     FreeBASIC $(FB_VER_BIN): compilatore e header combaciano"; \
	fi
	@if [ -x "$(FB_NATIVO)/fbc" ]; then \
	    FBT=exos-x86; \
	    FBL=$(ISO_ROOT)/exos/lib/freebasic/$$FBT; \
	    FBI=$(ISO_ROOT)/exos/include/freebasic; \
	    mkdir -p $(ISO_ROOT)/exos/bin $$FBL $$FBI; \
	    cp "$(FB_NATIVO)/fbc" $(ISO_ROOT)/exos/bin/fbc; \
	    [ -f "$(FB_NATIVO)/libfb.a" ] && cp "$(FB_NATIVO)/libfb.a" $$FBL/; \
	    [ -f "$(FB_NATIVO)/fbrt0.o" ] && cp "$(FB_NATIVO)/fbrt0.o" $$FBL/; \
	    [ -f "$(FB_NATIVO)/fbextra.x" ] && cp "$(FB_NATIVO)/fbextra.x" $$FBL/; \
	    for i in crt.bi fbgfx.bi fbio.bi fbthread.bi \
	             file.bi datetime.bi string.bi dir.bi vbcompat.bi utf_conv.bi; do \
	        cp $(FB_SORGENTI)/inc/$$i $$FBI/ 2>/dev/null || true; \
	    done; \
	    for d in crt fbc-int; do \
	        cp -r $(FB_SORGENTI)/inc/$$d $$FBI/ 2>/dev/null || true; \
	    done; \
	    cp $(TOOLS_DIR)/iso/prova-fb.bas $(ISO_ROOT)/prova-fb.bas; \
	    cp $(TOOLS_DIR)/iso/prova-fb2.bas $(ISO_ROOT)/prova-fb2.bas; \
	    echo "     FreeBASIC: /exos/bin/fbc ($$(du -h $(ISO_ROOT)/exos/bin/fbc | cut -f1)),"; \
	    echo "                lib/freebasic/$$FBT e include/freebasic ($$(du -sh $$FBI | cut -f1))"; \
	else \
	    echo "     FreeBASIC assente: si costruisce con tools/freebasic-exos/prepara-fb.sh"; \
	fi
	@# --- I SORGENTI di FreeBASIC, per ricostruirlo DENTRO EX-OS ------------
	@#
	@# ! E' L'UNICA COSA SUL CD CHE NON SIA UNO STRUMENTO, ed e' il motivo
	@# per cui gli strumenti ci sono. Con make, gcc, as, ld, ar, ranlib e fbc
	@# dentro EX-OS si puo' costruire un programma vero; questo e' il
	@# programma vero, ed e' un compilatore che ricostruisce se stesso.
	@#
	@# ! SI ESCLUDE `obj/`, e sono i due terzi. Le directory degli oggetti
	@# dell'albero di sviluppo contengono i .o della build su Linux: 12 MB di
	@# roba per un'altra macchina, che sul CD sarebbe peggio che inutile —
	@# `make` li troverebbe piu' nuovi dei sorgenti e NON RICOMPILEREBBE
	@# NIENTE, collegando oggetti x86-64 in un eseguibile per EX-OS.
	@#
	@# ! NON C'E' `bootstrap/`, e la mancanza e' voluta: quei 6,6 MB di .asm
	@# servono a costruire un fbc SENZA avere un fbc, e qui un fbc c'e' gia'
	@# — sta in /exos/bin. Il bersaglio `compiler` compila i .bas veri, che e'
	@# la cosa che si vuole provare.
	@#
	@# ! `lib/fbextra.x` SI E LE ALTRE NO. In quella directory, nell'albero
	@# di sviluppo, ci sono anche crt0.o, libc.a, libgcc.a e libm.a: ce li ha
	@# messi a mano chi fa il cross su Linux (vedi bootstrap-exos.md). Sul CD
	@# sarebbero copie vecchie di file che stanno gia' in /exos/lib, e il
	@# link ne prenderebbe una a caso. `fbextra.x` invece e' di FreeBASIC ed
	@# e' l'unico che serva davvero a ogni collegamento.
	@# ! SI CONTROLLA CHE SIANO SORGENTI, e il controllo e' nato da un caso
	@# vero: al posto del pacchetto «source-bootstrap» era stato scompattato
	@# quello «linux-x86_64», cioe' la distribuzione BINARIA — bin/, lib/ e
	@# include/ gia' compilati per un'altra architettura, e nessun src/.
	@# Senza guardare, il CD si sarebbe portato dietro 47 MB di roba
	@# inservibile e chi avesse provato a costruirla si sarebbe sentito dire
	@# «No rule to make target» — un messaggio che parla del makefile mentre
	@# il problema e' il pacchetto scaricato.
	@#
	@# Le tre cose che servono davvero sono il makefile, i .bas del
	@# compilatore e i .c della runtime: se manca una di quelle, non e' un
	@# albero da cui si costruisce.
	@#
	@# ! LA DOCUMENTAZIONE VIAGGIA COL CODICE. I due MODIFICHE-FBC e le
	@# due procedure PORTING stavano solo nel repository, e
	@# BERSAGLIO-EXOS.txt li citava: da dentro EX-OS erano
	@# irraggiungibili. Un rimando a un file che il supporto non porta
	@# e' peggio di nessun rimando.
	@#
	@# ! IL NOME DELLE PROCEDURE PORTA LA VERSIONE e si compone da
	@# FB_VERSIONE: passando alla prossima o si copiano i file giusti,
	@# oppure non si copia niente e si vede — invece di spedire in
	@# silenzio la procedura di quella vecchia.
	@set -e; \
	F="$(FB_SORGENTI_CD)"; \
	if [ ! -d "$$F" ]; then \
	    echo "     sorgenti FreeBASIC assenti ($$F): niente /freebasic"; \
	elif [ ! -f "$$F/makefile" ] || [ ! -d "$$F/src/compiler" ] || \
	     [ ! -d "$$F/src/rtlib" ]; then \
	    echo "     !  $$F NON e' un albero di sorgenti:"; \
	    echo "         manca makefile, src/compiler o src/rtlib."; \
	    if [ -d "$$F/bin" ] && [ -d "$$F/include" ]; then \
	        echo "         Sembra la distribuzione BINARIA (bin/ lib/ include/):"; \
	        echo "         serve il pacchetto «source-bootstrap» di freebasic.net."; \
	    fi; \
	    echo "         niente /freebasic sul CD."; \
	else \
	    mkdir -p $(ISO_ROOT)/freebasic/lib; \
	    tar -C "$$F" --exclude=obj -cf - src \
	        | tar -C $(ISO_ROOT)/freebasic -xf -; \
	    cp "$$F/makefile"   $(ISO_ROOT)/freebasic/makefile; \
	    cp "$$F/version.mk" $(ISO_ROOT)/freebasic/version.mk 2>/dev/null || true; \
	    cp "$$F/lib/fbextra.x" $(ISO_ROOT)/freebasic/lib/fbextra.x 2>/dev/null || true; \
	    cp $(TOOLS_DIR)/iso/leggimi-freebasic.txt \
	       $(ISO_ROOT)/freebasic/leggimi.txt; \
	    cp $(FB_CD_DOC) $(ISO_ROOT)/freebasic/ 2>/dev/null || true; \
	    echo "     sorgenti FreeBASIC da $$F:"; \
	    echo "       /freebasic ($$(du -sh $(ISO_ROOT)/freebasic | cut -f1), $$(find $(ISO_ROOT)/freebasic -type f | wc -l) file)"; \
	    if [ -d "$(ISO_ROOT)/freebasic/src/rtlib/exos" ]; then \
	        echo "       bersaglio exos: PRESENTE, si costruisce dentro EX-OS"; \
	        sed 's/@VERSIONE@/$(FB_VERSIONE)/g' \
	            $(TOOLS_DIR)/iso/bersaglio-exos.txt \
	            > $(ISO_ROOT)/freebasic/BERSAGLIO-EXOS.txt; \
	    else \
	        echo "       !  bersaglio exos ASSENTE: manca src/rtlib/exos,"; \
	        echo "           questo albero NON si costruisce con TARGET_OS=exos."; \
	        echo "           Va portato: vedi tools/freebasic-exos/."; \
	        { echo "QUESTO ALBERO NON SI COSTRUISCE PER EX-OS."; \
	          echo ""; \
	          echo "Manca src/rtlib/exos/, cioe' lo strato di sistema, e i"; \
	          echo "sorgenti del compilatore non conoscono -target exos."; \
	          echo "Con TARGET_OS=exos il link fallira' su funzioni che non"; \
	          echo "c'entrano con il file che stava compilando."; \
	          echo ""; \
	          echo "Il porting sta in tools/freebasic-exos/ del repository:"; \
	          echo "  applica.py         lo strato di runtime  (src/rtlib/exos/)"; \
	          echo "  bersaglio-exos.py  il bersaglio nel compilatore"; \
	          echo ""; \
	          echo "Erano scritti per la 1.07.3. Su un albero piu' nuovo le"; \
	          echo "righe di riferimento cambiano e vanno rifatte a mano."; \
	        } > $(ISO_ROOT)/freebasic/NON-COSTRUIBILE.txt; \
	    fi; \
	fi
	@# --- GNU make: chi decide cosa ricompilare ----------------------------
	@#
	@# Stessa regola di tutto il resto: non sta nel repository come binario,
	@# si costruisce con tools/make-exos/prepara-make.sh e se non c'e' il CD
	@# si fa lo stesso dicendo che manca.
	@#
	@# ! E' L'ULTIMO PEZZO DEL KIT, e va detto perche' sembra il meno
	@# importante. Con gcc, as, ld e ar dentro EX-OS si compila e si collega;
	@# quello che ancora non si puo' fare e' COSTRUIRE UN PROGRAMMA, perche'
	@# nessuno sa in che ordine e cosa e' gia' aggiornato. Senza make, l'unico
	@# modo di costruire FreeBASIC dentro EX-OS e' battere trecento comandi a
	@# mano nell'ordine giusto.
	@#
	@# ! VA IN /exos/bin E ANCHE IN /bin, a differenza di gcc. Il driver di
	@# GCC da /bin non puo' funzionare — si calcola il prefisso da dove sta e
	@# li' non trova ne' libexec ne' lib/gcc — mentre make non ha niente da
	@# ritrovare: e' un programma solo, senza un albero attorno. In /bin ci va
	@# perche' e' il primo posto del PATH, e `make` deve rispondere subito
	@# anche quando il CD e' montato su /cdrom e /exos/bin non e' nel PATH.
	@if [ -x "$(MAKE_NATIVO)/make" ]; then \
	    mkdir -p $(ISO_ROOT)/exos/bin; \
	    if command -v i386-exos-strip >/dev/null 2>&1; then \
	        i386-exos-strip -o $(ISO_ROOT)/exos/bin/make $(MAKE_NATIVO)/make; \
	    else \
	        cp $(MAKE_NATIVO)/make $(ISO_ROOT)/exos/bin/make; \
	    fi; \
	    cp $(ISO_ROOT)/exos/bin/make $(ISO_ROOT)/bin/make; \
	    mkdir -p $(ISO_ROOT)/prova-make; \
	    cp $(TOOLS_DIR)/iso/prova-make/. $(ISO_ROOT)/prova-make/ -r; \
	    echo "     GNU make: /exos/bin/make e /bin/make ($$(du -h $(ISO_ROOT)/exos/bin/make | cut -f1)),"; \
	    echo "               /prova-make/ (make + gcc + as + ld + ar + ranlib insieme)"; \
	else \
	    echo "     GNU make assente: si costruisce con tools/make-exos/prepara-make.sh"; \
	fi
	@# --- Il SECONDO albero di FreeBASIC, se c'e' -------------------------
	@#
	@# ! VA IN /freebasic-nuovo E NON SOSTITUISCE /freebasic. Sono due
	@# mestieri diversi: il primo e' l'albero che si costruisce davvero — con
	@# cui si e' chiuso il punto fisso a tre generazioni — il secondo e' il
	@# bersaglio di lavoro. Sostituire il primo col secondo toglierebbe
	@# l'unico riferimento contro cui confrontarsi proprio mentre si porta il
	@# nuovo, e lascerebbe il CD senza NESSUN albero costruibile.
	@#
	@# ! E SI DICE SE E' COSTRUIBILE. Un albero senza src/rtlib/exos non
	@# conosce il bersaglio: il link fallirebbe su funzioni che non c'entrano
	@# con il file in compilazione. Meglio un file che lo dichiara di un
	@# leggimi che promette comandi destinati a fallire.
	@set -e; \
	F=$(patsubst %/.,%,$(FB_SORGENTI_NUOVI)); \
	if [ -z "$$F" ] || [ ! -d "$$F" ]; then \
	    echo "     secondo albero FreeBASIC: nessuno sotto freebasic/"; \
	elif [ ! -f "$$F/makefile" ] || [ ! -d "$$F/src/compiler" ]; then \
	    echo "     !  $$F non e' un albero di sorgenti: saltato"; \
	else \
	    mkdir -p $(ISO_ROOT)/freebasic-nuovo/lib; \
	    tar -C "$$F" --exclude=obj -cf - src \
	        | tar -C $(ISO_ROOT)/freebasic-nuovo -xf -; \
	    cp "$$F/makefile" $(ISO_ROOT)/freebasic-nuovo/makefile; \
	    cp "$$F/version.mk" $(ISO_ROOT)/freebasic-nuovo/version.mk 2>/dev/null || true; \
	    cp "$$F/lib/fbextra.x" $(ISO_ROOT)/freebasic-nuovo/lib/ 2>/dev/null || true; \
	    mkdir -p $(ISO_ROOT)/freebasic-nuovo/inc; \
	    for i in crt.bi fbgfx.bi fbio.bi fbthread.bi \
	             file.bi datetime.bi string.bi dir.bi vbcompat.bi utf_conv.bi; do \
	        cp "$$F/inc/$$i" $(ISO_ROOT)/freebasic-nuovo/inc/ 2>/dev/null || true; \
	    done; \
	    for d in crt fbc-int; do \
	        cp -r "$$F/inc/$$d" $(ISO_ROOT)/freebasic-nuovo/inc/ 2>/dev/null || true; \
	    done; \
	    V=$$(sed -n 's/.*FBVERSION *:*= *//p' "$$F/version.mk" 2>/dev/null | head -1); \
	    echo "     secondo albero FreeBASIC: /freebasic-nuovo (versione $$V,"; \
	    echo "       $$(du -sh $(ISO_ROOT)/freebasic-nuovo | cut -f1))"; \
	    if [ -d "$(ISO_ROOT)/freebasic-nuovo/src/rtlib/exos" ]; then \
	        echo "       bersaglio exos: PRESENTE, si costruisce dentro EX-OS"; \
	        { echo "QUESTO ALBERO CONOSCE EX-OS."; \
	          echo ""; \
	          echo "Versione: $$V"; \
	          echo ""; \
	          echo "Le modifiche le ha messe tools/freebasic-exos/, e sono"; \
	          echo "raccontate una per una in MODIFICHE-FBC.md (italiano) e"; \
	          echo "MODIFICHE-FBC.en.md (inglese), QUI ACCANTO."; \
	          echo ""; \
	          echo "La PROCEDURA passo per passo - come si porta e come si"; \
	          echo "costruisce, con le trappole - sta in:"; \
	          echo "  PORTING-1.10.1.it.txt   italiano"; \
	          echo "  PORTING-1.10.1.en.txt   inglese"; \
	          echo ""; \
	          echo "PER COSTRUIRLO QUI DENTRO, con l'fbc gia' installato."; \
	          echo "Prima si copia su ext2: su FAT i nomi tornano in"; \
	          echo "MAIUSCOLO e i modelli del makefile non li riconoscono."; \
	          echo ""; \
	          echo "  make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 \\"; \
	          echo "       CFLAGS=\"-O2 -DDISABLE_FFI\" rtlib"; \
	          echo "  make TARGET_OS=exos TARGET_ARCH=x86 DISABLE_MT=1 compiler"; \
	          echo ""; \
	          echo "! LA VARIABILE E' TARGET_OS, NON TARGET. TARGET esiste, ma"; \
	          echo "il makefile lo legge come triplet GNU e lo cerca in una"; \
	          echo "tabella dove exos non c'e': lascia TARGET_OS VUOTO e mette"; \
	          echo "BUILD_PREFIX=exos-, cioe' exos-gcc ed exos-ar che non"; \
	          echo "esistono. Con TARGET_OS vuoto la runtime si cerca nella sola"; \
	          echo "src/rtlib/, si salta src/rtlib/exos/, e il link fallisce su"; \
	          echo "funzioni che non c'entrano col file in compilazione."; \
	          echo ""; \
	          echo "! DISABLE_MT=1 non e' un'ottimizzazione: senza, si"; \
	          echo "costruisce anche libfbmt.a — altri 427 file, quasi tre ore,"; \
	          echo "per una runtime con i thread che EX-OS non ha."; \
	          echo ""; \
	          echo "! L'albero /freebasic (1.07.3) NON va sostituito con"; \
	          echo "questo: e' quello con cui si e' chiuso il punto fisso a tre"; \
	          echo "generazioni, ed e' l'unico riferimento contro cui"; \
	          echo "confrontarsi se questo si comporta in modo strano."; \
	        } > $(ISO_ROOT)/freebasic-nuovo/BERSAGLIO-EXOS.txt; \
	    else \
	        echo "       !  bersaglio exos ASSENTE: da portare prima di costruirlo"; \
	        { echo "QUESTO ALBERO NON SI COSTRUISCE ANCORA PER EX-OS."; \
	          echo ""; \
	          echo "Versione: $$V"; \
	          echo ""; \
	          echo "Manca src/rtlib/exos/ — lo strato di sistema — e i sorgenti"; \
	          echo "del compilatore non conoscono -target exos. Con TARGET=exos"; \
	          echo "il link fallisce su funzioni che non c'entrano con il file in"; \
	          echo "compilazione."; \
	          echo ""; \
	          echo "L'albero che SI COSTRUISCE e' /freebasic: usa quello come"; \
	          echo "riferimento mentre porti questo."; \
	          echo ""; \
	          echo "Il porting sta in tools/freebasic-exos/ del repository:"; \
	          echo "  applica.py             lo strato di runtime (src/rtlib/exos/)"; \
	          echo "  bersaglio-exos.py      il bersaglio nel compilatore, per la 1.07.3"; \
	          echo "  bersaglio-exos-110.py  lo stesso, per la 1.10.1"; \
	          echo ""; \
	          echo "Su un albero ANCORA piu' nuovo servira' un terzo script."; \
	          echo "Dalla 1.07.3 alla 1.10.1 delle 26 ancore ne reggevano 14:"; \
	          echo "le altre vanno ritrovate a mano in fb.bi, fb.bas, fbc.bas"; \
	          echo "ed emit_x86.bas. Il perche' di ognuna sta in MODIFICHE-FBC.md."; \
	        } > $(ISO_ROOT)/freebasic-nuovo/NON-COSTRUIBILE.txt; \
	    fi; \
	fi
	@# L'assembly di prova-gcc.c lo produce il CROSS, e ci va per due motivi:
	@# permette di provare la meta' "assembla e collega" senza aspettare cc1,
	@# e da' il termine di paragone — il .s che cc1 dovra' produrre uguale.
	@# Si rigenera a ogni `make iso`, cosi' non puo' divergere dal .c.
	@if command -v i386-exos-gcc >/dev/null 2>&1; then \
	    i386-exos-gcc -O2 -S -I lib/include -o $(ISO_ROOT)/prova-gcc.s \
	        $(TOOLS_DIR)/iso/prova-gcc.c 2>/dev/null && \
	    echo "     prova-gcc.s generato dal cross (termine di paragone per cc1)"; \
	fi
	@python3 $(ISO_MKISO) $(ISO_IMG) --da $(ISO_ROOT) --etichetta "EXOS TOOLS"
	@echo "[OK] CD degli strumenti: $(ISO_IMG)"

# =============================================================================
# verifica-programmi — nessun sorgente resta fuori dalle immagini
#
# Confronta il contenuto di bin/ e drivers/ con quello che la build ha
# davvero prodotto. Un sorgente senza regola, o con una regola che nessuno
# ha collegato a una lista, non e' un errore che si vede: e' un comando che
# manca sulla macchina mesi dopo, e allora si cerca il difetto nel comando
# invece che nel Makefile.
#
# ! SI GUARDA IL RISULTATO, NON SI LEGGE IL MAKEFILE. Un controllo che
# analizza le variabili direbbe che tutto e' a posto anche quando la regola
# c'e' ma non produce niente. Qui si chiede al filesystem: per ogni
# bin/<nome>/ ci dev'essere build/bin/<nome> oppure build/bin-cd/<nome>.
#
# ! E' UN ERRORE, NON UN AVVISO. Un CD a cui manca un comando e uno
# completo si distinguono solo provandoli, e chi lo prova e' l'utente.
# =============================================================================
# =============================================================================
# verifica-cpu — i binari non devono contenere istruzioni oltre la CPU di base
#
# ! UN REQUISITO CHE NESSUNO VERIFICA NON E' UN REQUISITO. La CPU di base era
# dichiarata Pentium 133 MMX e il compilatore produceva per i686: 591 `cmov`
# nei binari, istruzioni che su quella CPU non esistono. La costruzione
# passava senza un avviso, e il sistema si sarebbe fermato con un'eccezione di
# istruzione non valida al primo programma — su hardware vero, non in QEMU,
# che di CPU ne emula una moderna.
#
# ! SI GUARDANO I BINARI, NON I FLAG. Controllare che -march sia giusto direbbe
# solo che l'abbiamo scritto; qui si rilegge cio' che e' stato PRODOTTO, che e'
# l'unica cosa che gira davvero.
# =============================================================================
.PHONY: verifica-cpu
verifica-cpu:
	@python3 -c "import subprocess,glob,re,sys; \
	f=sorted(glob.glob('$(BUILD_DIR)/bin/*')+glob.glob('$(BUILD_DIR)/bin-cd/*')+ \
	         glob.glob('$(BUILD_DIR)/exwin/bin/*')+glob.glob('$(BUILD_DIR)/lib/*.so')+ \
	         glob.glob('$(BUILD_DIR)/exwin/lib/*.so')+glob.glob('$(BUILD_DIR)/drivers/*.drv')+ \
	         ['$(BUILD_DIR)/kernel.elf']); \
	bad=[]; \
	[bad.append((x,n)) for x in f for n in [len(re.findall(r'\bcmov\w+', \
	   subprocess.run(['objdump','-d',x],capture_output=True,text=True).stdout))] if n]; \
	print('[OK] nessuna istruzione oltre il Pentium MMX in %d file' % len(f)) if not bad \
	else (print('[ERRORE] cmov (Pentium PRO) trovate:'), \
	      [print('   %s: %d' % b) for b in bad], \
	      print('   La CPU di base e\' il Pentium 133 MMX: vedi CPU_BASE nel Makefile.'), \
	      sys.exit(1))"


.PHONY: verifica-programmi
verifica-programmi: $(PROGRAMMI_FLOPPY) $(PROGRAMMI_CD) $(DRIVER_CD)
	@mancanti=""; \
	for d in $(BIN_DIR)/*/; do \
	    n=$$(basename "$$d"); \
	    [ -f "$(BUILD_BIN)/$$n" ]    && continue; \
	    [ -f "$(BUILD_BIN_CD)/$$n" ] && continue; \
	    mancanti="$$mancanti $$n"; \
	done; \
	for d in $(DRIVER_DIR)/*/; do \
	    n=$$(basename "$$d"); \
	    salta=0; \
	    for e in $(NON_DRIVER); do [ "$$n" = "$$e" ] && salta=1; done; \
	    [ $$salta -eq 1 ] && continue; \
	    [ -f "$(BUILD_DRIVERS)/$$n.drv" ]    && continue; \
	    [ -f "$(BUILD_DRIVERS_CD)/$$n.drv" ] && continue; \
	    mancanti="$$mancanti $$n.drv"; \
	done; \
	if [ -n "$$mancanti" ]; then \
	    echo ""; \
	    echo "  ! Questi sorgenti non producono niente:$$mancanti"; \
	    echo ""; \
	    echo "    Ognuno ha bisogno di due cose nel Makefile:"; \
	    echo "      1. una regola che lo compili in build/bin (sistema di"; \
	    echo "         base, va anche sul floppy) oppure in build/bin-cd"; \
	    echo "         (solo CD di EX-OS);"; \
	    echo "      2. il suo nome in PROGRAMMI_FLOPPY o in PROGRAMMI_CD,"; \
	    echo "         che e' cio' che lo fa costruire."; \
	    echo ""; \
	    echo "    Vedi il blocco 'DOVE VA OGNI PROGRAMMA' in testa a questo"; \
	    echo "    Makefile. I linguaggi (gcc, fbc, as, ld) non vanno li':"; \
	    echo "    stanno sul CD degli strumenti, che li prende gia' fatti."; \
	    echo ""; \
	    exit 1; \
	fi; \
	echo "[OK] ogni sorgente di bin/ e drivers/ finisce su un'immagine"

# =============================================================================
# LE DUE IMMAGINI COMPLETE
#
#   dist/exos.iso         il SISTEMA: kernel, comandi di base, rete, driver
#   dist/exos-tools.iso   i LINGUAGGI: gcc, g++, cpp, cc1, fbc, as, ld,
#                         libstdc++, OpenSSL — tutto cio' che si installa
#                         a parte con `toolinst`
#
# Sono due dischi e non uno perche' il secondo pesa 150 MB e serve a chi
# sviluppa; il primo basta a installare e usare il sistema.
# =============================================================================
.PHONY: iso-tutte
iso-tutte: iso-exos iso
	@echo ""
	@echo "============================================"
	@echo " Le due immagini sono pronte"
	@echo "============================================"
	@ls -la $(ISOX_IMG) $(ISO_IMG) 2>/dev/null || true
	@echo ""
	@echo "  $(ISOX_IMG)"
	@echo "      il sistema: comandi, rete, driver. Avviabile."
	@echo "  $(ISO_IMG)"
	@echo "      i linguaggi. Si installa con  toolinst /disco"
	@echo ""

.PHONY: iso
iso: $(ISO_IMG)

# =============================================================================
# IL CD DI EX-OS — avviabile, con il sistema sopra
#
# ! E' UN DISCO DIVERSO da exos-tools.iso. Quello e' il CD di SVILUPPO:
# GCC, as, ld, le librerie di calcolo, i sorgenti — roba di terzi portata
# qui. Questo porta solo cio' che e' nato in questo progetto: il sistema,
# i driver, gli strumenti.
#
# ! AVVIABILE PER EMULAZIONE FLOPPY. Dentro c'e' dist/floppy.img come
# immagine di avvio El Torito: il BIOS la presenta come A:, stage1 e
# stage2 la leggono con l'INT 13h — che e' proprio cio' che il BIOS emula —
# e caricano il kernel. Il percorso di avvio collaudato non cambia.
#
# ! POI IL KERNEL PASSA AL CD, e deve. In modo protetto l'INT 13h non si
# puo' chiamare, e dietro l'emulazione non c'e' nessun controller floppy:
# fat12_init fallisce, e quel fallimento e' il segnale che vfs_init usa
# per montare come root il lettore ATAPI. Da qui la conseguenza che decide
# il contenuto di questo disco: il sistema che gira e' QUELLO SUL CD, non
# quello dentro boot.img. I due devono contenere le stesse cose, o si
# avvia una versione e se ne esegue un'altra.
# =============================================================================
ISOX_ROOT := $(BUILD_DIR)/iso-exos
ISOX_IMG  := $(DIST_DIR)/exos.iso

# I binari che esistono SOLO su questo CD, raccolti in una variabile invece
# che ripetuti nella riga della regola: erano quattordici nomi, e xcp non
# c'era perche' un elenco lungo scritto a mano e' il posto dove le cose si
# dimenticano. Le variabili sono definite piu' sopra, accanto alle rispettive
# regole, e qui sono tutte gia' note perche' questa riga make la legge dopo.
BINARI_SOLO_CD := $(NETDETECT_BIN) $(NETTEST_BIN) $(PING_BIN) $(IPCFG_BIN) \
                  $(DHCP_BIN) $(HOST_BIN) $(TCPTEST_BIN) $(FTP_BIN) \
                  $(TELNET_BIN) $(XCP_BIN) $(WINPROVA_BIN) $(EXWINCMD_BIN)
# ! QUESTA LISTA E' LA DIPENDENZA DELL'ISO, E VA TENUTA ALLINEATA A
# DRIVER_CD. Sono due elenchi della stessa cosa: DRIVER_CD dice COSA
# COSTRUIRE (nomi di bersagli .PHONY), questo dice DA COSA DIPENDE L'IMMAGINE
# (nomi di file). Un driver aggiunto solo al primo si costruisce e non entra
# mai in un'ISO nuova.
#
# ! ED E' SUCCESSO, il 13 agosto 2026, con uhci.drv. Il codice degli hub USB
# era giusto al primo colpo e per TRE PROVE DI FILA ha detto «non e' un HID»:
#
#     build/drivers-cd/uhci.drv   13:12:25
#     dist/exos.iso               12:53:59
#
# L'ISO non si rifaceva, quindi ogni prova girava sul driver di venti minuti
# prima. Tre corse «fallite» erano tre corse dello stesso binario superato.
#
# ! E verifica-programmi NON lo prende: quella controlla che ogni sorgente
# finisca su un'immagine, cioe' la PRESENZA. Qui mancava la DIPENDENZA, e le
# due cose si somigliano abbastanza da far credere di essere coperti.
# Il controllo che manca lo fa `verifica-dipendenze-cd` qui sotto.
DRIVER_SOLO_CD_OUT := $(NE2K_DRV_OUT) $(PCNET_DRV_OUT) \
                      $(E1000_DRV_OUT) \
                      $(IP_DRV_OUT) $(WSERVER_OUT)

# ! verifica-programmi E' UNA PREREQUISITA D'ORDINE (dopo la barra).
# Cosi' viene eseguita prima di costruire il CD — e ferma tutto se un
# sorgente e' rimasto fuori dalle liste — senza per questo far risultare il
# CD da rifare a ogni invocazione: e' un bersaglio finto, non ha una data, e
# fra le prerequisite normali renderebbe l'immagine perennemente scaduta.
# ! LA GUARDIA CHE MANCAVA. Confronta i .drv che il Makefile sa costruire per
# il CD con quelli da cui l'ISO dichiara di dipendere, e si ferma se i due
# elenchi divergono. Senza, l'unico sintomo e' un'immagine vecchia — che non
# assomiglia per niente a un errore di compilazione.
.PHONY: verifica-dipendenze-cd
verifica-dipendenze-cd:
	@fatti=""; \
	for d in $(DRIVER_SOLO_CD_OUT); do fatti="$$fatti $$(basename $$d)"; done; \
	manca=""; \
	for s in drivers/*/; do \
	    n=$$(basename $$s); \
	    if [ -f "$$s/$$n.c" ] && [ -f "$(BUILD_DRIVERS_CD)/$$n.drv" ]; then \
	        case " $$fatti " in *" $$n.drv "*) ;; *) manca="$$manca $$n.drv";; esac; \
	    fi; \
	done; \
	if [ -n "$$manca" ]; then \
	    echo "[ERRORE] driver del CD fuori da DRIVER_SOLO_CD_OUT:$$manca"; \
	    echo "         l'ISO non dipende da loro: si costruiscono e l'immagine"; \
	    echo "         resta VECCHIA, senza nessun errore. Vedi il commento"; \
	    echo "         accanto a DRIVER_SOLO_CD_OUT nel Makefile."; \
	    exit 1; \
	fi; \
	echo "[OK] ogni driver del CD e' fra le dipendenze dell'ISO"

$(ISOX_IMG): Makefile $(FLOPPY_IMG) boot/autoexec.sh $(DRIVER_SOLO_CD_OUT) \
             $(EXWIN_OUT) $(EXWIN_APPLIST) $(PROVA_PNG) $(PROVA_ICO) $(PROVA_JPG) \
             $(BINARI_SOLO_CD) $(ISO_MKISO) README.md README.en.md \
             gpl-2.0.txt boot/kernel.cfg boot/kernel.txt boot/help.txt \
             | verifica-programmi verifica-dipendenze-cd
	@echo "=== Creazione CD di EX-OS (avviabile) ==="
	@mkdir -p $(DIST_DIR)
	@rm -rf $(ISOX_ROOT)
	@mkdir -p $(ISOX_ROOT)/bin $(ISOX_ROOT)/lib $(ISOX_ROOT)/dev \
	          $(ISOX_ROOT)/boot $(ISOX_ROOT)/doc
	@cp $(BUILD_BIN)/* $(ISOX_ROOT)/bin/ 2>/dev/null || true
	@cp $(BUILD_BIN)/mount $(ISOX_ROOT)/bin/umount 2>/dev/null || true
	@# Programmi che esistono solo sul CD, vedi BUILD_BIN_CD in testa.
	@cp $(BUILD_BIN_CD)/* $(ISOX_ROOT)/bin/ 2>/dev/null || true
	@cp $(BUILD_LIB)/* $(ISOX_ROOT)/lib/ 2>/dev/null || true
	@cp $(BUILD_DRIVERS)/*.drv $(ISOX_ROOT)/dev/ 2>/dev/null || true
	@# I driver che sul floppy non ci stanno (o non ci servono): il CD è
	@# la loro unica destinazione, vedi BUILD_DRIVERS_CD in testa.
	@cp $(BUILD_DRIVERS_CD)/*.drv $(ISOX_ROOT)/dev/ 2>/dev/null || true
	@# /exwin: le applicazioni grafiche, con il loro elenco. Separate da
	@# /bin perche' vogliono il server a finestre — vedi il blocco /exwin.
	@mkdir -p $(ISOX_ROOT)/exwin/bin $(ISOX_ROOT)/exwin/lib $(ISOX_ROOT)/exwin/dev
	@cp $(BUILD_EXWIN_BIN)/* $(ISOX_ROOT)/exwin/bin/ 2>/dev/null || true
	@cp $(EXWIN_APPLIST) $(ISOX_ROOT)/exwin/lib/ 2>/dev/null || true
	@# ! E LA LIBRERIA CONDIVISA, che senza di lei le applicazioni grafiche
	@# non partono affatto. E' un file di /exwin/lib come l'elenco, ma non
	@# viene dai sorgenti: viene da build/, quindi ha una riga sua.
	@cp $(BUILD_EXWIN_LIB)/*.so $(ISOX_ROOT)/exwin/lib/ 2>/dev/null || true
	@# Le immagini di prova dei lettori: vedi PROVE_IMG_DIR.
	@cp $(PROVA_PNG) $(ISOX_ROOT)/exwin/prova.png
	@cp $(PROVA_ICO) $(ISOX_ROOT)/exwin/prova.ico
	@cp $(PROVA_JPG) $(ISOX_ROOT)/exwin/prova.jpg
	@# La RISERVA dei driver, da cui si installa.
	@#
	@# ! È UNA COPIA DI dev/, NON UN'ALTERNATIVA, e le due hanno compiti
	@# diversi: dev/ è ciò che serve al CD per avviarsi e funzionare
	@# mentre lo si usa, drivers/ è il catalogo che `hwconfig -d` sonda
	@# uno per uno con `-i` per decidere cosa copiare sul disco rigido.
	@# Tenerle separate è ciò che permette al catalogo di contenere anche
	@# driver che su QUESTA macchina non servono — è esattamente il suo
	@# scopo — senza che il CD provi a caricarli all'avvio.
	@mkdir -p $(ISOX_ROOT)/drivers
	@cp $(BUILD_DRIVERS)/*.drv $(ISOX_ROOT)/drivers/ 2>/dev/null || true
	@cp $(BUILD_DRIVERS_CD)/*.drv $(ISOX_ROOT)/drivers/ 2>/dev/null || true
	@cp boot/kernel.cfg $(ISOX_ROOT)/boot/kernel.cfg
	@# La spiegazione delle voci del .cfg. Il kernel non la legge: sta
	@# accanto al file che descrive perche' e' li' che serve.
	@cp boot/kernel.txt $(ISOX_ROOT)/boot/kernel.txt
	@# Il testo che /bin/help sfoglia: senza, `help` non ha niente da dire.
	@cp boot/help.txt $(ISOX_ROOT)/boot/help.txt
	@# L'autoexec: accende la rete da solo. Vedi il file per la via
	@# d'uscita se un comando qui dentro si blocca.
	@cp boot/autoexec.sh $(ISOX_ROOT)/boot/autoexec.sh
	@cp README.md README.en.md HANDOFF.md KERNEL_CORE_NOTES.md gpl-2.0.txt $(ISOX_ROOT)/doc/
	@# ! Anche il kernel e stage2 sulla radice: non servono ad avviare —
	@# quelli usati stanno dentro boot.img — ma servono a `install`, che
	@# li cerca li' per copiarli su un disco rigido.
	@cp $(STAGE2_BIN) $(ISOX_ROOT)/LOADER.BIN
	@cp $(KERNEL_BIN) $(ISOX_ROOT)/KERNEL.BIN
	@python3 $(ISO_MKISO) $(ISOX_IMG) --da $(ISOX_ROOT) \
	    --avvio $(FLOPPY_IMG) --etichetta "EXOS"
	@echo "[OK] CD di EX-OS: $(ISOX_IMG)"
	@echo "     Provalo senza floppy:  qemu-system-i386 -cdrom $(ISOX_IMG) -boot d -m 32M"

# =============================================================================
# IL CD IN PEZZI — perche' un file solo non ci sta piu'
#
# dist/exos-tools.iso e' cresciuto oltre i 50 MB, e sopra quella soglia le
# piattaforme di hosting cominciano ad avvisare (sopra i 100 MB rifiutano).
# Continuera' a crescere: cc1plus da solo vale una trentina di megabyte.
#
# ! NON SERVE A METTERLO IN GIT — DA GIT E' USCITO. Spezzare toglieva
# l'avviso dei 50 MB e lasciava intatto il guasto vero: un artefatto che
# cambia a ogni ricostruzione lascia una copia nuova e INTERA in ogni
# commit, per sempre. Le due ISO sono in .gitignore, e li' c'e' scritto
# perche'.
#
# Questi pezzi servono a PUBBLICARE il CD fuori dalla cronologia: allegato
# a un rilascio, messo su un disco, mandato a qualcuno. E' la stessa cosa
# che si fa con $(HD_IMG), solo che il CD non ci sta in un pezzo solo.
#
#     make iso-parti     -> dist/parti/exos-tools.zip + .z01, .z02, ...
#     make iso-unisci    -> ricompone dist/exos-tools.iso e ne verifica la
#                           sha256, cosi' un pezzo mancante si vede subito
#
# La dimensione dei pezzi si cambia:  make iso-parti ISO_PARTE=45m
# =============================================================================
ISO_PARTI  := $(DIST_DIR)/parti
ISO_PARTE  ?= 20m

.PHONY: iso-parti
iso-parti: $(ISO_IMG)
	@echo "=== CD degli strumenti in pezzi da $(ISO_PARTE) ==="
	@rm -rf $(ISO_PARTI)
	@mkdir -p $(ISO_PARTI)
	@# ! -j (junk paths): senza, dentro lo zip finirebbe "dist/exos-tools.iso"
	@# e chi lo apre si ritrova una directory che non si aspetta.
	@zip -q -s $(ISO_PARTE) -j $(ISO_PARTI)/exos-tools.zip $(ISO_IMG)
	@# ! sha256sum DA DENTRO dist/, cosi' il percorso nel file e' relativo
	@# e `sha256sum -c` lo ritrova. Con il percorso completo la verifica
	@# fallirebbe su qualunque macchina che non sia questa.
	@cd $(DIST_DIR) && sha256sum exos-tools.iso > parti/exos-tools.sha256
	@printf '%s\n' \
	  'Il CD degli strumenti di EX-OS, spezzato per stare sotto i 50 MB.' \
	  '' \
	  'Per rimetterlo insieme servono TUTTI i pezzi nella stessa directory:' \
	  'i .z01, .z02, ... e il .zip, che e'"'"' l'"'"'ULTIMO, non il primo.' \
	  '' \
	  'Linux/macOS:' \
	  '    zip -s 0 exos-tools.zip --out intero.zip' \
	  '    unzip intero.zip' \
	  '    sha256sum -c exos-tools.sha256' \
	  '' \
	  'Windows: 7-Zip e WinRAR aprono direttamente exos-tools.zip' \
	  'trovando i pezzi accanto.' \
	  '' \
	  'Oppure, dal repository:  make iso-unisci' \
	  > $(ISO_PARTI)/LEGGIMI.txt
	@echo "     $$(ls $(ISO_PARTI) | wc -l) file:"
	@ls -la $(ISO_PARTI) | awk 'NR>3 {printf "       %-24s %8.1f MB\n", $$9, $$5/1048576}'
	@echo "[OK] pezzi in $(ISO_PARTI)"

.PHONY: iso-unisci
iso-unisci:
	@echo "=== Ricomposizione del CD degli strumenti ==="
	@if [ ! -f $(ISO_PARTI)/exos-tools.zip ]; then \
	    echo "  ! manca $(ISO_PARTI)/exos-tools.zip" >&2; exit 1; \
	fi
	@cd $(ISO_PARTI) && zip -q -s 0 exos-tools.zip --out intero.zip
	@cd $(ISO_PARTI) && unzip -o -q intero.zip -d ..
	@rm -f $(ISO_PARTI)/intero.zip
	@# ! LA VERIFICA NON E' UNA FORMALITA': uno zip spezzato a cui manca un
	@# pezzo di mezzo si ricompone SENZA errore e da' un ISO troncato, che
	@# si monta e poi non trova i file.
	@cd $(DIST_DIR) && sha256sum -c parti/exos-tools.sha256
	@echo "[OK] $(ISO_IMG) ricomposto e verificato"

.PHONY: iso-exos
iso-exos: $(ISOX_IMG)

# Prova il CD degli strumenti dentro QEMU, montato su /cdrom.
.PHONY: run-iso
run-iso: $(FLOPPY_IMG) $(ISO_IMG)
	@echo "=== Avvio QEMU con il CD degli strumenti ==="
	$(QEMU) $(QEMU_FLAGS) -cdrom $(ISO_IMG)

# =============================================================================
# DISCO AVVIABILE
#
# Il terzo supporto, e il primo su cui EX-OS ha spazio per crescere: 511 MB
# contro gli 897 KB liberi del floppy. Serve per gli strumenti che sul
# floppy non entrano, e per la ragione che il CD non puo' coprire — su un
# CD si legge e basta, mentre l'OUTPUT di una compilazione deve poter
# essere scritto da qualche parte.
#
# NON fa parte di `make all`, per la stessa ragione del CD: costruirlo
# richiede un giro completo in QEMU (formattazione e installazione le fa
# EX-OS stesso, vedi tools/mkhd.sh) e chi compila il sistema non deve
# aspettarlo.
# =============================================================================
# ! IL DISCO NON STA IN dist/, ED E' UNA CORREZIONE. dist/ e' cio' che si
# DISTRIBUISCE — il floppy e i due CD — e `make distclean` lo cancella tutto.
# Un disco rigido non e' un prodotto da distribuire: e' uno STATO. Ci sta
# dentro un sistema installato, con i file che ci ha messo chi lo usa, e
# ricostruirlo costa un giro completo in QEMU perche' formattazione e
# installazione le fa EX-OS stesso. Tenerlo in dist/ voleva dire che un
# `make distclean` — che uno lancia per liberare spazio dalle ISO — portava
# via anche quello, senza chiedere niente.
DISCHI_DIR := dischi
HD_IMG := $(DISCHI_DIR)/hd.img

.PHONY: hd
hd: $(FLOPPY_IMG)
	@chmod +x $(TOOLS_DIR)/mkhd.sh
	@$(TOOLS_DIR)/mkhd.sh

# -boot c e nessun floppy: e' il punto della prova. Lasciare il floppy
# attaccato proverebbe una cosa diversa da quella voluta — un sistema che
# parte davvero da disco, non uno che ci ripiega sopra.
.PHONY: run-hd
run-hd: $(HD_IMG)
	@echo "=== Avvio QEMU dal disco (senza floppy) ==="
	$(QEMU) -drive file=$(HD_IMG),format=raw,if=ide \
		-m 32M -boot c -no-reboot -no-shutdown

$(HD_IMG):
	@echo "$(HD_IMG) non esiste: lancia 'make hd'." >&2
	@false

# =============================================================================
# ESECUZIONE E DEBUG
# =============================================================================

QEMU        := qemu-system-i386
QEMU_FLAGS  := -fda $(FLOPPY_IMG) \
               -m 32M \
               -boot a \
               -no-reboot \
               -no-shutdown

.PHONY: run
run: $(FLOPPY_IMG)
	@echo "=== Avvio QEMU ==="
	$(QEMU) $(QEMU_FLAGS)

.PHONY: run-serial
run-serial: $(FLOPPY_IMG)
	@echo "=== Avvio QEMU con output seriale ==="
	$(QEMU) $(QEMU_FLAGS) \
		-serial stdio \
		-display none

# Debug: GDB usa kernel.elf (con simboli), non kernel.bin
.PHONY: debug
debug: $(FLOPPY_IMG)
	@echo "=== Avvio QEMU in modalità DEBUG (GDB porta 1234) ==="
	@echo "In un altro terminale esegui:"
	@echo "  gdb $(KERNEL_ELF)"
	@echo "  (gdb) target remote :1234"
	@echo "  (gdb) break kernel_main"
	@echo "  (gdb) continue"
	@echo ""
	$(QEMU) $(QEMU_FLAGS) \
		-s \
		-S \
		-d int,cpu_reset \
		-D $(BUILD_DIR)/qemu.log

.PHONY: debug-vga
debug-vga: $(FLOPPY_IMG)
	$(QEMU) $(QEMU_FLAGS) \
		-s \
		-S \
		-monitor stdio

# =============================================================================
# VERIFICA E ANALISI
# =============================================================================

.PHONY: verify
verify: $(FLOPPY_IMG)
	@echo "=== Verifica Immagine Floppy ==="
	@# ! SI CONTROLLA CHE I DRIVER AGGIUNTIVI NON CI SIANO FINITI.
	@# La regola — sul floppy solo il sistema, i driver in piu' sul CD —
	@# fin qui era una convenzione, cioe' una cosa che si rispetta finche'
	@# ci si ricorda. Il modo di violarla e' aggiungere una voce a
	@# PROGRAMMI_FLOPPY per comodita' durante una prova e lasciarcela.
	@set -e; \
	trovati=""; \
	for d in $(DRIVER_SOLO_CD); do \
	    if mdir -i $(FLOPPY_IMG) ::/dev 2>/dev/null | \
	       grep -qi "^$$(echo $$d | cut -d. -f1) *drv"; then \
	        trovati="$$trovati $$d"; \
	    fi; \
	done; \
	if [ -n "$$trovati" ]; then \
	    echo "!! sul floppy ci sono driver che vanno solo sul CD:$$trovati"; \
	    echo "   vedi il commento su PROGRAMMI_FLOPPY nel Makefile"; \
	    exit 1; \
	fi; \
	echo "[OK] nessun driver da CD sul floppy"
	@# La versione dichiarata nei leggimi deve essere quella di version.h.
	@set -e; \
	for f in README.md README.en.md; do \
	    d=$$(sed -n 's/^\*\*Versione\?:\*\* //p' $$f | head -1); \
	    if [ "$$d" != "$(VERSIONE)" ]; then \
	        echo "!! $$f dichiara la versione '$$d', version.h dice '$(VERSIONE)'"; \
	        echo "   si sistema con:  make leggimi-versione"; \
	        exit 1; \
	    fi; \
	done; \
	echo "[OK] leggimi allineati alla versione $(VERSIONE)"
	@echo "Contenuto floppy:"
	@mdir -i $(FLOPPY_IMG) -/ ::
	@echo ""
	@echo "Boot sector (primi 16 byte):"
	@dd if=$(FLOPPY_IMG) bs=512 count=1 status=none | od -A x -t x1z | head -8
	@echo ""
	@echo "Firma boot sector (byte 510-511):"
	@dd if=$(FLOPPY_IMG) bs=1 skip=510 count=2 status=none | od -A n -t x1

# =============================================================================
# La versione nei due leggimi
#
# ! E' UN NUMERO COPIATO, e i numeri copiati invecchiano. EXOS_VERSION si
# incrementa a ogni modifica del kernel, e i leggimi non vengono
# ricompilati da niente: senza questa regola la riga "Versione:" resta
# quella del giorno in cui e' stata scritta, e un leggimi che dichiara una
# versione sbagliata e' peggio di uno che non la dichiara.
#
# `verify` lo CONTROLLA e fallisce dicendo cosa lanciare; `leggimi-versione`
# lo sistema. La stessa forma del controllo sui driver del floppy: la
# regola non si affida alla memoria di chi la deve rispettare.
# =============================================================================
VERSIONE := $(shell sed -n 's/^#define EXOS_VERSION *"\(.*\)".*/\1/p' kernel/include/version.h)

.PHONY: leggimi-versione
leggimi-versione:
	@sed -i 's/^\*\*Versione:\*\* .*/**Versione:** $(VERSIONE)/' README.md
	@sed -i 's/^\*\*Version:\*\* .*/**Version:** $(VERSIONE)/'   README.en.md
	@echo "[OK] leggimi allineati alla versione $(VERSIONE)"

# =============================================================================
# ABI — il bersaglio e' allineato alla libc di adesso?
#
# I binari per i386-exos costruiti fuori da qui (cc1, as, ld, gmp, mpfr,
# mpc, libm, libstdc++, libcrypto) non hanno modo di accorgersi che un
# tipo della libc e' cambiato: si collegano lo stesso. Il come e il
# perche' — con il caso vero che e' costato due giorni — stanno in testa a
# tools/ricostruisci-bersaglio.sh.
# =============================================================================
.PHONY: abi
abi:
	@tools/ricostruisci-bersaglio.sh --verifica

.PHONY: disasm-stage1
disasm-stage1: $(STAGE1_BIN)
	@echo "=== Disassembly Stage 1 ==="
	ndisasm -b 16 -o 0x7C00 $(STAGE1_BIN)

.PHONY: disasm-kernel
disasm-kernel: $(KERNEL_ELF)
	@echo "=== Disassembly Kernel ==="
	$(OBJDUMP) -d -M intel $(KERNEL_ELF) | less

.PHONY: symbols
symbols: $(KERNEL_ELF)
	nm $(KERNEL_ELF) | sort

.PHONY: size
size: $(KERNEL_ELF) $(KERNEL_BIN) $(STAGE2_BIN)
	@echo "Dimensioni Stage 2:"
	@size $(STAGE2_BIN) || stat -c%s $(STAGE2_BIN)
	@echo "Dimensioni Kernel ELF:"
	@size $(KERNEL_ELF)
	@echo "Dimensioni Kernel flat binary:"
	@stat -c%s $(KERNEL_BIN)

# =============================================================================
# DIRECTORY E PULIZIA
# =============================================================================

.PHONY: dirs
dirs:
	@mkdir -p $(BUILD_DIR) \
	           $(BUILD_STAGE1) \
	           $(BUILD_STAGE2) \
	           $(BUILD_KERNEL)/arch/x86 \
	           $(BUILD_KERNEL)/mm \
	           $(BUILD_KERNEL)/sched \
	           $(BUILD_KERNEL)/fs \
	           $(BUILD_KERNEL)/loader \
	           $(BUILD_KERNEL)/syscall \
	           $(BUILD_DRIVERS) \
	           $(BUILD_DRIVERS_CD) \
	           $(BUILD_BIN) \
	           $(BUILD_BIN_CD) \
	           $(BUILD_OBJ) \
	           $(BUILD_LIB) \
	           $(DIST_DIR)

.PHONY: clean
clean:
	@echo "[..] Pulizia file compilati..."
	@rm -rf $(BUILD_DIR)
	@echo "[OK] Build directory rimossa"

.PHONY: distclean
distclean: clean
	@echo "[..] Pulizia completa..."
	@rm -rf $(DIST_DIR)
	@echo "[OK] Dist directory rimossa"
	@# ! $(DISCHI_DIR) NON SI TOCCA, ED E' IL PUNTO DI QUESTA SEPARAZIONE.
	@# In dist/ ci sono solo cose che si rifanno da sole: il floppy e i due
	@# CD. Un disco rigido e' uno stato che si e' costruito una volta e che
	@# nessuno si aspetta di perdere lanciando una pulizia.
	@if [ -d $(DISCHI_DIR) ]; then \
	    echo "     $(DISCHI_DIR)/ resta: i dischi non si distribuiscono"; \
	fi

# =============================================================================
# CONFIGURAZIONE KERNEL
# =============================================================================

$(KERNEL_DIR)/../boot/kernel.cfg:
	@echo "[..] Creo kernel.cfg di default..."
	@mkdir -p boot
	@printf "# EX-OS Kernel Configuration\n" > boot/kernel.cfg
	@printf "# Copyright (C) 2025 Graziano Falcone\n\n" >> boot/kernel.cfg
	@printf "[kernel]\nloglevel=3\ntimer_hz=100\nverboseboot=0\n\n" >> boot/kernel.cfg
	@printf "[env]\nPATH=/bin:/dev\nHOME=/\nTERM=vga\n\n" >> boot/kernel.cfg
	@printf "[boot]\nshell=/bin/sh\nmodules=kbd\n\n" >> boot/kernel.cfg
	@printf "[modules]\nkbd=/dev/kbd.drv\n" >> boot/kernel.cfg

.PHONY: config
config: boot/kernel.cfg
	@echo "[OK] kernel.cfg creato in boot/"

# =============================================================================
# HELP
# =============================================================================

.PHONY: help
help:
	@echo ""
	@echo "EX-OS — Sistema Operativo x86 32-bit"
	@echo "====================================="
	@echo ""
	@echo "Target di build:"
	@echo "  make all          — Compila tutto e crea immagine floppy"
	@echo "  make stage1       — Solo Stage 1 (boot sector 512b)"
	@echo "  make stage2       — Solo Stage 2 (loader FAT12)"
	@echo "  make kernel       — Kernel ELF + flat binary"
	@echo "  make floppy       — Solo immagine floppy"
	@echo ""
	@echo "Supporti:"
	@echo "  make iso-tutte    — LE DUE ISO COMPLETE (exos.iso + exos-tools.iso)"
	@echo "  make floppy       — Immagine di avvio dist/floppy.img"
	@echo "                      (solo il sistema di base, vedi PROGRAMMI_FLOPPY)"
	@echo "  make iso-exos     — CD del sistema dist/exos.iso"
	@echo "                      (base + rete + driver: tutto bin/ e drivers/)"
	@echo "  make iso          — CD degli strumenti dist/exos-tools.iso"
	@echo "                      (gcc, g++, cpp, cc1, fbc, as, ld, OpenSSL)"
	@echo "  make verifica-programmi"
	@echo "                      — controlla che nessun sorgente resti fuori"
	@echo "  make hd           — Disco avviabile $(HD_IMG) (512 MB, ext2)"
	@echo "  make run-hd       — Avvia dal disco, SENZA floppy"
	@echo ""
	@echo "Test e debug:"
	@echo "  make run          — Avvia con QEMU"
	@echo "  make run-serial   — Avvia con output seriale su terminale"
	@echo "  make run-iso      — Avvia con il CD degli strumenti inserito"
	@echo "  make debug        — Avvia QEMU + GDB stub (porta 1234)"
	@echo "  make debug-vga    — Avvia QEMU + monitor interattivo"
	@echo ""
	@echo "Analisi:"
	@echo "  make iso-parti    — CD degli strumenti spezzato in pezzi da 20 MB"
	@echo "  make iso-unisci   — Ricompone il CD dai pezzi e ne verifica la sha256"
	@echo "  make verify       — Verifica struttura floppy"
	@echo "  make abi          — Il bersaglio e' allineato alla libc di adesso?"
	@echo "  make disasm-stage1— Disassembla boot sector"
	@echo "  make disasm-kernel— Disassembla kernel (usa kernel.elf)"
	@echo "  make symbols      — Mostra simboli kernel"
	@echo "  make size         — Mostra dimensioni sezioni"
	@echo ""
	@echo "Pulizia:"
	@echo "  make clean        — Rimuove file compilati"
	@echo "  make distclean    — Rimuove tutto incluso floppy.img"
	@echo ""
	@echo "Toolchain:"
	@echo "  CC  = $(CC)"
	@echo "  LD  = $(LD)"
	@echo "  AS  = $(AS)"
	@echo ""
	@echo "Bug corretti rispetto alla versione originale:"
	@echo "  #1 kernel.bin ora è flat binary (objcopy da kernel.elf)"
	@echo "  #2 FAT a 0xA000, root dir a 0x7E00 (no overlap)"
	@echo "  #3 BOOTINFO_ADDR=0xC000 (non coincide con DISK_BUFFER=0xB000)"
	@echo "  #4 Unreal Mode abilitato prima del caricamento kernel"
	@echo "  #5 jump_to_kernel in ASM puro (no .code32 in binario 16-bit)"
	@echo "  #6 lib/libc.ld aggiunto"
	@echo ""
