#!/bin/bash
# =============================================================================
# tools/install_apt.sh
# EX-OS — Extensible Operating System
# Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
# Installa tutti i pacchetti necessari per compilare EX-OS su Debian/Ubuntu.
# Usa gcc-i686-linux-gnu (disponibile tramite apt, ~5 minuti).
# =============================================================================

set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'
log_info() { echo -e "${CYAN}[INFO]${NC} $1"; }
log_ok()   { echo -e "${GREEN}[ OK ]${NC} $1"; }
log_err()  { echo -e "${RED}[ERR ]${NC} $1"; exit 1; }

echo -e "${CYAN}"
echo "  ╔══════════════════════════════════════════════╗"
echo "  ║   EX-OS — Installazione dipendenze apt       ║"
echo "  ║   Graziano Falcone <exagonx@hotmail.com>     ║"
echo "  ╚══════════════════════════════════════════════╝"
echo -e "${NC}"

log_info "Aggiornamento lista pacchetti..."
sudo apt-get update -qq

log_info "Installazione pacchetti..."
sudo apt-get install -y \
    gcc-i686-linux-gnu \
    binutils-i686-linux-gnu \
    nasm \
    mtools \
    dosfstools \
    qemu-system-i386 \
    gdb \
    make \
    xxd 2>/dev/null || true

# xxd potrebbe non esistere — alternativa
if ! command -v xxd &>/dev/null; then
    sudo apt-get install -y vim-common 2>/dev/null || true
fi

echo ""
log_info "Verifica installazione:"

FAIL=0

check() {
    if command -v "$1" &>/dev/null; then
        log_ok "$1: $(${1} --version 2>&1 | head -1)"
    else
        echo -e "${RED}[MANCA]${NC} $1 — installa: sudo apt install $2"
        FAIL=$((FAIL+1))
    fi
}

check i686-linux-gnu-gcc  "gcc-i686-linux-gnu"
check i686-linux-gnu-ld   "binutils-i686-linux-gnu"
check nasm                "nasm"
check mformat             "mtools"
check mkdosfs             "dosfstools"
check qemu-system-i386    "qemu-system-i386"

echo ""
if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}╔══════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║   Tutto installato! Ora compila EX-OS:        ║${NC}"
    echo -e "${GREEN}║                                               ║${NC}"
    echo -e "${GREEN}║     cd myos && make all                       ║${NC}"
    echo -e "${GREEN}║                                               ║${NC}"
    echo -e "${GREEN}║   Il Makefile rileva automaticamente          ║${NC}"
    echo -e "${GREEN}║   gcc-i686-linux-gnu e lo usa come            ║${NC}"
    echo -e "${GREEN}║   cross-compiler bare metal.                  ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════════╝${NC}"
else
    echo -e "${RED}$FAIL pacchetti mancanti — installa manualmente${NC}"
    exit 1
fi
