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
BUILD_BIN     := $(BUILD_DIR)/bin
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

CFLAGS_USER := -m32 -ffreestanding -fno-builtin -fno-stack-protector \
               -fno-pic -fno-pie -Wall -O2 -std=c11 -nostdlib

$(SHELL_BIN): $(SHELL_SRC) $(SHELL_LD)
	@echo "=== Compilazione Shell utente /bin/sh ==="
	@mkdir -p $(BUILD_BIN) $(BUILD_OBJ)
	$(CC) $(CFLAGS_USER) -c $(SHELL_SRC) -o $(BUILD_OBJ)/shell.o
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(SHELL_LD) $(BUILD_OBJ)/shell.o -o $@
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(HELLO_LD) $(BUILD_OBJ)/hello.o -o $@
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(LS_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(MEM_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(STACK_LD) \
	    $(BUILD_OBJ)/stack_start.o \
	    $(BUILD_OBJ)/stack_main.o  \
	    $(BUILD_OBJ)/stack_libc.o  \
	    -o $@
	@echo "[OK] stack compilato: $@"

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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(DISK_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(FDISK_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(MKFS_LD) \
	    $(BUILD_OBJ)/mkfs_start.o $(BUILD_OBJ)/mkfs_main.o \
	    $(BUILD_OBJ)/mkfs_ext2.o  $(BUILD_OBJ)/mkfs_libc.o -o $@
	@echo "[OK] mkfs compilato: $@"

.PHONY: mkfs
mkfs: dirs $(MKFS_BIN)

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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(TEXTLINE_LD) \
	    $(BUILD_OBJ)/textline_start.o \
	    $(BUILD_OBJ)/textline_main.o  \
	    $(BUILD_OBJ)/textline_libc.o  \
	    -o $@
	@echo "[OK] textline compilato: $@"

.PHONY: textline
textline: dirs $(TEXTLINE_BIN)

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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(INSTALL_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(CP_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(MOUNT_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(MKDIR_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(RMDIR_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(DELETE_LD) \
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
	$(LD) -m $(CROSS_LD_EMU) -nostdlib -T $(KBD_DRV_LD) \
	    $(BUILD_DRIVERS)/kbd_start.o \
	    $(BUILD_DRIVERS)/kbd_main.o  \
	    $(BUILD_DRIVERS)/kbd_libc.o  \
	    -o $@
	@echo "[OK] kbd.drv compilato: $@"

.PHONY: kbd_drv
kbd_drv: dirs $(KBD_DRV_OUT)

.PHONY: all
all: dirs stage1 stage2 kernel shell hello ls mem stack disk fdisk mkfs mount_prog cp_prog install_prog textline mkdir_prog rmdir_prog delete_prog libc floppy_drv kbd_drv floppy
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
                  $(KERNEL_DIR)/arch/x86/vga.c \
                  $(KERNEL_DIR)/arch/x86/kprintf.c \
                  $(KERNEL_DIR)/mm/pmm.c \
                  $(KERNEL_DIR)/mm/paging.c \
                  $(KERNEL_DIR)/mm/kmalloc.c \
                  $(KERNEL_DIR)/sched/sched.c \
                  $(KERNEL_DIR)/ipc/ipc.c \
                  $(KERNEL_DIR)/syscall/syscall.c \
                  $(KERNEL_DIR)/syscall/syscall_impl.c \
                  $(KERNEL_DIR)/fs/fat12.c \
                  $(KERNEL_DIR)/fs/fat.c \
                  $(KERNEL_DIR)/fs/ext2.c \
                  $(KERNEL_DIR)/fs/vfs.c \
                  $(KERNEL_DIR)/boot/bootinst.c \
                  $(KERNEL_DIR)/block/ata.c \
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

$(BUILD_KERNEL)/tty.o: drivers/tty/tty.c
	@echo "[..] Compilo TTY inline: $<"
	@mkdir -p $(BUILD_KERNEL)
	$(CC) $(CFLAGS) -I drivers/tty -c $< -o $@

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
	           $(BUILD_BIN) \
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
	@printf "[kernel]\nloglevel=3\ntimer_hz=100\nverboseboot=1\n\n" >> boot/kernel.cfg
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
	@echo "Test e debug:"
	@echo "  make run          — Avvia con QEMU"
	@echo "  make run-serial   — Avvia con output seriale su terminale"
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
