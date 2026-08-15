#!/bin/bash
# =============================================================================
# tools/test_vbox.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# Script di test completo per EX-OS su VirtualBox.
#
# Uso:
#   ./tools/test_vbox.sh [build|check|run|vbox-setup|all]
#
# Comandi:
#   build       — compila tutto e crea l'immagine floppy
#   check       — verifica struttura immagine (senza compilare)
#   run         — avvia con QEMU (test veloce)
#   debug       — QEMU + GDB stub porta 1234
#   vbox-setup  — mostra istruzioni configurazione VirtualBox
#   all         — build + check + run
# =============================================================================

set -e

# --- Configurazione -----------------------------------------------------------
IMG="dist/floppy.img"
QEMU="qemu-system-i386"
QEMU_FLAGS="-fda $IMG -m 32M -boot a -no-reboot -no-shutdown"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${CYAN}[TEST]${NC} $1"; }
log_ok()    { echo -e "${GREEN}[ OK ]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_err()   { echo -e "${RED}[FAIL]${NC} $1"; }
log_step()  { echo -e "\n${CYAN}=== $1 ===${NC}"; }

# =============================================================================
# cmd_build — Compila EX-OS completo
# =============================================================================
cmd_build() {
    log_step "Build EX-OS"

    # Verifica cross-compiler
    if ! command -v i686-elf-gcc &>/dev/null; then
        log_err "i686-elf-gcc non trovato!"
        echo "  Esegui lo script di installazione del cross-compiler:"
        echo "  export PATH=\"\$HOME/opt/cross/bin:\$PATH\""
        exit 1
    fi

    if ! command -v nasm &>/dev/null; then
        log_err "nasm non trovato! sudo apt install nasm"
        exit 1
    fi

    if ! command -v mformat &>/dev/null; then
        log_err "mtools non trovato! sudo apt install mtools"
        exit 1
    fi

    log_info "Cross-compiler: $(i686-elf-gcc --version | head -1)"
    log_info "NASM: $(nasm --version)"

    # Build
    log_info "Avvio build..."
    make all

    if [ $? -eq 0 ]; then
        log_ok "Build completata con successo"
    else
        log_err "Build fallita!"
        exit 1
    fi
}

# =============================================================================
# cmd_check — Verifica struttura immagine floppy
# =============================================================================
cmd_check() {
    log_step "Verifica immagine floppy"

    if [ ! -f "$IMG" ]; then
        log_err "Immagine non trovata: $IMG (esegui prima 'make' o '$0 build')"
        exit 1
    fi

    # Dimensione immagine
    SIZE=$(stat -c%s "$IMG")
    if [ "$SIZE" -eq 1474560 ]; then
        log_ok "Dimensione corretta: $SIZE byte (1.44MB)"
    else
        log_err "Dimensione errata: $SIZE byte (atteso 1474560)"
        exit 1
    fi

    # Firma boot sector 0x55AA
    B510=$(dd if="$IMG" bs=1 skip=510 count=1 status=none | xxd -p)
    B511=$(dd if="$IMG" bs=1 skip=511 count=1 status=none | xxd -p)
    if [ "$B510" = "55" ] && [ "$B511" = "aa" ]; then
        log_ok "Firma boot sector: 0x55AA trovata"
    else
        log_err "Firma boot sector MANCANTE (byte510=0x$B510 byte511=0x$B511)"
        exit 1
    fi

    # BPB: bytes per sector = 512 (offset 11, little-endian: 00 02)
    BPS_L=$(dd if="$IMG" bs=1 skip=11 count=1 status=none | xxd -p)
    BPS_H=$(dd if="$IMG" bs=1 skip=12 count=1 status=none | xxd -p)
    if [ "$BPS_L" = "00" ] && [ "$BPS_H" = "02" ]; then
        log_ok "BPB bytes/sector: 512"
    else
        log_warn "BPB bytes/sector inatteso (L=0x$BPS_L H=0x$BPS_H)"
    fi

    # Verifica OEM name nei byte 3-10
    OEM=$(dd if="$IMG" bs=1 skip=3 count=8 status=none 2>/dev/null | cat)
    log_info "OEM name: '$OEM'"

    # Lista contenuto floppy
    echo ""
    log_info "Contenuto floppy:"
    echo "  ----------------------------------------"
    mdir -i "$IMG" -/ :: 2>/dev/null | head -40 | sed 's/^/  /'
    echo "  ----------------------------------------"

    # Verifica file obbligatori
    echo ""
    log_info "Verifica file obbligatori:"

    check_file() {
        local name=$1
        local fat_name=$2
        if mdir -i "$IMG" :: 2>/dev/null | grep -qi "$fat_name"; then
            log_ok "  $name trovato"
        else
            log_warn "  $name NON trovato (potrebbe non essere ancora compilato)"
        fi
    }

    check_file "LOADER.BIN" "LOADER"
    check_file "KERNEL.BIN" "KERNEL"

    # Dimensioni file chiave
    echo ""
    log_info "Dimensioni file chiave:"
    for f in LOADER.BIN KERNEL.BIN; do
        FSIZE=$(mdir -i "$IMG" :: 2>/dev/null | grep -i "$f" | awk '{print $3}' | tr -d ',')
        if [ -n "$FSIZE" ]; then
            log_info "  $f: $FSIZE byte"
        fi
    done

    # Verifica /boot/kernel.cfg
    if mdir -i "$IMG" ::/boot/ 2>/dev/null | grep -qi "KERNEL.CFG\|KERNEL  CFG"; then
        log_ok "/boot/kernel.cfg trovato"
    else
        log_warn "/boot/kernel.cfg non trovato"
    fi

    # Spazio usato vs disponibile
    USED=$(mdu -i "$IMG" :: 2>/dev/null | awk '{print $1}' || echo "?")
    log_info "Spazio floppy: circa $USED cluster usati"

    echo ""
    log_ok "Verifica completata"
}

# =============================================================================
# cmd_run — Avvia con QEMU
# =============================================================================
cmd_run() {
    log_step "Avvio QEMU"

    if ! command -v "$QEMU" &>/dev/null; then
        log_err "$QEMU non trovato! sudo apt install qemu-system-i386"
        exit 1
    fi

    if [ ! -f "$IMG" ]; then
        log_err "Immagine non trovata: $IMG"
        exit 1
    fi

    log_info "Avvio: $QEMU $QEMU_FLAGS"
    log_info "Chiudi QEMU con: Ctrl+Alt+Q oppure chiudi la finestra"
    echo ""

    $QEMU $QEMU_FLAGS \
        -serial stdio \
        2>/dev/null || true
}

# =============================================================================
# cmd_run_display — Avvia con QEMU con display VGA
# =============================================================================
cmd_run_display() {
    log_step "Avvio QEMU con display VGA"
    $QEMU $QEMU_FLAGS || true
}

# =============================================================================
# cmd_debug — QEMU + GDB
# =============================================================================
cmd_debug() {
    log_step "Avvio QEMU in modalita' debug"

    echo -e "${YELLOW}In un altro terminale esegui:${NC}"
    echo "  i686-elf-gdb build/kernel/kernel.bin"
    echo "  (gdb) target remote :1234"
    echo "  (gdb) break kernel_main"
    echo "  (gdb) continue"
    echo "  (gdb) layout src"
    echo ""
    echo -e "${CYAN}Oppure usa questo one-liner:${NC}"
    echo "  i686-elf-gdb build/kernel/kernel.bin -ex 'target remote :1234' -ex 'break kernel_main' -ex 'continue'"
    echo ""

    $QEMU $QEMU_FLAGS \
        -s -S \
        -d int,cpu_reset \
        -D build/qemu_debug.log \
        2>/dev/null || true
}

# =============================================================================
# cmd_vbox_setup — Istruzioni VirtualBox
# =============================================================================
cmd_vbox_setup() {
    log_step "Configurazione VirtualBox per EX-OS"

    cat << 'VBOX_INSTRUCTIONS'

  ╔══════════════════════════════════════════════════════════════╗
  ║         CONFIGURAZIONE VIRTUALBOX — EX-OS                    ║
  ╠══════════════════════════════════════════════════════════════╣
  ║                                                              ║
  ║  1. Nuova macchina virtuale:                                 ║
  ║     Nome:        EX-OS                                       ║
  ║     Tipo:        Other                                       ║
  ║     Versione:    Other/Unknown (32-bit)                      ║
  ║                                                              ║
  ║  2. RAM: 32MB (minimo) — 64MB consigliato                    ║
  ║                                                              ║
  ║  3. Storage — NON aggiungere disco rigido virtuale           ║
  ║                                                              ║
  ║  4. Aggiungi controller Floppy:                              ║
  ║     Storage → Add Floppy Controller                          ║
  ║     Floppy Device → Add Floppy Drive                         ║
  ║     Image → scegli: dist/floppy.img                          ║
  ║                                                              ║
  ║  5. Boot order:                                              ║
  ║     System → Boot Order: Floppy per primo                    ║
  ║     Deseleziona Hard Disk e Optical                          ║
  ║                                                              ║
  ║  6. Display:                                                 ║
  ║     Video Memory: 8MB                                        ║
  ║     Graphics Controller: VBoxVGA                             ║
  ║                                                              ║
  ║  7. Audio: disabilitato (EX-OS non ha driver audio)          ║
  ║                                                              ║
  ║  8. Network: disabilitato                                    ║
  ║                                                              ║
  ║  9. Avvia la macchina virtuale                               ║
  ║                                                              ║
  ║  Dovrai vedere:                                              ║
  ║    - "MyOS Boot..." (Stage 1)                                ║
  ║    - "Stage 2 attivo" + mappa memoria (Stage 2)              ║
  ║    - Messaggi di init kernel con [PASSO N]                   ║
  ║    - Banner EX-OS verde                                      ║
  ║    - Prompt shell: ex-os:/> _                                ║
  ║                                                              ║
  ╚══════════════════════════════════════════════════════════════╝

  Script VBoxManage (automatico):

VBOX_INSTRUCTIONS

    # Script VBoxManage automatico
    cat << 'VBOXCLI'
  # Crea e configura la VM da riga di comando:
  VBoxManage createvm --name "EX-OS" --ostype "Other" --register
  VBoxManage modifyvm "EX-OS" \
    --memory 32 \
    --cpus 1 \
    --boot1 floppy \
    --boot2 none \
    --boot3 none \
    --boot4 none \
    --audio none \
    --nic1 none \
    --vram 8

  # Aggiungi controller floppy
  VBoxManage storagectl "EX-OS" \
    --name "Floppy" \
    --add floppy

  # Converti .img in .vfd (formato VirtualBox per floppy)
  cp dist/floppy.img dist/floppy.vfd

  # Collega il floppy
  VBoxManage storageattach "EX-OS" \
    --storagectl "Floppy" \
    --port 0 --device 0 \
    --type fdd \
    --medium "$(pwd)/dist/floppy.vfd"

  # Avvia
  VBoxManage startvm "EX-OS"

VBOXCLI
}

# =============================================================================
# cmd_disasm — Disassembla componenti chiave
# =============================================================================
cmd_disasm() {
    log_step "Disassembly"
    echo "--- Stage 1 (boot sector) ---"
    if [ -f "build/stage1.bin" ]; then
        ndisasm -b 16 -o 0x7C00 build/stage1.bin | head -50
    fi
    echo ""
    echo "--- Kernel (prime 40 istruzioni) ---"
    if [ -f "build/kernel/kernel_main.o" ]; then
        i686-elf-objdump -d -M intel build/kernel/kernel_main.o | head -60
    fi
}

# =============================================================================
# cmd_size — Mostra dimensioni di tutti i componenti
# =============================================================================
cmd_size() {
    log_step "Dimensioni componenti"

    printf "%-25s %10s %10s %10s\n" "Componente" "Testo" "Dati" "BSS"
    echo "  -------------------------------------------------------"

    for obj in build/stage1.bin build/stage2.bin; do
        if [ -f "$obj" ]; then
            SIZE=$(stat -c%s "$obj")
            printf "  %-23s %10s byte\n" "$(basename $obj)" "$SIZE"
        fi
    done

    if [ -f "build/kernel.bin" ]; then
        i686-elf-size build/kernel.bin 2>/dev/null || \
            printf "  %-23s %10s byte\n" "kernel.bin" "$(stat -c%s build/kernel.bin)"
    fi

    if [ -f "build/bin/sh" ]; then
        i686-elf-size build/bin/sh 2>/dev/null || \
            printf "  %-23s %10s byte\n" "bin/sh" "$(stat -c%s build/bin/sh)"
    fi
}

# =============================================================================
# cmd_clean — Pulizia
# =============================================================================
cmd_clean() {
    log_step "Pulizia"
    make distclean
    log_ok "Pulizia completata"
}

# =============================================================================
# Dispatcher principale
# =============================================================================
CMD="${1:-help}"

case "$CMD" in
    build)      cmd_build ;;
    check)      cmd_check ;;
    run)        cmd_run ;;
    run-vga)    cmd_run_display ;;
    debug)      cmd_debug ;;
    vbox-setup) cmd_vbox_setup ;;
    disasm)     cmd_disasm ;;
    size)       cmd_size ;;
    clean)      cmd_clean ;;
    all)
        cmd_build
        cmd_check
        echo ""
        log_info "Build e verifica completate."
        log_info "Per avviare: $0 run    (QEMU)"
        log_info "             $0 debug  (QEMU + GDB)"
        log_info "             $0 vbox-setup  (istruzioni VirtualBox)"
        ;;
    help|*)
        echo ""
        echo -e "${CYAN}EX-OS Test Script${NC}"
        echo -e "${CYAN}Graziano Falcone <exagonx@hotmail.com>${NC}"
        echo ""
        echo "Uso: $0 <comando>"
        echo ""
        echo "Comandi:"
        echo "  build       — compila tutto (kernel + shell + driver + floppy)"
        echo "  check       — verifica struttura immagine floppy"
        echo "  run         — avvia con QEMU (consigliato per sviluppo)"
        echo "  run-vga     — avvia con QEMU + display VGA"
        echo "  debug       — avvia QEMU con GDB stub (porta 1234)"
        echo "  vbox-setup  — mostra come configurare VirtualBox"
        echo "  disasm      — disassembla boot sector e kernel"
        echo "  size        — mostra dimensioni di ogni componente"
        echo "  clean       — rimuove tutti i file compilati"
        echo "  all         — build + check"
        echo ""
        ;;
esac
