#!/bin/sh
# =============================================================================
# tools/mkfloppy64.sh — il dischetto di prova del kernel a 64 bit (@EXOS-64)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Un FAT12 da 1,44 MB con dentro tre cose: il settore di avvio e LOADER.BIN
# della versione a 32 bit, TALI E QUALI, e il KERNEL.BIN a 64 bit. E' la
# stessa disposizione di tools/mkfloppy.sh senza i programmi: finche' il
# kernel a 64 bit non sa caricarne, non c'e' niente da metterci.
#
# Lo chiama `make ARCH=x86_64 kernel64-prova`.
# =============================================================================
set -e
cd "$(dirname "$0")/.."
# Senza argomenti: il kernel di PROVA (build-64/kernel.bin) in dist/floppy64.img.
# Con "intero": il kernel INTERO (make ARCH=x86_64 kernel64) in
# dist/floppy64-intero.img - un dischetto a parte, cosi' le prove delle tappe
# restano avviabili mentre il kernel intero cresce.
IMG=dist/floppy64.img
KERNEL=build-64/kernel.bin
if [ "${1:-}" = "intero" ]; then
    IMG=dist/floppy64-intero.img
    KERNEL=build-64/kernel-intero.bin
fi
for f in build/stage1.bin build/stage2.bin "$KERNEL"; do
    [ -f "$f" ] || { echo "mkfloppy64: manca $f" >&2; exit 1; }
done
mkdir -p dist
dd if=/dev/zero of="$IMG" bs=512 count=2880 status=none
mformat -f 1440 -v "EXOS64  " -i "$IMG" ::
dd if=build/stage1.bin of="$IMG" bs=512 count=1 conv=notrunc status=none
mcopy -i "$IMG" build/stage2.bin ::/LOADER.BIN
mcopy -i "$IMG" "$KERNEL" ::/KERNEL.BIN
if [ "${1:-}" = "intero" ] && [ -f build-64/prove/primo ]; then
    # il kernel intero avvia /bin/sh sulle console: per ora e' il programma
    # di prova, l'unico a 64 bit che c'e'
    mmd -i "$IMG" ::/bin
    mcopy -i "$IMG" build-64/prove/primo ::/bin/sh
fi
