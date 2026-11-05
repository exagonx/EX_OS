#!/bin/bash
# =============================================================================
# tools/libpng-exos/costruisci.sh - libpng for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/libpng-exos/costruisci.sh [32|64]     both by default
#
# The PNG library: libpng16.so.16 (-lpng16, and -lpng through the ld script),
# png.h, pngconf.h, pnglibconf.h. Under FreeType (colour emoji, PNG glyphs)
# and Qt (QImage reads PNG; voce 85). Over zlib. See
# tools/librerie-exos/comune.sh.
#
# The archive: libpng-1.6.59.tar.xz from
# https://sourceforge.net/projects/libpng/files/libpng16/1.6.59/ (the link of
# libpng.org), md5 checked against the one SourceForge publishes
# (fce35ac83494252c178249c3aad9621e), sha256 kept next to it.
# =============================================================================
NOME=libpng
VER=1.6.59
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/libpng/libpng-$VER.tar.xz

costruisci() {
    local hw=""
    lib_per "$1"
    lib_sorgente
    lib_config_sub
    # the base 32-bit CPU is a Pentium MMX: no SSE2 code paths there
    [ "$BIT" = 32 ] && hw="--enable-hardware-optimizations=no"
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/configure" --host=$T --build=x86_64-pc-linux-gnu \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS -D__unix__ -DEXOS_SOLO_POSIX" \
        CPPFLAGS="-I$CROSS/$T/include" LDFLAGS="-static -L$CROSS/$T/lib" \
        --disable-shared --enable-static --disable-tests --disable-tools $hw ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j4 libpng16.la > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install-libLTLIBRARIES install-pkgincludeHEADERS install-nodist_pkgincludeHEADERS \
        install-header-links DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST"/lib/*.la "$DEST/lib/libpng.a"
    # header links are symbolic links on Linux: EX-OS has none
    for f in "$DEST"/include/*.h; do [ -L "$f" ] && cp --remove-destination "$(readlink -f "$f")" "$f"; done
    lib_condivisa png16 16 "-lz -lm"
    lib_per_ld "$DEST/lib" png libpng16.so.16
    lib_fine
}
lib_tutte "$1"
