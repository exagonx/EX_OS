#!/bin/bash
# =============================================================================
# tools/lz4-exos/costruisci.sh — LZ4 for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/lz4-exos/costruisci.sh [32|64]       both by default
#
# liblz4.a with lz4.h, lz4hc.h, lz4frame.h (-llz4), and the lz4 program.
# See tools/librerie-exos/comune.sh for where everything ends up.
#
# ! NOT THE SAME LZ4 AS tools/lz4exos.py: that one packs the boot ramdisk on
# the host with its own small block format; this is the library programs
# link with (MariaDB's InnoDB page compression, among others).
#
# The archive: lz4-1.10.0.tar.gz and its .sha256 from
# https://github.com/lz4/lz4/releases (no signature upstream: the sha256 of
# the release page was checked). Its Makefiles take CC, AR and CFLAGS.
# =============================================================================
NOME=lz4
VER=1.10.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/lz4/lz4-$VER.tar.gz

costruisci() {
    lib_per "$1"
    lib_sorgente
    make -C "$SRC/lib" -j3 CC=$CC AR=$AR CFLAGS="$CFLAGS" liblz4.a \
        > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$SRC/programs" -j3 CC=$CC AR=$AR CFLAGS="$CFLAGS" LDFLAGS=-static lz4 \
        >> "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    cp -p "$SRC/lib/liblz4.a" "$DEST/lib/"
    cp -p "$SRC/lib/lz4.h" "$SRC/lib/lz4hc.h" "$SRC/lib/lz4frame.h" "$DEST/include/"
    cp -p "$SRC/programs/lz4" "$DEST/bin/"
    lib_fine
}
lib_tutte "$1"
