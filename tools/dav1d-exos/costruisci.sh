#!/bin/bash
# =============================================================================
# tools/dav1d-exos/costruisci.sh - dav1d (AV1 video) for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/dav1d-exos/costruisci.sh [32|64]      both by default
#
# See tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: dav1d-1.5.4.tar.xz with .sha256 from
# https://download.videolan.org/pub/videolan/dav1d/1.5.4/
# ! Meson is told the system is linux, as CMake is in comune.sh.
# =============================================================================
NOME=dav1d
VER=1.5.4
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/dav1d/dav1d-$VER.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    case $BIT in 64) F=x86_64 ;; 32) F=x86 ;; esac
    # ! the threads are in libc: this gcc has no -pthread, and Meson adds it
    sed -i "s/dependency('threads')/declare_dependency()/" "$SRC/meson.build"
    cat > "$B/exos.meson" <<FINE
[binaries]
c = '$T-gcc'
ar = '$T-ar'
strip = '$T-strip'
nasm = 'nasm'
[built-in options]
c_args = [$(for x in $MARCH -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE; do printf "'%s'," "$x"; done)]
c_link_args = ['-static']
[host_machine]
system = 'linux'
cpu_family = '$F'
cpu = '${T%-exos}'
endian = 'little'
FINE
    meson setup "$B/obj" "$SRC" --cross-file "$B/exos.meson" --prefix=/exos --libdir=lib \
        --default-library=static --buildtype=release -Denable_tools=false -Denable_tests=false \
        -Denable_examples=false > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    ninja -C "$B/obj" -j6 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    DESTDIR="$OUT" ninja -C "$B/obj" install > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/lib/pkgconfig"
    lib_fine
}
lib_tutte "$1"
