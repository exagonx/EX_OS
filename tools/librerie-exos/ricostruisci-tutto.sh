#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/librerie-exos/ricostruisci-tutto.sh — every port again, in order
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/librerie-exos/ricostruisci-tutto.sh            32 and 64 bit
#     tools/librerie-exos/ricostruisci-tutto.sh 64         one only
#     DA=ffmpeg tools/librerie-exos/ricostruisci-tutto.sh  from that one on
#
# After the libc of the sysroots changes (18 November 2026: <stdio_ext.h>,
# st_mtim, wcswidth, O_EXCL since 0.330): a port compiled before keeps the
# old libc and the old headers inside (a static libc, constants such as
# O_EXCL compiled in). Each tools/<name>-exos/costruisci.sh in the order of
# who needs whom, the log in cross_build/ricostruzione/<name>-<bit>.log; the
# first failure stops it, and says where.
#
# ! NOT HERE: Exilla (Firefox, hours: tools/exilla), the compilers that run
# on EX-OS (binutils, gcc, make, nasm, rust, freebasic: their own scripts),
# libgit2 (64 bit only, its own usage). The tools CD is made after
# (make iso, tools/mktools64.sh).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
ORDINE="zlib bzip2 xz lz4 zstd pcre2 expat fmt openssl ncurses nano libpng freetype harfbuzz
        ogg vorbis opus flac libvpx dav1d ffmpeg mariadb-connector-c mariadb-connector-cpp
        mariadb apr apr-util httpd php valkey mesa qt5 vlc"
L=${EXOS_FUORI}/cross_build/ricostruzione
mkdir -p "$L"
BITS=${1:-"32 64"}
via=${DA:+no}
for n in $ORDINE; do
    [ "$via" = no ] && { [ "$n" = "$DA" ] && via=; [ -n "$via" ] && continue; }
    for b in $BITS; do
        if pgrep -af "[r]epo-update" > /dev/null; then
            echo "! repo-update sta pubblicando: fermo prima di $n $b"; exit 1
        fi
        printf '%-24s %s bit ... ' "$n" "$b"
        inizio=$SECONDS
        if tools/$n-exos/costruisci.sh "$b" > "$L/$n-$b.log" 2>&1; then
            echo "ok ($((SECONDS - inizio)) s)"
        else
            echo "NO: $L/$n-$b.log"; tail -15 "$L/$n-$b.log"
            echo "riparti da qui: DA=$n $0 $*"
            exit 1
        fi
    done
done
echo "=== tutto ricostruito ==="
