#!/bin/sh
# =============================================================================
# tools/librerie-exos/su-cd.sh — the third-party libraries on the tools CD
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/librerie-exos/su-cd.sh <cd tree> 32|64
#
# Called by `make iso` (build/iso, 32 bit) and tools/mktools64.sh
# (build-64/iso). For every library tools/<name>-exos/costruisci.sh built
# (cross_build/<name>-exos<bit>/exos, see tools/librerie-exos/comune.sh):
#
#     exos/lib/*.a, exos/include/...   (and exos/i386-exos/include on the
#                                       32-bit CD: the 32-bit gcc looks there)
#     exos/bin/...                     the programs, stripped
#     exos/src/<name>/                 the upstream archive, its signature,
#                                      the EX-OS build script
#     prova-librerie.cpp               the example (tools/prove/libreprova.cpp)
#
# One group per library in tools/iso/strumenti.txt: [zlib] [bzip2] [xz]
# [lz4] [zstd] [pcre2] [fmt], and [ssl] for OpenSSL (tools/openssl-exos:
# the Makefile and mktools64.sh also build provassl and provatls with it). A library not built is said on stderr, and the
# CD is made without it. ncurses has its own (tools/ncurses-exos/su-cd.sh):
# its terminfo and its links need more care.
# =============================================================================
CD="$1"
BIT="$2"
cd "$(dirname "$0")/../.." || exit 1
case "$BIT" in
    64) STRIP=cross_build/nasi5deb/exos-cross64/bin/x86_64-exos-strip ;;
    32) STRIP=cross_build/nasi5deb/exos-cross/bin/i386-exos-strip ;;
    *) echo "uso: $0 <albero> 32|64" >&2; exit 2 ;;
esac
[ -n "$CD" ] || exit 2
[ -x "$STRIP" ] || STRIP=strip
E="$CD/exos"
fatte=""
for NOME in zlib bzip2 xz lz4 zstd pcre2 fmt openssl; do
    N=cross_build/$NOME-exos$BIT/exos
    if [ ! -d "$N/lib" ]; then
        echo "     $NOME assente: tools/$NOME-exos/costruisci.sh $BIT" >&2
        continue
    fi
    mkdir -p "$E/lib" "$E/include" "$E/src"
    cp -p "$N"/lib/*.a "$E/lib/"
    cp -rp "$N/include/." "$E/include/"
    [ -d "$E/i386-exos/include" ] && cp -rp "$N/include/." "$E/i386-exos/include/"
    if [ -d "$N/bin" ]; then
        mkdir -p "$E/bin"
        for b in "$N"/bin/*; do
            "$STRIP" -o "$E/bin/$(basename "$b")" "$b" && chmod 755 "$E/bin/$(basename "$b")"
        done
    fi
    rm -rf "$E/src/$NOME"
    cp -rp "$N/src/$NOME" "$E/src/"
    fatte="$fatte $NOME"
done
[ -n "$fatte" ] && cp tools/prove/libreprova.cpp "$CD/prova-librerie.cpp"
[ -n "$fatte" ] && echo "     librerie:$fatte (con i sorgenti)"
exit 0
