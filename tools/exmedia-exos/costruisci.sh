#!/bin/bash
# =============================================================================
# tools/exmedia-exos/costruisci.sh — libexmedia.so.1, EXBrowser's media over FFmpeg
#
# Copyright (C) 2026 Graziano Falcone
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exmedia-exos/costruisci.sh [32|64]   both by default
#
# exmedia.c and exmedia.h, next to this: a small library over FFmpeg 8
# (tools/ffmpeg-exos, which must be built first) for <video>, <audio> and
# Media Source Extensions (debug/implementazione_BROWSER002.txt, P13).
# EXBrowser opens it with dlopen; FFmpeg's structures stay in here.
# =============================================================================
NOME=exmedia
VER=1
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$R/tools/exmedia-exos/exmedia.c

costruisci() {
    lib_per "$1"
    [ -f "$CROSS/$T/include/libavformat/avformat.h" ] || manca "FFmpeg per $T" "tools/ffmpeg-exos/costruisci.sh $1"
    : > "$B/costruisci.log"
    mkdir -p "$B/obj"
    # shellcheck disable=SC2086
    $CC $CFLAGS -std=gnu11 -Wall -D__unix__ -DEXOS_SOLO_POSIX -I"$CROSS/$T/include" \
        -c "$R/tools/exmedia-exos/exmedia.c" -o "$B/obj/exmedia.o" >> "$B/costruisci.log" 2>&1 \
        || { cat "$B/costruisci.log"; exit 1; }
    rm -f "$DEST/lib/libexmedia.a"
    $AR rcs "$DEST/lib/libexmedia.a" "$B/obj/exmedia.o"
    cp -p "$R/tools/exmedia-exos/exmedia.h" "$DEST/include/"
    lib_condivisa exmedia 1 "-lavformat -lavcodec -lswscale -lswresample -lavutil -lm"
    lib_fine "$R/tools/exmedia-exos/exmedia.h"
}
lib_tutte "$1"
