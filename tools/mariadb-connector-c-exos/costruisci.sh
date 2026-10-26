#!/bin/bash
# =============================================================================
# tools/mariadb-connector-c-exos/costruisci.sh — MariaDB Connector/C for EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mariadb-connector-c-exos/costruisci.sh [32|64]      both by default
#
# libmariadbclient.a, the C API of MariaDB (<mysql.h>: mysql_real_connect,
# mysql_query, prepared statements), for C programs that talk to a MariaDB
# or MySQL server; Connector/C++ (tools/mariadb-connector-cpp-exos) and the
# FreeBASIC header are built on it. Headers in include/mariadb and the same
# in include/mysql (programs write either <mysql.h> with -I or
# <mysql/mysql.h>), mariadb_config; freebasic/mariadb.bi (FreeBASIC) and
# rust/ (the "mariadb" crate) over it. Group [mariadb-connector] of the tools
# ISO and netinst (the user, 26 October 2026: the connectors on their own,
# and with the server). See tools/librerie-exos/comune.sh for where
# everything ends up.
#
# The archive: mariadb-connector-c-3.4.11-src.tar.gz and its .asc from
# https://archive.mariadb.org/connector-c-3.4.11/ (MariaDB Signing Key
# 177F4010FE56CA3336300305F1656F24C74CD1D8), in
# cross_build/terze-parti/mariadb-connector-c.
#
# ! STATIC, EVERY PLUGIN INSIDE: EX-OS programs are static. The
# authentication plugins (ed25519, caching_sha2, sha256, dialog, parsec,
# clear password) and zstd compression go into the library
# (CLIENT_PLUGIN_*=STATIC); remote_io wants curl, which EX-OS has not.
# libmariadb always builds its .so: tools/mariadb-exos/patch/
# libmariadb-senza-so.diff (the server's copy of Connector/C is the same
# tree) puts it under DISABLE_SHARED. TLS is OpenSSL 4.1.0, compression the
# EX-OS zlib.
# =============================================================================
NOME=mariadb-connector-c
VER=3.4.11
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/mariadb-connector-c/mariadb-connector-c-$VER-src.tar.gz
PATCH=$R/tools/mariadb-exos/patch/libmariadb-senza-so.diff

costruisci() {
    local S
    lib_per "$1"
    for l in ssl:openssl z:zlib zstd:zstd; do
        [ -f "$CROSS/$T/lib/lib${l%%:*}.a" ] || manca "lib${l%%:*}.a ($CROSS/$T)" "tools/${l#*:}-exos/costruisci.sh $BIT"
    done
    lib_sorgente
    git -C "$SRC" apply "$PATCH" || { echo "! $PATCH non si applica"; exit 1; }
    lib_cmake_toolchain
    echo 'set(ENV{PKG_CONFIG_LIBDIR} /nessuno)' >> "$B/exos.cmake"
    # -Werror is on in a release build, and OpenSSL 4.1 marks deprecated the
    # X509_check_host/X509_check_ip_asc it verifies the server's name with:
    # they are there and work, the warning stays a warning
    sed -i 's|-DEXOS_SOLO_POSIX")$|-DEXOS_SOLO_POSIX -Wno-error=deprecated-declarations")|' "$B/exos.cmake"
    PKG_CONFIG_LIBDIR=/nessuno cmake -S "$SRC" -B "$B/obj" -DCMAKE_TOOLCHAIN_FILE="$B/exos.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/exos \
        -DINSTALL_LIBDIR=lib -DINSTALL_INCLUDEDIR=include/mariadb -DINSTALL_BINDIR=bin \
        -DINSTALL_PLUGINDIR=lib/mariadb/plugin -DDISABLE_SHARED=ON \
        -DWITH_UNIT_TESTS=OFF -DWITH_CURL=OFF -DWITH_SSL=OPENSSL -DWITH_EXTERNAL_ZLIB=ON \
        -DCLIENT_PLUGIN_DIALOG=STATIC -DCLIENT_PLUGIN_CLIENT_ED25519=STATIC \
        -DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC -DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC \
        -DCLIENT_PLUGIN_PARSEC=STATIC -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC \
        -DCLIENT_PLUGIN_ZSTD=STATIC -DCLIENT_PLUGIN_REMOTE_IO=OFF \
        > "$B/configura.log" 2>&1 || { grep -B2 -A8 'CMake Error' "$B/configura.log" | head -40; exit 1; }
    make -C "$B/obj" -j"$(nproc)" > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/lib/pkgconfig" "$DEST/lib/mariadb" "$DEST/share" "$DEST/man"
    # the same headers as <mysql/...>, the name the server's install uses
    mkdir -p "$DEST/include/mysql"
    cp -rp "$DEST/include/mariadb/." "$DEST/include/mysql/"
    for S in "$DEST"/bin/*; do $STRIP "$S"; done
    # mariadb_config says -lmariadb: a copy, not the symlink cmake makes (the
    # CD and netinst carry files)
    rm -f "$DEST/lib/libmariadb.a"
    cp -p "$DEST/lib/libmariadbclient.a" "$DEST/lib/libmariadb.a"
    lib_fine "$PATCH"
    # after lib_fine, which copies include/ into the gcc sysroots: the
    # FreeBASIC header (fbc looks in /exos/include/freebasic) and the Rust
    # crate, both over this library (see their own heads)
    mkdir -p "$DEST/include/freebasic" "$DEST/lib/rust/mariadb/src"
    cp -p "$(dirname "$0")/freebasic/mariadb.bi" "$DEST/include/freebasic/"
    cp -p "$(dirname "$0")/rust/Cargo.toml" "$DEST/lib/rust/mariadb/"
    cp -p "$(dirname "$0")/rust/src/lib.rs" "$DEST/lib/rust/mariadb/src/"
    cp -rp "$(dirname "$0")/freebasic" "$(dirname "$0")/rust" "$DEST/src/$NOME/"
}
lib_tutte "$1"
