#!/bin/bash
# =============================================================================
# tools/prova-letture/prova-letture.sh - known files read back while other
# threads and processes write new ones, in EX-OS under QEMU (voce 5 of
# @CODA-20-10: Firefox once read blocks of a file just written)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-letture/prova-letture.sh         64 bit (dist/exos64.iso)
#     tools/prova-letture/prova-letture.sh 32      32 bit (dist/exos.iso)
#
# Builds prova-letture.c with the cross gcc and runs it on the ext2 scratch
# disk of tools/exilla/esegui-in-exos.sh, with 2 processors (CPU=...):
#
#   1. RAM 512 MB, 3 readers and 2 writers in 2 processes, SECONDI (90)
#   2. RAM 96 MB: the block cache has to give blocks back all the time,
#      which is where a block of one file could end up read as another's
#   3. the same as 1 on ext4 (mkfs -t ext4), the format of the laptop
#
# PASS = "prova-letture: 0 NO" in every case and no FAULT/panic.
# CASI="1 2" chooses the cases.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${1:-64}" = 32 ]; then
    T=i386-exos; ARCH=""; CD="$RADICE/dist/exos.iso"
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross/bin:$PATH"
else
    T=x86_64-exos; ARCH=x86_64; CD="$RADICE/dist/exos64.iso"
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross64/bin:$PATH"
fi
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-letture-$T"
mkdir -p "$D"
$T-gcc -O2 -o "$D/prova-letture" tools/prova-letture/prova-letture.c -lpthread || exit 1
S="${SECONDI:-90}"
esito=0

caso() {   # caso N NOME RAM PRIMA
    local n="$1" nome="$2" ram="$3" prima="$4" uscita
    uscita=$(EXOS_ARCH=$ARCH EXOS_RAM=$ram CD_FORZATO="$CD" DISCO_MB=256 SECONDI=$((S + 60)) \
             ATTESA=$((S + 400)) PRIMA="$prima" QEMU_PIU="-smp ${CPU:-2}" \
             tools/exilla/esegui-in-exos.sh "$D/prova-letture" /disk/pl $S 3 2 2)
    echo "--- caso $n: $nome"
    echo "$uscita" | grep -a -E "prova-letture|FAULT|panic|PANIC"
    if ! echo "$uscita" | grep -aq "prova-letture: 0 NO"; then echo "[NO] caso $n"; esito=1; fi
    if echo "$uscita" | grep -aq "\[FAULT\]\|PANIC\|panic"; then echo "[NO] caso $n: FAULT/panic"; esito=1; fi
}

for n in ${CASI:-1 2 3}; do
    case $n in
        1) caso 1 "RAM 512 MB, ext2" 512M "" ;;
        2) caso 2 "RAM 96 MB, ext2: la cache ricicla" 96M "" ;;
        # ext4: esegui-in-exos makes ext2, so the program goes to /rdboot (the RAM
        # disk; / is the CD), the disk is formatted again, and it comes back
        3) caso 3 "RAM 512 MB, ext4" 512M "cp:/disk/prova-letture:/rdboot/pl-bin@3 umount:/disk@3 mkfs:-t:ext4:-L:prova:hd0p1@8 si@40 mount:hd0p1:/disk@5 cp:/rdboot/pl-bin:/disk/prova-letture@3 chmod:755:/disk/prova-letture@2 mount@2" ;;
    esac
done
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
