#!/bin/sh
# =============================================================================
# tools/ricostruisci-bersaglio.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Ricostruisce TUTTO il codice di terzi compilato per i386-exos, nell'ordine
# in cui dipende da se' stesso.
#
#     tools/ricostruisci-bersaglio.sh [fase ...]     dalla radice del repository
#     tools/ricostruisci-bersaglio.sh --verifica     dice solo se serve farlo
#
# Fasi, in ordine:  libc gcclibs libm libgcc binutils cc1 openssl iso
# Senza argomenti le fa tutte. Sono ore di macchina: la sola `cc1` ne vale
# la maggior parte, perche' va a -j1 (vedi tools/gcc-exos/prepara-cc1.sh).
#
# -----------------------------------------------------------------------------
# ! PERCHE' ESISTE: UN CAMBIO DI ABI NELLA LIBC NON SI VEDE, SI SUBISCE
#
# Il 4 agosto 2026 `time_t` e' passato da `long` a `long long` (32 -> 64
# bit) per non finire nel 2038. E' una riga in lib/include/libc.h, ed e'
# giusta. Ma `struct stat` contiene tre `time_t`:
#
#     ... st_size, st_blksize, st_blocks, st_atime, st_mtime, st_ctime
#         offset 24                       ^^^^ da qui in poi tutto cambia
#
# La struttura e' passata da 48 a 60 byte. Il giorno dopo la libc.a nel
# sysroot era la nuova, e cc1 — RICOLLEGATO ma non RICOMPILATO — era il
# vecchio: i suoi oggetti credevano ancora a 48 byte. Quindi
#
#     fstat (file->fd, &file->st);
#
# scriveva 60 byte dentro un campo che per chi lo legge ne occupa 48, e i
# 12 di troppo finivano sui campi successivi di `struct _cpp_file`:
#
#     struct stat st;   <- 48 byte secondo cc1, 60 secondo la libc
#     size_t limit;     <- calpestato
#     off_t  offset;    <- calpestato
#     int    fd;        <- CALPESTATO dalla meta' bassa di st_mtime
#
# Il sintomo, dopo, non somigliava per niente alla causa: `read()` rendeva
# zero byte senza errore — perche' `fd` era diventato 0, cioe' stdin, e un
# processo in background che legge da stdin trova la fine dell'input — e
# cc1 compilava un file VUOTO in silenzio, con uscita 0. Sono stati due
# giorni di indagine su filesystem, syscall e iconv, tutti innocenti.
#
# Il collegamento NON se ne accorge e non puo': i simboli sono gli stessi,
# e' la forma di cio' che si scambiano a essere cambiata. Per questo la
# regola e' meccanica e sta in uno script invece che nella memoria di
# qualcuno: SE CAMBIA UN TIPO CONDIVISO CON I PROGRAMMI, si ricostruisce
# tutto il bersaglio — non si ricollega.
#
# `--verifica` confronta l'impronta degli header e della libc con quella
# registrata nel sysroot all'ultima ricostruzione completa, e lo dice.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/.." && pwd)
PREFISSO="${PREFISSO:-$HOME/exos-cross}"
SYSROOT="$PREFISSO/i386-exos"
NATIVI="${NATIVI:-$HOME/exos-native}"
BUILD_CC1="${BUILD_CC1:-$HOME/gcc-build-rel}"
BUILD_CXX="${BUILD_CXX:-$HOME/gcc-build-cxx}"
IMPRONTA="$SYSROOT/.abi-libc"

cd "$RADICE"

# ! Il PATH PRIMA di tutto, --verifica compreso: l'impronta la calcola
# i386-exos-gcc, e senza questa riga `make abi` risponderebbe "non
# compila" su una macchina dove il cross c'e' ed e' a posto.
PATH="$PREFISSO/bin:$PATH"
export PATH

# L'impronta e' la FORMA dei tipi che la libc e i programmi si scambiano,
# non il testo dei sorgenti: aggiungere una funzione non rompe niente e non
# deve far gridare al lupo. Il ragionamento per esteso, e cosa mettere
# dentro, stanno in testa a tools/abi-bersaglio.c.
#
# ! Le dimensioni le calcola il COMPILATORE DEL BERSAGLIO e si leggono
# con nm: non c'e' niente da eseguire, e un binario i386-exos su Linux non
# girerebbe comunque.
impronta_corrente() {
    o=$(mktemp --suffix=.o)
    if ! i386-exos-gcc -c -w -I lib/include tools/abi-bersaglio.c -o "$o" 2>/dev/null; then
        rm -f "$o"
        echo "impronta: tools/abi-bersaglio.c non compila con i386-exos-gcc" >&2
        echo "          (il cross e' nel PATH? $PREFISSO/bin)" >&2
        exit 1
    fi
    i386-exos-nm --print-size --defined-only "$o" |
        awk '{print $4, $2}' | sort | sha256sum | cut -d' ' -f1
    rm -f "$o"
}

if [ "$1" = "--verifica" ]; then
    ora=$(impronta_corrente)
    if [ ! -f "$IMPRONTA" ]; then
        echo "ABI: nessuna impronta in $IMPRONTA."
        echo "     Il codice di terzi nel sysroot non si sa contro quale libc"
        echo "     sia stato costruito: tools/ricostruisci-bersaglio.sh"
        exit 1
    fi
    if [ "$ora" != "$(cat "$IMPRONTA")" ]; then
        echo "ABI: la libc e' CAMBIATA dopo l'ultima ricostruzione del bersaglio."
        echo "     I binari per i386-exos (cc1, as, ld, gmp, mpfr, mpc, libm,"
        echo "     libstdc++, libcrypto) vanno RICOSTRUITI, non ricollegati:"
        echo "     tools/ricostruisci-bersaglio.sh"
        exit 1
    fi
    echo "ABI: il bersaglio e' allineato alla libc corrente ($ora)."
    exit 0
fi

# Registra l'impronta senza ricostruire niente. Serve dopo una ripresa a
# fasi separate: le fasi ci sono state tutte, solo non in una volta sola.
# ! E' una DICHIARAZIONE, e vale quanto chi la fa: dirla senza aver
# davvero ricostruito tutto e' peggio che non dire niente.
if [ "$1" = "--impronta" ]; then
    impronta_corrente > "$IMPRONTA"
    echo "[OK] impronta registrata: $(cat "$IMPRONTA")"
    exit 0
fi

FASI="${*:-libc gcclibs libm libgcc binutils cc1 openssl iso}"

fase() {
    echo
    echo "############################################################"
    echo "### $1"
    echo "############################################################"
    date
}

vuoi() {
    for f in $FASI; do [ "$f" = "$1" ] && return 0; done
    return 1
}

# --- libc: gli header e libc.a nel sysroot -----------------------------------
# Prima di tutto, e da sola non basta mai: e' il punto di partenza di cui
# tutto il resto e' funzione.
if vuoi libc; then
    fase "libc.a e header nel sysroot"
    tools/gcc-exos/prepara-cross.sh "$PREFISSO"
fi

# --- GMP, MPFR, MPC: cc1 ci si LEGA, quindi vengono prima --------------------
if vuoi gcclibs; then
    fase "GMP, MPFR, MPC per i386-exos"
    tools/gcclibs-exos/prepara-gcclibs.sh "$PREFISSO" "$NATIVI"
fi

# --- openlibm: -lm, e il configure di libstdc++ la interroga ------------------
if vuoi libm; then
    fase "openlibm (libm.a)"
    tools/openlibm-exos/prepara-libm.sh "$PREFISSO" \
        "$(ls -d "$NATIVI"/openlibm-*/ | head -1)"
fi

# --- libgcc e libstdc++ del bersaglio ----------------------------------------
#
# ! SI CANCELLANO LE DIRECTORY, non si fa `make clean`: gli header del
# sysroot non sono una dipendenza che `make` conosca: sono fuori
# dall'albero. Senza cancellare, make direbbe "non c'e' niente da fare" e
# lascerebbe in giro proprio gli oggetti sbagliati che si vogliono
# buttare — che e' come e' nato il difetto in testa a questo file.
if vuoi libgcc; then
    fase "libgcc e libstdc++ per i386-exos"
    rm -rf "$BUILD_CXX/i386-exos/libgcc" "$BUILD_CXX/i386-exos/libstdc++-v3"
    # ! all-gcc PRIMA e a -j1, ESPLICITO. Le librerie del bersaglio lo
    # tirano dentro da sole come dipendenza, e a quel punto lo farebbero
    # con il -j delle righe qui sotto: su 4 GB di RAM due gimple-match-*.cc
    # insieme fanno intervenire il killer OOM, e una build morta cosi' dice
    # «Terminato», che sembra un errore di compilazione e non lo e'.
    # Il compilatore del cross va ricostruito comunque, perche' un albero
    # GCC toccato (qui: libcpp/files.cc) invalida i suoi oggetti.
    make -C "$BUILD_CXX" -j1 all-gcc
    make -C "$BUILD_CXX" -j2 all-target-libgcc
    make -C "$BUILD_CXX" install-target-libgcc
    make -C "$BUILD_CXX" -j2 all-target-libstdc++-v3
    make -C "$BUILD_CXX" install-target-libstdc++-v3
fi

# --- as e ld NATIVI, quelli che girano dentro EX-OS --------------------------
#
# ! Il configure va RIFATTO, non solo il make: libiberty compila una
# propria copia delle funzioni che l'ospite non ha, e quali siano lo
# decide il configure interrogando la libc. Vedi
# tools/binutils-exos/leggimi.md.
if vuoi binutils; then
    fase "binutils nativi (as, ld dentro EX-OS)"
    ALBERO=$(ls -d "$NATIVI"/binutils-*/ | head -1)
    rm -rf "$NATIVI/build-nativi"
    mkdir -p "$NATIVI/build-nativi"
    cd "$NATIVI/build-nativi"
    CC="i386-exos-gcc -std=gnu17" "$ALBERO/configure" \
        --build=x86_64-pc-linux-gnu --host=i386-exos --target=i386-exos \
        --prefix=/usr --disable-nls --disable-werror \
        --disable-shared --enable-static \
        --disable-plugins --disable-gprofng \
        --without-zstd --without-msgpack --without-debuginfod
    make -j2
    cd "$RADICE"
    for b in gas/as-new ld/ld-new; do
        [ -x "$NATIVI/build-nativi/$b" ] || {
            echo "  ! manca $NATIVI/build-nativi/$b" >&2; exit 1; }
    done
    echo "  [OK] as e ld nativi"
fi

# --- cc1: il canadian cross, da zero -----------------------------------------
#
# ! DIRECTORY NUOVA. Non e' prudenza: --enable-checking=release cambia
# macro incluse dappertutto e il Makefile non se ne accorge (vedi
# prepara-cc1.sh), e qui in piu' c'e' proprio il motivo di questo script —
# gli oggetti vecchi hanno l'ABI vecchia e nessuno li ricompilerebbe.
if vuoi cc1; then
    fase "cc1, xgcc, cpp (canadian cross) — la fase lunga, a -j1"
    rm -rf "$BUILD_CC1"
    tools/gcc-exos/prepara-cc1.sh "$BUILD_CC1"
    ( cd "$BUILD_CC1" && export ac_cv_c_bigendian=no && make -j1 all-gcc )
    # La regola di verifica di tools/gcc-exos/leggimi.md: non ci si fida
    # del codice di uscita, si guarda che i binari ci siano tutti.
    for b in cc1 xgcc cpp; do
        [ -x "$BUILD_CC1/gcc/$b" ] || {
            echo "  ! manca $BUILD_CC1/gcc/$b" >&2; exit 1; }
        echo "  $b: $(du -h "$BUILD_CC1/gcc/$b" | cut -f1)"
    done
fi

# --- OpenSSL -----------------------------------------------------------------
if vuoi openssl; then
    fase "OpenSSL (libcrypto.a)"
    tools/openssl-exos/prepara-openssl.sh
    # ! libcrypto.a E BASTA, non `build_libs`. Quello tira dentro anche
    # libssl, che NON si costruisce: ssl/rio/ di OpenSSL 4.x vuole fd_set
    # e il polling sui socket, cioe' un BIO sopra lo stack IPC — lavoro
    # vero, non uno stub, e finche' non c'e' la fase intera morirebbe qui
    # portandosi dietro le successive.
    ( cd "$HOME/openssl-build-exos" && make -j2 libcrypto.a )
fi

# --- Il CD, con dentro tutto quanto sopra ------------------------------------
if vuoi iso; then
    fase "CD degli strumenti"
    make iso
fi

# L'impronta si scrive SOLO alla fine e SOLO se sono state fatte tutte le
# fasi: un mondo ricostruito a meta' non e' allineato, e dichiararlo tale
# sarebbe peggio che non dichiararlo affatto.
if [ $# -eq 0 ]; then
    impronta_corrente > "$IMPRONTA"
    echo
    echo "[OK] bersaglio ricostruito e allineato: $(cat "$IMPRONTA")"
fi
