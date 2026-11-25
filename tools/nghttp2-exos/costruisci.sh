#!/bin/bash
# =============================================================================
# tools/nghttp2-exos/costruisci.sh — nghttp2 (the library) for EX-OS, 32 and 64
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/nghttp2-exos/costruisci.sh [32|64]   both by default
#
# libnghttp2.so.14 and libnghttp2.a with <nghttp2/nghttp2.h>: HTTP/2 framing,
# HPACK, streams and flow control. It reads and writes bytes in memory and
# never touches a socket: the transport stays exhttp's (TLS of lib/extls).
# What it is for: debug/implementazione_BROWSER002.txt, P4. exhttp.so opens
# it with dlopen the first time an https connection is made, and offers "h2"
# in ALPN only if it is there.
#
# The archive: nghttp2-1.71.0.tar.xz with .asc (GPG, Tatsuhiro Tsujikawa,
# primary key F4F3B91474D1EB29889BD0EF7E8403D5D673C366) and .sha256, from
# https://github.com/nghttp2/nghttp2/releases. License MIT.
#
# ! ONLY lib/, AND NO CONFIGURE: the library is plain C99 and needs three
# answers from the system, given here as -D: <arpa/inet.h> and
# <netinet/in.h> (htonl and friends) and clock_gettime(CLOCK_MONOTONIC) (the
# rate limits of nghttp2_time.c), all in the EX-OS libc. nghttp2ver.h is in
# the release archive already. The soname is upstream's: LT_CURRENT -
# LT_AGE = 44 - 30 = 14 (configure.ac).
# =============================================================================
NOME=nghttp2
VER=1.71.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/nghttp2/nghttp2-$VER.tar.xz

costruisci() {
    local f o lista=""
    lib_per "$1"
    lib_sorgente
    : > "$B/costruisci.log"
    mkdir -p "$B/obj"
    for f in "$SRC"/lib/*.c; do
        o="$B/obj/$(basename "$f" .c).o"
        # shellcheck disable=SC2086
        $CC $CFLAGS -DBUILDING_NGHTTP2 -DHAVE_ARPA_INET_H=1 -DHAVE_NETINET_IN_H=1 \
            -DHAVE_CLOCK_GETTIME=1 -DHAVE_DECL_CLOCK_MONOTONIC=1 \
            -I"$SRC/lib" -I"$SRC/lib/includes" -c "$f" -o "$o" >> "$B/costruisci.log" 2>&1 \
            || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
        lista="$lista $o"
    done
    # shellcheck disable=SC2086
    $AR rcs "$DEST/lib/libnghttp2.a" $lista
    mkdir -p "$DEST/include/nghttp2"
    cp -p "$SRC/lib/includes/nghttp2/nghttp2.h" "$SRC/lib/includes/nghttp2/nghttp2ver.h" \
        "$DEST/include/nghttp2/"
    lib_condivisa nghttp2 14
    lib_fine
}
lib_tutte "$1"
