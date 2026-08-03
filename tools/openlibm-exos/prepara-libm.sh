#!/bin/sh
# =============================================================================
# tools/openlibm-exos/prepara-libm.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce openlibm per EX-OS e lo installa come libm.a nel sysroot del
# bersaglio.
#
#     tools/openlibm-exos/prepara-libm.sh [prefisso] [sorgenti]
#
#     prefisso   la radice del cross     (default: ~/exos-cross)
#     sorgenti   l'albero di openlibm    (default: ~/exos-native/openlibm-*)
#
# ⚠️ NON SCARICA NIENTE, come gli altri script del progetto:
#
#     wget https://github.com/JuliaMath/openlibm/archive/refs/tags/v0.8.7.tar.gz
#
# Provato con openlibm 0.8.7.
#
# -----------------------------------------------------------------------------
# PERCHE' UNA LIBM DI TERZI E NON LA NOSTRA
#
# Fino ad agosto 2026 <math.h> dichiarava tre funzioni e diceva che una
# libm non c'era, perche' «una sqrt quasi giusta e' peggio di nessuna sqrt:
# sbaglia in silenzio». Quel ragionamento non e' cambiato — e' cambiata la
# conseguenza. La risposta coerente non era scriverne una mediocre: era
# portarne una vera.
#
# openlibm e' la `msun` di FreeBSD in versione autonoma (MIT/BSD): con
# trent'anni di correzioni sugli arrotondamenti che nessuno rifarebbe
# meglio, e con una directory `i387` che usa le istruzioni dell'x87 dove
# convengono.
#
# CHI LA CHIEDE: libstdc++. Il suo <cmath> scrive `using ::sin;` per circa
# centottanta nomi, e quei nomi devono ESISTERE o la libreria non compila.
# Senza libm non c'e' libstdc++, e senza libstdc++ non c'e' cc1plus.
#
# -----------------------------------------------------------------------------
# ⚠️ ARCH=i387 E OS=Linux, E NESSUNO DEI DUE E' UNA BUGIA
#
# ARCH sceglie le implementazioni in assembly x87 — che e' il coprocessore
# che EX-OS ha e inizializza (kernel/include/fpu.h). OS serve solo a
# openlibm per decidere il formato della libreria e i flag: "Linux"
# significa "ELF con le convenzioni di sempre", che e' esattamente il
# nostro caso. Non c'e' un OS=exos da aggiungere perche' non ci sarebbe
# niente da metterci dentro di diverso.
#
# -----------------------------------------------------------------------------
# ⚠️ sqrt RESTA NELLA LIBC, e la doppia definizione non e' un problema
#
# libm.a definisce `sqrt` (i387/e_sqrt.S) e la nostra libc pure — la
# versione a due istruzioni con il controllo di EDOM, vedi lib/libc.c.
# Provato in entrambi gli ordini di collegamento: vince sempre quella
# della libc, perche' libc.o viene tirato dentro comunque (printf, crt0) e
# a quel punto il simbolo e' gia' risolto, quindi e_sqrt.S.o non entra.
# Non c'e' "multiple definition".
#
# La conseguenza utile e' che i programmi di EX-OS possono usare sqrt
# SENZA -lm, che e' cio' che fa /bin/libctest.
# =============================================================================

set -e

QUI=$(cd "$(dirname "$0")" && pwd)

PREFISSO="${1:-$HOME/exos-cross}"
SORGENTI="$2"
SYSROOT="$PREFISSO/i386-exos"

if [ -z "$SORGENTI" ]; then
    SORGENTI=$(ls -d "$HOME"/exos-native/openlibm-*/ 2>/dev/null | head -1)
fi

if [ -z "$SORGENTI" ] || [ ! -f "$SORGENTI/Make.inc" ]; then
    echo "prepara-libm: sorgenti di openlibm non trovati" >&2
    echo "  Uso: $0 [prefisso] <albero-openlibm>" >&2
    exit 1
fi
SORGENTI=$(cd "$SORGENTI" && pwd)

if [ ! -x "$PREFISSO/bin/i386-exos-gcc" ]; then
    echo "prepara-libm: manca $PREFISSO/bin/i386-exos-gcc" >&2
    exit 1
fi

PATH="$PREFISSO/bin:$PATH"
export PATH

echo "=== openlibm per EX-OS ==="
echo "  sorgenti : $SORGENTI"
echo "  sysroot  : $SYSROOT"

cd "$SORGENTI"
make clean > /dev/null 2>&1 || true
make ARCH=i387 OS=Linux USEGCC=1 \
     CC=i386-exos-gcc AR=i386-exos-ar RANLIB=i386-exos-ranlib \
     libopenlibm.a > costruzione.log 2>&1 || {
        echo "  ! make fallito: vedi $SORGENTI/costruzione.log" >&2
        exit 1
     }

cp libopenlibm.a "$SYSROOT/lib/libm.a"

# --- La funzione che a openlibm manca ----------------------------------------
#
# ⚠️ nearbyintl NON C'E' in openlibm 0.8.7: src/s_nearbyint.c genera solo
# le versioni double e float. Non e' una nostra configurazione sbagliata,
# e' una lacuna a monte — `rintl` c'e' ed e' pure in assembly x87, manca
# solo l'involucro che non alza INEXACT.
#
# Costa cara: il configure della libstdc++ compila UN SOLO programma che
# usa tutte le funzioni C99 di <math.h>, e una sola assenza gli fa
# dichiarare non conforme l'header intero. Il risultato e' che decine di
# funzioni che ci sono non finiscono in `namespace std`.
#
# Si aggiunge all'ARCHIVIO e non alla libc, cosi' resta vero che «se e'
# dichiarata in math.h sta in libm.a e vuole -lm». Il perche' esteso sta
# in testa a nearbyintl-exos.c.
echo "  aggiungo nearbyintl (manca a openlibm 0.8.7)"
i386-exos-gcc -O2 -c -o "$SORGENTI/nearbyintl-exos.o" \
    "$QUI/nearbyintl-exos.c" || {
        echo "  ! compilazione di nearbyintl-exos.c fallita" >&2
        exit 1
    }
i386-exos-ar rcs "$SYSROOT/lib/libm.a" "$SORGENTI/nearbyintl-exos.o"
rm -f "$SORGENTI/nearbyintl-exos.o"

# --- Verifica -----------------------------------------------------------------
#
# Non basta che l'archivio esista: si guarda che ci siano dentro i nomi che
# <cmath> di libstdc++ pretende, e che un programma che li usa si COLLEGHI
# davvero. Un archivio con dentro solo meta' delle funzioni si distingue
# in un modo solo: provandolo.
echo
MANCANTI=""
for f in sin cos tan exp log log10 pow atan2 sinh cosh tanh asin acos atan \
         floor ceil fmod modf hypot nearbyintl; do
    if ! i386-exos-nm --defined-only "$SYSROOT/lib/libm.a" 2>/dev/null \
         | grep -qE "^[0-9a-f]+ [TW] $f\$"; then
        MANCANTI="$MANCANTI $f"
    fi
done

if [ -n "$MANCANTI" ]; then
    echo "  ! mancano da libm.a:$MANCANTI" >&2
    exit 1
fi

TMPC=$(mktemp --suffix=.c)
TMPB=$(mktemp -u)
cat > "$TMPC" <<'EOF'
#include <math.h>
int main(void) { return (int)(sin(1.0) + pow(2.0, 3.0) + log(M_E) + hypot(3,4)); }
EOF
if i386-exos-gcc -O2 -o "$TMPB" "$TMPC" -lm 2>/dev/null; then
    echo "  [OK] un programma che usa la libm si collega"
    rm -f "$TMPB"
else
    echo "  ! un programma che usa la libm NON si collega" >&2
    rm -f "$TMPC"
    exit 1
fi
rm -f "$TMPC"

echo "     libm.a: $(wc -c < "$SYSROOT/lib/libm.a") byte"
echo
echo "[OK] libm installata in $SYSROOT/lib/libm.a"
echo "     I prototipi stanno in lib/include/math.h, RICAVATI dai simboli"
echo "     dell'archivio: se una funzione e' dichiarata li', esiste."
