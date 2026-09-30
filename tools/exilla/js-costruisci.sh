#!/bin/bash
# =============================================================================
# tools/exilla/js-costruisci.sh — SpiderMonkey per EX-OS (tappa 4 di Exilla)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/js-costruisci.sh [configure|build|...]   (default: configure)
#
# Lancia mach sull'albero firefox-main con tools/exilla/mozconfig-js, il
# compilatore i386-exos di questa macchina e il Rust con la std di EX-OS.
# ! Tutto cio' che mach scrive sta nel progetto: gli oggetti in
# cross_build/exilla-obj/js, il suo stato (virtualenv, cache) in
# cross_build/<macchina>/costruzione-mozbuild. Niente in $HOME.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$RADICE/cross_build/$MACCHINA"
export MOZCONFIG="$RADICE/tools/exilla/mozconfig-js"
export MOZBUILD_STATE_PATH="$B/costruzione-mozbuild"
export RUSTUP_HOME="$B/rust-macchina/rustup" CARGO_HOME="$B/rust-macchina/cargo"
# $CARGO_HOME/bin: cbindgen, installato con cargo install dentro il progetto
export PATH="$B/exos-cross/bin:$CARGO_HOME/bin:$HOME/.cargo/bin:$PATH"
# Rust: il bersaglio i686-unknown-exos per nome (tools/rust-exos/rustc-exos), la
# std nel sysroot del nightly (tools/rust-exos/std/installa-sysroot.sh).
export RUSTUP_TOOLCHAIN=nightly
export RUSTC="$RADICE/tools/rust-exos/rustc-exos"
export RUST_TARGET_PATH="$RADICE/tools/rust-exos"
export CC=i386-exos-gcc CXX=i386-exos-g++ HOST_CC=gcc HOST_CXX=g++
# ! LA CPU DI BASE E' IL PENTIUM MMX (Makefile, CPU_BASE): senza -march il GCC
# di EX-OS compila per i686 e mette cmov. I file SIMD di Mozilla (SSE2, SSSE3,
# AVX) restano come sono: si scelgono a runtime dopo aver chiesto alla CPU.
export CFLAGS="-march=pentium-mmx -mtune=pentium-mmx" CXXFLAGS="-march=pentium-mmx -mtune=pentium-mmx"
# mach vuole llvm-objdump per leggere le intestazioni dei binari di prova; quello
# di Debian (LLVM 14) legge anche gli ELF i386 di EX-OS.
export LLVM_OBJDUMP="${LLVM_OBJDUMP:-$(command -v llvm-objdump || command -v llvm-objdump-14)}"
export MACH_NO_TERMINAL_FOOTER=1 MOZ_NOSPAM=1
mkdir -p "$MOZBUILD_STATE_PATH" "$RADICE/cross_build/exilla-obj"
cd firefox-main || exit 1
[ $# -eq 0 ] && set -- configure
exec ./mach "$@"
