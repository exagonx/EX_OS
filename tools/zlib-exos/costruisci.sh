#!/bin/bash
# =============================================================================
# tools/zlib-exos/costruisci.sh — zlib for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/zlib-exos/costruisci.sh [32|64]      both by default
#
# libz.a and zlib.h/zconf.h (-lz), the deflate format of gzip, PNG, zip,
# MariaDB's compressed protocol and tables. See tools/librerie-exos/comune.sh
# for where everything ends up.
#
# The archive: zlib-1.3.2.tar.xz and its .asc from
# https://github.com/madler/zlib/releases (signed by Mark Adler, key
# 5ED46A6721D365587791E2AA783FCD8E58BCAFBA), in cross_build/terze-parti/zlib.
# zlib's own configure: CHOST names the tools, --static builds libz.a only.
# =============================================================================
NOME=zlib
VER=1.3.2
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/zlib/zlib-$VER.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    ( cd "$SRC" && CHOST=$T CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" \
        ./configure --static --prefix=/exos ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$SRC" -j3 libz.a > "$B/costruisci.log" 2>&1 || { tail -30 "$B/costruisci.log"; exit 1; }
    cp -p "$SRC/libz.a" "$DEST/lib/"
    cp -p "$SRC/zlib.h" "$SRC/zconf.h" "$DEST/include/"
    lib_fine
}
lib_tutte "$1"
