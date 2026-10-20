#!/bin/bash
# =============================================================================
# tools/rust-exos/prova-rete.sh — @SOCKET-BSD: std::net di Rust dentro EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Costruisce tools/rust-exos/prova-rete con la std di EX-OS (come
# prova-std.sh, di cui usa la preparazione) e la esegue con la rete accesa,
# con tools/exilla/prova-socket.sh e il suo host dall'altra parte.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
tools/rust-exos/prova-std.sh --solo-compila > /dev/null || { tools/rust-exos/prova-std.sh --solo-compila; exit 1; }
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
CARGO="${CARGO:-$(command -v cargo || echo "$HOME/.cargo/bin/cargo")}"
export RUSTUP_HOME="$B/rust-macchina/rustup" CARGO_HOME="$B/rust-macchina/cargo"
export CARGO_TARGET_DIR="$B/costruzione-rust"
export __CARGO_TESTS_ONLY_SRC_ROOT="$B/costruzione-std/library"
export PATH="$B/exos-cross/bin:$PATH"
# ! IL BERSAGLIO PER NOME, non per percorso del .json (30 settembre 2026): una
# libreria compilata col percorso porta nell'identita' un'impronta del file
# («i686-unknown-exos-8757...») e non si mescola con quelle cercate per nome —
# che e' come le cerca il sistema di costruzione di Mozilla, attraverso il
# sysroot (tools/rust-exos/std/installa-sysroot.sh). rustc-exos mette
# RUST_TARGET_PATH e -Zunstable-options.
export RUSTC="$PWD/tools/rust-exos/rustc-exos"
(cd tools/rust-exos/prova-rete && "$CARGO" +nightly build --release \
    -Z build-std=std,panic_abort --target i686-unknown-exos) || exit 1
BIN="$CARGO_TARGET_DIR/i686-unknown-exos/release/prova-rete"
n=$(i386-exos-objdump -d "$BIN" | grep -cE 'xmm|\bcmov')
echo "collegato: $(stat -c %s "$BIN") byte, istruzioni oltre il Pentium MMX: $n"
[ "$n" = 0 ] || { echo "[NO] il codice va oltre la CPU di base"; exit 1; }
exec tools/exilla/prova-socket.sh "$BIN" prova-rete
