#!/bin/bash
# =============================================================================
# tools/icu-exos/costruisci.sh — ICU4C 79.1 for EX-OS (Intl in EXBrowser)
#
# Copyright (C) 2026 Graziano Falcone
# SPDX-License-Identifier: GPL-2.0-or-later (this script; ICU: the Unicode License)
#
#     tools/icu-exos/costruisci.sh [32|64]   both by default
#
# libicuuc.so.79 and libicui18n.so.79 (the stub of libicudata inside the
# first), and the data apart, icudt79l.dat, for /lib/icu: EXBrowser's Intl
# (debug/implementazione_BROWSER002.txt, P11, 4.7 c) opens them with dlopen.
# The archives: icu4c-79.1-sources.tgz and icu4c-79.1-data.zip, signed by the
# ICU Release Robot (PROVENIENZA.txt).
#
# ! THE DATA ARE REDUCED with ICU's own filter (filtro.json, next to this):
# it, en, de, fr, es, pt and root, without the dictionaries of the break
# iterators, the converters' tables, the transliterators, the spelled-out
# numbers. 4.2 MB instead of 32. ! ONE FILE FOR BOTH ARCHITECTURES: the data
# are packaged as an archive (little-endian, "l"), not inside a library.
#
# ! TWO BUILDS: ICU builds its data with its own tools, so it is built first
# for the machine that builds (in $TC/costruzione-icu/host, kept), then for
# EX-OS with --with-cross-build pointing at it. The libraries of EX-OS are
# static, then shared objects with lib_condivisa: ICU's makefiles know no
# "exos" for shared libraries.
# =============================================================================
NOME=icu
VER=79.1
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/icu4c/icu4c-$VER-sources.tgz
DATI=$TP/icu4c/icu4c-$VER-data.zip
FILTRO=$R/tools/icu-exos/filtro.json
OSPITE=$TC/costruzione-icu/host

# the sources with the data sources in place of the prebuilt data
sorgenti() {
    lib_sorgente
    rm -rf "$SRC/source/data"
    unzip -q "$DATI" -d "$SRC/source/" || exit 1
}

ospite() {
    [ -f "$OSPITE/data/out/icudt79l.dat" ] && [ "$OSPITE/.filtro" -nt "$FILTRO" ] && return
    echo "--- ICU per la macchina che costruisce ($OSPITE)"
    rm -rf "$OSPITE"; mkdir -p "$OSPITE/build"
    local srcvero=$SRC
    cp -r "$srcvero" "$OSPITE/icu"
    (cd "$OSPITE/build" && ICU_DATA_FILTER_FILE=$FILTRO ../icu/source/runConfigureICU Linux \
        --disable-tests --disable-samples --with-data-packaging=archive > configura.log 2>&1 \
        && ICU_DATA_FILTER_FILE=$FILTRO make -j"$(nproc)" > make.log 2>&1) \
        || { tail -20 "$OSPITE/build/make.log"; echo "ICU per l'ospite non si costruisce"; exit 1; }
    mv "$OSPITE/build"/* "$OSPITE/" && rmdir "$OSPITE/build"
    touch "$OSPITE/.filtro"
}

costruisci() {
    lib_per "$1"
    sorgenti
    ospite
    chmod +x "$SRC/source/config.sub" "$SRC/source/config.guess" "$SRC/source/configure" "$SRC/source/install-sh" 2>/dev/null
    lib_config_sub
    # ! EX-OS IS AN ELF SYSTEM WITH GNU's TOOLS: the platform fragment is
    # Linux's, as ICU's own error message suggests ("copy mh-linux to
    # mh-unknown"); the libraries are static here, so nothing of its shared
    # library rules is used
    cp "$SRC/source/config/mh-linux" "$SRC/source/config/mh-unknown"
    mkdir -p "$B/build"
    (cd "$B/build" && ICU_DATA_FILTER_FILE=$FILTRO CC="$CC" CXX="$CXX" CFLAGS="$CFLAGS -DEXOS_SOLO_POSIX -D__unix__ -DU_HAVE_NL_LANGINFO_CODESET=0" CXXFLAGS="$CFLAGS -std=c++17 -DEXOS_SOLO_POSIX -D__unix__ -DU_HAVE_NL_LANGINFO_CODESET=0" \
        "$SRC/source/configure" --host=$T --with-cross-build="$OSPITE" \
        --enable-static --disable-shared --disable-tools --disable-tests --disable-samples \
        --disable-extras --disable-icuio --disable-layoutex --with-data-packaging=archive \
        --prefix=/ > configura.log 2>&1) || { tail -30 "$B/build/configura.log"; exit 1; }
    (cd "$B/build" && ICU_DATA_FILTER_FILE=$FILTRO make -j"$(nproc)" > make.log 2>&1) \
        || { grep -m20 -B3 -A3 'rror' "$B/build/make.log"; exit 1; }
    cp "$B/build/lib/libicuuc.a" "$B/build/lib/libicui18n.a" "$DEST/lib/"
    # the stub of the data goes into icuuc: the data come from the file
    $AR rs "$DEST/lib/libicuuc.a" "$B/build/stubdata/stubdata.ao" || exit 1
    mkdir -p "$DEST/include/unicode" "$DEST/share/icu"
    cp -p "$SRC"/source/common/unicode/*.h "$SRC"/source/i18n/unicode/*.h "$DEST/include/unicode/"
    cp -p "$OSPITE/data/out/icudt79l.dat" "$DEST/share/icu/"
    lib_condivisa icuuc 79 "-lstdc++ -lgcc_s -lm -lpthread"
    lib_condivisa icui18n 79 "-licuuc -lstdc++ -lgcc_s -lm -lpthread"
    lib_fine "$DATI" "$FILTRO"
}
lib_tutte "$1"
