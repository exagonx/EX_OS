#!/bin/bash
# =============================================================================
# tools/openssl-exos/costruisci.sh — OpenSSL for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/openssl-exos/costruisci.sh [32|64]      both by default
#
# libcrypto.a and libssl.a with the openssl/*.h headers (-lssl -lcrypto), and
# the openssl program. See tools/librerie-exos/comune.sh for where everything
# ends up; the same tree also stays in cross_build/openssl-build-exos (64)
# for what used it before (the Makefile, mktools64.sh, libgit2): the two
# archives and include/ in one place.
#
# The archive: openssl-4.1.0.tar.gz with .asc and .sha256 from
# https://github.com/openssl/openssl/releases (signed by OpenSSL
# <openssl@openssl.org>, C46ED3F2CBEFDA1FDAADA44264ED7B1DCCE71CB2; the keys
# at https://openssl-library.org/source/pubkeys.asc).
#
# ! WHAT CHANGED FROM prepara-openssl.sh (the user, 25 October 2026: MariaDB
# needs OpenSSL with sockets and threads; every library the latest public
# release). That script built a snapshot of the development branch with
# no-sock no-threads no-stdio: TLS only through two memory BIOs, as
# tools/iso/prova-tls.c does. The libc has had BSD sockets, poll, select and
# pthreads since, so here they are on: SSL_set_fd on a socket works, the
# `openssl` program builds (it needs stdio and sockets: s_client, req, x509,
# dgst...), and the locks are real (thread_scheme pthreads in 50-exos.conf).
# The memory BIOs still work as before. Still off:
#
#   no-shared no-dso no-module   static programs only
#   no-asm                       see 50-exos.conf (the 32-bit base CPU has
#                                no SSE2)
#   no-engine                    gone in 4.x anyway
#   no-tests                     test programs, not the library
#   no-afalgeng no-ktls          Linux kernel interfaces
#   no-ui-console                the passphrase prompt (needs a terminal
#                                with echo off; termios has it now, later)
#   no-autoload-config           openssl.cnf is not read unless asked for
#
# --with-rand-seed=getrandom as before: the libc's getentropy(), backed by
# kernel/arch/x86/entropia.c.
# =============================================================================
NOME=openssl
VER=4.1.0
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/openssl/openssl-$VER.tar.gz

costruisci() {
    lib_per "$1"
    lib_sorgente
    local OB=$TC/openssl-build-exos BERSAGLIO=exos-x86
    [ "$BIT" = 64 ] && { OB=$TC/openssl-build-exos64; BERSAGLIO=exos-x86_64; }
    cp "$R/tools/openssl-exos/50-exos.conf" "$SRC/Configurations/"
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/Configure" $BERSAGLIO $MARCH \
        --prefix=/exos --openssldir=/exos/ssl --libdir=lib \
        --with-rand-seed=getrandom \
        no-shared no-dso no-module no-asm no-engine no-tests \
        no-afalgeng no-ktls no-ui-console no-autoload-config ) \
        > "$B/configura.log" 2>&1 || { tail -30 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j3 build_sw > "$B/costruisci.log" 2>&1 || {
        grep -m10 -B2 -A5 -E 'rror|undefined reference' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install_sw DESTDIR="$B/inst" > "$B/installa.log" 2>&1 || {
        tail -20 "$B/installa.log"; exit 1; }
    cp -p "$B"/inst/exos/lib/libcrypto.a "$B"/inst/exos/lib/libssl.a "$DEST/lib/"
    cp -rp "$B/inst/exos/include/openssl" "$DEST/include/"
    cp -p "$B/inst/exos/bin/openssl" "$DEST/bin/"
    # the old place, for what still looks there
    rm -rf "$OB"
    mkdir -p "$OB"
    cp -p "$DEST"/lib/*.a "$OB/"
    cp -rp "$DEST/include" "$OB/"
    lib_fine "$R/tools/openssl-exos/50-exos.conf"
}
lib_tutte "$1"
