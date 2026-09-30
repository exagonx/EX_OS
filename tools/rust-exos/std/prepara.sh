#!/bin/bash
# =============================================================================
# tools/rust-exos/std/prepara.sh — la std di Rust per EX-OS (@RUST-STD)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/std/prepara.sh
#
# Copia la `library` del nightly (rust-src) e il crate `libc` che quella
# library usa in cross_build/<macchina>/costruzione-std (fuori da MEGA), poi ci
# aggiunge EX-OS con aggiungi-exos.py. Non tocca il toolchain: cargo la usa
# attraverso __CARGO_TESTS_ONLY_SRC_ROOT (vedi tools/rust-exos/prova-std.sh).
#
# ! SI RIFA' DA ZERO OGNI VOLTA: le copie sono un prodotto, i sorgenti veri
# sono libc-exos.rs, os-exos/ e aggiungi-exos.py in questa cartella.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$RADICE/cross_build/$MACCHINA"
LAV="$B/costruzione-std"
export RUSTUP_HOME="$B/rust-macchina/rustup" CARGO_HOME="$B/rust-macchina/cargo"
PROXY_RUSTC="$HOME/.cargo/bin/rustc"   # il proxy di rustup: RUSTC puo essere rustc-exos

SRC="$("$PROXY_RUSTC" +nightly --print sysroot)/lib/rustlib/src/rust/library"
[ -d "$SRC/std" ] || { echo "manca rust-src nel nightly ($SRC)" >&2; exit 1; }
VER=$(grep -A1 '^name = "libc"$' "$SRC/Cargo.lock" | sed -n 's/^version = "\(.*\)"/\1/p')
LIBC=$(ls -d "$CARGO_HOME"/registry/src/*/"libc-$VER" 2>/dev/null | head -1)
if [ -z "$LIBC" ]; then
    # Il crate si scarica una volta sola: cargo fetch sul Cargo.lock della library.
    (cd "$SRC" && "$(dirname "$PROXY_RUSTC")/cargo" +nightly fetch >/dev/null 2>&1) || true
    LIBC=$(ls -d "$CARGO_HOME"/registry/src/*/"libc-$VER" | head -1)
fi
echo "std da $SRC"
echo "libc $VER da $LIBC"

mkdir -p "$LAV"
rsync -a --delete "$SRC/" "$LAV/library/"
rsync -a --delete "$LIBC/" "$LAV/libc/"
python3 tools/rust-exos/std/aggiungi-exos.py "$LAV"
