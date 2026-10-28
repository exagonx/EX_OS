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
# [lz4] [zstd] [pcre2] [fmt], [mariadb-connector] (Connector/C and C++, the
# FreeBASIC header, the Rust crate), [mariadb] (tools/mariadb-exos: also exos/mariadb,
# the server and its share), [expat], [apr] (APR and APR-util), [apache]
# (tools/httpd-exos: exos/apache2, httpd and its tree, mod_php inside), [php]
# (tools/php-exos: exos/php, the CLI, php.ini, libphp.a), and [ssl] for OpenSSL (tools/openssl-exos:
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
# ! mariadb (the server) has no lib/: its client library is
# mariadb-connector-c's (see tools/mariadb-exos/costruisci.sh)
for NOME in zlib bzip2 xz lz4 zstd pcre2 fmt openssl mariadb-connector-c mariadb-connector-cpp mariadb \
        expat apr apr-util httpd php; do
    N=cross_build/$NOME-exos$BIT/exos
    if [ ! -d "$N/src/$NOME" ]; then
        echo "     $NOME assente: tools/$NOME-exos/costruisci.sh $BIT" >&2
        continue
    fi
    mkdir -p "$E/lib" "$E/include" "$E/src"
    if [ -d "$N/lib" ]; then
        # cp -rp: a library's own subdirectories too (lib/rust/mariadb)
        cp -rp "$N/lib/." "$E/lib/"
    fi
    if [ -d "$N/include" ]; then
        cp -rp "$N/include/." "$E/include/"
        # the 32-bit gcc's own include directory; FreeBASIC's .bi stay out
        if [ -d "$E/i386-exos/include" ]; then
            for x in "$N"/include/*; do
                [ "$(basename "$x")" = freebasic ] || cp -rp "$x" "$E/i386-exos/include/"
            done
        fi
    fi
    if [ -d "$N/bin" ]; then
        mkdir -p "$E/bin"
        for b in "$N"/bin/*; do
            # apr-1-config and apu-1-config are scripts: as they are
            "$STRIP" -o "$E/bin/$(basename "$b")" "$b" 2> /dev/null || cp -p "$b" "$E/bin/"
            chmod 755 "$E/bin/$(basename "$b")"
        done
    fi
    # a program with its own tree: exos/mariadb, exos/apache2, exos/php
    for d in mariadb apache2 php; do
        if [ -d "$N/$d" ]; then
            rm -rf "$E/$d"
            cp -rp "$N/$d" "$E/"
        fi
    done
    rm -rf "$E/src/$NOME"
    cp -rp "$N/src/$NOME" "$E/src/"
    fatte="$fatte $NOME"
done
[ -n "$fatte" ] && cp tools/prove/libreprova.cpp "$CD/prova-librerie.cpp"
[ -n "$fatte" ] && echo "     librerie:$fatte (con i sorgenti)"
exit 0
