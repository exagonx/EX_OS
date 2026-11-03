#!/bin/bash
# =============================================================================
# tools/ffmpeg-exos/costruisci.sh - FFmpeg for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/ffmpeg-exos/costruisci.sh [32|64]      both by default
#
# See tools/librerie-exos/comune.sh for where everything ends up.
#
# The archive: ffmpeg-9.0.2.tar.xz with .asc from https://ffmpeg.org/releases/
# (FFmpeg release signing key FCF986EA15E6E293A5644F10B4322F04D67658D8).
#
# LGPL build: no --enable-gpl and no x264 or x265. Decoders of its own for
# H.264, HEVC, VP8, VP9, AAC, MP3, Vorbis, Opus, FLAC; dav1d for AV1; libvpx,
# libvorbis and libopus for encoding WebM. The tools/<lib>-exos/costruisci.sh
# of dav1d, libvpx, ogg, vorbis and opus go first. Patent licensing of H.264,
# HEVC and AAC is a matter for whoever redistributes the binaries.
# ! pkg-config: the libraries live in the sysroots without .pc files, so the
# build writes its own into $B/pc.
# =============================================================================
NOME=ffmpeg
VER=9.0.2
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/ffmpeg/ffmpeg-$VER.tar.xz

pc() {  # name version libs
    printf 'Name: %s\nDescription: %s\nVersion: %s\nLibs: %s\nCflags: %s\n' "$1" "$1" "$2" "$3" "$4" > "$B/pc/$1.pc"
}

costruisci() {
    lib_per "$1"
    lib_sorgente
    # ! EX-OS's stdlib.h declares close(): a static function of that name clashes
    sed -i -E -e 's/void close\(/void apv_chiudi(/' -e 's/= close,/= apv_chiudi,/' "$SRC/libavcodec/apv_parser.c"
    mkdir -p "$B/obj" "$B/pc"
    pc dav1d 1.5.4 "-ldav1d -lpthread -lm"
    pc ogg 1.3.6 "-logg"
    pc opus 1.6.1 "-lopus -lm" "-I$CROSS/$T/include/opus"
    pc vorbis 1.3.7 "-lvorbis -logg -lm"
    pc vorbisenc 1.3.7 "-lvorbisenc -lvorbis -logg -lm"
    pc vpx 1.17.0 "-lvpx -lm -lpthread"
    case $BIT in
        64) ARCH=x86_64; CPU="" ;;
        32) ARCH=i386; CPU="--cpu=i586 --disable-inline-asm" ;;
    esac
    ( cd "$B/obj" && PKG_CONFIG_LIBDIR="$B/pc" "$SRC/configure" --enable-cross-compile \
        --cross-prefix=$T- --arch=$ARCH --target-os=linux $CPU --prefix=/exos \
        --pkg-config=pkg-config --pkg-config-flags=--static \
        --extra-cflags="$MARCH -D__unix__ -DEXOS_SOLO_POSIX -D_GNU_SOURCE" --extra-ldflags=-static \
        --enable-static --disable-shared --disable-autodetect --disable-doc --disable-debug \
        --disable-ffplay --disable-network --disable-devices \
        --enable-pthreads --enable-zlib --enable-libdav1d --enable-libvpx \
        --enable-libvorbis --enable-libopus ) > "$B/configura.log" 2>&1 \
        || { tail -20 "$B/configura.log"; tail -30 "$B/obj/ffbuild/config.log"; exit 1; }
    make -C "$B/obj" -j8 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig"
    lib_fine
}
lib_tutte "$1"
