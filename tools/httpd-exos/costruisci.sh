#!/bin/bash
# =============================================================================
# tools/httpd-exos/costruisci.sh — the Apache HTTP Server for EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/httpd-exos/costruisci.sh [32|64]     both by default
#
# httpd 2.4, one static program with its modules inside, laid out as on
# EX-OS under /exos/apache2 (bin/httpd, the tools ab, htpasswd, htdigest,
# rotatelogs..., conf/, htdocs/, logs/, include/ for modules). See
# tools/librerie-exos/comune.sh.
#
# It wants APR, APR-util and Expat built first, of the same bit width
# (tools/apr-exos, tools/expat-exos, tools/apr-util-exos), and the sysroots
# with PCRE2, OpenSSL and zlib.
#
# The archive: httpd-2.4.69.tar.bz2, .asc and .sha512 from
# https://downloads.apache.org/httpd/ (signed by Eric Covener, KEYS next to
# it); the clone httpd/ in the project follows the same tag.
#
# ! HOW IT RUNS ON EX-OS, WHICH HAS NO fork():
#   - the worker MPM in ONE process: "httpd -X" (or -DONE_PROCESS
#     -DNO_DETACH -DFOREGROUND), a listener thread and ThreadsPerChild
#     threads in the program itself; started without -X it would try to fork
#     its children and stop;
#   - not the event MPM: it wants an apr_pollset that threads can change
#     while another waits (epoll), and EX-OS's epoll takes only TCP sockets
#     while APR adds a pipe to every set (see tools/apr-exos/costruisci.sh);
#   - mod_cgi yes (APR starts the script with spawn_ex), mod_cgid no (it is
#     a forking daemon);
#   - every module static (--enable-mods-static): there is no dlopen of
#     modules in a static program.
# ! Not built: mod_http2 and mod_proxy_http2 (nghttp2), mod_md (curl,
# jansson), mod_brotli, mod_lua, mod_proxy_html and mod_xml2enc (libxml2),
# mod_session_crypto (apr_crypto), the LDAP modules: libraries EX-OS has not
# got yet.
# patch/*.diff, applied to the unpacked tree:
#   senza-crypt.diff  htpasswd and htdbm without crypt() (EX-OS's libc has
#                     none): MD5 ($apr1$, the default), bcrypt and SHA1.
#   worker-ascolto.diff the listener polls 100 ms at a time and looks at
#                     listener_may_exit: a signal does not interrupt accept or
#                     poll on EX-OS, and httpd -k stop left it running.
#   worker-uscita.diff  the worker MPM destroys pruntime before exit: the
#                     recycled request pools were destroyed twice at shutdown
#                     (httpd -k stop died in apr_pool_destroy; see the patch).
#
# ! CROSS COMPILING: gen_test_char makes a header during the build, so it is
# built for the host (CROSS_COMPILE, the way the source allows); pcre2-config
# is a small script here (PCRE2 is in the sysroot, without its config), and
# so is a front for apu-1-config.
# =============================================================================
NOME=httpd
VER=2.4.69
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/httpd/httpd-$VER.tar.bz2

CACHE="ap_cv_void_ptr_lt_long=no ac_cv_func_gettid=no ap_cv_gettid=no
    ac_cv_struct_tm_gmtoff=yes apr_cv_tcp_nodelay_with_cork=no"

costruisci() {
    local APR=$TC/apr-exos$1/exos APU=$TC/apr-util-exos$1/exos EXPAT=$TC/expat-exos$1/exos S f
    [ -x "$APR/bin/apr-1-config" ] || manca "APR a $1 bit" "tools/apr-exos/costruisci.sh $1"
    [ -x "$APU/bin/apu-1-config" ] || manca "APR-util a $1 bit" "tools/apr-util-exos/costruisci.sh $1"
    lib_per "$1"
    lib_sorgente
    for f in "$R"/tools/httpd-exos/patch/*.diff; do
        patch -p1 -d "$SRC" < "$f" > /dev/null || { echo "patch $f"; exit 1; }
    done
    lib_config_sub
    S=$CROSS/$T
    # gen_test_char for the host
    sed -i -e 's#^gen_test_char: \$(gen_test_char_OBJECTS)#gen_test_char: $(top_srcdir)/server/gen_test_char.c#' \
        -e 's#^\t\$(LINK) \$(EXTRA_LDFLAGS) \$(gen_test_char_OBJECTS) \$(EXTRA_LIBS)#\tgcc -DCROSS_COMPILE -o $@ $(top_srcdir)/server/gen_test_char.c#' \
        "$SRC/server/Makefile.in"
    grep -q 'gcc -DCROSS_COMPILE' "$SRC/server/Makefile.in" || { echo "gen_test_char: Makefile.in cambiato"; exit 1; }
    mkdir -p "$B/bin"
    cat > "$B/bin/pcre2-config" <<PCRE
#!/bin/sh
case "\$1" in
    --version) echo 10.49 ;;
    --cflags)  echo "" ;;
    --libs8|--libs) echo "-lpcre2-8" ;;
    --prefix)  echo "$S" ;;
esac
PCRE
    # apu-1-config answers with the paths on EX-OS (/exos/...): unlike APR's
    # it has no cross-compiling mode, so a front turns them into the copy here
    cat > "$B/bin/apu-1-config" <<APU
#!/bin/sh
"$APU/bin/apu-1-config" "\$@" | sed -E "s#(^| |-I|-L)/exos/#\\1$APU/#g"
APU
    chmod +x "$B/bin/pcre2-config" "$B/bin/apu-1-config"
    mkdir -p "$B/obj"
    # shellcheck disable=SC2086
    ( cd "$B/obj" && env $CACHE "$SRC/configure" --host=$T --build="$("$SRC/build/config.guess")" \
        --prefix=/exos/apache2 CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" \
        LDFLAGS="-static -Wl,-u,main" \
        LIBS="-lssl -lcrypto -lexpat -lpthread" \
        --with-apr="$APR/bin/apr-1-config" --with-apr-util="$B/bin/apu-1-config" \
        --with-pcre="$B/bin/pcre2-config" --with-mpm=worker --enable-mpms-shared=no \
        --enable-mods-static=most --enable-so=no --enable-cgi --disable-cgid \
        --enable-ssl --with-ssl="$S" --enable-deflate --with-z="$S" \
        --disable-http2 --disable-proxy-http2 --disable-md --disable-brotli \
        --disable-lua --disable-proxy-html --disable-xml2enc --disable-session-crypto \
        --disable-ldap --disable-authnz-ldap --disable-socache-dc ) \
        > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    # APR's rules name its build files where they are on EX-OS (/exos/build-1)
    sed -i "s#^\\(apr_builddir\\|apr_builders\\)=/exos/build-1#\\1=$APR/build-1#" "$B/obj/build/rules.mk"
    # ! the MPM comes after libmain in the link and wants its mpm_fdqueue.c:
    # with static libraries libmain is named once more after it (libtool
    # keeps only that one, so main() in it is asked for with -u main)
    make -C "$B/obj" -j3 MPM_LIB="server/mpm/worker/libworker.la server/libmain.la" \
        > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 ' error\|Error ' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" MPM_LIB="server/mpm/worker/libworker.la server/libmain.la" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/apache2/man" "$DEST/apache2/manual" "$DEST/apache2/build"
    # the Perl and shell tools that want a Unix around them (apxs builds
    # shared modules, apachectl starts the forking daemon, dbmmanage is Perl)
    rm -f "$DEST"/apache2/bin/{apxs,apachectl,envvars,envvars-std,dbmmanage}
    for f in "$DEST"/apache2/bin/*; do $STRIP "$f"; done
    cp -p "$R/tools/httpd-exos/conf/httpd-exos.conf" "$DEST/apache2/conf/"
    mkdir -p "$DEST/src/httpd/patch"
    cp -p "$R"/tools/httpd-exos/patch/*.diff "$DEST/src/httpd/patch/"
    cp -p "$R/tools/httpd-exos/conf/httpd-exos.conf" "$DEST/src/httpd/"
    lib_fine
}
lib_tutte "$1"
