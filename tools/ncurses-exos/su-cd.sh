#!/bin/sh
# =============================================================================
# tools/ncurses-exos/su-cd.sh — ncurses on the tools CD
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/ncurses-exos/su-cd.sh <cd tree> 32|64
#
# Called by `make iso` (build/iso, 32 bit) and tools/mktools64.sh
# (build-64/iso): the same layout on both CDs, group [curses] of
# tools/iso/strumenti.txt. From what tools/ncurses-exos/costruisci.sh left
# in cross_build/ncurses-exos32 (64):
#
#     exos/lib/       libncurses.a libcurses.a libpanel.a libmenu.a
#                     libform.a libncurses++.a
#     exos/include/   curses.h ncurses.h term.h panel.h menu.h form.h ...
#     exos/share/terminfo/   the database (vga, xterm, vt100, screen...)
#     exos/bin/       tic infocmp toe tput tset reset clear tabs (stripped)
#     exos/src/ncurses/      the upstream archive and its signature, and the
#                            EX-OS part: exos.ti, costruisci.sh, terminfo.src
#     prova-curses.c  the example (tools/prove/cursesprova.c)
#
# The sources go with the binaries (the user, 25 October 2026): whoever has
# the CD, or installs [curses] with netupdate, has what they were built from.
# Missing build: a line on stderr, the CD is made without ncurses.
# =============================================================================
CD="$1"
BIT="$2"
cd "$(dirname "$0")/../.." || exit 1
N=cross_build/ncurses-exos$BIT/exos
case "$BIT" in
    64) STRIP=cross_build/nasi5deb/exos-cross64/bin/x86_64-exos-strip ;;
    32) STRIP=cross_build/nasi5deb/exos-cross/bin/i386-exos-strip ;;
    *) echo "uso: $0 <albero> 32|64" >&2; exit 2 ;;
esac
[ -x "$STRIP" ] || STRIP=strip
if [ ! -f "$N/lib/libncurses.a" ] || [ -z "$CD" ]; then
    echo "     ncurses assente: tools/ncurses-exos/costruisci.sh $BIT" >&2
    exit 0
fi
E="$CD/exos"
mkdir -p "$E/lib" "$E/include" "$E/share" "$E/bin" "$E/src/ncurses"
cp -p "$N"/lib/*.a "$E/lib/"
cp -rp "$N/include/." "$E/include/"
# the 32-bit gcc looks for headers in exos/i386-exos/include (where the
# Makefile puts the libc's too); the catalogue names them with `file`
[ -d "$E/i386-exos/include" ] && cp -rp "$N/include/." "$E/i386-exos/include/"
rm -rf "$E/share/terminfo"
cp -rp "$N/share/terminfo" "$E/share/"
for b in "$N"/bin/*; do
    "$STRIP" -o "$E/bin/$(basename "$b")" "$b" && chmod 755 "$E/bin/$(basename "$b")"
done
VER=$(sed -n 's/^VER=//p' tools/ncurses-exos/costruisci.sh)
cp -p cross_build/terze-parti/ncurses/ncurses-$VER.tar.gz \
      cross_build/terze-parti/ncurses/ncurses-$VER.tar.gz.sig \
      tools/ncurses-exos/exos.ti tools/ncurses-exos/costruisci.sh "$E/src/ncurses/"
[ -f cross_build/costruzione-ncurses/terminfo.src ] && \
    cp -p cross_build/costruzione-ncurses/terminfo.src "$E/src/ncurses/"
cp tools/prove/cursesprova.c "$CD/prova-curses.c"
echo "     ncurses $VER: $(ls "$N/lib" | tr '\n' ' ')+ $(find "$E/share/terminfo" -type f | wc -l) voci terminfo e i sorgenti"
