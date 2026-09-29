#!/bin/bash
# =============================================================================
# tools/rust-exos/prova-std.sh — @RUST-STD: la std di Rust dentro EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/prova-std.sh [--solo-compila]
#
# Prepara la std con EX-OS dentro (tools/rust-exos/std/prepara.sh), compila
# tools/rust-exos/prova-std con -Z build-std, la collega col gcc i386-exos di
# questa macchina (crt0.o e libc.a vengono da li': rifarle con
# tools/gcc-exos/prepara-cross.sh dopo ogni modifica alla libc) e la esegue
# dentro EX-OS con tools/exilla/esegui-in-exos.sh.
#
# ! __CARGO_TESTS_ONLY_SRC_ROOT e' il modo in cui cargo accetta una `library`
# diversa da quella del toolchain: la nostra copia, in costruzione-std.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$RADICE/cross_build/$MACCHINA"
CARGO="${CARGO:-$(command -v cargo || echo "$HOME/.cargo/bin/cargo")}"
export RUSTUP_HOME="$B/rust-macchina/rustup" CARGO_HOME="$B/rust-macchina/cargo"
export CARGO_TARGET_DIR="$B/costruzione-rust"
export __CARGO_TESTS_ONLY_SRC_ROOT="$B/costruzione-std/library"
export PATH="$B/exos-cross/bin:$PATH"

tools/rust-exos/std/prepara.sh > "$B/costruzione-std.log" 2>&1 || {
    cat "$B/costruzione-std.log"; exit 1; }

# ! CARGO NON VEDE CHE LA NOSTRA std E' CAMBIATA: per le librerie di
# -Z build-std guarda la versione del pacchetto, non la data dei file, e
# ricompila la libc vecchia senza dirlo (visto il 29 settembre 2026: costanti
# aggiunte e «non trovate»). Quando cambiano i sorgenti del porting si butta
# la costruzione del bersaglio e si rifa'.
IMPR=$(cat tools/rust-exos/std/*.rs tools/rust-exos/std/*.py tools/rust-exos/std/os-exos/*.rs \
       tools/rust-exos/i686-unknown-exos.json | sha256sum | cut -c1-16)
if [ "$(cat "$CARGO_TARGET_DIR/impronta-std" 2>/dev/null)" != "$IMPR" ]; then
    rm -rf "$CARGO_TARGET_DIR/i686-unknown-exos"
    mkdir -p "$CARGO_TARGET_DIR"; echo "$IMPR" > "$CARGO_TARGET_DIR/impronta-std"
fi

(cd tools/rust-exos/prova-std && "$CARGO" +nightly build --release \
    -Z build-std=std,panic_abort -Z json-target-spec \
    --target ../i686-unknown-exos.json) || exit 1
BIN="$CARGO_TARGET_DIR/i686-unknown-exos/release/prova-std"

# ! LA CPU DI BASE E' IL PENTIUM MMX (Makefile, CPU_BASE): niente SSE e niente
# cmov. Le MMX ci sono, e la memcpy della libc le usa apposta; il codice Rust
# no (il bersaglio le spegne: condividono i registri con la x87). Si guarda il
# binario, non i flag.
n=$(i386-exos-objdump -d "$BIN" | grep -cE 'xmm|\bcmov')
echo "collegato: $(stat -c %s "$BIN") byte, istruzioni oltre il Pentium MMX: $n"
[ "$n" = 0 ] || { echo "[NO] il codice va oltre la CPU di base"; exit 1; }
[ "$1" = "--solo-compila" ] && exit 0

uscita=$(SECONDI=15 tools/exilla/esegui-in-exos.sh "$BIN" uno due)
echo "$uscita"
echo "$uscita" | grep -q "rust-std: 0 NO" && { echo "=== [OK] la std di Rust gira dentro EX-OS ==="; exit 0; }
echo "=== [NO] ==="; exit 1
