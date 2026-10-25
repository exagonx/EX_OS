#!/bin/bash
# =============================================================================
# tools/zstd-exos/costruisci.sh — Zstandard for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/zstd-exos/costruisci.sh [32|64]      both by default
#
# libzstd.a with zstd.h, zdict.h, zstd_errors.h (-lzstd), and the zstd
# program. See tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: zstd-1.5.7.tar.gz with .sig and .sha256 from
# https://github.com/facebook/zstd/releases (signed by the Zstandard Release
# Signing Key, 4EF4AC63455FC9F4545D9B7DEF8FE99528B52FFD).
#
# ! Single-threaded (no ZSTD_MULTITHREAD): the library's threads would only
# matter for compressing one big stream on several processors, and the
# simplest build is the one to trust first. The x86-64 assembly decoder
# (huf_decompress_amd64.S) stays: it is plain x86-64 code, no system calls.
# =============================================================================
NOME=zstd
VER=1.5.7
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/zstd/zstd-$VER.tar.gz

costruisci() {
    lib_per "$1"
    lib_sorgente
    make -C "$SRC/lib" -j3 CC=$CC AR=$AR CFLAGS="$CFLAGS" libzstd.a \
        > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$SRC/programs" -j3 CC=$CC AR=$AR CFLAGS="$CFLAGS" LDFLAGS=-static \
        HAVE_THREAD=0 HAVE_ZLIB=0 HAVE_LZMA=0 HAVE_LZ4=0 zstd \
        >> "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    cp -p "$SRC/lib/libzstd.a" "$DEST/lib/"
    cp -p "$SRC/lib/zstd.h" "$SRC/lib/zdict.h" "$SRC/lib/zstd_errors.h" "$DEST/include/"
    cp -p "$SRC/programs/zstd" "$DEST/bin/"
    lib_fine
}
lib_tutte "$1"
