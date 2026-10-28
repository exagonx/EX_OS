#!/bin/bash
# =============================================================================
# tools/php-exos/costruisci.sh — PHP for EX-OS, the CLI and the Apache module
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/php-exos/costruisci.sh [32|64]     both by default
#
# PHP 8.5 thread safe (ZTS), laid out as on EX-OS under /exos/php:
#     bin/php            the command line, one static program
#     lib/libphp.a       the engine with its extensions and the Apache 2
#                        handler (everything of mod_php but the module
#                        record), for tools/httpd-exos/costruisci.sh
#     lib/mod_php.c      the module record, compiled into httpd
#     include/php/       the headers, for mod_php and for extensions
#     etc/php.ini        the settings
#
# ! THE SOURCES COME FROM THE CLONE php-src/ in the project (the user, 28
# October 2026: every piece from its public GitHub sources, so that an update
# brings the sources along), the release tag php-$VER taken with git archive.
# The signed archive php-$VER.tar.xz from php.net is in
# cross_build/terze-parti/php/ with its .asc and .sha256: the clone was checked
# against it (the same files, the archive only adds the 28 generated ones) and
# it goes on the CD next to the binaries. A tree from git has no configure and
# no parsers: buildconf makes the first, bison and re2c the others during the
# build, so the host needs re2c (cross_build/re2c-host, built from its GitHub
# clone in cross_build/terze-parti/re2c, tag 4.6).
#
# ! HOW IT SITS IN APACHE: httpd on EX-OS is one static program in one process
# with the worker MPM (threads), and there is no dlopen of modules. So PHP is
# thread safe (--enable-zts, TSRM on the libc's pthread keys and __thread),
# built as the static embed library, and the four files of
# sapi/apache2handler are compiled here against the httpd headers: the
# module record (mod_php.c) goes into httpd with --with-module, the rest into
# libphp.a. Order: httpd, then PHP, then httpd again (it finds libphp.a and
# takes the module in).
#
# Extensions: Core, date, hash, json, pcre, random, reflection, spl, standard,
# uri (always there), opcache (always there since 8.5, without JIT), ctype,
# filter, mbstring (without mbregex: no oniguruma yet), mysqlnd, mysqli,
# pdo, pdo_mysql (on mysqlnd: no Connector/C), openssl, session, tokenizer,
# zlib, posix.
# ! Not built: everything on libxml2 (dom, xml, simplexml, xmlreader,
# xmlwriter, soap), sqlite3 and pdo_sqlite, curl, gd, intl, iconv, sodium,
# zip...: libraries EX-OS has not got yet. phar, fpm and phpdbg: fpm and
# phpdbg fork, phar wants nothing but is left for later. sockets: the
# libc has no SO_DONTROUTE, SO_SNDLOWAT, SO_RCVLOWAT yet.
# patch/*.diff, applied to the tree from git:
#   senza-st_rdev.diff      EX-OS's struct stat has no st_rdev (no device
#                           files): stat() gives rdev -1.
#   apache-un-processo.diff httpd -X: mod_php's child shutdown destroyed TSRM
#                           and the server shutdown, in the same process,
#                           used it (httpd -k stop died in ts_free_id).
# ! EX-OS has no /tmp: php.ini sends temporary files, sessions, uploads and
# opcache's lock file to /exos/php/tmp.
# =============================================================================
NOME=php
VER=8.5.12
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/php/php-$VER.tar.xz
RE2C=$TC/re2c-host/bin/re2c

# answers configure cannot find by running a test program on the host
CACHE="ac_cv_func_dlopen=no ac_cv_lib_dl_dlopen=no ac_cv_func_fork=no
    ac_cv_func_vfork=no php_cv_func_getaddrinfo=yes ac_cv_func_getaddrinfo=yes"

costruisci() {
    local HTTPD=$TC/httpd-exos$1/exos/apache2 APR=$TC/apr-exos$1/exos APU=$TC/apr-util-exos$1/exos S f o
    [ -x "$RE2C" ] || manca "re2c per l'host ($RE2C)" "vedi il commento in testa a $0"
    [ -d "$R/php-src/.git" ] || manca "il clone php-src/" "git clone https://github.com/php/php-src.git php-src"
    [ -f "$HTTPD/include/httpd.h" ] || manca "httpd a $1 bit" "tools/httpd-exos/costruisci.sh $1"
    lib_per "$1"
    SRC=$B/src/php-$VER
    mkdir -p "$SRC"
    git -C "$R/php-src" archive "php-$VER" | tar -x -C "$SRC"
    for f in "$R"/tools/php-exos/patch/*.diff; do
        [ -f "$f" ] || continue
        patch -p1 -d "$SRC" < "$f" > /dev/null || { echo "patch $f"; exit 1; }
    done
    ( cd "$SRC" && ./buildconf --force ) > "$B/buildconf.log" 2>&1 || { tail -20 "$B/buildconf.log"; exit 1; }
    lib_config_sub
    S=$CROSS/$T
    mkdir -p "$B/obj"
    # shellcheck disable=SC2086
    ( cd "$B/obj" && env $CACHE "$SRC/configure" --host=$T --build="$("$SRC/build/config.guess")" \
        --prefix=/exos/php --with-config-file-path=/exos/php/etc \
        CC=$CC CXX=$CXX AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" RE2C="$RE2C" \
        LDFLAGS="-static" LIBS="-lpthread" \
        PCRE2_CFLAGS="-I$S/include" PCRE2_LIBS="-lpcre2-8" \
        OPENSSL_CFLAGS="-I$S/include" OPENSSL_LIBS="-lssl -lcrypto" \
        ZLIB_CFLAGS="-I$S/include" ZLIB_LIBS="-lz" \
        --enable-zts --enable-embed=static --disable-cgi --disable-phpdbg \
        --disable-all --without-pear --disable-opcache-jit \
        --with-external-pcre --without-pcre-jit \
        --enable-ctype --enable-filter --enable-mbstring --disable-mbregex \
        --enable-mysqlnd --with-mysqli=mysqlnd --enable-pdo --with-pdo-mysql=mysqlnd \
        --with-openssl --enable-session --enable-tokenizer --with-zlib \
        --enable-posix ) \
        > "$B/configura.log" 2>&1 || { tail -30 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j3 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 ' error\|Error ' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install-cli install-sapi install-headers INSTALL_ROOT="$OUT" \
        > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    # the man page (EX-OS has no man) and the build tools for phpize (there
    # are no shared extensions on EX-OS)
    rm -rf "$DEST/php/php" "$DEST/php/lib/php/build" "$DEST/php/bin/phpize" "$DEST/php/bin/php-config"
    # the Apache 2 handler, against the httpd and APR headers of this width
    mkdir -p "$B/apache2"
    for f in sapi_apache2 apache_config php_functions; do
        $CC $CFLAGS -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1 \
            -I"$B/obj/main" -I"$B/obj/Zend" -I"$B/obj" \
            -I"$SRC" -I"$SRC/main" -I"$SRC/Zend" -I"$SRC/TSRM" -I"$SRC/ext" -I"$SRC/sapi/apache2handler" \
            -I"$HTTPD/include" -I"$APR/include/apr-1" -I"$APU/include/apr-1" \
            -c "$SRC/sapi/apache2handler/$f.c" -o "$B/apache2/$f.o" \
            || { echo "apache2handler: $f.c"; exit 1; }
    done
    $AR rs "$DEST/php/lib/libphp.a" "$B"/apache2/*.o
    # mod_php.c with its header beside it, as httpd's --with-module wants
    cp -p "$SRC/sapi/apache2handler/mod_php.c" "$SRC/sapi/apache2handler/php_apache.h" "$DEST/php/lib/"
    # the libraries libphp.a needs after it, for httpd's link
    sed -n 's/^EXTRA_LIBS = //p' "$B/obj/Makefile" > "$DEST/php/lib/libphp.libs"
    mkdir -p "$DEST/php/etc" "$DEST/php/tmp"
    # a file in it, so that the directory exists on the CD and in netinst
    # (empty directories are not carried) - opcache will not start without it
    printf 'PHP temporary files: sessions, uploads, the opcache lock.\n' > "$DEST/php/tmp/LEGGIMI.txt"
    cp -p "$R/tools/php-exos/php.ini" "$DEST/php/etc/php.ini"
    $STRIP "$DEST/php/bin/php"
    mkdir -p "$DEST/src/php"
    cp -p "$TP/php/php-$VER.tar.xz.asc" "$TP/php/php-$VER.tar.xz.sha256" "$R/tools/php-exos/php.ini" "$DEST/src/php/"
    for f in "$R"/tools/php-exos/patch/*.diff; do [ -f "$f" ] && cp -p "$f" "$DEST/src/php/"; done
    lib_fine
}
lib_tutte "$1"
