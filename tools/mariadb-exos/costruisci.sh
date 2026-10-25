#!/bin/bash
# =============================================================================
# tools/mariadb-exos/costruisci.sh — MariaDB (11.8 LTS) for EX-OS
#
#     tools/mariadb-exos/costruisci.sh            64 bit (the default)
#     EXOS_ARCH=i386 tools/mariadb-exos/costruisci.sh      32 bit
#     tools/mariadb-exos/costruisci.sh 32|64 [stage]   the same
#     tools/mariadb-exos/costruisci.sh host|sorgente|configura|compila|installa
#                                                 one stage only
#
# Stages:
#   host       a native Linux build of the programs the build RUNS
#              (comp_err, gen_lex_hash, gen_lex_token, factorial):
#              import_executables.cmake, given to the cross build (skipped
#              when it is already there)
#   configura  cmake with the EX-OS toolchain file
#   sorgente   the official archive unpacked in costruzione-mariadb-src
#              (once: every build of this version shares it)
#   compila    make
#   installa   cross_build/mariadb-exosNN/exos, laid out as /exos on EX-OS
#              and as the [mariadb] group of the tools CD takes it:
#                  mariadb/bin/    mariadbd and the maintenance programs
#                  mariadb/share/  charsets, error messages, the *.sql of
#                                  the system tables and bootstrap.sql
#                  bin/            the clients (mariadb, mariadb-admin,
#                                  mariadb-dump...) and mariadb-svc
#                  lib/ include/   libmariadbclient.a and <mysql/*.h>,
#                                  also copied into the cross sysroots
#                  src/mariadb/    the archive, its signature, this
#                                  directory (script, patches, mariadb-svc.c)
#
# ! EVERY LIBRARY IS AN EX-OS LIBRARY, from the cross sysroot (the user, 25
# October 2026: first every dependency as a real library, 32 and 64 bit):
# ncurses (tools/ncurses-exos), zlib, bzip2, xz, lz4, zstd, PCRE2, fmt
# (tools/<name>-exos), OpenSSL 4.1.0 (tools/openssl-exos). Nothing bundled,
# nothing downloaded: MariaDB's cmake would fetch PCRE2 and libfmt from
# GitHub (ExternalProject) and build them for Linux.
#
# ! THE SOURCES ARE THE PUBLISHED RELEASE, 11.8.9 (the user, 25 October
# 2026: the latest release, not the head of the 11.8 branch, as for the
# other libraries): mariadb-11.8.9.tar.gz and its .asc from
# https://archive.mariadb.org/mariadb-11.8.9/source/, signed with the
# MariaDB Signing Key 177F4010FE56CA3336300305F1656F24C74CD1D8, in
# cross_build/terze-parti/mariadb. The archive has libmariadb in it.
#
# ! THE TREE IS NOT TOUCHED BY HAND: what EX-OS needs is in this directory
# and is applied from here. The few changes to the sources are
# patch/<submodule-or-tree>-*.diff, applied by `sorgente` to the unpacked
# copy (git apply, which works outside a repository too). A new version:
# VER below, the archive next to the old one, and the build directories
# start again by themselves (they remember the version they were made for).
#
# Build directories (outside MEGA, see backup.sh: */costruzione-* is not
# archived): cross_build/costruzione-mariadb-host, costruzione-mariadb64,
# costruzione-mariadb32, costruzione-mariadb-src (the unpacked sources).
# Results: cross_build/mariadb-exos64 (32).
# =============================================================================
set -e
cd "$(dirname "$0")/../.." || exit 1
R=$PWD
TC=$R/cross_build
VER=11.8.9
ARCHIVIO=$TC/terze-parti/mariadb/mariadb-$VER.tar.gz
SRC=$TC/costruzione-mariadb-src/mariadb-$VER
ARCH=${EXOS_ARCH:-x86_64}
# "costruisci.sh 32|64", as the library scripts (tools/librerie-exos/su-cd.sh says it)
case "$1" in 32) ARCH=i386; shift ;; 64) ARCH=x86_64; shift ;; esac
# MARCH: the base CPU of the 32-bit system, as tools/librerie-exos/comune.sh
case "$ARCH" in
    x86_64) BIT=64; T=x86_64-exos; CROSS=$TC/nasi5deb/exos-cross64; ALTRO=$TC/exos-cross64
            MARCH="" ;;
    i386)   BIT=32; T=i386-exos;   CROSS=$TC/nasi5deb/exos-cross;   ALTRO=$TC/exos-cross
            MARCH="-march=pentium-mmx -mtune=pentium-mmx" ;;
    *) echo "EXOS_ARCH=$ARCH: i386 oppure x86_64" >&2; exit 2 ;;
esac
HOST=$TC/costruzione-mariadb-host
B=$TC/costruzione-mariadb$BIT
OUT=$TC/mariadb-exos$BIT
SYSROOT=$CROSS/$T
export PATH="$CROSS/bin:$PATH"
TAPPA=${1:-tutto}

manca() { echo "! manca $1"; echo "    rimedio: $2"; exit 1; }
[ -f "$ARCHIVIO" ] || manca "$ARCHIVIO" "lo scarica chi aggiorna MariaDB: vedi il commento in testa a $0"
[ -x "$CROSS/bin/$T-g++" ] || manca "$T-g++ ($CROSS)" "tools/exilla/leggimi.md (la toolchain con i fili, gcc-fili.sh)"
for l in ssl:openssl ncurses:ncurses z:zlib bz2:bzip2 lzma:xz lz4:lz4 zstd:zstd pcre2-8:pcre2 fmt:fmt; do
    [ -f "$SYSROOT/lib/lib${l%%:*}.a" ] || manca "lib${l%%:*}.a in $SYSROOT" "tools/${l#*:}-exos/costruisci.sh $BIT"
done

# Options shared by the host and the EX-OS builds: what is not built.
SPENTI="-DWITH_WSREP=OFF -DPLUGIN_ROCKSDB=NO -DPLUGIN_COLUMNSTORE=NO -DPLUGIN_MROONGA=NO
 -DPLUGIN_SPIDER=NO -DPLUGIN_CONNECT=NO -DPLUGIN_S3=NO -DPLUGIN_OQGRAPH=NO
 -DPLUGIN_AUTH_PAM=NO -DPLUGIN_AUTH_PAM_V1=NO -DPLUGIN_AUTH_GSSAPI=NO
 -DWITH_UNIT_TESTS=OFF -DWITH_EMBEDDED_SERVER=OFF"

# --- host ----------------------------------------------------------------------
# a build directory made from another version starts again
stessa_versione() {
    [ "$(cat "$1/VERSIONE" 2>/dev/null)" = "$VER" ] && return
    rm -rf "$1"; mkdir -p "$1"; echo "$VER" > "$1/VERSIONE"
}

tappa_host() {
    stessa_versione "$HOST"
    [ -f "$HOST/import_executables.cmake" ] && return
    echo "=== MariaDB: gli strumenti della macchina (import_executables) ==="
    (cd "$HOST" && cmake "$SRC" -DCMAKE_BUILD_TYPE=Release $SPENTI > configura.log 2>&1) ||
        { tail -20 "$HOST/configura.log"; exit 1; }
    make -C "$HOST" -j"$(nproc)" import_executables > "$HOST/make.log" 2>&1 ||
        { grep -m10 -A5 error "$HOST/make.log"; exit 1; }
}

# --- the toolchain file --------------------------------------------------------
# ! CMAKE IS TOLD THE SYSTEM IS "Linux", as for libgit2 and LLVM: cmake knows
# nothing of EX-OS, and "Linux" gives UNIX, pthreads and the POSIX paths.
# Every function MariaDB uses is checked at configure time, so the list of
# what CMake does not find is the real list of what the libc lacks.
# -fcommon: the bundled readline defines PC, BC and UP itself on every system
# but Linux (terminal.c, "#if !defined (__linux__)"); ncurses defines them too.
# As common symbols, which is what GCC made them before GCC 10, the two meet
# as one variable instead of a "multiple definition".
toolchain() {
    stessa_versione "$B"
    cat > "$B/exos.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $ARCH)
set(CMAKE_C_COMPILER $T-gcc)
set(CMAKE_CXX_COMPILER $T-g++)
set(CMAKE_AR $T-ar)
set(CMAKE_RANLIB $T-ranlib)
set(CMAKE_FIND_ROOT_PATH $SYSROOT)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "$MARCH -D__unix__ -DEXOS_SOLO_POSIX -fcommon")
set(CMAKE_CXX_FLAGS_INIT "$MARCH -D__unix__ -DEXOS_SOLO_POSIX")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(ENV{PKG_CONFIG_LIBDIR} /nessuno)
set(ENV{PKG_CONFIG_PATH} "")
EOF
}

# --- configura -----------------------------------------------------------------
# ! TLS CANNOT BE SWITCHED OFF in MariaDB (WITH_SSL has no "off"): it is the
# EX-OS OpenSSL. The compression providers (bzip2, lz4, lzma) are built into
# the server (STATIC: there are no dynamic plugins); lzo and snappy have no
# EX-OS library. The *_EXITCODE are the answers of programs cmake would run
# to test a library, which a cross build cannot run: fmt was tested in EX-OS
# (tools/prova_librerie.sh).
tappa_sorgente() {
    local d f
    [ -f "$SRC/.patch-fatte" ] && return
    echo "=== MariaDB $VER: i sorgenti da $(basename "$ARCHIVIO") ==="
    rm -rf "$SRC"; mkdir -p "$(dirname "$SRC")"
    tar -xzf "$ARCHIVIO" -C "$(dirname "$SRC")"
    for f in "$R"/tools/mariadb-exos/patch/*.diff; do
        case "$(basename "$f")" in
            libmariadb-*) d=$SRC/libmariadb ;;
            *)            d=$SRC ;;
        esac
        git -C "$d" apply "$f" || { echo "! $f non si applica a $d"; exit 1; }
        echo "    applicata $(basename "$f")"
    done
    : > "$SRC/.patch-fatte"
}

tappa_configura() {
    echo "=== MariaDB $VER: configurazione per $T ==="
    rm -f "$B/CMakeCache.txt"
    # pkg-config would answer with the Linux libraries in /usr/lib (-L
    # /usr/lib/x86_64-linux-gnu, -I/usr/include): it looks nowhere, cmake
    # finds them in the sysroot. The toolchain file says it again, for the
    # cmake that make reruns by itself when a CMakeLists.txt changes. SECURITY_HARDENED would add -pie -fPIC: EX-OS programs are
    # static executables, and DISABLE_SHARED leaves out libmariadb.so and the
    # other shared libraries; the client plugins of libmariadb that would be
    # .so modules go into the library (CLIENT_PLUGIN_*=STATIC). GROFF/NROFF:
    # the dbug manual is not wanted. DISABLE_THREADPOOL: the pool of threads
    # wants epoll on "Linux", which EX-OS does not have; one thread per
    # connection is what is left (the default of MariaDB anyway). FEEDBACK
    # would send statistics to mariadb.org and reads /etc/*-release with glob.
    # HAVE_LINK_H empty: with <link.h> the server reads a dlopen handle as
    # glibc's struct link_map, which an EX-OS handle is not.
    # HAVE_SYS_UN_H empty: <sys/un.h> has the structure, but socket(AF_UNIX)
    # is EAFNOSUPPORT; without it the server listens on TCP only (it failed
    # creating /tmp/mysql.sock) and the clients go to 127.0.0.1.
    PKG_CONFIG_LIBDIR=/nessuno PKG_CONFIG_PATH= \
    cmake -S "$SRC" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$B/exos.cmake" \
        -DIMPORT_EXECUTABLES="$HOST/import_executables.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/exos/mariadb \
        $SPENTI -DWITH_SSL=system -DWITH_ZLIB=system -DWITH_PCRE=system \
        -DWITH_LIBFMT=system -DHAVE_SYSTEM_LIBFMT_EXITCODE=0 \
        -DWITH_READLINE=ON -DSECURITY_HARDENED=OFF -DDISABLE_SHARED=ON \
        -DDISABLE_THREADPOOL=ON -DPLUGIN_FEEDBACK=NO \
        -DHAVE_LINK_H= -DHAVE_SYS_UN_H= \
        -DGROFF=GROFF-NOTFOUND -DNROFF=NROFF-NOTFOUND \
        -DCLIENT_PLUGIN_DIALOG=STATIC -DCLIENT_PLUGIN_CLIENT_ED25519=STATIC \
        -DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD=STATIC -DCLIENT_PLUGIN_SHA256_PASSWORD=STATIC \
        -DCLIENT_PLUGIN_PARSEC=STATIC -DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD=STATIC \
        -DCLIENT_PLUGIN_ZSTD=STATIC -DCLIENT_PLUGIN_REMOTE_IO=OFF \
        -DPLUGIN_PROVIDER_BZIP2=STATIC -DPLUGIN_PROVIDER_LZ4=STATIC \
        -DPLUGIN_PROVIDER_LZMA=STATIC -DPLUGIN_PROVIDER_LZO=NO -DPLUGIN_PROVIDER_SNAPPY=NO \
        -DPLUGIN_TOKUDB=NO -DWITH_SYSTEMD=no -DWITH_NUMA=OFF -DWITH_LIBNUMA=OFF \
        -DWITH_URING=no -DWITH_LIBAIO=OFF -DWITH_INNODB_PMEM=OFF -DCONC_WITH_UNIT_TESTS=OFF \
        -DWITHOUT_DYNAMIC_PLUGINS=ON -DSTACK_DIRECTION=-1 -DHAVE_IB_GCC_ATOMIC_BUILTINS=1 \
        > "$B/configura.log" 2>&1 || { grep -B2 -A8 'CMake Error' "$B/configura.log" | head -60; exit 1; }
    echo "    registro: $B/configura.log"
}

# --- compila -------------------------------------------------------------------
tappa_compila() {
    echo "=== MariaDB $VER: compilazione per $T ==="
    make -C "$B" -j"$(nproc)" -k > "$B/make.log" 2>&1 ||
        { grep -m40 -B1 -A3 'error' "$B/make.log"; echo "    registro: $B/make.log"; exit 1; }
}

# --- installa ------------------------------------------------------------------
# make install into $B/installato, then only what EX-OS uses: no test suite
# (mariadb-test, mariadb-client-test, 250 MB), no shell or perl scripts (EX-OS
# runs neither: mariadb-install-db is replaced by bootstrap.sql and
# `mariadb-svc -install`), no man pages, every program stripped. The clients
# go to /exos/bin, which is in the PATH; the server and its maintenance
# programs stay in /exos/mariadb/bin.
CLIENT="mariadb mariadb-admin mariadb-dump mariadb-import mariadb-check mariadb-show
 mariadb-slap mariadb-binlog mariadb-upgrade mariadb-conv mariadb-tzinfo-to-sql"
tappa_installa() {
    local I M D p n S f
    echo "=== MariaDB $VER: installazione in $OUT ==="
    I=$B/installato
    rm -rf "$I" "$OUT"
    make -C "$B" install DESTDIR="$I" > "$B/installa.log" 2>&1 ||
        { tail -20 "$B/installa.log"; exit 1; }
    M=$I/exos/mariadb
    D=$OUT/exos
    mkdir -p "$D/mariadb/bin" "$D/mariadb/share" "$D/bin" "$D/lib" "$D/include" "$D/src/mariadb"
    for p in "$M"/bin/*; do
        n=$(basename "$p")
        [ -L "$p" ] && continue                         # mysql* -> mariadb*
        [ "$(head -c4 "$p" | tail -c3)" = ELF ] || continue
        case " $(echo $CLIENT) " in
            *" $n "*) $T-strip -o "$D/bin/$n" "$p" ;;
            *) case $n in
                   mariadb-test|mariadb-client-test|mariadb-config|mariadb_config) ;;
                   *) $T-strip -o "$D/mariadb/bin/$n" "$p" ;;
               esac ;;
        esac
    done
    cp -rp "$M/share/charsets" "$D/mariadb/share/"
    for p in "$M"/share/*/; do                          # english, italian...
        [ -f "$p/errmsg.sys" ] && cp -rp "$p" "$D/mariadb/share/"
    done
    cp -p "$M"/share/*.sql "$D/mariadb/share/"
    # what mysql_install_db.sh pipes into mariadbd --bootstrap (cat_sql), with
    # its cross-bootstrap filter: no root@<this host>, the name is not known
    # when the tables are made; root@localhost without a password, no
    # unix_socket user (no AF_UNIX on EX-OS)
    {
        echo "create database if not exists mysql;"
        echo "use mysql;"
        echo "SET @auth_root_socket=NULL;"
        for f in mariadb_system_tables mariadb_performance_tables mariadb_system_tables_data \
                 fill_help_tables maria_add_gis_sp_bootstrap mariadb_sys_schema mariadb_test_db; do
            cat "$M/share/$f.sql"
        done
    } | sed -e '/@current_hostname/d' > "$D/mariadb/share/bootstrap.sql"
    cp -p "$SRC/COPYING" "$SRC/README.md" "$SRC/THIRDPARTY" "$D/mariadb/"
    # the client library: C programs talk to the server with -lmariadbclient
    cp -p "$M/lib/libmariadbclient.a" "$D/lib/"
    cp -rp "$M/include/mysql" "$D/include/"
    # the service (libcrypto: the TLS certificate it makes at -install)
    $T-gcc $MARCH -O2 -Wall -static -o "$D/bin/mariadb-svc" "$R/tools/mariadb-exos/mariadb-svc.c" \
        -lcrypto -lpthread > "$B/mariadb-svc.log" 2>&1 || { cat "$B/mariadb-svc.log"; exit 1; }
    $T-strip "$D/bin/mariadb-svc"
    # the sources: the archive and everything this directory adds to it
    cp -p "$ARCHIVIO" "$ARCHIVIO".asc "$D/src/mariadb/"
    cp -rp "$R/tools/mariadb-exos/." "$D/src/mariadb/"
    for S in "$CROSS/$T" "$ALTRO/$T"; do
        [ -d "$S/lib" ] || continue
        cp -p "$D/lib/libmariadbclient.a" "$S/lib/"
        cp -rp "$D/include/mysql" "$S/include/"
    done
    echo "    $(du -sh "$D" | cut -f1)  $(ls "$D/bin" | tr '\n' ' ')"
}

toolchain
case "$TAPPA" in
    host) tappa_sorgente; tappa_host ;;
    sorgente) tappa_sorgente ;;
    configura) tappa_sorgente; tappa_configura ;;
    compila) tappa_compila ;;
    installa) tappa_installa ;;
    tutto) tappa_sorgente; tappa_host; tappa_configura; tappa_compila; tappa_installa ;;
    *) echo "tappa sconosciuta: $TAPPA (host sorgente configura compila installa)" >&2; exit 2 ;;
esac
