#!/bin/sh
# =============================================================================
# tools/gcc-exos/prepara-cc1.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Configura il CANADIAN CROSS di GCC: un compilatore che GIRA su EX-OS e
# PRODUCE codice per EX-OS, costruito su Linux.
#
#     tools/gcc-exos/prepara-cc1.sh [directory-di-build] [albero-gcc]
#
# -----------------------------------------------------------------------------
# ! TRE MACCHINE, NON DUE, E VANNO TENUTE DISTINTE
#
#     --build   x86_64-pc-linux-gnu   chi COMPILA (questa macchina)
#     --host    i386-exos             dove GIRERA' cc1
#     --target  i386-exos             per chi cc1 produrra' codice
#
# Il cross normale (tools/gcc-exos/prepara-cross.sh) ha build == host: gira
# qui e produce per EX-OS. Questo no: il binario prodotto non e' eseguibile
# sulla macchina che lo costruisce, e questa e' l'unica differenza — ma e'
# quella che fa fallire meta' dei test di configure, perche' non possono
# ESEGUIRE niente per sapere la risposta.
#
# -----------------------------------------------------------------------------
# ! LE RISPOSTE PRECOTTE, e perche' NON sono bugie
#
# Ogni `ac_cv_*` qui sotto e' un fatto verificabile sul bersaglio, che
# configure non riesce a stabilire da solo. Non si mette una risposta per
# far passare la compilazione: si mette perche' la risposta e' nota.
#
#   ac_cv_c_bigendian=no
#       i386 e' little-endian. Il test fallisce per un difetto SUO, non
#       per un'incognita: prova quattro strade e l'ultima, quella che
#       cerca una stringa magica nell'oggetto, non compila in C++ perche'
#       usa un'inizializzazione con restringimento:
#           error: narrowing conversion of '35283' from 'int' to 'short'
#       Le altre tre falliscono perche' <sys/param.h> di EX-OS non
#       definisce BYTE_ORDER. La risposta resta comunque "no".
#
# Se ne servissero altre, si aggiungono QUI con la stessa regola: prima si
# stabilisce il fatto, poi lo si scrive.
#
# -----------------------------------------------------------------------------
# ! --disable-fixincludes, E NON E' UN AGGIRAMENTO
#
# fixincludes esiste per CORREGGERE GLI HEADER DI SISTEMA rotti dell'ospite
# — i vecchi <sys/*.h> di SunOS, HP-UX, IRIX — riscrivendoli in una copia
# privata di GCC. Qui si configura con --without-headers: header di sistema
# da correggere non ce ne sono.
#
# L'opzione da sola non basta: mette STMP_FIXINC='' dentro gcc/, ma il
# Makefile di primo livello costruisce la directory lo stesso, e fixincl.c
# usa fork() — che su EX-OS non c'e' e non ci sara'. Per questo
# tools/gcc-exos/applica.py toglie `fixincludes` da noconfigdirs per
# *-exos*. Le due cose vanno insieme.
#
# -----------------------------------------------------------------------------
# ! --prefix=/exos E' IL PERCORSO DENTRO EX-OS, non su Linux
#
# Ci finiscono i percorsi che cc1 e il driver useranno A RUNTIME per
# cercarsi fra loro e per trovare gli header. Devono essere i percorsi del
# sistema di destinazione: metterci quelli di questa macchina darebbe un
# compilatore che cerca /home/... su un sistema dove quella directory non
# esiste.
#
# ! -j1 E NON -j$(nproc): vedi tools/gcc-exos/applica.py. Su 4 GB di RAM i
# file gimple-match-*.cc arrivano a 1,5 GB di picco ciascuno.
#
# -----------------------------------------------------------------------------
# ! --enable-checking=release, E NON E' UN'OTTIMIZZAZIONE PRUDENZIALE
#
# Senza questa opzione, un albero configurato con --disable-bootstrap eredita
# il default di stage1: --enable-checking=yes,types,extra. Sono i controlli
# di coerenza interni che GCC usa per sviluppare SE STESSO — verifiche di
# tipo su ogni accesso agli alberi, invarianti sul GC, assert ovunque. Non
# servono a chi compila un programma: servono a chi modifica GCC.
#
# Il prezzo si vede solo quando si prova a metterlo su una macchina vera:
# il cc1 costruito con quei controlli pesa 39,6 MB spogliato dei simboli,
# e EX-OS gira su 32 MB di RAM. Il caricamento ELF e' a richiesta, quindi
# non deve starci tutto insieme — ma ogni pagina toccata arriva dal disco
# a 0,75 MB/s in PIO, e 40 MB di codice sono 40 MB da leggere.
#
# `release` non toglie gli assert utili: lascia i controlli che segnalano
# un compilatore rotto e toglie quelli che servono a chi lo sta scrivendo.
# E' cio' con cui e' costruito il GCC di qualunque distribuzione.
#
# ! NON SI PUO' AGGIUNGERE A UN ALBERO GIA' CONFIGURATO. L'opzione cambia
# delle macro incluse dappertutto: gli oggetti gia' compilati sono
# incompatibili con quelli nuovi e il Makefile non se ne accorge. Va usata
# una directory di build NUOVA — che e' anche il motivo per cui questo
# script prende la directory come argomento.
# =============================================================================

set -e

BUILD="${1:-$HOME/gcc-build-canadian}"
SORGENTI="${2:-$(cd "$(dirname "$0")/../.." && pwd)/gcc}"
# ! I LINGUAGGI SONO UN ARGOMENTO, e il default sono DUE: `c,c++`.
# Con il solo `c` si ottiene cc1 e basta, cioe' un sistema che compila C e
# non C++ — e il buco non si vede finche' qualcuno non prova a compilare un
# .cpp dentro EX-OS e scopre che cc1plus non esiste. La libstdc++ per il
# bersaglio c'e' gia' nel sysroot: manca solo il compilatore che la usi.
LINGUE="${3:-c,c++}"
PREFISSO="$HOME/exos-cross"
SYSROOT="$PREFISSO/i386-exos"

if [ ! -f "$SORGENTI/gcc/config.gcc" ]; then
    echo "prepara-cc1: '$SORGENTI' non e' un albero di GCC" >&2
    exit 1
fi

if [ ! -x "$PREFISSO/bin/i386-exos-gcc" ]; then
    echo "prepara-cc1: manca il cross i386-exos-gcc" >&2
    echo "  si prepara con tools/gcc-exos/prepara-cross.sh" >&2
    exit 1
fi

# ! cc1 si LEGA a GMP, MPFR e MPC: devono essere gia' compilate PER
# i386-exos, non quelle di sistema. Le prepara tools/gcclibs-exos/.
for l in libgmp.a libmpfr.a libmpc.a; do
    if [ ! -f "$SYSROOT/lib/$l" ]; then
        echo "prepara-cc1: manca $SYSROOT/lib/$l" >&2
        echo "  si preparano con tools/gcclibs-exos/prepara-gcclibs.sh" >&2
        exit 1
    fi
done

PATH="$PREFISSO/bin:$PATH"
export PATH

echo "=== canadian cross di GCC per EX-OS ==="
echo "  build    : x86_64-pc-linux-gnu (questa macchina)"
echo "  host     : i386-exos           (dove girera' cc1)"
echo "  target   : i386-exos           (per chi produrra' codice)"
echo "  lingue   : $LINGUE"
echo "  build dir: $BUILD"

mkdir -p "$BUILD"
cd "$BUILD"

# Le risposte precotte: vedi il commento in testa. Vanno ESPORTATE, o le
# sotto-configure lanciate da `make` non le vedono.
export ac_cv_c_bigendian=no

"$SORGENTI/configure" \
    --build=x86_64-pc-linux-gnu --host=i386-exos --target=i386-exos \
    --prefix=/exos --with-sysroot= \
    --enable-languages="$LINGUE" --without-headers --with-newlib \
    --disable-nls --disable-shared --disable-threads \
    --disable-libssp --disable-libgomp --disable-libquadmath \
    --disable-libatomic --disable-libvtv --disable-libstdcxx \
    --disable-bootstrap --disable-lto --disable-plugin \
    --disable-fixincludes --enable-checking=release \
    --with-gmp="$SYSROOT" --with-mpfr="$SYSROOT" --with-mpc="$SYSROOT"

echo
echo "[OK] configurato. Per costruire:"
echo
echo "    cd $BUILD"
echo "    export ac_cv_c_bigendian=no"
echo "    make -j1 all-gcc          # ! -j1: 4 GB di RAM, vedi applica.py"
echo
echo "Il risultato e' gcc/cc1, gcc/cc1plus e gcc/xgcc, eseguibili SU EX-OS."
