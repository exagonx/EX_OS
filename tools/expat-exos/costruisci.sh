#!/bin/bash
# =============================================================================
# tools/expat-exos/costruisci.sh — Expat for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/expat-exos/costruisci.sh [32|64]     both by default
#
# The XML parser: libexpat.a (-lexpat), expat.h and expat_external.h, and the
# xmlwf program. Apache's apr-util needs it (apr_xml, mod_dav). See
# tools/librerie-exos/comune.sh.
#
# The archive: expat-2.9.0.tar.xz and its .asc from
# https://github.com/libexpat/libexpat/releases (tag R_2_9_0, signed by
# Sebastian Pipping, key 3176EF7DB2367F1FCA4F306B1F9B0E909AF37285); the
# clone libexpat/ in the project follows the same tag.
# =============================================================================
NOME=expat
VER=2.9.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/expat/expat-$VER.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    lib_config_sub
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/configure" --host=$T --build="$("$SRC/conftools/config.guess")" \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" LDFLAGS=-static \
        --disable-shared --enable-static --without-docbook --without-examples \
        --without-tests ) \
        > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j3 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST/lib/cmake" "$DEST"/lib/*.la
    lib_fine
}
lib_tutte "$1"
