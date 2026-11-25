#!/bin/bash
# =============================================================================
# tools/woff2-exos/costruisci.sh — WOFF2 (Google's decoder) for EX-OS, 32 and 64
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/woff2-exos/costruisci.sh [32|64]   both by default
#
# libwoff2common.so.1.0.2 and libwoff2dec.so.1.0.2 (.a too) with
# <woff2/decode.h>: from a .woff2 file - the format of almost every font a
# web page asks for - back to the TrueType/OpenType file it was made from.
# What it is for: debug/implementazione_BROWSER002.txt, P6 (@font-face).
# EXBrowser opens libwoff2dec with dlopen the first time a page names a
# .woff2 (exwin/bin/exbrowser/browser_caratteri.c); without it the page
# keeps our Liberation faces.
#
# The archive: woff2-1.0.2.tar.gz, the GitHub archive of tag v1.0.2 (commit
# 1bccf208bca986e53a647dfe4811322adb06ecf8), checked against a clone of the
# tag; its sha256 next to it. No signature upstream. License MIT.
#
# ! NO CMAKE, AND NO ENCODER: CMakeLists.txt makes woff2common of three files
# and woff2dec of two; the encoder (woff2enc, and the programs) is not
# needed. The sonames are upstream's: SOVERSION is the whole version, 1.0.2.
# ! C++, AND THE API IS C++ (woff2::ConvertWOFF2ToTTF): the browser calls it
# by its mangled name, which has the same two forms on every compiler of
# the Itanium ABI (size_t is `m` at 64 bit, `j` at 32). The library needs
# libstdc++.so.6 and libgcc_s.so.1, which /lib already has, and
# libbrotlidec.so.1 (tools/brotli-exos, P2).
# ! -include cstdint: output.h of 1.0.2 uses uint8_t without including
# <cstdint>, which GCC 13's <string> no longer brings in (fixed upstream
# after the release); the flag does what the missing line would.
# =============================================================================
NOME=woff2
VER=1.0.2
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/woff2/woff2-$VER.tar.gz

# compiles the .cc of $SRC/src named after $2... into $DEST/lib/lib$1.a
archivio() {
    local nome=$1 f o lista=""
    shift
    mkdir -p "$B/obj/$nome"
    for f in "$@"; do
        o="$B/obj/$nome/$f.o"
        # shellcheck disable=SC2086
        $CXX $CFLAGS -std=c++11 -fno-exceptions -include cstdint -I"$SRC/include" -I"$SRC/src" -I"$CROSS/$T/include" \
            -c "$SRC/src/$f.cc" -o "$o" >> "$B/costruisci.log" 2>&1 \
            || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
        lista="$lista $o"
    done
    rm -f "$DEST/lib/lib$nome.a"
    # shellcheck disable=SC2086
    $AR rcs "$DEST/lib/lib$nome.a" $lista
}

costruisci() {
    lib_per "$1"
    [ -f "$CROSS/$T/include/brotli/decode.h" ] || manca "brotli per $T" "tools/brotli-exos/costruisci.sh $1"
    lib_sorgente
    : > "$B/costruisci.log"
    archivio woff2common table_tags variable_length woff2_common
    archivio woff2dec    woff2_dec woff2_out
    mkdir -p "$DEST/include/woff2"
    cp -p "$SRC"/include/woff2/decode.h "$SRC"/include/woff2/output.h "$DEST/include/woff2/"
    lib_condivisa woff2common $VER "-lstdc++ -lgcc_s -lm"
    lib_condivisa woff2dec $VER "-lwoff2common -lbrotlidec -lstdc++ -lgcc_s -lm"
    lib_fine
}
lib_tutte "$1"
