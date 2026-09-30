#!/bin/sh
# =============================================================================
# tools/exilla/toolchain-questa-macchina.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# La toolchain i386-exos (C e C++) che GIRA SU QUESTA MACCHINA, accanto a
# quella condivisa e senza toccarla (tappa 0 di Exilla, 28 settembre 2026).
#
#     tools/exilla/toolchain-questa-macchina.sh
#
# ! PERCHE' ESISTE. cross_build/exos-cross e' stata costruita su Debian 13: i
# suoi programmi (as, ld, gcc, g++, cc1plus) vogliono GLIBC 2.38 e su Debian
# 12 non partono. Ricostruirla sopra quella condivisa romperebbe l'altro PC
# (ISTRUZIONI-SECONDO-PROFILO.md, 3.3). Si costruisce qui, in
# cross_build/<macchina>/exos-cross.
#
# ! SI RICOSTRUISCE SOLO CIO' CHE GIRA SULL'HOST. Le librerie PER EX-OS —
# libc.a, crt0.o, libgcc.a, libstdc++.a, libsupc++.a, gli header — sono codice
# i386-exos: non dipendono dalla glibc di chi le ha compilate. Si copiano da
# quella condivisa (ORIGINE). Ricompilarle vorrebbe dire rifare anche la parte
# di libstdc++ che su EX-OS e' gia' provata, per ottenere gli stessi byte.
#
# ! LE CARTELLE DI COMPILAZIONE SI CHIAMANO costruzione-*: il .megaignore del
# progetto le esclude da MEGA (sono gigabyte che servono a una macchina sola).
# L'installazione (exos-cross, qualche centinaio di MB) invece passa: e' uno
# strumento, e la regola vuole gli strumenti dentro il progetto.
#
# Variabili:
#     MACCHINA   nome della cartella        (default: hostname in minuscolo)
#     ORIGINE    toolchain da cui prendere le librerie del bersaglio
#                                          (default: cross_build/exos-cross)
#     GCC_SRC    albero di GCC col bersaglio exos (default: gcc/)
#
# Vuole: binutils_2.44.orig.tar.xz (lo scarica), libgmp/mpfr/mpc-dev, flex,
# bison, texinfo, m4 (gperf solo se l'albero di GCC non ha i file generati).
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/../.." && pwd)
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
DEST="$RADICE/cross_build/$MACCHINA"
PREFISSO="$DEST/exos-cross"
ORIGINE="${ORIGINE:-$RADICE/cross_build/exos-cross}"
GCC_SRC="${GCC_SRC:-$RADICE/gcc}"
VERSIONE=$(cat "$GCC_SRC/gcc/BASE-VER")

echo "=== toolchain i386-exos per $MACCHINA ==="
echo "  installazione : $PREFISSO"
echo "  librerie da   : $ORIGINE"
echo "  GCC           : $GCC_SRC ($VERSIONE)"

# Le librerie del bersaglio devono essere della STESSA versione di GCC.
if [ ! -d "$ORIGINE/lib/gcc/i386-exos/$VERSIONE" ]; then
    echo "! in $ORIGINE non c'e' libgcc $VERSIONE: le librerie del bersaglio" >&2
    echo "  vanno ricostruite, non copiate (vedi tools/gcc-exos/leggimi.md)" >&2
    exit 1
fi

# --- 1. binutils -------------------------------------------------------------
if [ ! -x "$PREFISSO/bin/i386-exos-ld" ] || ! "$PREFISSO/bin/i386-exos-ld" --version >/dev/null 2>&1; then
    mkdir -p "$DEST/costruzione-binutils"
    cd "$DEST/costruzione-binutils"
    [ -f binutils_2.44.orig.tar.xz ] || \
        wget -q http://deb.debian.org/debian/pool/main/b/binutils/binutils_2.44.orig.tar.xz
    [ -d binutils-2.44 ] || tar xf binutils_2.44.orig.tar.xz
    TC="$DEST" "$RADICE/tools/binutils-exos/prepara-binutils.sh" "$PREFISSO" \
        "$DEST/costruzione-binutils/binutils-2.44"
fi

# --- 2. gcc e g++ (solo i programmi dell'host) --------------------------------
# Le stesse opzioni della toolchain condivisa (config.log di gcc-build-cxx),
# col prefisso di questa macchina.
if [ ! -x "$PREFISSO/libexec/gcc/i386-exos/$VERSIONE/cc1plus" ]; then
    mkdir -p "$DEST/costruzione-gcc"
    cd "$DEST/costruzione-gcc"
    if [ ! -f config.status ]; then
        PATH="$PREFISSO/bin:$PATH" "$GCC_SRC/configure" \
            --target=i386-exos --prefix="$PREFISSO" \
            --without-headers --with-newlib --disable-nls --disable-shared \
            --disable-threads --disable-libssp --disable-libgomp \
            --disable-libquadmath --disable-libatomic --disable-libvtv \
            --disable-bootstrap --enable-clocale=generic \
            --disable-libstdcxx-pch --enable-languages=c,c++,lto \
            > configure.log 2>&1 || { echo "configure fallito: $DEST/costruzione-gcc/configure.log" >&2; exit 1; }
    fi
    PATH="$PREFISSO/bin:$PATH" make -j"$(nproc)" all-gcc > make.log 2>&1 || {
        echo "make all-gcc fallito: $DEST/costruzione-gcc/make.log" >&2; exit 1; }
    # ! NON SI CREDE AL CODICE DI USCITA (tools/gcc-exos/leggimi.md): un link
    # finale mancato lascia xgcc funzionante e niente dietro.
    for f in xgcc cc1 cc1plus; do
        [ -x "gcc/$f" ] || { echo "! manca gcc/$f dopo make all-gcc" >&2; exit 1; }
    done
    make install-gcc > install.log 2>&1
fi

# --- 3. le librerie del bersaglio, dalla toolchain condivisa --------------------
# cp -n: cio' che install-gcc ha appena messo (gli header di GCC) resta suo.
mkdir -p "$PREFISSO/i386-exos" "$PREFISSO/lib/gcc/i386-exos/$VERSIONE"
cp -an "$ORIGINE/i386-exos/lib"     "$PREFISSO/i386-exos/" 2>/dev/null || true
cp -an "$ORIGINE/i386-exos/include" "$PREFISSO/i386-exos/" 2>/dev/null || true
for f in "$ORIGINE/lib/gcc/i386-exos/$VERSIONE"/*.a "$ORIGINE/lib/gcc/i386-exos/$VERSIONE"/*.o; do
    [ -f "$f" ] && cp -n "$f" "$PREFISSO/lib/gcc/i386-exos/$VERSIONE/"
done
# ! E IL limits.h DI GCC SI PRENDE DALL'ORIGINE, sovrascrivendo (30 settembre
# 2026). GCC ne installa due versioni: quella che fa #include_next del
# limits.h della libc, se in fase di costruzione trova gli header del
# bersaglio, e quella «senza libc sotto» altrimenti. Qui GCC si costruisce
# PRIMA di copiare gli header, e usciva la seconda: PATH_MAX (lib/include/
# limits.h) non arrivava a nessun programma, e se n'e' accorta la shell di
# SpiderMonkey. Il file non dipende dall'host.
cp "$ORIGINE/lib/gcc/i386-exos/$VERSIONE/include/limits.h" \
   "$PREFISSO/lib/gcc/i386-exos/$VERSIONE/include/limits.h"

# --- 4. la verifica: compila e collega, C e C++ ---------------------------------
PROVA="$DEST/costruzione-prova"
mkdir -p "$PROVA"
printf 'int main(void){return 0;}\n' > "$PROVA/c.c"
printf '#include <string>\n#include <vector>\nint main(){std::vector<std::string> v; v.push_back("x"); return (int)v.size()-1;}\n' > "$PROVA/cxx.cc"
"$PREFISSO/bin/i386-exos-gcc" "$PROVA/c.c" -o "$PROVA/c"
"$PREFISSO/bin/i386-exos-g++" -fno-exceptions -fno-rtti "$PROVA/cxx.cc" -o "$PROVA/cxx"
echo "[OK] $("$PREFISSO/bin/i386-exos-g++" --version | head -1)"
echo "     C e C++ compilano e collegano per i386-exos su $MACCHINA"
echo "     (se ne prova l'esecuzione dentro EX-OS: tools/exilla/prova-cxx.sh)"
