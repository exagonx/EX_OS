#!/bin/bash
# =============================================================================
# tools/xz-exos/costruisci.sh — XZ Utils (liblzma) for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/xz-exos/costruisci.sh [32|64]        both by default
#
# liblzma.a with lzma.h and lzma/ (-llzma), and the xz, xzdec, lzmadec and
# lzmainfo programs. See tools/librerie-exos/comune.sh for where everything
# ends up.
#
# The archive: xz-5.8.4.tar.gz and its .sig from
# https://github.com/tukaani-project/xz/releases (signed by Lasse Collin,
# key 3690C240CE51B4670D30AD1C38EE757D69184620).
# Without the shell scripts (xzgrep, xzdiff...: EX-OS's shell is not sh),
# without translations; with POSIX threads, which xz uses for -T.
# =============================================================================
NOME=xz
VER=5.8.4
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/xz/xz-$VER.tar.gz

costruisci() {
    lib_per "$1"
    lib_sorgente
    lib_config_sub
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/configure" --host=$T --build="$("$SRC/build-aux/config.guess")" \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" LDFLAGS=-static \
        --disable-shared --enable-static --disable-nls --disable-scripts --disable-doc \
        --enable-threads=posix ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j3 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST/lib/liblzma.la"
    # unxz, xzcat, lzma... are links to xz: no links on a CD, and `xz -d`,
    # `xz -c` say the same
    find "$DEST/bin" -type l -delete
    lib_fine
}
lib_tutte "$1"
