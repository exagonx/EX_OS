#!/bin/bash
# =============================================================================
# tools/valkey-exos/costruisci.sh — Valkey (the BSD Redis) for EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/valkey-exos/costruisci.sh [32|64]     both by default
#
# The in-memory database that speaks the Redis protocol, laid out as on
# EX-OS under /exos/valkey: bin/valkey-server, valkey-cli, valkey-benchmark,
# valkey-check-rdb, valkey-check-aof, valkey-sentinel; etc/valkey.conf;
# dati/ for the snapshots (dump.rdb) and the AOF. See
# tools/librerie-exos/comune.sh.
#
# Valkey, not Redis (the user, 5 November 2026): Redis 8 is AGPLv3 / RSAL /
# SSPL; Valkey is the BSD-3 line of Redis 7.2 (Linux Foundation), the same
# protocol, the same RDB/AOF, the same BGSAVE, and redis-cli and every Redis
# client work with it.
#
# The archive: valkey-9.1.3.tar.gz from
# https://github.com/valkey-io/valkey/archive/refs/tags/9.1.3.tar.gz, checked
# with the sha256 of https://github.com/valkey-io/valkey-hashes (the
# .sha256 next to it).
#
# ! WHY IT IS HERE (voce 33 i): BGSAVE and BGREWRITEAOF fork the server, and
# the child writes the snapshot of the memory it was born with while the
# parent goes on serving (copy on write, kernel 0.313). It is the test of
# fork under a real load.
#
# ! HOW IT IS BUILT:
#   - MALLOC=libc: jemalloc wants madvise/MADV_DONTNEED and its own page
#     maps; the libc's malloc is the one every EX-OS program uses;
#   - the event loop is select (ae_select.c): HAVE_EPOLL is set only for
#     __linux__, and Valkey puts pipes in its loop (the child's info pipe),
#     which EX-OS's epoll does not take (TCP sockets only);
#   - -D__unix__ -DEXOS_SOLO_POSIX, as for the CMake ports: libvalkey picks
#     pthread_once by __unix__, and libc.h keeps its own names out;
#   - BUILD_TLS=no (OpenSSL is there, it can come later), no systemd;
#   - -O2 without LTO: one less thing between the cross compiler and the
#     result;
#   - uname_S=EXOS: none of Valkey's per-system branches (Linux, Darwin,
#     SunOS...) applies.
# The libc needed for it (5 November 2026): pthread_cancel, glob, fnmatch,
# regex.h, IOV_MAX, ctime_r, strndup, asprintf, bzero, ESOCKTNOSUPPORT, and
# BYTE_ORDER from <sys/types.h>. See README, "Valkey".
# =============================================================================
NOME=valkey
VER=9.1.3
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/valkey/valkey-$VER.tar.gz

costruisci() {
    local M V f
    lib_per "$1"
    lib_sorgente
    case $BIT in 64) M=x86_64 ;; 32) M=i386 ;; esac
    V="$DEST/valkey"
    # shellcheck disable=SC2086
    make -C "$SRC" -j3 MALLOC=libc CC="$CC -D__unix__ -DEXOS_SOLO_POSIX $MARCH" \
        AR=$AR RANLIB=$RANLIB uname_S=EXOS uname_M=$M BUILD_TLS=no USE_SYSTEMD=no \
        OPTIMIZATION=-O2 valkey-server valkey-cli valkey-benchmark \
        > "$B/costruisci.log" 2>&1 || { grep -a -m10 -B2 -A5 ' error\|Error ' "$B/costruisci.log"; exit 1; }
    mkdir -p "$V/bin" "$V/etc" "$V/dati"
    for f in valkey-server valkey-cli valkey-benchmark; do
        cp "$SRC/src/$f" "$V/bin/"
        $STRIP "$V/bin/$f"
    done
    # ! COPIES, NOT LINKS: Valkey chooses what to be by the name it was
    # started with, and EX-OS has no symbolic links
    for f in valkey-check-rdb valkey-check-aof valkey-sentinel; do
        cp "$V/bin/valkey-server" "$V/bin/$f"
    done
    # the configuration of the release, with the EX-OS paths: IPv4 only (no
    # "-::1"), the data in /exos/valkey/dati, the pid file there too
    sed -e 's#^bind 127.0.0.1 -::1#bind 127.0.0.1#' \
        -e 's#^pidfile /var/run/valkey_6379.pid#pidfile /exos/valkey/dati/valkey.pid#' \
        -e 's#^dir \./#dir /exos/valkey/dati#' \
        "$SRC/valkey.conf" > "$V/etc/valkey.conf"
    grep -q '^dir /exos/valkey/dati' "$V/etc/valkey.conf" || { echo "valkey.conf: dir non trovato"; exit 1; }
    cp "$SRC/sentinel.conf" "$V/etc/"
    cp "$SRC/COPYING" "$V/"
    : > "$V/dati/.tieni"                # the repository carries files, not folders
    lib_fine
}
lib_tutte "$1"
