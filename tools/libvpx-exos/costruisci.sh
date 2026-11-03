#!/bin/bash
# =============================================================================
# tools/libvpx-exos/costruisci.sh - libvpx (VP8 and VP9 video) for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/libvpx-exos/costruisci.sh [32|64]      both by default
#
# See tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: v1.17.0.tar.gz from https://github.com/webmproject/libvpx/tags
# ! The target says linux: libvpx has no idea of EX-OS, and it only wants a
# gcc, nasm and POSIX threads. The assembly chooses the CPU at run time, so the
# 32-bit library still runs on the base CPU.
# =============================================================================
NOME=libvpx
VER=1.17.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/libvpx/v$VER.tar.gz

costruisci() {
    lib_per "$1"
    lib_sorgente
    mkdir -p "$B/obj"
    case $BIT in 64) BERSAGLIO=x86_64-linux-gcc ;; 32) BERSAGLIO=x86-linux-gcc ;; esac
    ( cd "$B/obj" && CROSS=$T- CFLAGS="$MARCH -D__unix__ -DEXOS_SOLO_POSIX" \
        CXXFLAGS="$MARCH -D__unix__ -DEXOS_SOLO_POSIX" "$SRC/configure" --target=$BERSAGLIO \
        --prefix=/exos --as=nasm --enable-static --disable-shared --disable-examples \
        --disable-tools --disable-docs --disable-unit-tests --enable-vp8 --enable-vp9 \
        --enable-runtime-cpu-detect --enable-pic --disable-install-bins ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j6 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/lib/pkgconfig"
    lib_condivisa vpx 12 "-lm"
    lib_fine
}
lib_tutte "$1"
