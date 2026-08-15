#!/bin/sh
# =============================================================================
# tools/binutils-exos/prepara-binutils.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce e installa i binutils VERI per il bersaglio i386-exos, al
# posto dei wrapper che tools/gcc-exos/prepara-cross.sh mette attorno agli
# strumenti di sistema.
#
#     tools/binutils-exos/prepara-binutils.sh [prefisso] [sorgenti]
#
#     prefisso   dove installare              (default: ~/exos-cross)
#     sorgenti   albero binutils gia' scompattato, oppure il tarball
#                (default: cerca ./binutils-*/ nella directory di lavoro)
#
# PERCHE' SOSTITUIRE I WRAPPER. Finche' si compila SU Linux PER EX-OS, i
# wrapper bastano: il formato di uscita e' ELF32 i386, cioe' quello che
# l'`as` di sistema produce con --32. Cambiano tre cose quando si comincia
# a fare sul serio:
#
#   1. `ld` non conosce il bersaglio, quindi non puo' avere un emulazione
#      predefinita per lui: ogni link dipende dal -m elf_i386 che passa
#      GCC, e chi invoca `ld` a mano se lo dimentica.
#   2. Gli strumenti di ispezione (nm, objdump, readelf) di sistema
#      funzionano per caso, perche' il formato coincide. Il giorno che
#      EX-OS avesse una sola convenzione propria — una nota in un segmento,
#      un tipo di rilocazione — smetterebbero senza dirlo.
#   3. Soprattutto: un binutils NATIVO, quello che girera' dentro EX-OS,
#      si costruisce solo a partire da un binutils CROSS. Questo e' il
#      primo dei due passi, e senza non c'e' il secondo.
#
# COSA CAMBIA NEI SORGENTI: quattro righe, in quattro file, che dicono
# "exos e' un sistema operativo" e "per lui il formato e' ELF32 i386".
# Le applica tools/binutils-exos/applica.py — vedi li' il dettaglio e la
# nota sulla licenza (binutils e' GPLv3+).
#
# ! DOVE PRENDERE I SORGENTI. Questo script non scarica niente: la rete
# non c'e' su tutte le macchine e un download silenzioso dentro uno script
# di build e' il genere di cosa che si scopre quando fallisce. Con Debian:
#
#     apt-get source binutils           # oppure
#     wget http://deb.debian.org/debian/pool/main/b/binutils/binutils_2.44.orig.tar.xz
#     tar xf binutils_2.44.orig.tar.xz
#
# Provato con binutils 2.44.
# =============================================================================

set -e

PREFISSO="${1:-$HOME/exos-cross}"
SORGENTI="$2"

# --- 1. trova i sorgenti ------------------------------------------------------
if [ -z "$SORGENTI" ]; then
    SORGENTI=$(ls -d binutils-*/ 2>/dev/null | head -1)
fi

if [ -z "$SORGENTI" ] || [ ! -d "$SORGENTI" ]; then
    echo "Sorgenti di binutils non trovati." >&2
    echo "Uso: $0 [prefisso] <albero-binutils>" >&2
    echo "Vedi il commento in testa a questo script per dove prenderli." >&2
    exit 1
fi

SORGENTI=$(cd "$SORGENTI" && pwd)
RADICE=$(cd "$(dirname "$0")/../.." && pwd)

echo "=== binutils per i386-exos ==="
echo "  sorgenti : $SORGENTI"
echo "  prefisso : $PREFISSO"
echo

# --- 2. applica il bersaglio --------------------------------------------------
python3 "$RADICE/tools/binutils-exos/applica.py" "$SORGENTI"

# La prova che la modifica a config.sub ha preso: senza, ogni configure
# risponde "Invalid configuration" e la causa non e' evidente.
echo
echo "config.sub riconosce il bersaglio: $("$SORGENTI/config.sub" i386-exos)"

# --- 3. costruisci fuori dall'albero ------------------------------------------
COSTRUZIONE="$SORGENTI/../build-binutils-exos"
mkdir -p "$COSTRUZIONE"
cd "$COSTRUZIONE"

echo
echo "=== configure ==="
"$SORGENTI/configure" --target=i386-exos --prefix="$PREFISSO" \
    --disable-nls --disable-werror --with-sysroot > configure.log 2>&1 || {
        echo "configure fallito: vedi $COSTRUZIONE/configure.log" >&2
        exit 1
    }

echo "=== make ==="
make -j"$(nproc)" > make.log 2>&1 || {
    echo "make fallito: vedi $COSTRUZIONE/make.log" >&2
    exit 1
}

echo "=== make install ==="
make install > install.log 2>&1

# --- 4. verifica --------------------------------------------------------------
#
# Non ci si fida del codice di uscita: la stessa regola gia' imparata con
# GCC (vedi tools/gcc-exos/leggimi.md). Si guarda che i binari ci siano E
# che siano binari, non i wrapper di prima.
echo
for t in as ld ar ranlib nm objcopy objdump strip readelf; do
    B="$PREFISSO/bin/i386-exos-$t"
    if [ ! -x "$B" ]; then
        echo "  ! manca $B" >&2
        exit 1
    fi
    case "$(file -b "$B")" in
        *"shell script"*)
            echo "  ! $B e' ancora un wrapper" >&2
            exit 1
            ;;
    esac
done

echo "[OK] binutils installati in $PREFISSO"
echo "     $("$PREFISSO/bin/i386-exos-as" --version | head -1)"
echo "     $("$PREFISSO/bin/i386-exos-ld" --version | head -1)"
echo
echo "     emulazioni di ld: $("$PREFISSO/bin/i386-exos-ld" -V 2>/dev/null | \
                               sed -n '2,4p' | tr -d ' ' | tr '\n' ' ')"
