#!/bin/bash
# =============================================================================
# tools/fmt-exos/costruisci.sh — {fmt} for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/fmt-exos/costruisci.sh [32|64]       both by default
#
# The C++ formatting library (what std::format grew from): the headers in
# include/fmt/, usable header-only (FMT_HEADER_ONLY, as MariaDB does), and
# libfmt.a (-lfmt) for programs that link it. See
# tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: fmt-12.2.0.zip from https://github.com/fmtlib/fmt/releases,
# with its build attestation (.intoto.jsonl); the same file, MD5 and all, as
# MariaDB 11.8's cmake asks for (cross_build/terze-parti/mariadb/).
#
# ! 12.2.0 DOES NOT BUILD FOR ANY 32-BIT TARGET (fmt issues #4811, #4821,
# #4886): without __int128 its fallback uint128 has no operator~, which the
# hexadecimal long double code uses. Fixed upstream by pull request #4813
# (merged 18 June 2026, commit 588b3a0f8f6a), not released yet:
# pr4813-uint128-not.diff here is that pull request as GitHub serves it, and
# it is applied to the copy of the sources before building. A release after
# 12.2.0 has it, and then the patch goes.
# =============================================================================
NOME=fmt
VER=12.2.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/fmt/fmt-$VER.zip

costruisci() {
    lib_per "$1"
    lib_sorgente
    patch -d "$SRC" -p1 -s < "$(dirname "$0")/pr4813-uint128-not.diff"
    lib_cmake_toolchain
    cmake -S "$SRC" -B "$B/obj" -DCMAKE_TOOLCHAIN_FILE="$B/exos.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/exos \
        -DBUILD_SHARED_LIBS=OFF -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=ON \
        > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j3 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/lib/cmake" "$DEST/lib/pkgconfig" "$DEST/share"
    lib_fine "$TP/fmt/fmt-$VER.intoto.jsonl" "$(dirname "$0")/pr4813-uint128-not.diff"
}
lib_tutte "$1"
