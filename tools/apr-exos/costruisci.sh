#!/bin/bash
# =============================================================================
# tools/apr-exos/costruisci.sh — the Apache Portable Runtime for EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/apr-exos/costruisci.sh [32|64]     both by default
#
# libapr-1.a (-lapr-1), the headers in include/apr-1 and apr-1-config, the
# base of apr-util and of the Apache web server (httpd). See
# tools/librerie-exos/comune.sh.
#
# The archive: apr-1.7.6.tar.bz2 and its .asc from
# https://downloads.apache.org/apr/ (signed by Eric Covener; the KEYS file
# next to it); the clone apr/ in the project follows the same tag. The
# release, not the clone: its configure is already made (the clone needs
# buildconf, that is autoconf and the libtool program).
#
# patch/*.diff, applied to the unpacked tree:
#   proc-spawn.diff   apr_proc_create with spawn_ex (EX-OS has no fork: see
#                     the comment in the patch); apr_proc_detach does not fork
#                     (an EX-OS daemon is spawned apart already);
#                     apr_proc_fork stays as it is and answers ENOSYS.
#
# ! CROSS COMPILING: configure cannot run its test programs, so the answers
# are given (the CACHE list below), each one what EX-OS really does.
# ! epoll IS NOT USED by apr_pollset/apr_pollcb (apr_cv_epoll=no): EX-OS's
# epoll takes only TCP sockets, and APR puts a pipe in every wakeable set
# (httpd's event MPM does). The poll() backend takes every descriptor.
# ! WITHOUT IPv6 (--disable-ipv6): ip.drv speaks IPv4 only; the headers have
# sockaddr_in6, so APR would believe the opposite.
# ! RANDOM BYTES FROM arc4random_buf, not getrandom: getrandom answers EAGAIN
# when asked often (the kernel counts its entropy), arc4random never fails.
# =============================================================================
NOME=apr
VER=1.7.6
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/apr/apr-$VER.tar.bz2

CACHE="ac_cv_file__dev_zero=yes ac_cv_func_setpgrp_void=yes
    apr_cv_process_shared_works=no apr_cv_mutex_robust_shared=no
    apr_cv_tcp_nodelay_with_cork=no apr_cv_mutex_recursive=yes
    ac_cv_func_sem_open=no ac_cv_func_shm_open=no
    apr_cv_epoll=no apr_cv_epoll_create1=no apr_cv_sock_cloexec=no
    apr_cv_dup3=no apr_cv_accept4=no ac_cv_struct_rlimit=yes
    apr_cv_gai_addrconfig=no apr_cv_negative_eai=yes
    ac_cv_strerror_r_rc_int=yes apr_cv_hostbyname_is_thread_safe=no
    ac_cv_func_getrandom=no ac_cv_have_decl_SYS_getrandom=no
    apr_cv_getaddrinfo=yes ac_cv_working_getaddrinfo=yes ac_cv_working_getnameinfo=yes"

costruisci() {
    local f
    lib_per "$1"
    lib_sorgente
    for f in "$R"/tools/apr-exos/patch/*.diff; do
        patch -p1 -d "$SRC" < "$f" > /dev/null || { echo "patch $f"; exit 1; }
    done
    lib_config_sub
    case $BIT in
        64) CACHE64="ac_cv_sizeof_struct_iovec=16" ;;
        32) CACHE64="ac_cv_sizeof_struct_iovec=8" ;;
    esac
    mkdir -p "$B/obj"
    # shellcheck disable=SC2086
    ( cd "$B/obj" && env $CACHE $CACHE64 "$SRC/configure" --host=$T \
        --build="$("$SRC/build/config.guess")" --prefix=/exos \
        CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" LDFLAGS=-static \
        CC_FOR_BUILD=gcc --disable-shared --enable-static --enable-threads \
        --without-devrandom --disable-dso --disable-ipv6 ) \
        > "$B/configura.log" 2>&1 || { tail -20 "$B/configura.log"; exit 1; }
    make -C "$B/obj" -j3 > "$B/costruisci.log" 2>&1 || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    make -C "$B/obj" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    rm -rf "$DEST/lib/pkgconfig" "$DEST"/lib/*.la "$DEST/lib/apr.exp"
    mkdir -p "$DEST/src/apr/patch"
    cp -p "$R"/tools/apr-exos/patch/*.diff "$DEST/src/apr/patch/"
    lib_fine
}
lib_tutte "$1"
