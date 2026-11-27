#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
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
#
# ! A MOVED OBJECT TREE KEEPS ITS OLD PATHS IN THE mozbuild CRATE. Cargo does
# not rebuild it when buildconfig.rs changes, so every build script linked to
# it (gecko-profiler) looks for dist/include in the old place: «Include not
# found: mozilla-config.h» (24 November 2026, after the move to ~/ex_os).
# Remedy: delete <objdir>/release/build/mozbuild and
# <objdir>/<target>/release/build/mozbuild, then build again. And the links
# in dist/bin (application.ini, default.locale, ...) still point to the old
# tree: `mach package` says «Missing file(s): bin/application.ini». Remedy:
# find <objdir> -type l -lname '<old>/*', and point each one to the new tree.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
# (a mozconfig already given wins: Thunderbird, tools/thunderbird-exos)
if [ -n "${EXILLA_MOZCONFIG:-}" ]; then
    export EXILLA_MOZCONFIG
elif [ "$EXOS_ARCH" = x86_64 ]; then
    export EXILLA_MOZCONFIG="$PWD/tools/exilla/mozconfig-gecko64"
else
    export EXILLA_MOZCONFIG="$PWD/tools/exilla/mozconfig-gecko"
fi
# Node.js serve al browser per costruire (non gira dentro EX-OS): sta in
# cross_build/<macchina>/costruzione-strumenti/node, scaricato da nodejs.org.
export MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
export NODEJS="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-strumenti/node/bin/node"
[ -x "$NODEJS" ] || { echo "manca $NODEJS (Node 22 linux-x64 da nodejs.org)" >&2; exit 1; }
# bindgen legge gli header di Gecko con la libclang dell'HOST: gli si danno gli
# header di EX-OS (libstdc++ e libc della toolchain di questa macchina) al posto
# di quelli di Linux, e le macro che il GCC di EX-OS predefinisce (NSPR sceglie
# md/_exos.cfg da __exos__). -nostdlibinc tiene gli header propri di clang.
# ! LA libc CON -idirafter, NON -isystem: gli header di clang (limits.h,
# stdint.h) devono venire prima e passare ai nostri con #include_next. Il
# nostro limits.h da solo non ha CHAR_BIT: se lo aspetta dal compilatore.
if [ "$EXOS_ARCH" = x86_64 ]; then
    T=x86_64-exos; I="${EXOS_FUORI}/cross_build/$MACCHINA/exos-cross64/$T/include"
    [ -d "$I" ] || I="${EXOS_FUORI}/cross_build/exos-cross64/$T/include"
else
    T=i386-exos;   I="${EXOS_FUORI}/cross_build/$MACCHINA/exos-cross/$T/include"
fi
V=$(ls "$I/c++" | head -1)
export BINDGEN_CFLAGS="-nostdlibinc -isystem $I/c++/$V -isystem $I/c++/$V/$T -idirafter $I -D__exos__ -D__EXOS__ -D__ELF__ -D__unix__=1 -D__unix=1 -DEXOS_SOLO_POSIX"
exec tools/exilla/js-costruisci.sh "$@"
