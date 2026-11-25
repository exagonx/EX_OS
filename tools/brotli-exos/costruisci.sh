#!/bin/bash
# =============================================================================
# tools/brotli-exos/costruisci.sh — Brotli for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/brotli-exos/costruisci.sh [32|64]    both by default
#
# libbrotlicommon, libbrotlidec, libbrotlienc (.so.1 and .a) with
# <brotli/decode.h>, <brotli/encode.h>, and the brotli program. See
# tools/librerie-exos/comune.sh for where everything ends up.
#
# What it is for: "Content-Encoding: br", which Google and most big sites
# serve to whoever asks for it (debug/implementazione_BROWSER002.txt, P2).
# exhttp.so opens libbrotlidec.so.1 with dlopen the first time a page is
# asked for (lib/exhttp/srotola.c).
#
# The archive: brotli-1.2.1.tar.gz, the GitHub archive of tag v1.2.1
# (commit db24a98aed38a16de78cc05ca15cdd8dc2a0d92d), checked against a clone
# of the tag; its sha256 next to it. No signature upstream. License MIT.
#
# ! NO CMAKE: brotli's CMakeLists.txt only globs c/common, c/dec and c/enc
# into three libraries; the same three lists are compiled here with the
# cross compiler, which is all its build does. The soname is the upstream
# one: BROTLI_ABI_CURRENT - BROTLI_ABI_AGE = 3 - 2 = 1 (c/common/version.h).
# ! -lm FOR brotlienc: the encoder uses log2 (CMake links LIBM the same way).
# =============================================================================
NOME=brotli
VER=1.2.1
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/brotli/brotli-$VER.tar.gz

# compiles every .c of $SRC/c/$1 and puts them in $DEST/lib/lib$2.a
archivio() {
    local f o lista=""
    mkdir -p "$B/obj/$1"
    for f in "$SRC/c/$1"/*.c; do
        o="$B/obj/$1/$(basename "$f" .c).o"
        # shellcheck disable=SC2086
        $CC $CFLAGS -I"$SRC/c/include" -c "$f" -o "$o" >> "$B/costruisci.log" 2>&1 \
            || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
        lista="$lista $o"
    done
    rm -f "$DEST/lib/lib$2.a"
    # shellcheck disable=SC2086
    $AR rcs "$DEST/lib/lib$2.a" $lista
}

costruisci() {
    lib_per "$1"
    lib_sorgente
    : > "$B/costruisci.log"
    archivio common brotlicommon
    archivio dec    brotlidec
    archivio enc    brotlienc
    cp -rp "$SRC/c/include/brotli" "$DEST/include/"
    # the libraries in the order they need each other: common first
    lib_condivisa brotlicommon 1
    lib_condivisa brotlidec 1 "-lbrotlicommon"
    lib_condivisa brotlienc 1 "-lbrotlicommon -lm"
    # the program, linked to the shared ones as any EX-OS program
    # shellcheck disable=SC2086
    $CC $CFLAGS -I"$SRC/c/include" "$SRC/c/tools/brotli.c" -o "$DEST/bin/brotli" \
        -L"$DEST/lib" -lbrotlienc -lbrotlidec -lbrotlicommon -lm >> "$B/costruisci.log" 2>&1 \
        || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    lib_fine
}
lib_tutte "$1"
