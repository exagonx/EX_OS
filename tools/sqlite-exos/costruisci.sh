#!/bin/bash
# =============================================================================
# tools/sqlite-exos/costruisci.sh — SQLite 3.54.1 as libsqlite3.so.0 for EX-OS
#
# Copyright (C) 2026 Graziano Falcone
# SPDX-License-Identifier: GPL-2.0-or-later (this script; SQLite is public domain)
#
#     tools/sqlite-exos/costruisci.sh [32|64]   both by default
#
# What it is for: EXBrowser's IndexedDB (debug/implementazione_BROWSER002.txt,
# P11, 10.8), opened with dlopen. The archive: sqlite-autoconf-3540100.tar.gz,
# its SHA3-256 checked against sqlite.org/download.html (PROVENIENZA.txt).
#
# ! THE AMALGAMATION, NOT configure: one file, sqlite3.c, with the options
# Firefox uses on EX-OS already (tools/prova_sqlite_io.sh): threads on, no
# loadable extensions, temporary tables in memory. The soname is upstream's,
# libsqlite3.so.0.
# =============================================================================
NOME=sqlite
VER=3540100
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/sqlite/sqlite-autoconf-$VER.tar.gz

costruisci() {
    lib_per "$1"
    lib_sorgente
    : > "$B/costruisci.log"
    mkdir -p "$B/obj"
    # shellcheck disable=SC2086
    $CC $CFLAGS -w -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_DEPRECATED \
        -DSQLITE_TEMP_STORE=2 -DHAVE_USLEEP=1 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 \
        -c "$SRC/sqlite3.c" -o "$B/obj/sqlite3.o" >> "$B/costruisci.log" 2>&1 \
        || { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    rm -f "$DEST/lib/libsqlite3.a"
    $AR rcs "$DEST/lib/libsqlite3.a" "$B/obj/sqlite3.o"
    cp -p "$SRC/sqlite3.h" "$DEST/include/"
    lib_condivisa sqlite3 0 "-lpthread"
    lib_fine
}
lib_tutte "$1"
