#!/bin/bash
# =============================================================================
# tools/rust-exos/prova-std.sh — @RUST-STD: la std di Rust dentro EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/prova-std.sh [--solo-compila]
#     EXOS_ARCH=x86_64 tools/rust-exos/prova-std.sh [--solo-compila]
#
# The second form is the 64-bit system: target x86_64-unknown-exos, linked
# with cross_build/exos-cross64 (tools/gcc-exos/cross64.sh), run in
# dist/exos64.iso.
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
export PATH="$B/exos-cross/bin:$RADICE/cross_build/exos-cross64/bin:$PATH"
BERSAGLIO=i686-unknown-exos
[ "$EXOS_ARCH" = x86_64 ] && BERSAGLIO=x86_64-unknown-exos
# ! IL BERSAGLIO PER NOME, non per percorso del .json (30 settembre 2026): una
# libreria compilata col percorso porta nell'identita' un'impronta del file
# («i686-unknown-exos-8757...») e non si mescola con quelle cercate per nome —
# che e' come le cerca il sistema di costruzione di Mozilla, attraverso il
# sysroot (tools/rust-exos/std/installa-sysroot.sh). rustc-exos mette
# RUST_TARGET_PATH e -Zunstable-options.
export RUSTC="$RADICE/tools/rust-exos/rustc-exos"

tools/rust-exos/std/prepara.sh > "$B/costruzione-std.log" 2>&1 || {
    cat "$B/costruzione-std.log"; exit 1; }

# ! CARGO NON VEDE CHE LA NOSTRA std E' CAMBIATA: per le librerie di
# -Z build-std guarda la versione del pacchetto, non la data dei file, e
# ricompila la libc vecchia senza dirlo (visto il 29 settembre 2026: costanti
# aggiunte e «non trovate»). Quando cambiano i sorgenti del porting si butta
# la costruzione del bersaglio e si rifa'.
IMPR=$(cat tools/rust-exos/std/*.rs tools/rust-exos/std/*.py tools/rust-exos/std/os-exos/*.rs \
       tools/rust-exos/*-unknown-exos.json | sha256sum | cut -c1-16)
if [ "$(cat "$CARGO_TARGET_DIR/impronta-std" 2>/dev/null)" != "$IMPR" ]; then
    rm -rf "$CARGO_TARGET_DIR/i686-unknown-exos" "$CARGO_TARGET_DIR/x86_64-unknown-exos"
    mkdir -p "$CARGO_TARGET_DIR"; echo "$IMPR" > "$CARGO_TARGET_DIR/impronta-std"
fi

# PROVA, STD, SEGNO (16 October 2026, native Rust): another test crate in
# tools/rust-exos, the std parts it needs, the line that says it passed -
# PROVA=prova-unwind STD=std,panic_unwind SEGNO="rust-unwind: 0 NO" tests
# panic=unwind. Without them, the std test as always.
PROVA="${PROVA:-prova-std}"
(cd tools/rust-exos/$PROVA && "$CARGO" +nightly build --release \
    -Z build-std=${STD:-std,panic_abort} --target $BERSAGLIO) || exit 1
BIN="$CARGO_TARGET_DIR/$BERSAGLIO/release/$PROVA"

# ! LA CPU DI BASE E' IL PENTIUM MMX (Makefile, CPU_BASE): niente SSE e niente
# cmov. Le MMX ci sono, e la memcpy della libc le usa apposta; il codice Rust
# no (il bersaglio le spegne: condividono i registri con la x87). Si guarda il
# binario, non i flag.
# (At 64 bit SSE2 is part of the base CPU: nothing to look for.)
if [ "$BERSAGLIO" = i686-unknown-exos ]; then
    n=$(i386-exos-objdump -d "$BIN" | grep -cE 'xmm|\bcmov')
    echo "collegato: $(stat -c %s "$BIN") byte, istruzioni oltre il Pentium MMX: $n"
    [ "$n" = 0 ] || { echo "[NO] il codice va oltre la CPU di base"; exit 1; }
else
    echo "collegato: $(stat -c %s "$BIN") byte, $(file -b "$BIN" | cut -d, -f1-2)"
fi
[ "$1" = "--solo-compila" ] && exit 0

uscita=$(SECONDI=15 tools/exilla/esegui-in-exos.sh "$BIN" uno due)
echo "$uscita"
echo "$uscita" | grep -q "${SEGNO:-rust-std: 0 NO}" && { echo "=== [OK] la std di Rust gira dentro EX-OS ==="; exit 0; }
echo "=== [NO] ==="; exit 1
