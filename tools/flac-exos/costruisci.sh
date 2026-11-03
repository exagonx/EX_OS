#!/bin/bash
# =============================================================================
# tools/flac-exos/costruisci.sh - FLAC (lossless audio) for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/flac-exos/costruisci.sh [32|64]      both by default
#
# See tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: flac-1.5.0.tar.xz with SHA256SUMS.txt from https://downloads.xiph.org/releases/flac/
# =============================================================================
NOME=flac
VER=1.5.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/flac/flac-1.5.0.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    lib_config_sub
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/configure" --host=$T --build=x86_64-pc-linux-gnu \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS -D__unix__ -DEXOS_SOLO_POSIX" \
        CPPFLAGS="-I$CROSS/$T/include" LDFLAGS="-static" \
        --disable-shared --enable-static am_cv_func_iconv=no --disable-programs --disable-examples --disable-cpplibs --disable-oggtest --disable-doxygen-docs --disable-xmms-plugin --disable-thorough-tests --disable-stack-smash-protection --disable-fortify-source-tests ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j4 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST"/lib/*.la
    lib_condivisa FLAC 14 "-logg -lm"
    lib_fine
}
lib_tutte "$1"
