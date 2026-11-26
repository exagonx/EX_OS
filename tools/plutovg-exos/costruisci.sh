#!/bin/bash
# =============================================================================
# tools/plutovg-exos/costruisci.sh — plutovg (2D vector graphics) for EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/plutovg-exos/costruisci.sh [32|64]   both by default
#
# libplutovg.so.1 and libplutovg.a with <plutovg/plutovg.h>: paths, fills,
# strokes with joins and caps, linear and radial gradients, matrices,
# clipping, antialiasing, on an ARGB32 premultiplied surface; it reads SVG
# path data (plutovg_path_parse) and TrueType through stb_truetype. What it
# is for: debug/implementazione_BROWSER002.txt, P10 - EXBrowser's rounded
# corners, shadows and gradients, <canvas> 2D and inline SVG. EXBrowser opens
# it with dlopen; without it a page draws as before.
#
# The archive: plutovg-1.3.3.tar.gz, the GitHub archive of tag v1.3.3
# (commit bbd91f0d06a71491691b36330f29dffa4af87ccf), checked against a clone
# of the tag; its sha256 next to it. License MIT (the FreeType rasterizer
# inside, plutovg-ft-*, under the FreeType License, FTL.TXT).
#
# ! NO MESON/CMAKE: CMakeLists.txt compiles source/*.c with PLUTOVG_BUILD;
# the same here. No threads.h on EX-OS: the font face cache's lock is not
# compiled (HAVE_THREADS_H unset), and the cache of loaded faces is off
# (PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD), as upstream allows. The soname is
# ours (upstream builds none): libplutovg.so.1, its major version.
# =============================================================================
NOME=plutovg
VER=1.3.3
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/plutovg/plutovg-$VER.tar.gz

costruisci() {
    local f o lista=""
    lib_per "$1"
    lib_sorgente
    : > "$B/costruisci.log"
    mkdir -p "$B/obj"
    for f in "$SRC"/source/*.c; do
        o="$B/obj/$(basename "$f" .c).o"
        # shellcheck disable=SC2086
        $CC $CFLAGS -std=gnu11 -DPLUTOVG_BUILD -DPLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD \
            -I"$SRC/include" -I"$SRC/source" -c "$f" -o "$o" >> "$B/costruisci.log" 2>&1 \
            || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
        lista="$lista $o"
    done
    rm -f "$DEST/lib/libplutovg.a"
    # shellcheck disable=SC2086
    $AR rcs "$DEST/lib/libplutovg.a" $lista
    mkdir -p "$DEST/include/plutovg"
    cp -p "$SRC/include/plutovg.h" "$DEST/include/plutovg/"
    lib_condivisa plutovg 1 "-lm"
    lib_fine
}
lib_tutte "$1"
