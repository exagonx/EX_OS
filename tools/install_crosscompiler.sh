#!/bin/bash
# =============================================================================
# tools/install_crosscompiler.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
# Installa il cross-compiler i686-elf-gcc su Debian 12.
# Tempo stimato: 20-40 minuti (dipende dalla CPU).
#
# Uso:
#   chmod +x tools/install_crosscompiler.sh
#   ./tools/install_crosscompiler.sh
#
# Dopo l'installazione aggiungi al tuo ~/.bashrc:
#   export PATH="$HOME/opt/cross/bin:$PATH"
# =============================================================================

set -e

# --- Versioni testate su Debian 12 -------------------------------------------
BINUTILS_VER="2.41"
GCC_VER="13.2.0"
TARGET="i686-elf"
PREFIX="$HOME/opt/cross"

# --- Colori ------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

log_info()  { echo -e "${CYAN}[INFO]${NC} $1"; }
log_ok()    { echo -e "${GREEN}[ OK ]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_err()   { echo -e "${RED}[ERR ]${NC} $1"; exit 1; }
log_step()  { echo -e "\n${CYAN}========================================${NC}"; \
              echo -e "${CYAN} $1${NC}"; \
              echo -e "${CYAN}========================================${NC}"; }

# =============================================================================
# 1. Verifica sistema
# =============================================================================
log_step "Verifica sistema"

if [ "$(uname -s)" != "Linux" ]; then
    log_err "Questo script è per Linux (testato su Debian 12)"
fi

# Controlla spazio disco (serve ~2GB per i sorgenti + build)
AVAIL=$(df -BG "$HOME" | awk 'NR==2{print $4}' | tr -d 'G')
log_info "Spazio disponibile in $HOME: ${AVAIL}GB"
if [ "$AVAIL" -lt 3 ]; then
    log_warn "Spazio disco basso: ${AVAIL}GB (consigliati almeno 3GB)"
fi

# =============================================================================
# 2. Installa dipendenze
# =============================================================================
log_step "Installazione dipendenze (richiede sudo)"

sudo apt-get update -qq
sudo apt-get install -y \
    build-essential \
    bison \
    flex \
    libgmp3-dev \
    libmpc-dev \
    libmpfr-dev \
    libisl-dev \
    texinfo \
    wget \
    nasm \
    mtools \
    dosfstools \
    qemu-system-i386 \
    gdb \
    make \
    xorriso

log_ok "Dipendenze installate"

# =============================================================================
# 3. Scarica sorgenti
# =============================================================================
log_step "Download sorgenti"

mkdir -p "$HOME/src"
cd "$HOME/src"

BINUTILS_TAR="binutils-${BINUTILS_VER}.tar.gz"
GCC_TAR="gcc-${GCC_VER}.tar.gz"

BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/${BINUTILS_TAR}"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/${GCC_TAR}"

if [ ! -f "$BINUTILS_TAR" ]; then
    log_info "Download binutils ${BINUTILS_VER}..."
    wget -q --show-progress "$BINUTILS_URL"
else
    log_info "binutils già scaricato, skip"
fi

if [ ! -f "$GCC_TAR" ]; then
    log_info "Download GCC ${GCC_VER}..."
    wget -q --show-progress "$GCC_URL"
else
    log_info "GCC già scaricato, skip"
fi

log_ok "Sorgenti pronti"

# =============================================================================
# 4. Estrai
# =============================================================================
log_step "Estrazione sorgenti"

if [ ! -d "binutils-${BINUTILS_VER}" ]; then
    log_info "Estrazione binutils..."
    tar xf "$BINUTILS_TAR"
fi

if [ ! -d "gcc-${GCC_VER}" ]; then
    log_info "Estrazione GCC..."
    tar xf "$GCC_TAR"
fi

log_ok "Estrazione completata"

# =============================================================================
# 5. Compila binutils
# =============================================================================
log_step "Compilazione binutils-${BINUTILS_VER}"
log_info "Target: $TARGET  Prefix: $PREFIX"

mkdir -p "$HOME/src/build-binutils"
cd "$HOME/src/build-binutils"

log_info "Configure binutils..."
"$HOME/src/binutils-${BINUTILS_VER}/configure" \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot \
    --disable-nls \
    --disable-werror \
    2>&1 | tail -3

log_info "Build binutils ($(nproc) core)..."
make -j$(nproc) 2>&1 | tail -5

log_info "Install binutils..."
make install 2>&1 | tail -3

log_ok "binutils installato in $PREFIX"

# =============================================================================
# 6. Compila GCC
# =============================================================================
log_step "Compilazione GCC-${GCC_VER} (solo C — può richiedere 20-30 min)"

export PATH="$PREFIX/bin:$PATH"

mkdir -p "$HOME/src/build-gcc"
cd "$HOME/src/build-gcc"

log_info "Configure GCC..."
"$HOME/src/gcc-${GCC_VER}/configure" \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --disable-nls \
    --enable-languages=c \
    --without-headers \
    2>&1 | tail -3

log_info "Build GCC ($(nproc) core)..."
make -j$(nproc) all-gcc 2>&1 | tail -5

log_info "Build libgcc..."
make -j$(nproc) all-target-libgcc 2>&1 | tail -5

log_info "Install GCC..."
make install-gcc 2>&1 | tail -3
make install-target-libgcc 2>&1 | tail -3

log_ok "GCC cross-compiler installato"

# =============================================================================
# 7. Verifica installazione
# =============================================================================
log_step "Verifica installazione"

TESTS_OK=0
TESTS_FAIL=0

check_tool() {
    local tool="$1"
    local expected="$2"
    if command -v "$PREFIX/bin/$tool" &>/dev/null; then
        local ver=$("$PREFIX/bin/$tool" --version 2>&1 | head -1)
        log_ok "$tool: $ver"
        TESTS_OK=$((TESTS_OK+1))
    else
        log_err "$tool non trovato in $PREFIX/bin/"
        TESTS_FAIL=$((TESTS_FAIL+1))
    fi
}

check_tool "i686-elf-gcc"    ""
check_tool "i686-elf-ld"     ""
check_tool "i686-elf-objdump" ""
check_tool "i686-elf-nm"     ""

echo ""
log_info "Test compilazione minima..."
cat > /tmp/test_cross.c << 'CEOF'
void _start(void) {
    volatile int x = 42;
    (void)x;
}
CEOF

"$PREFIX/bin/i686-elf-gcc" \
    -m32 -ffreestanding -fno-builtin -nostdlib \
    -c /tmp/test_cross.c -o /tmp/test_cross.o

if [ $? -eq 0 ]; then
    log_ok "Compilazione test OK"
    TESTS_OK=$((TESTS_OK+1))
else
    log_warn "Compilazione test fallita"
    TESTS_FAIL=$((TESTS_FAIL+1))
fi
rm -f /tmp/test_cross.c /tmp/test_cross.o

echo ""
log_info "Risultato: $TESTS_OK OK, $TESTS_FAIL FAIL"

# =============================================================================
# 8. Aggiorna ~/.bashrc
# =============================================================================
log_step "Configurazione PATH"

EXPORT_LINE="export PATH=\"$PREFIX/bin:\$PATH\""

if grep -q "opt/cross/bin" "$HOME/.bashrc" 2>/dev/null; then
    log_info "PATH già configurato in ~/.bashrc"
else
    echo "" >> "$HOME/.bashrc"
    echo "# EX-OS cross-compiler" >> "$HOME/.bashrc"
    echo "$EXPORT_LINE" >> "$HOME/.bashrc"
    log_ok "Aggiunto a ~/.bashrc: $EXPORT_LINE"
fi

# =============================================================================
# 9. Riepilogo finale
# =============================================================================
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Cross-compiler i686-elf installato!            ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║   Prefix:  $PREFIX${NC}"
echo -e "${GREEN}║   Versioni:${NC}"
echo -e "${GREEN}║     binutils: $BINUTILS_VER${NC}"
echo -e "${GREEN}║     gcc:      $GCC_VER${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║   Per usarlo nella sessione corrente:            ║${NC}"
echo -e "${GREEN}║     export PATH=\"$PREFIX/bin:\$PATH\"    ║${NC}"
echo -e "${GREEN}║                                                  ║${NC}"
echo -e "${GREEN}║   Già aggiunto a ~/.bashrc per le future         ║${NC}"
echo -e "${GREEN}║   sessioni (riapri il terminale o: source        ║${NC}"
echo -e "${GREEN}║   ~/.bashrc)                                     ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║   Ora puoi compilare EX-OS:                      ║${NC}"
echo -e "${GREEN}║     cd myos && make all                          ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════╝${NC}"
echo ""

# Attiva nell'ambiente corrente
export PATH="$PREFIX/bin:$PATH"
log_info "PATH aggiornato nella sessione corrente"
log_info "Verifica: $(i686-elf-gcc --version | head -1)"
