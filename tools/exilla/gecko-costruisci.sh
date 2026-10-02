#!/bin/bash
# =============================================================================
# tools/exilla/gecko-costruisci.sh — Gecko per EX-OS (tappa 6 di Exilla)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/gecko-costruisci.sh [configure|build|...]   (default: configure)
#
# Lo stesso ambiente di tools/exilla/js-costruisci.sh (compilatore, Rust, stato
# di mach dentro il progetto) con tools/exilla/mozconfig-gecko: il browser
# intero, oggetti in cross_build/exilla-obj/gecko.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
export EXILLA_MOZCONFIG="$PWD/tools/exilla/mozconfig-gecko"
# Node.js serve al browser per costruire (non gira dentro EX-OS): sta in
# cross_build/<macchina>/costruzione-strumenti/node, scaricato da nodejs.org.
export MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
export NODEJS="$PWD/cross_build/$MACCHINA/costruzione-strumenti/node/bin/node"
[ -x "$NODEJS" ] || { echo "manca $NODEJS (Node 22 linux-x64 da nodejs.org)" >&2; exit 1; }
# bindgen legge gli header di Gecko con la libclang dell'HOST: gli si danno gli
# header di EX-OS (libstdc++ e libc della toolchain di questa macchina) al posto
# di quelli di Linux, e le macro che il GCC di EX-OS predefinisce (NSPR sceglie
# md/_exos.cfg da __exos__). -nostdlibinc tiene gli header propri di clang.
# ! LA libc CON -idirafter, NON -isystem: gli header di clang (limits.h,
# stdint.h) devono venire prima e passare ai nostri con #include_next. Il
# nostro limits.h da solo non ha CHAR_BIT: se lo aspetta dal compilatore.
I="$PWD/cross_build/$MACCHINA/exos-cross/i386-exos/include"
V=$(ls "$I/c++" | head -1)
export BINDGEN_CFLAGS="-nostdlibinc -isystem $I/c++/$V -isystem $I/c++/$V/i386-exos -idirafter $I -D__exos__ -D__EXOS__ -D__ELF__ -D__unix__=1 -D__unix=1 -DEXOS_SOLO_POSIX"
exec tools/exilla/js-costruisci.sh "$@"
