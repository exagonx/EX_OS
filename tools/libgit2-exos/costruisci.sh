#!/bin/bash
# =============================================================================
# tools/libgit2-exos/costruisci.sh — libgit2 for 64-bit EX-OS
#
#     tools/libgit2-exos/costruisci.sh          build + install in the sysroot
#
# The user (17 October 2026): the libgit2 library, for cargo (@RUST-64) and
# for any C program. libgit2 1.9.6, the copy cargo's libgit2-sys vendors in
# the rustc sources (cross_build/terze-parti/rust-src/.../libgit2-sys-0.18.7
# +1.9.6/libgit2), copied to cross_build/terze-parti/libgit2-1.9.6.
#
# Built with cmake and the x86_64-exos cross compiler as a STATIC library
# (EX-OS programs are static at 64 bit): libgit2.a with its bundled zlib,
# llhttp, pcre2 and xdiff; HTTPS through the EX-OS OpenSSL
# (cross_build/openssl-build-exos64); no SSH (no libssh2 for EX-OS yet), no
# NTLM, no iconv, no GSSAPI. Also the `git2` command-line program libgit2
# ships (its CLI: init, clone, cat-file, hash-object, config, index-pack...)
# as a test tool for EX-OS.
#
# Output: cross_build/libgit2-exos64/{lib/libgit2.a,include/git2*,bin/git2}
# and the library + headers copied into the x86_64-exos sysroot, so that
# `x86_64-exos-gcc prog.c -lgit2 -lssl -lcrypto` works.
#
# ! CMAKE IS TOLD THE SYSTEM IS "Linux", as for LLVM in
# tools/rust-exos/rustc-nativo: cmake has no idea of EX-OS, and "Linux" gets
# the POSIX paths (UNIX true, pthreads, the unix/ sources). Every function
# libgit2 uses is checked at configure time, so nothing Linux-only is
# assumed; __unix__ and EXOS_SOLO_POSIX (libc.h's own names out of the way)
# as for LLVM.
# =============================================================================
set -e
cd "$(dirname "$0")/../.." || exit 1
R=$PWD
TC=$R/cross_build
VEND=$TC/terze-parti/rust-src/rustc-nightly-src/vendor/libgit2-sys-0.18.7+1.9.6/libgit2
SRC=$TC/terze-parti/libgit2-1.9.6
B=$TC/libgit2-build-exos64
OUT=$TC/libgit2-exos64
SYSROOT=$TC/exos-cross64/x86_64-exos
O=$TC/openssl-build-exos64
export PATH="$TC/exos-cross64/bin:$PATH"

# the rustc sources come back from their tarball (see prepara-sorgenti.sh)
A=$TC/terze-parti/rust-src/rustc-nightly-src.tar.xz
[ -d "$SRC" ] || [ -d "$VEND" ] || [ ! -f "$A" ] || (cd "$(dirname "$A")" && tar xf "$(basename "$A")")
[ -d "$SRC" ] || { [ -d "$VEND" ] || { echo "manca $VEND"; exit 1; }; cp -r "$VEND" "$SRC"; }
[ -f "$O/libssl.a" ] || { echo "manca OpenSSL a 64 bit: EXOS_ARCH=x86_64 tools/openssl-exos/prepara-openssl.sh"; exit 1; }

mkdir -p "$B" "$OUT"
# OpenSSL's headers in one directory: the source ones, then the generated.
mkdir -p "$B/ssl-include/openssl"
cp -r "$R/openssl/include/openssl/." "$B/ssl-include/openssl/"
chmod -R u+w "$B/ssl-include"
cp -rf "$O/include/openssl/." "$B/ssl-include/openssl/"

cat > "$B/exos64.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-exos-gcc)
set(CMAKE_AR x86_64-exos-ar)
set(CMAKE_RANLIB x86_64-exos-ranlib)
set(CMAKE_FIND_ROOT_PATH $SYSROOT)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_C_FLAGS_INIT "-D__unix__ -DEXOS_SOLO_POSIX -include $R/tools/libgit2-exos/exos-compat.h")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(THREADS_PREFER_PTHREAD_FLAG OFF)
EOF

cmake -S "$SRC" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$B/exos64.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OUT" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_CLI=ON -DBUILD_EXAMPLES=OFF \
    -DUSE_BUNDLED_ZLIB=ON -DUSE_SSH=OFF -DUSE_NTLMCLIENT=OFF -DUSE_ICONV=OFF \
    -DUSE_GSSAPI=OFF -DUSE_NSEC=OFF -DREGEX_BACKEND=builtin -DUSE_HTTP_PARSER=builtin \
    -DUSE_HTTPS=OpenSSL -DUSE_SHA1=CollisionDetection -DUSE_SHA256=OpenSSL-FIPS \
    -DOPENSSL_INCLUDE_DIR="$B/ssl-include" \
    -DOPENSSL_SSL_LIBRARY="$O/libssl.a" -DOPENSSL_CRYPTO_LIBRARY="$O/libcrypto.a" \
    > "$B/configura.log" 2>&1 || { tail -30 "$B/configura.log"; exit 1; }

make -C "$B" -j3 > "$B/costruisci.log" 2>&1 || { grep -m20 -B2 -A6 'error' "$B/costruisci.log"; exit 1; }
make -C "$B" install > "$B/installa.log" 2>&1

cp -p "$OUT/lib/libgit2.a" "$SYSROOT/lib/"
cp -rp "$OUT/include/git2.h" "$OUT/include/git2" "$SYSROOT/include/"
[ -x "$B/git2" ] && cp -p "$B/git2" "$OUT/bin/git2" 2>/dev/null || true
ls -la "$OUT/lib/libgit2.a" "$OUT/bin/" 2>/dev/null
echo "libgit2 $(sed -n 's/^#define LIBGIT2_VERSION  *"\(.*\)"/\1/p' "$SRC/include/git2/version.h") per x86_64-exos: $OUT"
