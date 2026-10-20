#!/bin/bash
# =============================================================================
# tools/exilla/prova-js.sh — la shell di SpiderMonkey dentro EX-OS (tappa 4)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/prova-js.sh
#
# Prende la shell costruita da tools/exilla/js-costruisci.sh build
# (cross_build/exilla-obj/js/dist/bin/js), la spoglia dei simboli di debug
# (458 MB -> 14 MB), la mette su un disco di prova con tools/exilla/prova-js.js
# e la esegue in QEMU con 256 MB. Il verdetto e' l'ultima riga della prova.
#
# ! LA CPU: si guarda il binario come per Rust e per il C (Makefile,
# verifica-cpu). Le istruzioni SSE sono ammesse solo nelle funzioni SIMD che
# Mozilla sceglie a runtime dopo aver chiesto alla CPU (gemmology/xsimd per
# l'intgemm di WebAssembly, rdrand); cmov non e' ammessa da nessuna parte.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
JS="$PWD/cross_build/exilla-obj/js/dist/bin/js"
D="$B/costruzione-prova"
mkdir -p "$D"
[ -f "$JS" ] || { echo "manca $JS: tools/exilla/js-costruisci.sh build" >&2; exit 1; }
"$B/exos-cross/bin/i386-exos-strip" -o "$D/js" "$JS" || exit 1

fuori=$("$B/exos-cross/bin/i386-exos-objdump" -d --demangle "$JS" | \
        awk '/^[0-9a-f]+ <.*>:$/{f=$0} /%xmm|\tcmov/{print f}' | sort -u | \
        grep -v "gemmology::\|xsimd::\|__x86_rdrand" | head -5)
echo "js: $(stat -c %s "$D/js") byte"
if [ -n "$fuori" ]; then
    echo "[NO] istruzioni oltre il Pentium MMX fuori dai percorsi SIMD:"
    echo "$fuori"
    exit 1
fi

uscita=$(ALTRI=tools/exilla/prova-js.js EXOS_RAM=256M DISCO_MB=48 SECONDI=120 \
         tools/exilla/esegui-in-exos.sh "$D/js" /disk/prova-js.js)
echo "$uscita"
echo "$uscita" | grep -q "prova-js: 0 NO" && { echo "=== [OK] SpiderMonkey gira dentro EX-OS ==="; exit 0; }
echo "=== [NO] ==="; exit 1
