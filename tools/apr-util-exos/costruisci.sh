#!/bin/bash
# =============================================================================
# tools/apr-util-exos/costruisci.sh — APR-util for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/apr-util-exos/costruisci.sh [32|64]     both by default
#
# libaprutil-1.a (-laprutil-1), its headers in include/apr-1 next to APR's,
# and apu-1-config: buckets and brigades, apr_xml (on Expat), apr_uri,
# apr_md5/sha1, apr_dbm with SDBM, apr_memcache, apr_redis, the base of the
# Apache web server. See tools/librerie-exos/comune.sh.
#
# It wants APR and Expat built first, of the same bit width:
#     tools/apr-exos/costruisci.sh; tools/expat-exos/costruisci.sh
#
# The archive: apr-util-1.6.5.tar.bz2 and its .asc from
# https://downloads.apache.org/apr/ (signed by Eric Covener, KEYS in
# cross_build/terze-parti/apr); the clone apr-util/ in the project follows
# the same tag.
#
# patch/*.diff, applied to the unpacked tree:
#   senza-crypt.diff  EX-OS's libc has no crypt(): apr_password_validate
#                     keeps the hashes APR-util has inside ($apr1$ MD5,
#                     bcrypt, SHA1), as on Windows and Android.
#
# ! WITHOUT the database drivers (MariaDB, SQLite, PostgreSQL, ODBC), LDAP,
# Berkeley DB/GDBM and apr_crypto: each one is a plugin loaded with dlopen in
# apr-util, and they are the next step (MariaDB's Connector/C is already on
# EX-OS); apr_dbm keeps SDBM, which is inside.
# =============================================================================
NOME=apr-util
VER=1.6.5
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/apr-util/apr-util-$VER.tar.bz2

costruisci() {
    local f APR=$TC/apr-exos$1/exos EXPAT=$TC/expat-exos$1/exos
    [ -x "$APR/bin/apr-1-config" ] || manca "APR a $1 bit" "tools/apr-exos/costruisci.sh $1"
    [ -f "$EXPAT/lib/libexpat.a" ] || manca "Expat a $1 bit" "tools/expat-exos/costruisci.sh $1"
    lib_per "$1"
    lib_sorgente
    for f in "$R"/tools/apr-util-exos/patch/*.diff; do
        patch -p1 -d "$SRC" < "$f" > /dev/null || { echo "patch $f"; exit 1; }
    done
    lib_config_sub
    mkdir -p "$B/obj"
    ( cd "$B/obj" && "$SRC/configure" --host=$T --build="$("$SRC/build/config.guess")" \
        --prefix=/exos CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" LDFLAGS=-static \
        --with-apr="$APR/bin/apr-1-config" --with-expat="$EXPAT" \
        --without-crypto --without-openssl --without-nss --without-commoncrypto \
        --without-ldap --without-berkeley-db --without-gdbm --without-ndbm \
        --without-pgsql --without-mysql --without-sqlite2 --without-sqlite3 \
        --without-oracle --without-odbc --without-iconv --disable-util-dso ) \
        > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    # APR's rules name its build files where they are on EX-OS (/exos/build-1)
    sed -i "s#^\(apr_builddir\|apr_builders\)=/exos/build-1#\1=$APR/build-1#" "$B/obj/build/rules.mk"
    make -C "$B/obj" -j3 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 ' error\|Error ' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/lib/pkgconfig" "$DEST"/lib/*.la "$DEST/lib/aprutil.exp"
    mkdir -p "$DEST/src/apr-util/patch"
    cp -p "$R"/tools/apr-util-exos/patch/*.diff "$DEST/src/apr-util/patch/"
    lib_fine
}
lib_tutte "$1"
