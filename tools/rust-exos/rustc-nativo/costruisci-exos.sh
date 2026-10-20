#!/bin/bash
# =============================================================================
# tools/rust-exos/rustc-nativo/costruisci-exos.sh — rustc FOR 64-bit EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     nohup tools/rust-exos/rustc-nativo/costruisci-exos.sh > LOG 2>&1 &
#
# @RUST-64 R64-2 step d. Prepares the sources (prepara-sorgenti.sh, which
# also patches the workspace's libc and LLVM's system name), then has x.py
# build the compiler for host x86_64-unknown-exos: LLVM cross-built with
# cmake, then rustc. Shares cross_build/<machine>/rustc-build with
# costruisci-stage1.sh (the Linux stage 1 is reused). Last line of the log:
# FINITO or FALLITO. Hours: LLVM is built once.
# =============================================================================
cd "$(dirname "$0")/../../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
T="$RADICE/cross_build/terze-parti/rust-src/rustc-nightly-src"
BUILD="$RADICE/cross_build/$MACCHINA/rustc-build"
# the gcc with threads (tools/exilla/gcc-fili.sh), else the shared one
TCF="$RADICE/cross_build/$MACCHINA/exos-cross64"
[ -x "$TCF/bin/x86_64-exos-g++" ] || TCF="$RADICE/cross_build/exos-cross64"
tools/rust-exos/rustc-nativo/prepara-sorgenti.sh || { echo FALLITO; exit 1; }
sed -e "s|@BUILD@|$BUILD|" -e "s|@TCF@|$TCF|g" \
    tools/rust-exos/rustc-nativo/bootstrap-exos.toml > "$T/bootstrap.toml"
export PATH="$TCF/bin:$PATH"
# Third-party code on EX-OS: libc.h hides the system's own names (ProcInfo,
# DiskInfo... clash with LLVM's) and __unix__ is said, as for Gecko
# (tools/exilla/js-costruisci.sh). cmake-rs and cc read these per target.
for v in CFLAGS CXXFLAGS; do
    export ${v}_x86_64_unknown_exos="-DEXOS_SOLO_POSIX -D__unix__=1 -D__unix=1"
done
cd "$T" && python3 x.py build --stage 2 compiler/rustc library \
    --host x86_64-unknown-exos --target x86_64-unknown-exos \
    && echo FINITO || echo FALLITO
