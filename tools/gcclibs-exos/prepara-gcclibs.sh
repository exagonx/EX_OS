#!/bin/sh
# =============================================================================
# tools/gcclibs-exos/prepara-gcclibs.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce GMP, MPFR e MPC PER EX-OS e li installa nel sysroot del
# bersaglio. Sono le tre librerie a cui `cc1` si linka
# (GMPLIBS = -lmpc -lmpfr -lgmp nel Makefile di GCC): senza, un compilatore
# ospitato non si puo' nemmeno cominciare.
#
#     tools/gcclibs-exos/prepara-gcclibs.sh [prefisso] [sorgenti]
#
#     prefisso   la radice del cross          (default: ~/exos-cross)
#     sorgenti   dove stanno gli alberi       (default: ~/exos-native)
#
# ⚠️ QUESTO SCRIPT NON SCARICA NIENTE, come gli altri del progetto:
#
#     wget https://gmplib.org/download/gmp/gmp-6.3.0.tar.xz
#     wget https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.1.tar.xz
#     wget https://ftp.gnu.org/gnu/mpc/mpc-1.3.1.tar.gz
#
# Provato con GMP 6.3.0, MPFR 4.2.1, MPC 1.3.1.
#
# -----------------------------------------------------------------------------
# LE TRE COSE CHE NON SONO OVVIE
#
# 1. CC_FOR_BUILD=gcc, E NON E' RIDONDANTE.
#
#    GMP compila dei generatori di tabelle (gen-fac, gen-fib, gen-bases…)
#    che devono girare sulla macchina che COMPILA, e per sceglierne il
#    compilatore fa la prova piu' ragionevole del mondo: ne compila uno e
#    lo esegue. La prova RIESCE con il cross-compilatore, perche' un
#    binario di EX-OS e' un ELF32 i386 statico e Linux lo carica; e le
#    syscall di EX-OS hanno i numeri di Linux, quindi la printf funziona
#    pure. Il programma parte, stampa, e sembra tutto a posto.
#
#    Quello che NON combacia sono argc e argv, che EX-OS passa a modo suo:
#
#        ./gen-fac 32 0 >fac_table.h
#        Usage: gen-fac limbbits nailbits
#
#    cioe' un generatore che non vede i propri argomenti e si rifiuta di
#    generare. Dirgli esplicitamente quale compilatore usare toglie di
#    mezzo la domanda.
#
# 2. --host=i486-pc-exos, NON i386.
#
#    Con `i386` GMP compila tutto con -march=i386 -mtune=i386, e su quella
#    strada GCC 17 (snapshot 20260801) emette
#
#        rolw $8, %eax
#
#    cioe' una rotazione a 16 bit scritta con il nome del registro a 32, che
#    l'assemblatore rifiuta: "incorrect register `%eax' used with `w'
#    suffix". E' un difetto A MONTE — si riproduce in tre righe di C, vedi
#    HANDOFF.md — e non riguarda il nostro bersaglio.
#
#    i486 non e' un ripiego per aggirarlo: e' il minimo che EX-OS gia'
#    richiede per conto suo (kernel/mm/paging.c usa `invlpg`, che e' 486+).
#    Con i486 c'e' `bswap` e la strada rotta non si percorre.
#
# 3. L'ORDINE E' OBBLIGATO: GMP, poi MPFR, poi MPC.
#
#    Ognuna si configura contro la precedente gia' INSTALLATA, non contro
#    la sua directory di build. Per questo si installa dopo ogni passo
#    invece che tutto alla fine.
# =============================================================================

set -e

PREFISSO="${1:-$HOME/exos-cross}"
SORGENTI="${2:-$HOME/exos-native}"
SYSROOT="$PREFISSO/i386-exos"

if [ ! -x "$PREFISSO/bin/i386-exos-gcc" ]; then
    echo "prepara-gcclibs: manca $PREFISSO/bin/i386-exos-gcc" >&2
    echo "  Prima serve il cross: vedi tools/gcc-exos/leggimi.md" >&2
    exit 1
fi

PATH="$PREFISSO/bin:$PATH"
export PATH

RADICE=$(cd "$(dirname "$0")/../.." && pwd)
APPLICA="$RADICE/tools/gcclibs-exos/applica.py"

# Il compilatore e' sempre quello del bersaglio i386-exos, anche quando il
# triplo host dice i486: quello che cambia con i486 sono i flag -march che
# la libreria si sceglie, non lo strumento.
CROSS_CC="i386-exos-gcc -std=gnu17"
COMUNI="--build=x86_64-pc-linux-gnu --host=i486-pc-exos --prefix=$SYSROOT
        --disable-shared --enable-static"

costruisci() {
    nome="$1"      # gmp / mpfr / mpc
    shift
    albero=$(ls -d "$SORGENTI"/$nome-*/ 2>/dev/null | head -1)

    if [ -z "$albero" ] || [ ! -d "$albero" ]; then
        echo "  ! sorgenti di $nome non trovati in $SORGENTI" >&2
        exit 1
    fi
    albero=$(cd "$albero" && pwd)

    echo
    echo "=== $nome ==="
    echo "  sorgenti: $albero"

    python3 "$APPLICA" "$albero"

    costruzione="$SORGENTI/build-$nome"
    rm -rf "$costruzione"
    mkdir -p "$costruzione"
    cd "$costruzione"

    # shellcheck disable=SC2086
    "$albero/configure" $COMUNI "$@" \
        CC="$CROSS_CC" CC_FOR_BUILD=gcc > configure.log 2>&1 || {
            echo "  ! configure fallito: vedi $costruzione/configure.log" >&2
            exit 1
        }

    # ⚠️ -j2 e non -j$(nproc): vedi la nota sulla memoria in
    # tools/binutils-exos/leggimi.md. Su una macchina da 4 GB un -j4 su
    # questi alberi manda in swap.
    make -j2 > make.log 2>&1 || {
        echo "  ! make fallito: vedi $costruzione/make.log" >&2
        exit 1
    }

    make install > install.log 2>&1 || {
        echo "  ! make install fallito: vedi $costruzione/install.log" >&2
        exit 1
    }

    echo "  [OK] $nome installata"
}

echo "=== GMP, MPFR e MPC per EX-OS ==="
echo "  sysroot: $SYSROOT"

costruisci gmp
costruisci mpfr --with-gmp="$SYSROOT"
costruisci mpc  --with-gmp="$SYSROOT" --with-mpfr="$SYSROOT"

# --- Verifica -----------------------------------------------------------------
#
# Non ci si fida del codice di uscita, come per binutils: si guarda che i
# tre archivi ci siano E che un programma che li usa tutti e tre si
# COLLEGHI. Un archivio presente ma con dentro simboli irrisolti non si
# distingue in nessun altro modo.
echo
for l in gmp mpfr mpc; do
    if [ ! -f "$SYSROOT/lib/lib$l.a" ]; then
        echo "  ! manca $SYSROOT/lib/lib$l.a" >&2
        exit 1
    fi
    echo "  lib$l.a: $(wc -c < "$SYSROOT/lib/lib$l.a") byte"
done

TMPBIN=$(mktemp -u)
if i386-exos-gcc -O2 -o "$TMPBIN" "$RADICE/tools/iso/prova-mp.c" \
       -lmpc -lmpfr -lgmp 2>/dev/null; then
    echo "  [OK] tools/iso/prova-mp.c si collega contro tutte e tre"
    rm -f "$TMPBIN"
else
    echo "  ! tools/iso/prova-mp.c NON si collega" >&2
    exit 1
fi

echo
echo "[OK] le tre librerie sono in $SYSROOT/lib"
echo "     Per provarle DENTRO EX-OS: make iso && make run-iso,"
echo "     poi /cdrom/bin/provamp"
