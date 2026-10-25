#!/bin/bash
# =============================================================================
# tools/ncurses-exos/costruisci.sh — ncurses 6.6 for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/ncurses-exos/costruisci.sh            32 and 64 bit
#     tools/ncurses-exos/costruisci.sh 32|64      one only
#
# The real curses library, for every program that draws a full screen on a
# terminal (MariaDB's client, editors, menus): libncurses.a (and libcurses.a,
# the same), libpanel, libmenu, libform, libncurses++, the headers, the
# terminfo database, and tic, infocmp, tput, clear, reset, tset for EX-OS.
# It needs the termios of kernel 0.279: without it a curses program cannot
# read a key without Enter.
#
# ! NOTHING IS DOWNLOADED. The archive is cross_build/terze-parti/ncurses/
# ncurses-6.6.tar.gz with its .sig (Thomas E. Dickey, key 19882D92DDA4C400
# C22C0D56CC2AF4472167BE03), from https://invisible-island.net/ncurses/ —
# the upstream release, newer than the GitHub mirror's tags. A new release:
# put it there, change VER below.
#
# What changes for EX-OS, all from here (the archive stays as it is):
#   - config.sub learns "exos" (one line, like binutils, make and nasm);
#   - exos.ti, the console's entry "vga", is appended to terminfo.src;
#   - the fallbacks (entries compiled into the library): vga, linux, xterm,
#     xterm-256color, vt100, vt220, ansi, screen, dumb. A program works with
#     no /exos/share/terminfo, as on the CD before the tools are installed;
#   - the build also compiles two helpers for THIS machine (make_hash,
#     make_keys) with the curses.h made for EX-OS: there wint_t comes with
#     <stdio.h>, on glibc only with <wchar.h> (BUILD_CPPFLAGS);
#   - --disable-widec: libncurses, the byte library (6.6 builds the wide
#     ncursesw by default). EX-OS's wchar_t is Latin-1 (lib/include/wchar.h),
#     there is no wcwidth and no UTF-8 locale, and the console is CP437:
#     ncursesw waits for UTF-8 in the libc (in_lavorazione.txt, voce 30);
#   - a native ncurses 6.6 for this Linux first (costruzione-ncurses/host):
#     its tic builds the fallbacks and the database for the cross build.
#
# Results: cross_build/ncurses-exos32 and ncurses-exos64, each with exos/
# laid out as on EX-OS (/exos/include, /exos/lib, /exos/bin,
# /exos/share/terminfo), and the libraries and headers copied into the
# sysroots of both cross toolchains of that architecture (exos-cross* and
# nasi5deb/exos-cross*), so that `-lncurses` works in a cross build.
# The tools CD (make iso, tools/mktools64.sh) takes them from ncurses-exos*.
# =============================================================================
set -e
cd "$(dirname "$0")/../.." || exit 1
R=$PWD
TC=$R/cross_build
VER=6.6
ARC=$TC/terze-parti/ncurses/ncurses-$VER.tar.gz
LAVORO=$TC/costruzione-ncurses
SRC=$LAVORO/ncurses-$VER
HOSTB=$LAVORO/host
QUI=$R/tools/ncurses-exos
FALLBACKS=vga,linux,xterm,xterm-256color,vt100,vt220,ansi,screen,dumb
# The terminfo database on the disk: the terminals people really connect
# with (the console, telnet and ssh clients, terminal multiplexers). All
# lowercase: the full database has both a/ and A/, E/ and e/... and a CD read
# without regard to case (Joliet) would mix them. tic on EX-OS compiles any
# other entry from /exos/src/ncurses/terminfo.src.
VOCI=vga,linux,linux-16color,ansi,dumb,vt52,vt100,vt102,vt220,vt320,xterm,xterm-color,xterm-16color,xterm-256color,xterm-direct,xterm-new,xterm-mono,screen,screen-256color,screen-16color,tmux,tmux-256color,putty,putty-256color,rxvt,rxvt-256color,rxvt-unicode,rxvt-unicode-256color,konsole,konsole-256color,gnome,gnome-256color,st,st-256color,alacritty,foot,cygwin,cons25,pcansi,sun,wsvt25,ms-terminal,vscode

manca() { echo "! manca $1"; echo "    rimedio: $2"; exit 1; }
[ -f "$ARC" ] || manca "$ARC" "wget https://invisible-island.net/archives/ncurses/ncurses-$VER.tar.gz (e .sig) in $(dirname "$ARC")"

# --- the sources, and the two changes -----------------------------------------
if [ ! -f "$SRC/configure" ]; then
    mkdir -p "$LAVORO"
    tar -xzf "$ARC" -C "$LAVORO"
fi
if ! grep -q '| exos\* ' "$SRC/config.sub"; then
    sed -i 's/^\t| mint\* \\$/\t| mint* \\\n\t| exos* \\/' "$SRC/config.sub"
fi
[ "$("$SRC/config.sub" x86_64-exos)" = x86_64-pc-exos ] || { echo "config.sub non riconosce exos"; exit 1; }
cat "$SRC/misc/terminfo.src" "$QUI/exos.ti" > "$LAVORO/terminfo.src"

# --- a native tic, for the fallbacks and the database ---------------------------
if [ ! -x "$HOSTB/inst/bin/tic" ]; then
    echo "=== ncurses $VER nativo (solo per tic e infocmp) ==="
    rm -rf "$HOSTB"; mkdir -p "$HOSTB"
    ( cd "$HOSTB" && "$SRC/configure" --prefix="$HOSTB/inst" --without-shared \
        --without-cxx --without-cxx-binding --without-ada --without-manpages \
        --without-tests --without-debug --disable-db-install \
        --with-database="$LAVORO/terminfo.src" ) > "$HOSTB/configura.log" 2>&1 \
        || { tail -20 "$HOSTB/configura.log"; exit 1; }
    make -C "$HOSTB" -j3 > "$HOSTB/costruisci.log" 2>&1 || { tail -30 "$HOSTB/costruisci.log"; exit 1; }
    make -C "$HOSTB" install > "$HOSTB/installa.log" 2>&1 || { tail -30 "$HOSTB/installa.log"; exit 1; }
fi
TIC=$HOSTB/inst/bin/tic
INFOCMP=$HOSTB/inst/bin/infocmp

costruisci() {
    local BIT=$1 T CROSS ALTRO MARCH B OUT
    case $BIT in
        64) T=x86_64-exos; CROSS=$TC/nasi5deb/exos-cross64; ALTRO=$TC/exos-cross64; MARCH="" ;;
        32) T=i386-exos;   CROSS=$TC/nasi5deb/exos-cross;   ALTRO=$TC/exos-cross
            MARCH="-march=pentium-mmx -mtune=pentium-mmx" ;;
    esac
    B=$LAVORO/exos$BIT
    OUT=$TC/ncurses-exos$BIT
    [ -x "$CROSS/bin/$T-gcc" ] || manca "$T-gcc ($CROSS)" "tools/exilla/leggimi.md (la toolchain con i fili)"
    echo "=== ncurses $VER per $T ==="
    rm -rf "$B" "$OUT"; mkdir -p "$B"
    ( cd "$B" && PATH="$CROSS/bin:$PATH" "$SRC/configure" \
        --host=$T --build="$("$SRC/config.guess")" --prefix=/exos \
        CC=$T-gcc CXX=$T-g++ AR=$T-ar RANLIB=$T-ranlib STRIP=$T-strip \
        BUILD_CC=gcc BUILD_CPPFLAGS="-include wchar.h" CFLAGS="-O2 $MARCH" CXXFLAGS="-O2 $MARCH" LDFLAGS=-static \
        --with-database="$LAVORO/terminfo.src" \
        --with-default-terminfo-dir=/exos/share/terminfo \
        --with-terminfo-dirs=/exos/share/terminfo \
        --with-fallbacks=$FALLBACKS --with-tic-path="$TIC" --with-infocmp-path="$INFOCMP" \
        --without-shared --with-normal --without-debug --without-ada \
        --without-manpages --without-tests --enable-overwrite --disable-widec \
        --disable-stripping --disable-pc-files --disable-home-terminfo \
        --without-pkg-config ) > "$B/configura.log" 2>&1 \
        || { tail -30 "$B/configura.log"; exit 1; }
    PATH="$CROSS/bin:$PATH" make -C "$B" -j3 > "$B/costruisci.log" 2>&1 \
        || { grep -m20 -B3 -A8 'rror' "$B/costruisci.log"; exit 1; }
    PATH="$CROSS/bin:$PATH" make -C "$B" install DESTDIR="$OUT" TIC_PATH="$TIC" \
        > "$B/installa.log" 2>&1 || { tail -30 "$B/installa.log"; exit 1; }

    # the database: only $VOCI (see above), compiled again by the native tic
    rm -rf "$OUT/exos/share/terminfo" "$OUT/exos/lib/terminfo"
    mkdir -p "$OUT/exos/share/terminfo"
    "$TIC" -x -e "$VOCI" -o "$OUT/exos/share/terminfo" "$LAVORO/terminfo.src" \
        > "$B/terminfo.log" 2>&1 || { tail "$B/terminfo.log"; exit 1; }
    # no symbolic links on a CD: copies, and the two converters nobody uses
    # (tic -C and tic -I do the same) go
    rm -f "$OUT/exos/bin/captoinfo" "$OUT/exos/bin/infotocap" "$OUT/exos/bin/ncurses6-config"
    local l
    for l in lib/libcurses.a include/ncurses.h bin/reset; do
        cp --remove-destination "$(readlink -f "$OUT/exos/$l")" "$OUT/exos/$l"
    done

    # into the sysroots: -lncurses (-lcurses) and <curses.h> in a cross build
    local S
    for S in "$CROSS/$T" "$ALTRO/$T"; do
        [ -d "$S/lib" ] || continue
        cp -p "$OUT"/exos/lib/lib*.a "$S/lib/"
        cp -rp "$OUT"/exos/include/. "$S/include/"
    done
    ls "$OUT"/exos/lib "$OUT"/exos/bin | tr '\n' ' '; echo
    echo "    terminfo: $(find "$OUT/exos/share/terminfo" -type f | wc -l) nomi ($(du -sh "$OUT/exos/share/terminfo" | cut -f1))"
}

case "${1:-tutti}" in
    32|64) costruisci "$1" ;;
    tutti) costruisci 32; costruisci 64 ;;
    *) echo "uso: $0 [32|64]" >&2; exit 2 ;;
esac
