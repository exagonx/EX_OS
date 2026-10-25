#!/bin/bash
# =============================================================================
# tools/pcre2-exos/costruisci.sh — PCRE2 for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/pcre2-exos/costruisci.sh [32|64]     both by default
#
# Perl-compatible regular expressions: libpcre2-8.a (-lpcre2-8, 8-bit
# strings, what MariaDB's REGEXP uses), libpcre2-16.a, libpcre2-32.a,
# libpcre2-posix.a (regcomp/regexec on top), pcre2.h and pcre2posix.h, and
# the pcre2grep and pcre2test programs. See tools/librerie-exos/comune.sh.
#
# The archive: pcre2-10.49.tar.bz2 and its .sig from
# https://github.com/PCRE2Project/pcre2/releases (signed by Nicholas Wilson,
# key A95536204A3BB48971523128 2A98E77EB6F24CA8).
#
# ! WITHOUT THE JIT. It writes machine code into memory and runs it: that
# needs memory both writable and executable, or mprotect between the two,
# and nothing on EX-OS has been tried that way yet. The interpreter gives
# the same results, slower; --enable-jit is the step to try later.
# =============================================================================
NOME=pcre2
VER=10.49
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/pcre2/pcre2-$VER.tar.bz2

costruisci() {
    lib_per "$1"
    lib_sorgente
    lib_config_sub
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/configure" --host=$T --build="$("$SRC/config.guess")" \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" LDFLAGS=-static \
        --disable-shared --enable-static --enable-pcre2-16 --enable-pcre2-32 \
        --disable-jit --disable-pcre2grep-libz --disable-pcre2grep-libbz2 \
        --disable-pcre2test-libreadline --disable-pcre2test-libedit ) \
        > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j3 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/share" "$DEST/lib/pkgconfig" "$DEST"/lib/*.la "$DEST/bin/pcre2-config"
    lib_fine
}
lib_tutte "$1"
