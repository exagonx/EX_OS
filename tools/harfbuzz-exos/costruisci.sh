#!/bin/bash
# =============================================================================
# tools/harfbuzz-exos/costruisci.sh - HarfBuzz for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/harfbuzz-exos/costruisci.sh [32|64]     both by default
#
# The text shaper: which glyph, where, for a string of a given script and
# font. libharfbuzz.so.0 (-lharfbuzz) over FreeType, libharfbuzz-subset.so.0,
# headers in include/harfbuzz. Qt shapes all its text with it (voce 85), and
# Pango does for GTK 3 (voce 42).
#
# Built with Meson (tools/librerie-exos/comune.sh lib_meson_cross): FreeType
# is found through a .pc written here for the sysroot (lib_pc), never the
# host's pkg-config, and --wrap-mode=nodownload: nothing is fetched during a
# build. No GLib, Cairo, ICU, graphite (not on EX-OS yet), no tests or
# utilities; the newer raster/vector/gpu libraries are left out (nobody asks
# for them). with_libstdcxx=false is upstream's default: HarfBuzz uses no
# libstdc++ at all, so it does not depend on libstdc++.so.
#
# The archive: harfbuzz-15.0.0.tar.xz from
# https://github.com/harfbuzz/harfbuzz/releases/tag/15.0.0, sha256 equal to
# the digest of the release (968536e9...a44df).
# =============================================================================
NOME=harfbuzz
VER=15.0.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/harfbuzz/harfbuzz-$VER.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    lib_pc "$B/pc" freetype2 26.5.20 "-I$CROSS/$T/include/freetype2 -I$CROSS/$T/include" "-L$CROSS/$T/lib -lfreetype"
    lib_meson_cross "$B/pc"
    meson setup "$B/obj" "$SRC" --cross-file "$B/exos.meson" --prefix=/exos --libdir=lib \
        --buildtype=release --wrap-mode=nodownload -Ddefault_library=static \
        -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dchafa=disabled \
        -Dicu=disabled -Dpng=disabled -Dzlib=disabled -Draster=disabled -Dvector=disabled -Dgpu=disabled \
        -Dsubset=enabled -Dtests=disabled -Dintrospection=disabled -Ddocs=disabled -Dutilities=disabled \
        -Dbenchmark=disabled > "$B/configura.log" 2>&1 || { tail -30 "$B/configura.log"; exit 1; }
    grep -q "FreeType *: YES\|freetype.*: YES" "$B/configura.log" ||
        { grep -i freetype "$B/configura.log"; echo "harfbuzz: senza FreeType"; exit 1; }
    ninja -C "$B/obj" -j4 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    DESTDIR="$OUT" meson install -C "$B/obj" --no-rebuild > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    # meson puts it in $OUT/exos already (DESTDIR + prefix)
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST/lib/cmake"
    lib_condivisa harfbuzz 0 "-lfreetype -lm"
    lib_condivisa harfbuzz-subset 0 "-lharfbuzz -lm"
    lib_fine
}
lib_tutte "$1"
