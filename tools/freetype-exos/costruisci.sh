#!/bin/bash
# =============================================================================
# tools/freetype-exos/costruisci.sh - FreeType for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/freetype-exos/costruisci.sh [32|64]     both by default
#
# The font renderer: libfreetype.so.6 (-lfreetype), the headers in
# include/freetype2 (as everywhere: -I.../include/freetype2). Reads TrueType,
# OpenType, Type 1, WOFF; with zlib, bzip2 and libpng (compressed fonts, PNG
# glyphs). Under Qt (voce 85), and later Pango and GTK 3 (voce 42).
#
# ! WITHOUT HarfBuzz HERE: HarfBuzz is built over FreeType
# (tools/harfbuzz-exos), and FreeType with HarfBuzz would need it first - the
# usual circle. FreeType uses HarfBuzz only for its auto-hinter of complex
# scripts; Qt shapes the text with HarfBuzz itself.
#
# The archive: freetype-2.15.0.tar.xz and its .sig from
# https://download.savannah.gnu.org/releases/freetype/, signed by Werner
# Lemberg (key E30674707856409FF1948010BE6C3AAC63AD8E3F).
# =============================================================================
NOME=freetype
VER=2.15.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/freetype/freetype-$VER.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    lib_config_sub
    mkdir -p "$B/obj"
    # ! the pkg-config of the HOST must not answer for the target: the
    # libraries are named by hand
    ( cd "$B/obj" && PKG_CONFIG=false "$SRC/configure" --host=$T --build=x86_64-pc-linux-gnu \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS -D__unix__ -DEXOS_SOLO_POSIX" \
        CPPFLAGS="-I$CROSS/$T/include" LDFLAGS="-static -L$CROSS/$T/lib" \
        ZLIB_CFLAGS=" " ZLIB_LIBS="-lz" BZIP2_CFLAGS=" " BZIP2_LIBS="-lbz2" \
        LIBPNG_CFLAGS="-I$CROSS/$T/include" LIBPNG_LIBS="-lpng16 -lz" \
        --disable-shared --enable-static --with-zlib=yes --with-bzip2=yes --with-png=yes \
        --with-harfbuzz=no --with-brotli=no --enable-freetype-config=no ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; exit 1; }
    grep -q "png: *yes" "$B/configura.log" || grep -q "libpng.*yes" "$B/configura.log" ||
        { grep -i "png\|zlib\|bzip" "$B/configura.log" | tail; echo "freetype: senza libpng"; exit 1; }
    make -C "$B/obj" -j4 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST"/lib/*.la
    lib_condivisa freetype 6 "-lpng16 -lz -lbz2 -lm"
    lib_fine
}
lib_tutte "$1"
