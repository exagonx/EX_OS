#!/bin/bash
# =============================================================================
# tools/rust-exos/prova-dylib.sh — a Rust shared object loaded with dlopen
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/prova-dylib.sh          (64-bit EX-OS, dist/exos64.iso)
#
# @RUST-64 R64-2 step b: rustc loads procedural macros as shared objects. This
# builds tools/rust-exos/prova-dylib/libreria as a cdylib for
# x86_64-unknown-exos (its own std inside, PIC), the program that opens it
# with dlopen (lib/libc_dl.c), and runs the program inside EX-OS with
# LD_LIBRARY_PATH=/disk. Needs the x86_64-exos gcc's specs from
# tools/gcc-exos/specs-condivise.sh (crtbeginS.o registers the library's
# unwind tables: without it a panic inside the library aborts).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$RADICE/cross_build/$MACCHINA"
CARGO="${CARGO:-$(command -v cargo || echo "$HOME/.cargo/bin/cargo")}"
export RUSTUP_HOME="$B/rust-macchina/rustup" CARGO_HOME="$B/rust-macchina/cargo"
export CARGO_TARGET_DIR="$B/costruzione-rust"
export __CARGO_TESTS_ONLY_SRC_ROOT="$B/costruzione-std/library"
export PATH="$RADICE/cross_build/exos-cross64/bin:$PATH"
export RUSTC="$RADICE/tools/rust-exos/rustc-exos"
BERSAGLIO=x86_64-unknown-exos

tools/rust-exos/std/prepara.sh > "$B/costruzione-std.log" 2>&1 || {
    cat "$B/costruzione-std.log"; exit 1; }
for d in prova-dylib/libreria prova-dylib; do
    (cd tools/rust-exos/$d && "$CARGO" +nightly build --release \
        -Z build-std=std,panic_unwind --target $BERSAGLIO) || exit 1
done
OUT="$CARGO_TARGET_DIR/$BERSAGLIO/release"
# The procedural macro: built by rustc itself, as cargo builds one for the
# host - which, for a native rustc, is EX-OS. Its std comes from the sysroot
# (tools/rust-exos/std/installa-sysroot.sh, panic=unwind, with proc_macro).
M="$CARGO_TARGET_DIR/macro64"; mkdir -p "$M"
"$RUSTC" --edition 2021 --crate-type proc-macro --crate-name quarantadue \
    --target $BERSAGLIO -C panic=unwind -O tools/rust-exos/prova-dylib/macro/lib.rs \
    -o "$M/libquarantadue.so" || exit 1
DECLS=$(x86_64-exos-nm -D --defined-only "$M/libquarantadue.so" | awk '/__rustc_proc_macro_decls_/ {print $3}')
uscita=$(EXOS_ARCH=x86_64 SECONDI=20 ALTRI="$OUT/libprova_dylib.so $M/libquarantadue.so" \
         AMBIENTE="LD_LIBRARY_PATH=/disk PROVA_MACRO=/disk/libquarantadue.so PROVA_DECLS=$DECLS" \
         tools/exilla/esegui-in-exos.sh "$OUT/prova-dylib")
echo "$uscita"
echo "$uscita" | grep -q "prova-dylib: 0 NO" && { echo "=== [OK] ==="; exit 0; }
echo "=== [NO] ==="; exit 1
