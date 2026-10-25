#!/bin/bash
# =============================================================================
# tools/mariadb-exos/costruisci.sh — MariaDB (11.8 LTS) for EX-OS
#
#     tools/mariadb-exos/costruisci.sh            64 bit (the default)
#     EXOS_ARCH=i386 tools/mariadb-exos/costruisci.sh      32 bit
#     tools/mariadb-exos/costruisci.sh host|pcre2|termcap|configura|compila
#                                                 one stage only
#
# Stages, each skipped when its result is already there:
#   host       a native Linux build of the programs the build RUNS
#              (comp_err, gen_lex_hash, gen_lex_token, factorial):
#              import_executables.cmake, given to the cross build
#   pcre2      PCRE2 10.49 for EX-OS, static, from the local archive
#   termcap    the console's termcap, for the client's line editor
#   configura  cmake with the EX-OS toolchain file
#   compila    make
#
# ! NOTHING IS DOWNLOADED DURING THE BUILD. MariaDB's cmake fetches PCRE2 and
# libfmt from GitHub (ExternalProject); the same archives, with the MD5 its
# cmake files name, are in cross_build/terze-parti/mariadb/ (and so in the
# backup.sh archive). PCRE2 is built here and given as the "system" one:
# MariaDB's bundled PCRE2 does not pass the toolchain file to its sub-build,
# and would configure for Linux with the EX-OS compiler. libfmt is
# header-only: its include directory is enough.
#
# ! THE TREE IS NOT TOUCHED. mariadb-server/ follows the official 11.8
# branch (git -C mariadb-server pull); what EX-OS needs is in this
# directory and is applied from here.
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
ARCHIVI=$TC/terze-parti/mariadb
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
case "$BIT" in
    64) O=$TC/openssl-build-exos64 ;;
    32) O=$TC/openssl-build-exos ;;
esac
[ -f "$O/libssl.a" ] || manca "OpenSSL per $T ($O)" "tools/ambiente.sh dice il comando (tools/openssl-exos/costruisci.sh)"
for a in pcre2-10.49.zip fmt-12.2.0.zip; do
    [ -f "$ARCHIVI/$a" ] || manca "$ARCHIVI/$a" "ripristinare cross_build/terze-parti/mariadb dall'archivio di backup.sh"
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

# --- pcre2 ---------------------------------------------------------------------
P=$OUT/pcre2
tappa_pcre2() {
    [ -f "$P/lib/libpcre2-8.a" ] && return
    echo "=== MariaDB: PCRE2 10.49 per $T ==="
    local s=$TC/costruzione-mariadb-pcre2-$BIT
    rm -rf "$s"; mkdir -p "$s"
    (cd "$s" && unzip -q "$ARCHIVI/pcre2-10.49.zip")
    cmake -S "$s/pcre2-10.49" -B "$s/b" -DCMAKE_TOOLCHAIN_FILE="$B/exos.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
        -DBUILD_SHARED_LIBS=OFF -DPCRE2_BUILD_TESTS=OFF -DPCRE2_BUILD_PCRE2GREP=OFF \
        -DPCRE2_SUPPORT_JIT=OFF > "$s/configura.log" 2>&1 || { tail -20 "$s/configura.log"; exit 1; }
    make -C "$s/b" -j"$(nproc)" install > "$s/make.log" 2>&1 || { grep -m10 -A5 error "$s/make.log"; exit 1; }
}

# --- termcap -------------------------------------------------------------------
# The client's line editor (MariaDB's bundled readline) wants curses for the
# five termcap calls; EX-OS has one kind of terminal, and termcap/termcap.c
# answers for it. Installed also as curses.h and term.h, where cmake's
# FindCurses and readline look.
TCAP=$OUT/termcap
tappa_termcap() {
    [ -f "$TCAP/lib/libtermcap.a" ] && [ "$TCAP/lib/libtermcap.a" -nt "$R/tools/mariadb-exos/termcap/termcap.c" ] && return
    echo "=== MariaDB: termcap della console per $T ==="
    mkdir -p "$TCAP/lib" "$TCAP/include"
    $T-gcc -O2 -D__unix__ -DEXOS_SOLO_POSIX -c "$R/tools/mariadb-exos/termcap/termcap.c" -o "$TCAP/termcap.o"
    rm -f "$TCAP/lib/libtermcap.a"
    $T-ar rcs "$TCAP/lib/libtermcap.a" "$TCAP/termcap.o"
    for h in termcap.h curses.h term.h; do cp "$R/tools/mariadb-exos/termcap/termcap.h" "$TCAP/include/$h"; done
}

# --- the toolchain file --------------------------------------------------------
# ! CMAKE IS TOLD THE SYSTEM IS "Linux", as for libgit2 and LLVM: cmake knows
# nothing of EX-OS, and "Linux" gives UNIX, pthreads and the POSIX paths.
# Every function MariaDB uses is checked at configure time, so the list of
# what CMake does not find is the real list of what the libc lacks.
toolchain() {
    mkdir -p "$B"
    cat > "$B/exos.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $ARCH)
set(CMAKE_C_COMPILER $T-gcc)
set(CMAKE_CXX_COMPILER $T-g++)
set(CMAKE_AR $T-ar)
set(CMAKE_RANLIB $T-ranlib)
set(CMAKE_FIND_ROOT_PATH $SYSROOT $OUT/pcre2 $OUT/termcap)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-D__unix__ -DEXOS_SOLO_POSIX")
set(CMAKE_CXX_FLAGS_INIT "-D__unix__ -DEXOS_SOLO_POSIX")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
EOF
}

# --- configura -----------------------------------------------------------------
# ! TLS CANNOT BE SWITCHED OFF in MariaDB (WITH_SSL has no "off"): it is the
# EX-OS OpenSSL (tools/openssl-exos/costruisci.sh, 4.1.0 with sockets and
# threads), the one libgit2 uses.
tappa_configura() {
    echo "=== MariaDB: configurazione per $T ==="
    rm -rf "$B/ssl-include"; mkdir -p "$B/ssl-include/openssl"
    cp -rf "$O/include/openssl/." "$B/ssl-include/openssl/"
    local fmt=$TC/costruzione-mariadb-fmt
    [ -d "$fmt/fmt-12.2.0" ] || { mkdir -p "$fmt"; (cd "$fmt" && unzip -q "$ARCHIVI/fmt-12.2.0.zip"); }
    cmake -S "$SRC" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$B/exos.cmake" \
        -DIMPORT_EXECUTABLES="$HOST/import_executables.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/exos/mariadb \
        $SPENTI -DWITH_SSL=system -DOPENSSL_INCLUDE_DIR="$B/ssl-include" \
        -DOPENSSL_SSL_LIBRARY="$O/libssl.a" -DOPENSSL_CRYPTO_LIBRARY="$O/libcrypto.a" \
        -DWITH_ZLIB=bundled -DWITH_PCRE=system \
        -DPCRE2_INCLUDE_DIR="$P/include" -DWITH_LIBFMT=system \
        -DWITH_READLINE=ON -DCURSES_LIBRARY="$TCAP/lib/libtermcap.a" \
        -DCURSES_INCLUDE_PATH="$TCAP/include" \
        -DCMAKE_CXX_FLAGS="-I$fmt/fmt-12.2.0/include" \
        -DPLUGIN_TOKUDB=NO -DWITH_SYSTEMD=no -DWITH_NUMA=OFF -DWITH_LIBNUMA=OFF \
        -DWITH_URING=no -DWITH_LIBAIO=OFF -DWITH_PMEM=no -DCONC_WITH_UNIT_TESTS=OFF \
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
    pcre2) tappa_pcre2 ;;
    termcap) tappa_termcap ;;
    configura) tappa_configura ;;
    compila) tappa_compila ;;
    tutto) tappa_host; tappa_pcre2; tappa_termcap; tappa_configura; tappa_compila ;;
    *) echo "tappa sconosciuta: $TAPPA (host pcre2 configura compila)" >&2; exit 2 ;;
esac
