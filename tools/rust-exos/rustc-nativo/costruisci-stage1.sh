#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/rust-exos/rustc-nativo/costruisci-stage1.sh — check of step c
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     nohup tools/rust-exos/rustc-nativo/costruisci-stage1.sh > LOG 2>&1 &
#
# Prepares rustc's sources (prepara-sorgenti.sh), then has x.py build a
# stage-1 rustc for Linux and, with it, the std for x86_64-unknown-exos from
# the BUILT-IN target. Output in cross_build/<machine>/rustc-build; the last
# line of the log says FINITO or FALLITO. Long: the compiler is built once.
# =============================================================================
cd "$(dirname "$0")/../../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
T="${EXOS_FUORI}/cross_build/terze-parti/rust-src/rustc-nightly-src"
BUILD="${EXOS_FUORI}/cross_build/$MACCHINA/rustc-build"
TC64="${EXOS_FUORI}/cross_build/exos-cross64"
tools/rust-exos/rustc-nativo/prepara-sorgenti.sh || { echo FALLITO; exit 1; }
sed -e "s|@BUILD@|$BUILD|" -e "s|@TC64@|$TC64|g" \
    tools/rust-exos/rustc-nativo/bootstrap-stage1.toml > "$T/bootstrap.toml"
export PATH="$TC64/bin:$PATH"
cd "$T" && python3 x.py build --stage 1 library --target x86_64-unknown-exos \
    && echo FINITO || echo FALLITO
