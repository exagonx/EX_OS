#!/bin/bash
# =============================================================================
# tools/bzip2-exos/costruisci.sh — bzip2 for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/bzip2-exos/costruisci.sh [32|64]     both by default
#
# libbz2.a and bzlib.h (-lbz2), and the bzip2 and bzip2recover programs.
# See tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: bzip2-1.0.8.tar.gz and its .sig from
# https://sourceware.org/pub/bzip2/ (the upstream home; signed by Mark
# Wielaard, key EC3CFE88F6CA0788774F5C1D1AA44BE649DE760A). 1.0.8 is still
# the latest release. Its Makefile takes CC, AR, RANLIB and CFLAGS.
# =============================================================================
NOME=bzip2
VER=1.0.8
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/bzip2/bzip2-$VER.tar.gz

costruisci() {
    lib_per "$1"
    lib_sorgente
    make -C "$SRC" -j3 CC=$CC AR=$AR RANLIB=$RANLIB \
        CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64" LDFLAGS=-static \
        libbz2.a bzip2 bzip2recover > "$B/costruisci.log" 2>&1 \
        || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    cp -p "$SRC/libbz2.a" "$DEST/lib/"
    cp -p "$SRC/bzlib.h" "$DEST/include/"
    cp -p "$SRC/bzip2" "$SRC/bzip2recover" "$DEST/bin/"
    lib_fine
}
lib_tutte "$1"
