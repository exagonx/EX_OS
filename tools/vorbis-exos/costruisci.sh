#!/bin/bash
# =============================================================================
# tools/vorbis-exos/costruisci.sh - libvorbis (Vorbis audio) for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/vorbis-exos/costruisci.sh [32|64]      both by default
#
# See tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: libvorbis-1.3.7.tar.xz from https://downloads.xiph.org/releases/vorbis/
# =============================================================================
NOME=vorbis
VER=1.3.7
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/vorbis/libvorbis-1.3.7.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    # the config.sub of 2020 in this archive has no line lib_config_sub knows:
    # the one of libogg 1.3.6 is the same script, newer
    tar -xOf "$TP/ogg/libogg-1.3.6.tar.xz" libogg-1.3.6/config.sub > "$SRC/config.sub"
    lib_config_sub
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/configure" --host=$T --build=x86_64-pc-linux-gnu \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS -D__unix__ -DEXOS_SOLO_POSIX" \
        CPPFLAGS="-I$CROSS/$T/include" LDFLAGS="-static" \
        --disable-shared --enable-static --disable-oggtest --disable-docs --disable-examples ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j4 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST"/lib/*.la
    lib_condivisa vorbis 0 "-logg -lm"
    lib_condivisa vorbisenc 2 "-lvorbis -logg -lm"
    lib_condivisa vorbisfile 3 "-lvorbis -logg -lm"
    lib_fine
}
lib_tutte "$1"
