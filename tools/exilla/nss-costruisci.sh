#!/bin/bash
# =============================================================================
# tools/exilla/nss-costruisci.sh — NSS per EX-OS (tappa 5 di Exilla)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/nss-costruisci.sh [argomenti in piu' per build.sh]
#     BERSAGLI="tstclnt certutil" tools/exilla/nss-costruisci.sh
#
# NSS si costruisce col suo build.sh (gyp + ninja), STATICO: EX-OS non ha
# librerie condivise ELF, e il softoken — che NSS di solito carica con dlopen —
# finisce dentro i programmi. gyp e ninja stanno in
# cross_build/<macchina>/costruzione-strumenti (pip, dentro il progetto).
#
# ! SI COSTRUISCE DA UNA COPIA: build.sh scrive dist/ e out/ accanto ai
# sorgenti, e l'albero di Firefox non si sporca. La copia e' in
# cross_build/<macchina>/costruzione-nss (fuori da MEGA); le modifiche si fanno
# in firefox-main/security/nss, con tools/exilla/tocca.sh.
#
# ! NSPR e' quella dell'albero costruita da mozbuild (tools/exilla/js-costruisci.sh
# build, --enable-nspr-build): qui se ne fanno i tre archivi dagli oggetti.
# =============================================================================
set -e
cd "$(dirname "$0")/../.."
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$RADICE/cross_build/$MACCHINA"
C="$B/costruzione-nss"
O="$RADICE/cross_build/exilla-obj/js"
L="$O/config/external/nspr"
mkdir -p "$C/nspr/lib"

rsync -a --delete --exclude=/out "$RADICE/firefox-main/security/nss/" "$C/nss/"

export PATH="$B/exos-cross/bin:$B/costruzione-strumenti/bin:$PATH"
rm -f "$C"/nspr/lib/*.a
i386-exos-ar rcs "$C/nspr/lib/libnspr4.a" "$L"/pr/*.o
i386-exos-ar rcs "$C/nspr/lib/libplc4.a"  "$L"/libc/*.o
i386-exos-ar rcs "$C/nspr/lib/libplds4.a" "$L"/ds/*.o

# ! LA CPU DI BASE DENTRO CC: gyp non passa CFLAGS a tutti i passi.
CPU="-march=pentium-mmx -mtune=pentium-mmx"
export CC="i386-exos-gcc $CPU -DEXOS_SOLO_POSIX" CCC="i386-exos-g++ $CPU -DEXOS_SOLO_POSIX"
export CXX="$CCC" AR=i386-exos-ar RANLIB=i386-exos-ranlib
export CC_host=gcc CXX_host=g++

# ! SOLO GYP DA build.sh, POI NINJA SUI BERSAGLI SCELTI. Anche con --static,
# «tutto» contiene le librerie condivise (libnss3.so & c.), che EX-OS non sa
# fare. build.sh preferisce ninja-build a ninja: uno finto, che non fa niente,
# gli fa generare i file e fermarsi; poi si chiedono i programmi che servono,
# e ninja costruisce solo le librerie statiche da cui dipendono.
mkdir -p "$C/finto"
printf '#!/bin/sh\nexit 0\n' > "$C/finto/ninja-build"; chmod +x "$C/finto/ninja-build"
BERSAGLI="${BERSAGLI:-tstclnt selfserv certutil pk12util}"

cd "$C/nss"
PATH="$C/finto:$PATH" ./build.sh -g --static -t ia32 --opt \
    --with-nspr="$O/dist/include/nspr:$C/nspr/lib" \
    -DOS=exos -Duse_static_libs=1 -Ddisable_intel_hw_sha=1 -Dsign_libs=0 -Duse_system_zlib=0 \
    -Denable_sslkeylogfile=0 "$@" || true
# ! build.sh finisce scrivendo il pkg-config, e per quello cerca nspr.pc — che
# non c'e', perche' NSPR gliela diamo noi: esce con 2 dopo aver fatto il suo.
# Quel che conta e' che gyp abbia generato i file di ninja.
[ -f out/Release/build.ninja ] || { echo "nss-costruisci: gyp non ha generato out/Release" >&2; exit 1; }
ninja -C out/Release -k 0 -j"$(nproc)" $BERSAGLI
