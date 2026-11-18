#!/bin/bash
# =============================================================================
# tools/nano-exos/costruisci.sh — GNU nano 9.3 for EX-OS, 32 and 64 bit
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/nano-exos/costruisci.sh            32 and 64 bit
#     tools/nano-exos/costruisci.sh 32|64      one only
#
# The text editor of every Linux, on ncurses (tools/ncurses-exos, built
# first: libncurses.a in the sysroots).
#
# The archive: cross_build/terze-parti/nano/nano-9.3.tar.xz with its .sig,
# from https://ftp.gnu.org/gnu/nano/ (tools/aggiorna-sorgenti.sh -scarica
# nano), signed by Benno Schulenberg, key 168E6F4297BFD7A79AFD4496514BBE2EB8E1961F
# (in the GNU keyring; a copy in benno-schulenberg-key.asc next to it).
#
# ! HOW IT IS BUILT:
#   - no UTF-8 (--disable-utf8): libncurses is the byte library and the
#     libc has no UTF-8 locale (voce 30), the console is CP437;
#   - ! NOT THE HOST'S pkg-config: it answered for this Linux's ncurses
#     ("-lncurses -ltinfo", Debian's split); ours is one libncurses.a;
#   - no NLS and no libmagic (neither exists on EX-OS);
#   - gnulib fills what the libc lacks; what configure decides by running a
#     program it cannot run here is guessed with --enable-cross-guesses=risky
#     (the answer of a POSIX system);
#   - ! THE LIBC OF TODAY, NOT THE SYSROOT'S: nano's gnulib needs
#     <stdio_ext.h> (__fseterr, __fpending...: 18 November 2026), and the
#     sysroot's libc is older. lib_libc_di_oggi (comune.sh) compiles
#     lib/libc.c here, and nano links to it (-static) with the headers of
#     lib/include in front (struct stat with st_mtim is of the same day);
#   - /exos/nano/nanorc is the system nanorc, /exos/nano/share/nano the
#     syntax files (include them from nanorc, as on Linux).
# Result: cross_build/nano-exos<bit>/exos with bin/nano and nano/.
# =============================================================================
NOME=nano
VER=9.3
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/nano/nano-$VER.tar.xz

costruisci() {
    lib_per "$1"
    lib_sorgente
    lib_config_sub
    lib_libc_di_oggi
    ( cd "$B" && "$SRC/configure" --host=$T --build="$("$SRC/build-aux/config.guess")" \
        --prefix=/exos --sysconfdir=/exos/nano --datarootdir=/exos/nano/share \
        CC=$CC AR=$AR RANLIB=$RANLIB CFLAGS="$CFLAGS" CPPFLAGS="-I$B/libc/include" \
        LDFLAGS="-static -L$B/libc" PKG_CONFIG=false NCURSES_CFLAGS="" NCURSES_LIBS=-lncurses \
        --disable-utf8 --disable-nls --disable-libmagic --enable-cross-guesses=risky \
        --disable-year2038 ) > "$B/configura.log" 2>&1 || { tail -30 "$B/configura.log"; exit 1; }
    make -C "$B" -j3 > "$B/costruisci.log" 2>&1 || { grep -a -m10 -B3 -A6 'error' "$B/costruisci.log"; exit 1; }
    make -C "$B" install DESTDIR="$OUT" > "$B/installa.log" 2>&1 || { tail -20 "$B/installa.log"; exit 1; }
    $STRIP "$DEST/bin/nano"
    # the manuals and the HTML stay out (no man on EX-OS); rnano is nano -R
    rm -rf "$DEST/nano/share/man" "$DEST/nano/share/info" "$DEST/nano/share/doc" "$DEST/bin/rnano"
    cp "$SRC/COPYING" "$DEST/nano/"
    # the system nanorc: the release's sample with the syntax files of EX-OS
    # (configure wrote it in the build tree, with the paths of /exos)
    sed 's|^# include /exos/nano/share/nano/\*\.nanorc|include /exos/nano/share/nano/*.nanorc|' \
        "$B/doc/sample.nanorc" > "$DEST/nano/nanorc"
    grep -q '^include /exos/nano/share/nano/' "$DEST/nano/nanorc" || { echo "nanorc: include non trovato"; exit 1; }
    lib_fine
}
lib_tutte "$1"
