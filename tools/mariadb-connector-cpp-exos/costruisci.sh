#!/bin/bash
# =============================================================================
# tools/mariadb-connector-cpp-exos/costruisci.sh — MariaDB Connector/C++ for EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mariadb-connector-cpp-exos/costruisci.sh [32|64]    both by default
#
# libmariadbcpp.a, the C++ API of MariaDB in the JDBC style (<conncpp.hpp>:
# sql::Driver, Connection, PreparedStatement, ResultSet), over Connector/C:
# a program links -lmariadbcpp -lmariadbclient -lssl -lcrypto -lz -lzstd
# -lpthread -lm. Headers in include/mariadb (conncpp.hpp, conncpp/), as upstream
# installs them. Group [mariadb-connector] of the tools ISO and netinst.
#
# The archive: mariadb-connector-cpp-1.1.9-src.tar.gz, its .asc and
# sha256sums.txt from https://dlm.mariadb.com/4864137/Connectors/cpp/
# connector-cpp-1.1.9/source/ (the .asc signs sha256sums.txt; MariaDB Enterprise Signing Key
# 4C470FFFEFC4D3DC59778655CE1A3DD5E3C94F49), in
# cross_build/terze-parti/mariadb-connector-cpp.
#
# ! ON THE SYSTEM CONNECTOR/C, STATIC ONLY: the source archive has an empty
# libmariadb/ (the git submodule), so USE_SYSTEM_INSTALLED_LIB takes the
# Connector/C that tools/mariadb-connector-c-exos put in the sysroot. Its
# CMakeLists always adds the shared library too, which EX-OS has not: only
# the target mariadbcpp-static is built, and installed by hand.
# =============================================================================
NOME=mariadb-connector-cpp
VER=1.1.9
. "$(dirname "$0")/../librerie-exos/comune.sh"
ARCHIVIO=$TP/mariadb-connector-cpp/mariadb-connector-cpp-$VER-src.tar.gz

costruisci() {
    lib_per "$1"
    [ -f "$CROSS/$T/lib/libmariadbclient.a" ] && [ -f "$CROSS/$T/include/mariadb/mysql.h" ] ||
        manca "Connector/C ($CROSS/$T)" "tools/mariadb-connector-c-exos/costruisci.sh $BIT"
    lib_sorgente
    # two headers missing: PacketOutputStream.h uses std::istringstream
    # without <sstream>, and the public headers int32_t/int64_t without
    # <cstdint> (every one of them includes SQLString.hpp); glibc's
    # libstdc++ headers bring them in by chance, the EX-OS ones do not
    sed -i 's|^#include "StringImp.h"$|#include "StringImp.h"\n#include <sstream>|' "$SRC/src/io/PacketOutputStream.h"
    sed -i 's|^#include <memory>$|#include <cstdint>\n#include <memory>|' "$SRC/include/conncpp/SQLString.hpp"
    lib_cmake_toolchain
    sed -i "s|-DEXOS_SOLO_POSIX\")$|-DEXOS_SOLO_POSIX -I$CROSS/$T/include/mariadb\")|" "$B/exos.cmake"
    cmake -S "$SRC" -B "$B/obj" -DCMAKE_TOOLCHAIN_FILE="$B/exos.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/exos \
        -DUSE_SYSTEM_INSTALLED_LIB=ON -DMARIADB_LINK_DYNAMIC=OFF -DWITH_UNIT_TESTS=OFF \
        -DINSTALL_LIBDIR=lib -DINSTALL_INCLUDEDIR=include/mariadb \
        > "$B/configura.log" 2>&1 || { grep -B2 -A8 'CMake Error' "$B/configura.log" | head -40; exit 1; }
    make -C "$B/obj" -j"$(nproc)" mariadbcpp-static > "$B/costruisci.log" 2>&1 ||
        { grep -m10 -B2 -A5 'rror' "$B/costruisci.log"; exit 1; }
    cp -p "$B/obj/libmariadbcpp-static.a" "$DEST/lib/libmariadbcpp.a" 2>/dev/null ||
        cp -p "$(find "$B/obj" -name 'libmariadbcpp*.a' | head -1)" "$DEST/lib/libmariadbcpp.a"
    mkdir -p "$DEST/include/mariadb"
    cp -p "$SRC/include/conncpp.hpp" "$SRC/include/MariaDbDataSource.hpp" "$DEST/include/mariadb/"
    cp -rp "$SRC/include/conncpp" "$DEST/include/mariadb/"
    lib_fine "$TP/mariadb-connector-cpp/sha256sums.txt" "$TP/mariadb-connector-cpp/sha256sums.txt.asc"
}
lib_tutte "$1"
