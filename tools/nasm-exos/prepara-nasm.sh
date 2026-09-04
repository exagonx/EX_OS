#!/bin/sh
# =============================================================================
# tools/nasm-exos/prepara-nasm.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Costruisce NASM PER EX-OS: un assemblatore che gira DENTRO il sistema.
#
#     tools/nasm-exos/prepara-nasm.sh [sorgenti] [uscita]
#
#     sorgenti   albero di NASM gia' scompattato   (default: ./nasm)
#     uscita     dove lasciare i binari            (default: ~/exos-native/build-nasm)
#
# Il risultato finisce sul CD degli strumenti come /exos/bin/nasm e
# /bin/nasm: lo copia la regola di dist/exos-tools.iso nel Makefile.
#
# -----------------------------------------------------------------------------
# PERCHE' UN SECONDO ASSEMBLATORE, SE C'E' GIA' `as`
#
# Non e' un doppione, sono due lingue. `as` (GNU) parla la sintassi AT&T ed
# e' fatto per ricevere quel che sputa il compilatore: nessuno scrive `as` a
# mano se puo' evitarlo. NASM parla la sintassi INTEL, che e' quella dei
# manuali di Intel e AMD, dei bootloader, dei tutorial e di quasi tutto il
# codice assembly che si trova scritto da una persona.
#
# ! E SOPRATTUTTO: EX-OS E' UN SISTEMA OPERATIVO. Chi impara a scriverne uno
# comincia da sedici bit e da un settore di avvio, e quella roba e' scritta
# in NASM nel novantanove per cento dei casi. Un sistema che sa compilarsi il
# C ma non sa assemblare un `org 0x7c00` e' monco proprio nel punto in cui
# dovrebbe essere piu' forte.
#
# -----------------------------------------------------------------------------
# ! CHE COSA CAMBIA NELL'ALBERO: DUE RIGHE, E RIGUARDANO L'OSPITE
#
# NASM non ha un «bersaglio» da insegnare: i formati d'uscita — elf32, bin,
# coff, macho — li produce tutti sempre, e si scelgono con -f. Quel che serve
# e' solo che giri qui:
#
#   autoconf/helpers/config.sub   'exos' fra i sistemi ammessi, o il
#                                 configure risponde «Invalid configuration»
#   nasmlib/path.c                EX-OS ha i percorsi di Unix (la barra come
#                                 unico separatore, nessun volume). Senza,
#                                 lo stile e' PATH_UNKNOWN e path.c non
#                                 compila: «'separators' undeclared»
#
# Le mette (e le toglie) tools/nasm-exos/applica.py.
#
# -----------------------------------------------------------------------------
# ! -std=gnu17 COME PER binutils E make, e per la stessa ragione: il nostro
# GCC e' il 17 e come C23 legge le vecchie dichiarazioni in modo diverso. Qui
# in piu' c'e' che il Makefile di NASM aggiunge -std=c23 di suo: le due
# opzioni convivono perche' vince l'ultima, e l'ultima e' la sua.
#
# ! DOVE PRENDERE I SORGENTI. Questo script non scarica niente, per la stessa
# ragione scritta in tools/binutils-exos/prepara-binutils.sh: un download
# silenzioso dentro una build si scopre quando fallisce.
#
#     wget https://www.nasm.us/pub/nasm/releasebuilds/3.02/nasm-3.02.tar.xz
#     tar xf nasm-3.02.tar.xz
#
# Provato con NASM 3.02.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/../.." && pwd)
SORGENTI="${1:-$RADICE/nasm}"
USCITA="${2:-$HOME/exos-native/build-nasm}"
PREFISSO="${PREFISSO:-$HOME/exos-cross}"

if [ ! -f "$SORGENTI/configure.ac" ] || [ ! -d "$SORGENTI/asm" ]; then
    echo "prepara-nasm: '$SORGENTI' non e' un albero di NASM" >&2
    echo "  vedi il commento in testa a questo script per dove prenderlo" >&2
    exit 1
fi
SORGENTI=$(cd "$SORGENTI" && pwd)

if [ ! -x "$PREFISSO/bin/i386-exos-gcc" ]; then
    echo "prepara-nasm: manca il cross i386-exos-gcc" >&2
    echo "  si prepara con tools/gcc-exos/prepara-cross.sh" >&2
    exit 1
fi

PATH="$PREFISSO/bin:$PATH"
export PATH

echo "=== NASM per EX-OS ==="
echo "  sorgenti : $SORGENTI"
echo "  uscita   : $USCITA"
echo

# --- 1. l'ospite dentro l'albero ---------------------------------------------
python3 "$RADICE/tools/nasm-exos/applica.py" "$SORGENTI"

# --- 2. il configure, se non c'e' --------------------------------------------
#
# ! L'ALBERO DEL REPOSITORY DI NASM NON HA `configure`, ma ha autogen.sh: e'
# un albero di sviluppo, non un pacchetto di rilascio. Si genera una volta e
# resta li'; da un tarball di rilascio questo passo non serve.
if [ ! -x "$SORGENTI/configure" ]; then
    echo "  configure assente: lo genero con autogen.sh"
    ( cd "$SORGENTI" && sh autogen.sh > /dev/null 2>&1 )
fi

# --- 3. configure incrociato --------------------------------------------------
#
# ! SI COSTRUISCE FUORI DALL'ALBERO, come per make: il repository di EX-OS
# contiene i sorgenti di NASM, e riempirli di oggetti vorrebbe dire che
# `git status` non distingue piu' una modifica da un residuo di build.
echo
echo "=== configure ==="
mkdir -p "$USCITA"
cd "$USCITA"

"$SORGENTI/configure" \
    --host=i386-exos \
    --build="$(sh "$SORGENTI/autoconf/helpers/config.guess")" \
    --prefix=/exos \
    --disable-werror \
    CC="i386-exos-gcc -std=gnu17" \
    > configure.log 2>&1 || { tail -20 configure.log >&2; exit 1; }

# --- 4. costruzione -----------------------------------------------------------
echo
echo "=== costruzione ==="
make -j"${J:-2}" > build.log 2>&1 || { tail -30 build.log >&2; exit 1; }

for b in nasm ndisasm; do
    [ -x "$USCITA/$b" ] || { echo "prepara-nasm: manca $USCITA/$b" >&2; exit 1; }
done

# --- 5. la prova che e' per il bersaglio giusto -------------------------------
#
# ! NON E' UNA FORMALITA'. Un configure incrociato che ricade sul compilatore
# di sistema produce un binario che gira benissimo — su Linux — e se ne
# accorgerebbe solo chi prova ad avviarlo dentro EX-OS, dove il messaggio
# sarebbe «non e' un programma eseguibile».
for b in nasm ndisasm; do
    if ! i386-exos-readelf -h "$USCITA/$b" | grep -q "Intel 80386"; then
        echo "prepara-nasm: $USCITA/$b non e' un ELF per i386" >&2
        exit 1
    fi
    i386-exos-strip -o "$USCITA/$b.stripped" "$USCITA/$b"
    echo "  $b: $(du -h "$USCITA/$b" | cut -f1) con i simboli, "\
"$(du -h "$USCITA/$b.stripped" | cut -f1) senza (e' cosi' che va sul CD)"
done

echo
echo "[OK] NASM per EX-OS: $USCITA/nasm e $USCITA/ndisasm"
echo "Finiscono sul CD degli strumenti con \`make iso\`, in /exos/bin e /bin."
