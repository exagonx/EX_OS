#!/bin/bash
# =============================================================================
# tools/mariadb-exos/costruisci.sh — MariaDB (11.8 LTS) for EX-OS
#
#     tools/mariadb-exos/costruisci.sh            64 bit (the default)
#     EXOS_ARCH=i386 tools/mariadb-exos/costruisci.sh      32 bit
#     tools/mariadb-exos/costruisci.sh host|configura|compila
#                                                 one stage only
#
# Stages:
#   host       a native Linux build of the programs the build RUNS
#              (comp_err, gen_lex_hash, gen_lex_token, factorial):
#              import_executables.cmake, given to the cross build (skipped
#              when it is already there)
#   configura  cmake with the EX-OS toolchain file
#   compila    make
#
# ! EVERY LIBRARY IS AN EX-OS LIBRARY, from the cross sysroot (the user, 25
# October 2026: first every dependency as a real library, 32 and 64 bit):
# ncurses (tools/ncurses-exos), zlib, bzip2, xz, lz4, zstd, PCRE2, fmt
# (tools/<name>-exos), OpenSSL 4.1.0 (tools/openssl-exos). Nothing bundled,
# nothing downloaded: MariaDB's cmake would fetch PCRE2 and libfmt from
# GitHub (ExternalProject) and build them for Linux.
#
# ! THE TREE IS NOT TOUCHED BY HAND. mariadb-server/ follows the official
# 11.8 branch (git -C mariadb-server pull); what EX-OS needs is in this
# directory and is applied from here: the few changes to the sources are
# patch/<submodule-or-tree>-*.diff, applied by `configura` when they are not
# applied yet (git apply; `git -C mariadb-server/libmariadb checkout .` takes
# them away again, before a pull).
#
# Build directories (outside MEGA, see backup.sh: */costruzione-* is not
# archived): cross_build/costruzione-mariadb-host, costruzione-mariadb64,
# costruzione-mariadb32. Results: cross_build/mariadb-exos64 (32).
# =============================================================================
set -e
cd "$(dirname "$0")/../.." || exit 1
R=$PWD
TC=$R/cross_build
SRC=$R/mariadb-server
ARCH=${EXOS_ARCH:-x86_64}
case "$ARCH" in
    x86_64) BIT=64; T=x86_64-exos; CROSS=$TC/nasi5deb/exos-cross64 ;;
    i386)   BIT=32; T=i386-exos;   CROSS=$TC/nasi5deb/exos-cross ;;
    *) echo "EXOS_ARCH=$ARCH: i386 oppure x86_64" >&2; exit 2 ;;
esac
HOST=$TC/costruzione-mariadb-host
B=$TC/costruzione-mariadb$BIT
OUT=$TC/mariadb-exos$BIT
SYSROOT=$CROSS/$T
export PATH="$CROSS/bin:$PATH"
TAPPA=${1:-tutto}

manca() { echo "! manca $1"; echo "    rimedio: $2"; exit 1; }
[ -f "$SRC/CMakeLists.txt" ] || manca "mariadb-server/" "git clone -b 11.8 https://github.com/MariaDB/server mariadb-server"
[ -f "$SRC/libmariadb/CMakeLists.txt" ] || manca "il sottomodulo libmariadb" "git -C mariadb-server submodule update --init libmariadb"
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
tappa_host() {
    [ -f "$HOST/import_executables.cmake" ] && return
    echo "=== MariaDB: gli strumenti della macchina (import_executables) ==="
    mkdir -p "$HOST"
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
    mkdir -p "$B"
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
set(CMAKE_C_FLAGS_INIT "-D__unix__ -DEXOS_SOLO_POSIX -fcommon")
set(CMAKE_CXX_FLAGS_INIT "-D__unix__ -DEXOS_SOLO_POSIX")
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
applica_patch() {
    local d f
    for f in "$R"/tools/mariadb-exos/patch/*.diff; do
        case "$(basename "$f")" in
            libmariadb-*) d=$SRC/libmariadb ;;
            *)            d=$SRC ;;
        esac
        git -C "$d" apply --reverse --check "$f" 2>/dev/null && continue
        git -C "$d" apply "$f" || { echo "! $f non si applica a $d"; exit 1; }
        echo "    applicata $(basename "$f")"
    done
}

tappa_configura() {
    echo "=== MariaDB: configurazione per $T ==="
    applica_patch
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
    echo "=== MariaDB: compilazione per $T ==="
    make -C "$B" -j"$(nproc)" -k > "$B/make.log" 2>&1 ||
        { grep -m40 -B1 -A3 'error' "$B/make.log"; echo "    registro: $B/make.log"; exit 1; }
}

toolchain
case "$TAPPA" in
    host) tappa_host ;;
    configura) tappa_configura ;;
    compila) tappa_compila ;;
    tutto) tappa_host; tappa_configura; tappa_compila ;;
    *) echo "tappa sconosciuta: $TAPPA (host configura compila)" >&2; exit 2 ;;
esac
