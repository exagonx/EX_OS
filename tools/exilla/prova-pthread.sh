#!/bin/bash
# =============================================================================
# tools/exilla/prova-pthread.sh — @PTHREAD: i thread POSIX dentro EX-OS
#
# Compila tools/exilla/prova-pthread.c col gcc i386-exos di QUESTA macchina
# (libc.a e header rifatti da tools/gcc-exos/prepara-cross.sh dai sorgenti di
# adesso) e la esegue in EX-OS con tools/exilla/esegui-in-exos.sh.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
TCB="$PWD/cross_build/$MACCHINA/exos-cross/bin"
D="$PWD/cross_build/$MACCHINA/costruzione-prova"
mkdir -p "$D"
"$TCB/i386-exos-gcc" -O2 -Wall tools/exilla/prova-pthread.c -o "$D/prova-pthread" || exit 1
uscita=$(SECONDI=10 tools/exilla/esegui-in-exos.sh "$D/prova-pthread")
echo "$uscita"
echo "$uscita" | grep -q "prova-pthread: 0 NO" && { echo "=== [OK] i thread POSIX girano dentro EX-OS ==="; exit 0; }
echo "=== [NO] ==="; exit 1
