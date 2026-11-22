#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova-dl/prova-js.sh — ExJs and QuickJS as shared objects, 64 bit
#
# Links tools/prova-dl/prova-js.c the way tools/costruisci-utente64.sh links a
# command (build-64/lib: start.o, libc.o, libexos.a - run `make iso64` first)
# and runs it twice in dist/exos64.iso: with ExJs, then asking for QuickJS.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-prova-dl-js"
mkdir -p "$D"
gcc -m64 -ffreestanding -fno-builtin -fno-pic -fno-pie -O2 -std=c11 -nostdlib \
    -ffunction-sections -fdata-sections -Ilib/include -Ilib/exjs -c tools/prova-dl/prova-js.c -o "$D/prova-js.o" &&
ld -m elf_x86_64 -nostdlib --gc-sections -z max-page-size=4096 -T lib/programma64.ld \
    ${EXOS_FUORI}/build-64/lib/start.o "$D/prova-js.o" ${EXOS_FUORI}/build-64/lib/libc.o ${EXOS_FUORI}/build-64/lib/libexos.a -o "$D/prova-js" || exit 1
n=0
for m in "" 1; do
    u=$(EXOS_ARCH=x86_64 SECONDI=10 tools/exilla/esegui-in-exos.sh "$D/prova-js" $m)
    echo "$u"
    echo "$u" | grep -q "prova-js: OK" && n=$((n + 1))
done
[ $n = 2 ] && { echo "=== [OK] both engines ==="; exit 0; }
echo "=== [NO] ==="; exit 1
