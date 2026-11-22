#!/bin/sh
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/librerie-exos/su-cd.sh — the third-party libraries on the tools CD
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/librerie-exos/su-cd.sh <cd tree> 32|64
#
# Called by `make iso` (build/iso, 32 bit) and tools/mktools64.sh
# (build-64/iso). For every library tools/<name>-exos/costruisci.sh built
# (cross_build/<name>-exos<bit>/exos, see tools/librerie-exos/comune.sh):
#
#     exos/lib/*.a, exos/include/...   (and exos/i386-exos/include on the
#                                       32-bit CD: the 32-bit gcc looks there)
#     exos/bin/...                     the programs, stripped
#     exos/src/<name>/                 the upstream archive, its signature,
#                                      the EX-OS build script
#     prova-librerie.cpp               the example (tools/prove/libreprova.cpp)
#
# One group per library in tools/iso/strumenti.txt: [zlib] [bzip2] [xz]
# [lz4] [zstd] [pcre2] [fmt], [libpng] [freetype] [harfbuzz] (for Qt 5), [ogg] [vorbis] [opus] [flac] [libvpx] [dav1d] [ffmpeg] [vlc], [mariadb-connector] (Connector/C and C++, the
# FreeBASIC header, the Rust crate), [mariadb] (tools/mariadb-exos: also exos/mariadb,
# the server and its share), [expat], [apr] (APR and APR-util), [apache]
# (tools/httpd-exos: exos/apache2, httpd and its tree, mod_php inside), [php]
# (tools/php-exos: exos/php, the CLI, php.ini, libphp.a), [valkey]
# (tools/valkey-exos: exos/valkey, the server, valkey-cli, the config), [qt5], [nano]
# (tools/nano-exos: exos/bin/nano, exos/nano with nanorc and the syntax), [mesa] (tools/mesa-exos:
# libEGL, libGLESv2 and provaegl), and [ssl] for OpenSSL (tools/openssl-exos:
# the Makefile and mktools64.sh also build provassl and provatls with it). A library not built is said on stderr, and the
# CD is made without it. [qt5] (tools/qt5-exos) is its own tree, exos/qt5:
# the libraries, the plugins, the headers and the fonts; what is only for
# building Qt itself (cmake, pkgconfig, .prl, .la, the support archives) stays
# out. ncurses has its own (tools/ncurses-exos/su-cd.sh):
# its terminfo and its links need more care.
# =============================================================================
CD="$1"
BIT="$2"
cd "$(dirname "$0")/../.." || exit 1
. tools/mancanti.sh
case "$BIT" in
    64) STRIP=${EXOS_FUORI}/cross_build/nasi5deb/exos-cross64/bin/x86_64-exos-strip ;;
    32) STRIP=${EXOS_FUORI}/cross_build/nasi5deb/exos-cross/bin/i386-exos-strip ;;
    *) echo "uso: $0 <albero> 32|64" >&2; exit 2 ;;
esac
[ -n "$CD" ] || exit 2
[ -x "$STRIP" ] || STRIP=strip
E="$CD/exos"
fatte=""
# ! mariadb (the server) has no lib/: its client library is
# mariadb-connector-c's (see tools/mariadb-exos/costruisci.sh)
# ! libpng, freetype AND harfbuzz (20 November 2026): Qt 5 links them since
# voce 85, but they never reached the CD, and a VLC installed by netupdate
# said "libQt5Widgets.so.5 ... not found", then libzstd, then libpng16. The
# VLC tests copied them by hand, so nobody saw it.
for NOME in zlib bzip2 xz lz4 zstd pcre2 fmt libpng freetype harfbuzz openssl mariadb-connector-c mariadb-connector-cpp mariadb \
        expat apr apr-util httpd php valkey nano ogg vorbis opus flac libvpx dav1d ffmpeg vlc mesa; do
    N=${EXOS_FUORI}/cross_build/$NOME-exos$BIT/exos
    if [ ! -d "$N/src/$NOME" ]; then
        manca "$NOME a $BIT bit" "tools/$NOME-exos/costruisci.sh $BIT"
        continue
    fi
    mkdir -p "$E/lib" "$E/include" "$E/src"
    if [ -d "$N/lib" ]; then
        # cp -rp: a library's own subdirectories too (lib/rust/mariadb);
        # --remove-destination: an old link is replaced, not written through
        cp -rp --remove-destination "$N/lib/." "$E/lib/"
    fi
    if [ -d "$N/include" ]; then
        cp -rp --remove-destination "$N/include/." "$E/include/"
        # the 32-bit gcc's own include directory; FreeBASIC's .bi stay out
        if [ -d "$E/i386-exos/include" ]; then
            for x in "$N"/include/*; do
                [ "$(basename "$x")" = freebasic ] || cp -rp --remove-destination "$x" "$E/i386-exos/include/"
            done
        fi
    fi
    if [ -d "$N/bin" ]; then
        mkdir -p "$E/bin"
        for b in "$N"/bin/*; do
            # apr-1-config and apu-1-config are scripts: as they are
            "$STRIP" -o "$E/bin/$(basename "$b")" "$b" 2> /dev/null || cp -p "$b" "$E/bin/"
            chmod 755 "$E/bin/$(basename "$b")"
        done
    fi
    # a program with its own tree: exos/mariadb, exos/apache2, exos/php,
    # exos/valkey, exos/nano
    for d in mariadb apache2 php valkey nano; do
        if [ -d "$N/$d" ]; then
            rm -rf "$E/$d"
            cp -rp "$N/$d" "$E/"
            # ! AN EMPTY DIRECTORY GETS A FILE (5 November 2026): the network
            # repository carries files, not directories, so apache2/logs - empty
            # until httpd runs - never reached a machine installed with
            # netupdate, and httpd stopped at once: "AH02291: Cannot access
            # directory '/exos/apache2/logs/'" (seen on the CQ60). `.tieni` is
            # an ordinary file every netupdate already knows how to bring.
            find "$E/$d" -type d -empty -exec sh -c 'echo "EX-OS: this directory must exist (netupdate brings files only)" > "$1/.tieni"' _ {} \;
        fi
    done
    rm -rf "$E/src/$NOME"
    cp -rp "$N/src/$NOME" "$E/src/"
    fatte="$fatte $NOME"
done
# Qt 5: /exos/qt5 (its prefix), the sources in exos/src/qt5
Q=${EXOS_FUORI}/cross_build/qt5-exos$BIT/exos
if [ -f "$Q/qt5/lib/libQt5Core.so.5" ] && [ -d "$Q/src/qt5" ]; then
    rm -rf "$E/qt5" "$E/src/qt5"
    mkdir -p "$E/src"
    cp -rp "$Q/qt5" "$E/"
    rm -rf "$E/qt5/lib/cmake" "$E/qt5/lib/pkgconfig" "$E/qt5/lib/metatypes" "$E/qt5/mkspecs"
    rm -f "$E"/qt5/lib/*.prl "$E"/qt5/lib/*.la "$E"/qt5/lib/*.a
    cp -rp "$Q/src/qt5" "$E/src/"
    fatte="$fatte qt5"
else
    manca "qt5 a $BIT bit" "tools/qt5-exos/costruisci.sh $BIT"
fi
# VLC from the desktop (voce 79): the launcher the menu entry starts, and the
# icons (tools/vlc-exos/icone, from VLC's own share/icons/256x256/vlc.png).
# Group [vlc] of the catalogue: solo = bin/vlcapri, solo = share/vlc.
if [ -x ${EXOS_FUORI}/cross_build/vlc-exos$BIT/exos/bin/vlc ]; then
    if [ "$BIT" = 64 ]; then L=${EXOS_FUORI}/build-64/exwin/bin/vlcapri; else L=${EXOS_FUORI}/build/vlcapri/vlcapri; fi
    if [ -f "$L" ]; then
        "$STRIP" -o "$E/bin/vlcapri" "$L" 2> /dev/null || cp -p "$L" "$E/bin/vlcapri"
        chmod 755 "$E/bin/vlcapri"
    else
        echo "     vlcapri assente ($L): VLC senza voce di menu che funzioni" >&2
    fi
    mkdir -p "$E/share/vlc"
    cp tools/vlc-exos/icone/vlc_64.ico tools/vlc-exos/icone/vlc_128.ico "$E/share/vlc/"
fi
[ -n "$fatte" ] && cp tools/prove/libreprova.cpp "$CD/prova-librerie.cpp"
[ -n "$fatte" ] && echo "     librerie:$fatte (con i sorgenti)"
exit 0
