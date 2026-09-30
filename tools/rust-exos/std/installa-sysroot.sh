#!/bin/bash
# =============================================================================
# tools/rust-exos/std/installa-sysroot.sh — la std di EX-OS dentro il nightly
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/std/installa-sysroot.sh
#
# ! PERCHE' (30 settembre 2026, tappa 4 di Exilla): il sistema di costruzione
# di Mozilla chiama cargo e rustc da se', senza -Z build-std. Allora la std
# compilata per i686-unknown-exos (da tools/rust-exos/prova-std.sh) si mette
# dove rustc la cerca per ogni bersaglio: <sysroot>/lib/rustlib/<bersaglio>/lib.
# E' come si facevano i sysroot prima di build-std (xargo). Il sysroot e' quello
# del nightly di QUESTA macchina, in cross_build/<macchina>/rust-macchina.
#
# Il bersaglio si chiama per nome grazie a tools/rust-exos/rustc-exos, che
# mette RUST_TARGET_PATH e -Zunstable-options.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
export RUSTUP_HOME="$B/rust-macchina/rustup" CARGO_HOME="$B/rust-macchina/cargo"
PROXY_RUSTC="$HOME/.cargo/bin/rustc"

# Da zero: una serie sola di librerie, tutte dello stesso giro.
rm -rf "$B/costruzione-rust/i686-unknown-exos"
tools/rust-exos/prova-std.sh --solo-compila > "$B/installa-sysroot.log" 2>&1 || {
    tail -20 "$B/installa-sysroot.log"; exit 1; }

SYS="$("$PROXY_RUSTC" +nightly --print sysroot)/lib/rustlib/i686-unknown-exos/lib"
rm -rf "$SYS"; mkdir -p "$SYS"
n=0
# ! LE .rmeta INSIEME ALLE .rlib: il cargo di adesso tiene i metadati completi
# in un file a parte, e una .rlib da sola per rustc e' «only metadata stub».
for r in $(find "$B/costruzione-rust/i686-unknown-exos/release/build" -name "*.rlib" -o -name "*.rmeta" | grep -v "/prova"); do
    cp "$r" "$SYS/"; n=$((n + 1))
done
echo "sysroot: $n librerie in $SYS"
