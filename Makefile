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
CFLAGS := -m32 \
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
# ⚠️ DEFINITI QUI E NON ACCANTO ALLA REGOLA CHE LI PRODUCE. Le prerequisite
# di una regola vengono espanse quando make LEGGE la riga: una variabile
# definita piu' sotto risulta vuota, la dipendenza sparisce senza un
# errore, e modificare il protocollo smette di ricompilare i client. Ci
# sono gia' cascato con questi due — netdetect e nettest includono
# pci_proto.h e net_proto.h ma le loro regole stanno prima di quelle dei
# driver che li definivano.
PCI_DRV_PROTO := drivers/pci/pci_proto.h
NET_PROTO     := drivers/net/net_proto.h
IP_PROTO      := drivers/net/ip_proto.h
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
CFLAGS_USER := -m32 -ffreestanding -fno-builtin -fno-stack-protector \
               -fno-pic -fno-pie -Wall -O2 -std=c11 -nostdlib \
               -ffunction-sections -fdata-sections

$(SHELL_BIN): $(SHELL_SRC) $(SHELL_LD)
	@echo "=== Compilazione Shell utente /bin/sh ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -c $(SHELL_SRC) -o $(BUILD_OBJ)/shell.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(SHELL_LD) $(BUILD_OBJ)/shell.o -o $@
	@echo "[OK] Shell compilata: $@"

.PHONY: shell
shell: dirs $(SHELL_BIN)

# --- Programma utente di esempio (/bin/hello) ---------------------------------
HELLO_SRC := bin/hello/hello.c
HELLO_BIN := $(BUILD_BIN)/hello
HELLO_LD  := bin/hello/hello.ld

$(HELLO_BIN): $(HELLO_SRC) $(HELLO_LD)
	@echo "=== Compilazione /bin/hello ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -c $(HELLO_SRC) -o $(BUILD_OBJ)/hello.o
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

$(LS_BIN): $(LS_SRC) $(LS_LD) $(LIBC_SRC) $(LS_START)
	@echo "=== Compilazione /bin/ls ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(LS_SRC)   -o $(BUILD_OBJ)/ls_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                -o $(BUILD_OBJ)/ls_libc.o
	$(CC) -m32 -c $(LS_START)                          -o $(BUILD_OBJ)/ls_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(LS_LD) \
	    $(BUILD_OBJ)/ls_start.o \
	    $(BUILD_OBJ)/ls_main.o  \
	    $(BUILD_OBJ)/ls_libc.o  \
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

$(MEM_BIN): $(MEM_SRC) $(MEM_LD) $(LIBC_SRC) $(MEM_START)
	@echo "=== Compilazione /bin/mem ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(MEM_SRC)  -o $(BUILD_OBJ)/mem_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                -o $(BUILD_OBJ)/mem_libc.o
	$(CC) -m32 -c $(MEM_START)                         -o $(BUILD_OBJ)/mem_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MEM_LD) \
	    $(BUILD_OBJ)/mem_start.o \
	    $(BUILD_OBJ)/mem_main.o  \
	    $(BUILD_OBJ)/mem_libc.o  \
	    -o $@
	@echo "[OK] mem compilato: $@"

# --- Programma utente /bin/stack ----------------------------------------------
# Mostra come sono allocati gli stack di ogni processo: impegnato (RAM reale)
# contro riservato (solo spazio di indirizzamento). Stesso schema di /bin/ls.
STACK_SRC   := bin/stack/stack.c
STACK_BIN   := $(BUILD_BIN)/stack
STACK_LD    := bin/stack/stack.ld

$(STACK_BIN): $(STACK_SRC) $(STACK_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/stack ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(STACK_SRC) -o $(BUILD_OBJ)/stack_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/stack_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/stack_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(STACK_LD) \
	    $(BUILD_OBJ)/stack_start.o \
	    $(BUILD_OBJ)/stack_main.o  \
	    $(BUILD_OBJ)/stack_libc.o  \
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

$(LIBCTEST_BIN): $(LIBCTEST_SRC) $(LIBCTEST_LD) $(LIBC_SRC) $(LIBC_START) $(LIBC_HDR)
	@echo "=== Compilazione /bin/libctest ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(LIBCTEST_SRC) -o $(BUILD_OBJ)/libctest_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                    -o $(BUILD_OBJ)/libctest_libc.o
	$(CC) -m32 -c $(LIBC_START)                            -o $(BUILD_OBJ)/libctest_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(LIBCTEST_LD) \
	    $(BUILD_OBJ)/libctest_start.o $(BUILD_OBJ)/libctest_main.o \
	    $(BUILD_OBJ)/libctest_libc.o -o $@
	@echo "[OK] libctest compilato: $@"

.PHONY: libctest
libctest: dirs $(LIBCTEST_BIN)

# --- Programma utente /bin/disk -----------------------------------------------
# Mostra dischi e partizioni. SOLA LETTURA. Stesso schema di /bin/ls.
DISK_SRC   := bin/disk/disk.c
DISK_BIN   := $(BUILD_BIN)/disk
DISK_LD    := bin/disk/disk.ld

$(DISK_BIN): $(DISK_SRC) $(DISK_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/disk ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(DISK_SRC) -o $(BUILD_OBJ)/disk_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                -o $(BUILD_OBJ)/disk_libc.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/disk_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(DISK_LD) \
	    $(BUILD_OBJ)/disk_start.o $(BUILD_OBJ)/disk_main.o $(BUILD_OBJ)/disk_libc.o -o $@
	@echo "[OK] disk compilato: $@"

.PHONY: disk
disk: dirs $(DISK_BIN)

# --- Programma utente /bin/fdisk ----------------------------------------------
# Partizionatore MBR interattivo. SCRIVE: a differenza di /bin/disk, che
# resta in sola lettura di proposito. Stesso schema di /bin/ls.
FDISK_SRC  := bin/fdisk/fdisk.c
FDISK_BIN  := $(BUILD_BIN)/fdisk
FDISK_LD   := bin/fdisk/fdisk.ld

$(FDISK_BIN): $(FDISK_SRC) $(FDISK_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/fdisk ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(FDISK_SRC) -o $(BUILD_OBJ)/fdisk_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/fdisk_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/fdisk_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(FDISK_LD) \
	    $(BUILD_OBJ)/fdisk_start.o $(BUILD_OBJ)/fdisk_main.o $(BUILD_OBJ)/fdisk_libc.o -o $@
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

$(MKFS_BIN): $(MKFS_SRC) $(MKFS_EXT2) $(MKFS_HDR) $(MKFS_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/mkfs ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I bin/mkfs -c $(MKFS_SRC)  -o $(BUILD_OBJ)/mkfs_main.o
	$(CC) $(CFLAGS_USER) -I lib/include -I bin/mkfs -c $(MKFS_EXT2) -o $(BUILD_OBJ)/mkfs_ext2.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                             -o $(BUILD_OBJ)/mkfs_libc.o
	$(CC) -m32 -c $(LIBC_START)                                     -o $(BUILD_OBJ)/mkfs_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MKFS_LD) \
	    $(BUILD_OBJ)/mkfs_start.o $(BUILD_OBJ)/mkfs_main.o \
	    $(BUILD_OBJ)/mkfs_ext2.o  $(BUILD_OBJ)/mkfs_libc.o -o $@
	@echo "[OK] mkfs compilato: $@"

.PHONY: mkfs
mkfs: dirs $(MKFS_BIN)

# --- Programma utente /bin/trunc ----------------------------------------------
# Cambia la dimensione di un file (SYS_TRUNCATE). Stesso schema di /bin/ls.
TRUNC_SRC  := bin/trunc/trunc.c
TRUNC_BIN  := $(BUILD_BIN)/trunc
TRUNC_LD   := bin/trunc/trunc.ld

$(TRUNC_BIN): $(TRUNC_SRC) $(TRUNC_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/trunc ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(TRUNC_SRC) -o $(BUILD_OBJ)/trunc_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/trunc_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/trunc_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TRUNC_LD) \
	    $(BUILD_OBJ)/trunc_start.o $(BUILD_OBJ)/trunc_main.o $(BUILD_OBJ)/trunc_libc.o -o $@
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

$(CHKDSK_BIN): $(CHKDSK_SRC) $(CHKDSK_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/chkdsk ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(CHKDSK_SRC) -o $(BUILD_OBJ)/chkdsk_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                  -o $(BUILD_OBJ)/chkdsk_libc.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/chkdsk_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(CHKDSK_LD) \
	    $(BUILD_OBJ)/chkdsk_start.o $(BUILD_OBJ)/chkdsk_main.o $(BUILD_OBJ)/chkdsk_libc.o -o $@
	@echo "[OK] chkdsk compilato: $@"

.PHONY: chkdsk
chkdsk: dirs $(CHKDSK_BIN)

# --- Programma utente /bin/rename ---------------------------------------------
# Cambia il nome di un file (SYS_RENAME). ⚠️ NON si chiama `mv` di
# proposito: mv su Unix sposta anche fra filesystem, copiando e
# cancellando; qui i dati non si muovono mai. Il nome dice cosa fa.
RENAME_SRC := bin/rename/rename.c
RENAME_BIN := $(BUILD_BIN)/rename
RENAME_LD  := bin/rename/rename.ld

$(RENAME_BIN): $(RENAME_SRC) $(RENAME_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/rename ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(RENAME_SRC) -o $(BUILD_OBJ)/rename_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                  -o $(BUILD_OBJ)/rename_libc.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/rename_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(RENAME_LD) \
	    $(BUILD_OBJ)/rename_start.o $(BUILD_OBJ)/rename_main.o $(BUILD_OBJ)/rename_libc.o -o $@
	@echo "[OK] rename compilato: $@"

.PHONY: rename
rename: dirs $(RENAME_BIN)

# --- Programma /bin/netdetect (solo CD) ---------------------------------------
# Riconosce le schede di rete chiedendo al server PCI e dice quale driver
# serve. Va in $(BUILD_BIN_CD) e non in $(BUILD_BIN): sul floppy ci
# starebbe (ce ne sono 731 KB liberi), ma il floppy serve ad avviare e a
# installare, e uno strumento di rete senza i driver di rete — che sul
# floppy non ci stanno — non servirebbe a niente una volta li'.
NETDETECT_SRC := bin/netdetect/netdetect.c
NETDETECT_BIN := $(BUILD_BIN_CD)/netdetect
NETDETECT_LD  := bin/netdetect/netdetect.ld

$(NETDETECT_BIN): $(NETDETECT_SRC) $(NETDETECT_LD) $(PCI_DRV_PROTO) $(NET_PROTO) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/netdetect ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -I drivers/net -c $(NETDETECT_SRC) -o $(BUILD_OBJ)/netdetect_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                  -o $(BUILD_OBJ)/netdetect_libc.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/netdetect_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(NETDETECT_LD) \
	    $(BUILD_OBJ)/netdetect_start.o $(BUILD_OBJ)/netdetect_main.o $(BUILD_OBJ)/netdetect_libc.o -o $@
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

$(NETTEST_BIN): $(NETTEST_SRC) $(NETTEST_LD) $(NET_PROTO) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/nettest ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -c $(NETTEST_SRC) -o $(BUILD_OBJ)/nettest_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/nettest_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/nettest_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(NETTEST_LD) \
	    $(BUILD_OBJ)/nettest_start.o $(BUILD_OBJ)/nettest_main.o $(BUILD_OBJ)/nettest_libc.o -o $@
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

$(PING_BIN): $(PING_SRC) $(PING_LD) $(IP_PROTO) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/ping ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -c $(PING_SRC) -o $(BUILD_OBJ)/ping_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                -o $(BUILD_OBJ)/ping_libc.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/ping_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(PING_LD) \
	    $(BUILD_OBJ)/ping_start.o $(BUILD_OBJ)/ping_main.o $(BUILD_OBJ)/ping_libc.o -o $@
	@echo "[OK] ping compilato: $@"

.PHONY: ping
ping: dirs $(PING_BIN)

IPCFG_SRC := bin/ipcfg/ipcfg.c
IPCFG_BIN := $(BUILD_BIN_CD)/ipcfg
IPCFG_LD  := bin/ipcfg/ipcfg.ld

$(IPCFG_BIN): $(IPCFG_SRC) $(IPCFG_LD) $(IP_PROTO) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/ipcfg ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -c $(IPCFG_SRC) -o $(BUILD_OBJ)/ipcfg_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/ipcfg_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/ipcfg_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(IPCFG_LD) \
	    $(BUILD_OBJ)/ipcfg_start.o $(BUILD_OBJ)/ipcfg_main.o $(BUILD_OBJ)/ipcfg_libc.o -o $@
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

$(DHCP_BIN): $(DHCP_SRC) $(DHCP_LD) $(IP_PROTO) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/dhcp ==="
	@mkdir -p $(BUILD_BIN_CD) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/net -c $(DHCP_SRC) -o $(BUILD_OBJ)/dhcp_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                -o $(BUILD_OBJ)/dhcp_libc.o
	$(CC) -m32 -c $(LIBC_START)                        -o $(BUILD_OBJ)/dhcp_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(DHCP_LD) \
	    $(BUILD_OBJ)/dhcp_start.o $(BUILD_OBJ)/dhcp_main.o $(BUILD_OBJ)/dhcp_libc.o -o $@
	@echo "[OK] dhcp compilato: $@"

.PHONY: dhcp
dhcp: dirs $(DHCP_BIN)

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

$(TEXTLINE_BIN): $(TEXTLINE_SRC) $(TEXTLINE_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/textline ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(TEXTLINE_SRC) -o $(BUILD_OBJ)/textline_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                    -o $(BUILD_OBJ)/textline_libc.o
	$(CC) -m32 -c $(LIBC_START)                            -o $(BUILD_OBJ)/textline_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(TEXTLINE_LD) \
	    $(BUILD_OBJ)/textline_start.o \
	    $(BUILD_OBJ)/textline_main.o  \
	    $(BUILD_OBJ)/textline_libc.o  \
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

$(GFEDIT_BIN): $(GFEDIT_OBJ) $(GFEDIT_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/gfedit ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC) -o $(BUILD_OBJ)/gfedit_libc.o
	$(CC) -m32 -c $(LIBC_START)         -o $(BUILD_OBJ)/gfedit_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(GFEDIT_LD) \
	    $(BUILD_OBJ)/gfedit_start.o \
	    $(GFEDIT_OBJ)               \
	    $(BUILD_OBJ)/gfedit_libc.o  \
	    -o $@
	@echo "[OK] gfedit compilato: $@"

.PHONY: gfedit
gfedit: dirs $(GFEDIT_BIN)

# --- Programma utente /bin/install --------------------------------------------
INSTALL_SRC := bin/install/install.c
INSTALL_BIN := $(BUILD_BIN)/install
INSTALL_LD  := bin/install/install.ld

$(INSTALL_BIN): $(INSTALL_SRC) $(INSTALL_LD) $(LIBC_SRC) $(LIBC_START)
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

$(CP_BIN): $(CP_SRC) $(CP_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/cp ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(CP_SRC) -o $(BUILD_OBJ)/cp_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)              -o $(BUILD_OBJ)/cp_libc.o
	$(CC) -m32 -c $(LIBC_START)                      -o $(BUILD_OBJ)/cp_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(CP_LD) \
	    $(BUILD_OBJ)/cp_start.o \
	    $(BUILD_OBJ)/cp_main.o  \
	    $(BUILD_OBJ)/cp_libc.o  \
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

$(MOUNT_BIN): $(MOUNT_SRC) $(MOUNT_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/mount ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(MOUNT_SRC) -o $(BUILD_OBJ)/mount_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/mount_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/mount_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MOUNT_LD) \
	    $(BUILD_OBJ)/mount_start.o \
	    $(BUILD_OBJ)/mount_main.o  \
	    $(BUILD_OBJ)/mount_libc.o  \
	    -o $@
	@echo "[OK] mount compilato: $@"

.PHONY: mount_prog
mount_prog: dirs $(MOUNT_BIN)

# --- Programma utente /bin/mkdir ----------------------------------------------
MKDIR_SRC := bin/mkdir/mkdir.c
MKDIR_BIN := $(BUILD_BIN)/mkdir
MKDIR_LD  := bin/mkdir/mkdir.ld

$(MKDIR_BIN): $(MKDIR_SRC) $(MKDIR_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/mkdir ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(MKDIR_SRC) -o $(BUILD_OBJ)/mkdir_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/mkdir_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/mkdir_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(MKDIR_LD) \
	    $(BUILD_OBJ)/mkdir_start.o \
	    $(BUILD_OBJ)/mkdir_main.o  \
	    $(BUILD_OBJ)/mkdir_libc.o  \
	    -o $@
	@echo "[OK] mkdir compilato: $@"

.PHONY: mkdir_prog
mkdir_prog: dirs $(MKDIR_BIN)

# --- Programma utente /bin/rmdir ----------------------------------------------
RMDIR_SRC := bin/rmdir/rmdir.c
RMDIR_BIN := $(BUILD_BIN)/rmdir
RMDIR_LD  := bin/rmdir/rmdir.ld

$(RMDIR_BIN): $(RMDIR_SRC) $(RMDIR_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/rmdir ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(RMDIR_SRC) -o $(BUILD_OBJ)/rmdir_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                 -o $(BUILD_OBJ)/rmdir_libc.o
	$(CC) -m32 -c $(LIBC_START)                         -o $(BUILD_OBJ)/rmdir_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(RMDIR_LD) \
	    $(BUILD_OBJ)/rmdir_start.o \
	    $(BUILD_OBJ)/rmdir_main.o  \
	    $(BUILD_OBJ)/rmdir_libc.o  \
	    -o $@
	@echo "[OK] rmdir compilato: $@"

.PHONY: rmdir_prog
rmdir_prog: dirs $(RMDIR_BIN)

# --- Programma utente /bin/delete ---------------------------------------------
DELETE_SRC := bin/delete/delete.c
DELETE_BIN := $(BUILD_BIN)/delete
DELETE_LD  := bin/delete/delete.ld

$(DELETE_BIN): $(DELETE_SRC) $(DELETE_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione /bin/delete ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -I lib/include -c $(DELETE_SRC) -o $(BUILD_OBJ)/delete_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)                  -o $(BUILD_OBJ)/delete_libc.o
	$(CC) -m32 -c $(LIBC_START)                          -o $(BUILD_OBJ)/delete_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(DELETE_LD) \
	    $(BUILD_OBJ)/delete_start.o \
	    $(BUILD_OBJ)/delete_main.o  \
	    $(BUILD_OBJ)/delete_libc.o  \
	    -o $@
	@echo "[OK] delete compilato: $@"

.PHONY: delete_prog
delete_prog: dirs $(DELETE_BIN)

# --- Shared library libc.so ---------------------------------------------------
# (LIBC_SRC/LIBC_SO/LIBC_LD sono definite più in alto, prima della regola di
#  $(LS_BIN) che le usa come prerequisito.)
$(LIBC_SO): $(LIBC_SRC) $(LIBC_LD)
	@echo "=== Compilazione libc.so ==="
	@mkdir -p $(BUILD_LIB)
	$(CC) $(CFLAGS_USER) -shared -fPIC -c $(LIBC_SRC) -o $(BUILD_LIB)/libc.o
	@# -soname: senza, il DT_NEEDED dei programmi che si linkeranno a questa
	@# libreria conterrebbe il PERCORSO passato a ld ("build/lib/libc.so")
	@# invece del nome ("libc.so"), e il loader del kernel cercherebbe un
	@# file inesistente. Verificato con readelf -d.
	$(LD) -m $(CROSS_LD_EMU) -shared -soname libc.so --allow-shlib-undefined -T $(LIBC_LD) $(BUILD_LIB)/libc.o -o $@
	@echo "[OK] libc.so compilata: $@"

.PHONY: libc
libc: dirs $(LIBC_SO)

# --- Driver ELF: floppy.drv --------------------------------------------------
FLOPPY_DRV_SRC := drivers/floppy/floppy.c
FLOPPY_DRV_OUT := $(BUILD_DRIVERS)/floppy.drv
FLOPPY_DRV_LD  := drivers/floppy/floppy.ld

$(FLOPPY_DRV_OUT): $(FLOPPY_DRV_SRC) $(FLOPPY_DRV_LD)
	@echo "=== Compilazione driver floppy.drv ==="
	@mkdir -p $(BUILD_DRIVERS)
	$(CC) $(CFLAGS_USER) -shared -fPIC -c $(FLOPPY_DRV_SRC) -o $(BUILD_DRIVERS)/floppy.o
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

$(KBD_DRV_OUT): $(KBD_DRV_SRC) $(KBD_DRV_PROTO) $(KBD_DRV_LD) $(LIBC_SRC) $(LIBC_START)
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
PCI_DRV_OUT   := $(BUILD_DRIVERS_CD)/pci.drv
PCI_DRV_LD    := drivers/pci/pci.ld

$(PCI_DRV_OUT): $(PCI_DRV_SRC) $(PCI_DRV_PROTO) $(PCI_DRV_LD) $(LIBC_SRC) $(LIBC_START)
	@echo "=== Compilazione server ring3 pci.drv ==="
	@mkdir -p $(BUILD_DRIVERS_CD)
	$(CC) $(CFLAGS_USER) -I lib/include -I drivers/pci -c $(PCI_DRV_SRC) -o $(BUILD_DRIVERS_CD)/pci_main.o
	$(CC) $(CFLAGS_USER) -c $(LIBC_SRC)   -o $(BUILD_DRIVERS_CD)/pci_libc.o
	$(CC) -m32 -c $(LIBC_START)            -o $(BUILD_DRIVERS_CD)/pci_start.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib --gc-sections -T $(PCI_DRV_LD) \
	    $(BUILD_DRIVERS_CD)/pci_start.o \
	    $(BUILD_DRIVERS_CD)/pci_main.o  \
	    $(BUILD_DRIVERS_CD)/pci_libc.o  \
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

$(NE2K_DRV_OUT): $(NE2K_DRV_SRC) $(NET_PROTO) $(PCI_DRV_PROTO) $(NE2K_DRV_LD) $(LIBC_SRC) $(LIBC_START)
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

# --- Stack IPv4 ring3: ip.drv (solo CD) ---------------------------------------
# ARP + IPv4 + ICMP in un PROCESSO A SE'. Non tocca porte: parla col driver
# di scheda via IPC come qualunque altro programma. Sta fuori dal driver
# perche' questi protocolli sono uguali su ogni scheda, hanno tempi propri
# (scadenze ARP, attese di risposta) che il driver non deve gestire, e
# perche' se sbaglia lo stack si riavvia lo stack — la scheda resta accesa.
IP_DRV_SRC := drivers/ip/ip.c
IP_DRV_OUT := $(BUILD_DRIVERS_CD)/ip.drv
IP_DRV_LD  := drivers/ip/ip.ld

$(IP_DRV_OUT): $(IP_DRV_SRC) $(NET_PROTO) $(IP_PROTO) $(IP_DRV_LD) $(LIBC_SRC) $(LIBC_START)
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
all: dirs stage1 stage2 kernel shell hello ls mem stack disk libctest fdisk mkfs trunc chkdsk rename mount_prog cp_prog install_prog textline gfedit mkdir_prog rmdir_prog delete_prog libc floppy_drv kbd_drv pci_drv ne2k_drv ip_drv netdetect nettest ping ipcfg dhcp floppy
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

$(STAGE1_BIN): $(BOOT_DIR)/stage1/boot.asm
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

$(STAGE2_BIN): $(STAGE2_ASM_SRC)
	@echo "=== Assemblo Stage 2 (flat binary 16-bit) ==="
	@mkdir -p $(BUILD_STAGE2)
	$(AS) -f bin $(STAGE2_ASM_SRC) -o $@
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

$(MBR_BIN): $(MBR_ASM)
	@mkdir -p $(BOOTHD_DIR)
	@echo "[..] Assemblo MBR: $<"
	$(AS) $(ASFLAGS_BIN) $< -o $@

$(BOOTHD_BIN): $(BOOTHD_ASM)
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
                  $(KERNEL_DIR)/arch/x86/vga.c \
                  $(KERNEL_DIR)/arch/x86/rtc.c \
                  $(KERNEL_DIR)/arch/x86/kprintf.c \
                  $(KERNEL_DIR)/mm/pmm.c \
                  $(KERNEL_DIR)/mm/paging.c \
                  $(KERNEL_DIR)/mm/kmalloc.c \
                  $(KERNEL_DIR)/sched/sched.c \
                  $(KERNEL_DIR)/ipc/ipc.c \
                  $(KERNEL_DIR)/ipc/pipe.c \
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

# ⚠️ IL TTY AVEVA LA REGOLA MA NON LE DIPENDENZE (corretto 0.149)
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

$(KERNEL_BIN): $(KERNEL_ELF)
	@echo "=== Conversione Kernel ELF → flat binary ==="
	@echo "[..] objcopy: $< → $@"
	$(OBJCOPY) -O binary $< $@
	@KSIZE=$$(stat -c%s $@); \
	printf "  kernel.bin: %s byte\n" "$$KSIZE"
	@echo "[OK] kernel.bin pronto per il boot (flat binary a 0x100000)"

# =============================================================================
# FLOPPY IMAGE
# =============================================================================

.PHONY: floppy
floppy: $(STAGE1_BIN) $(STAGE2_BIN) $(KERNEL_BIN)
	@echo "=== Creazione Immagine Floppy FAT12 1.44MB ==="
	@chmod +x $(TOOLS_DIR)/mkfloppy.sh
	@$(TOOLS_DIR)/mkfloppy.sh

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
GCC_NATIVO_REL  ?= $(HOME)/gcc-build-rel/gcc
GCC_NATIVO_CHK  ?= $(HOME)/gcc-build-canadian/gcc
ISO_LEGGIMI := $(TOOLS_DIR)/iso/leggimi.txt
ISO_MKISO   := $(TOOLS_DIR)/mkiso.py

$(ISO_IMG): $(ISO_MKISO) $(ISO_LEGGIMI) $(TOOLS_DIR)/iso/prova.s \
                $(TOOLS_DIR)/iso/prova-cc1.c \
            $(TOOLS_DIR)/iso/prova-mp.c $(TOOLS_DIR)/iso/prova-mat.c \
            $(TOOLS_DIR)/iso/prova-cpp.cpp \
            $(LIBC_SRC) $(LIBC_HDR) $(LIBC_START) \
            README.md gpl-2.0.txt
	@echo "=== Creazione CD degli strumenti ==="
	@mkdir -p $(DIST_DIR)
	@rm -rf $(ISO_ROOT)
	@mkdir -p $(ISO_ROOT)/exos/include $(ISO_ROOT)/doc $(ISO_ROOT)/bin
	@cp $(ISO_LEGGIMI) $(ISO_ROOT)/leggimi.txt
	@cp $(TOOLS_DIR)/iso/prova.s $(ISO_ROOT)/prova.s
	@cp $(TOOLS_DIR)/iso/prova-mp.c $(ISO_ROOT)/prova-mp.c
	@cp $(TOOLS_DIR)/iso/prova-mat.c $(ISO_ROOT)/prova-mat.c
	@cp $(TOOLS_DIR)/iso/prova-cpp.cpp $(ISO_ROOT)/prova-cpp.cpp
	@cp -r lib/include/. $(ISO_ROOT)/exos/include/
	@cp $(LIBC_SRC) $(LIBC_START) $(ISO_ROOT)/exos/
	@cp README.md KERNEL_CORE_NOTES.md $(ISO_ROOT)/doc/
	@cp gpl-2.0.txt $(ISO_ROOT)/doc/
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
	@# ⚠️ NON e' un ripiego silenzioso: il messaggio dice quale delle due
	@# cose e' successa, perche' un CD senza `as` e uno con `as` si
	@# distinguono solo provandoli, e la differenza va detta qui.
	@# Si TOLGONO i simboli di debug: sono i cinque sesti del file (7,3 MB
	@# contro 1,4) e non li legge nessuno — il caricatore di EX-OS mappa
	@# solo i segmenti PT_LOAD, e un debugger che li usi qui non c'e'.
	@if [ -x "$(BINUTILS_NATIVI)/gas/as-new" ]; then \
	    if command -v i386-exos-strip >/dev/null 2>&1; then \
	        i386-exos-strip -o $(ISO_ROOT)/bin/as $(BINUTILS_NATIVI)/gas/as-new; \
	        i386-exos-strip -o $(ISO_ROOT)/bin/ld $(BINUTILS_NATIVI)/ld/ld-new; \
	    else \
	        cp $(BINUTILS_NATIVI)/gas/as-new $(ISO_ROOT)/bin/as; \
	        cp $(BINUTILS_NATIVI)/ld/ld-new  $(ISO_ROOT)/bin/ld; \
	    fi; \
	    printf 'as e ld nativi per EX-OS (binutils 2.44)\n' \
	        > $(ISO_ROOT)/bin/leggimi.txt; \
	    echo "     binutils nativi inclusi da $(BINUTILS_NATIVI)"; \
	    if [ -f $(CROSS_SYSROOT)/lib/libmpc.a ]; then \
	        i386-exos-gcc -O2 -o $(ISO_ROOT)/bin/provamp.tmp \
	            $(TOOLS_DIR)/iso/prova-mp.c -lmpc -lmpfr -lgmp && \
	        i386-exos-strip -o $(ISO_ROOT)/bin/provamp $(ISO_ROOT)/bin/provamp.tmp && \
	        rm -f $(ISO_ROOT)/bin/provamp.tmp && \
	        echo "     provamp: GMP + MPFR + MPC"; \
	    else \
	        echo "     GMP/MPFR/MPC assenti dal sysroot: niente provamp"; \
	    fi; \
	    if [ -f $(CROSS_SYSROOT)/lib/libm.a ]; then \
	        i386-exos-gcc -O2 -o $(ISO_ROOT)/bin/provamat.tmp \
	            $(TOOLS_DIR)/iso/prova-mat.c -lm && \
	        i386-exos-strip -o $(ISO_ROOT)/bin/provamat $(ISO_ROOT)/bin/provamat.tmp && \
	        rm -f $(ISO_ROOT)/bin/provamat.tmp && \
	        echo "     provamat: openlibm"; \
	    else \
	        echo "     libm assente dal sysroot: niente provamat"; \
	    fi; \
	    if [ -f $(CROSS_SYSROOT)/lib/libstdc++.a ]; then \
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
	@# ⚠️ SI DICE QUANTO PESANO. cc1 e' il file piu' grande che EX-OS abbia
	@# mai dovuto caricare, e la differenza fra l'albero `release` e quello
	@# con i controlli di sviluppo si vede solo in megabyte. Stamparlo qui
	@# evita di dover andare a misurare il CD per sapere quale dei due c'e'.
	@set -e; \
	G=""; \
	if [ -x "$(GCC_NATIVO_REL)/cc1" ]; then G="$(GCC_NATIVO_REL)"; M="release"; \
	elif [ -x "$(GCC_NATIVO_CHK)/cc1" ]; then G="$(GCC_NATIVO_CHK)"; M="controlli di sviluppo"; \
	fi; \
	if [ -n "$$G" ]; then \
	    for b in cc1 cpp xgcc collect2; do \
	        [ -x "$$G/$$b" ] || continue; \
	        i386-exos-strip -o $(ISO_ROOT)/bin/$$b "$$G/$$b"; \
	    done; \
	    if [ -f $(ISO_ROOT)/bin/xgcc ]; then \
	        mv $(ISO_ROOT)/bin/xgcc $(ISO_ROOT)/bin/gcc; \
	    fi; \
	    echo "     GCC nativo incluso da $$G ($$M):"; \
	    for b in cc1 cpp gcc collect2; do \
	        [ -f $(ISO_ROOT)/bin/$$b ] || continue; \
	        echo "       $$b  $$(du -h $(ISO_ROOT)/bin/$$b | cut -f1)"; \
	    done; \
	else \
	    echo "     GCC nativo assente: si costruisce con tools/gcc-exos/prepara-cc1.sh"; \
	fi
	@cp $(TOOLS_DIR)/iso/prova-cc1.c $(ISO_ROOT)/prova-cc1.c
	@python3 $(ISO_MKISO) $(ISO_IMG) --da $(ISO_ROOT) --etichetta "EXOS TOOLS"
	@echo "[OK] CD degli strumenti: $(ISO_IMG)"

.PHONY: iso
iso: $(ISO_IMG)

# =============================================================================
# IL CD DI EX-OS — avviabile, con il sistema sopra
#
# ⚠️ E' UN DISCO DIVERSO da exos-tools.iso. Quello e' il CD di SVILUPPO:
# GCC, as, ld, le librerie di calcolo, i sorgenti — roba di terzi portata
# qui. Questo porta solo cio' che e' nato in questo progetto: il sistema,
# i driver, gli strumenti.
#
# ⚠️ AVVIABILE PER EMULAZIONE FLOPPY. Dentro c'e' dist/floppy.img come
# immagine di avvio El Torito: il BIOS la presenta come A:, stage1 e
# stage2 la leggono con l'INT 13h — che e' proprio cio' che il BIOS emula —
# e caricano il kernel. Il percorso di avvio collaudato non cambia.
#
# ⚠️ POI IL KERNEL PASSA AL CD, e deve. In modo protetto l'INT 13h non si
# puo' chiamare, e dietro l'emulazione non c'e' nessun controller floppy:
# fat12_init fallisce, e quel fallimento e' il segnale che vfs_init usa
# per montare come root il lettore ATAPI. Da qui la conseguenza che decide
# il contenuto di questo disco: il sistema che gira e' QUELLO SUL CD, non
# quello dentro boot.img. I due devono contenere le stesse cose, o si
# avvia una versione e se ne esegue un'altra.
# =============================================================================
ISOX_ROOT := $(BUILD_DIR)/iso-exos
ISOX_IMG  := $(DIST_DIR)/exos.iso

$(ISOX_IMG): $(FLOPPY_IMG) $(PCI_DRV_OUT) $(NE2K_DRV_OUT) $(IP_DRV_OUT) $(NETDETECT_BIN) $(NETTEST_BIN) $(PING_BIN) $(IPCFG_BIN) $(DHCP_BIN) $(ISO_MKISO) README.md gpl-2.0.txt boot/kernel.cfg
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
	@cp boot/kernel.cfg $(ISOX_ROOT)/boot/kernel.cfg
	@cp README.md HANDOFF.md KERNEL_CORE_NOTES.md gpl-2.0.txt $(ISOX_ROOT)/doc/
	@# ⚠️ Anche il kernel e stage2 sulla radice: non servono ad avviare —
	@# quelli usati stanno dentro boot.img — ma servono a `install`, che
	@# li cerca li' per copiarli su un disco rigido.
	@cp $(STAGE2_BIN) $(ISOX_ROOT)/LOADER.BIN
	@cp $(KERNEL_BIN) $(ISOX_ROOT)/KERNEL.BIN
	@python3 $(ISO_MKISO) $(ISOX_IMG) --da $(ISOX_ROOT) \
	    --avvio $(FLOPPY_IMG) --etichetta "EXOS"
	@echo "[OK] CD di EX-OS: $(ISOX_IMG)"
	@echo "     Provalo senza floppy:  qemu-system-i386 -cdrom $(ISOX_IMG) -boot d -m 32M"

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
HD_IMG := $(DIST_DIR)/hd.img

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
	@echo "Contenuto floppy:"
	@mdir -i $(FLOPPY_IMG) -/ ::
	@echo ""
	@echo "Boot sector (primi 16 byte):"
	@dd if=$(FLOPPY_IMG) bs=512 count=1 status=none | od -A x -t x1z | head -8
	@echo ""
	@echo "Firma boot sector (byte 510-511):"
	@dd if=$(FLOPPY_IMG) bs=1 skip=510 count=2 status=none | od -A n -t x1

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
	@echo "  make floppy       — Immagine di avvio dist/floppy.img"
	@echo "  make iso          — CD degli strumenti dist/exos-tools.iso"
	@echo "  make hd           — Disco avviabile dist/hd.img (512 MB, ext2)"
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
	@echo "  make verify       — Verifica struttura floppy"
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
