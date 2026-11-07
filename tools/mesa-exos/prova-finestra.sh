#!/bin/bash
# =============================================================================
# tools/mesa-exos/prova-finestra.sh - OpenGL ES in an ExWin window (voce 88 c)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mesa-exos/prova-finestra.sh         64 bit (dist/exos64.iso)
#     tools/mesa-exos/prova-finestra.sh 32      32 bit (dist/exos.iso)
#
# Builds prova-finestra.c linked to libEGL.so.1 and libGLESv2.so.2 of the
# sysroot (tools/mesa-exos/costruisci.sh), starts EX-OS in QEMU with the
# laptop's screen (EDID 1366x768; from the CD ExWin may take 800x600), turns ExWin on, starts the program in the
# background and photographs the screen while its window is up. Two
# judgements:
#   - the program's own checks (listed at the top of prova-finestra.c):
#     "prova-finestra: 0 NO";
#   - the photograph: the screen has the window's red and blue, many pixels
#     of each. It is kept as a PNG in cross_build/<machine>/
#     costruzione-prova-finestra-<target>/schermo.png: looking at it is the
#     last word.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${1:-64}" = 32 ]; then
    T=i386-exos; ARCH=""; BIT=32; X="$RADICE/cross_build/$MACCHINA/exos-cross"
else
    T=x86_64-exos; ARCH=x86_64; BIT=64; X="$RADICE/cross_build/$MACCHINA/exos-cross64"
fi
export PATH="$X/bin:$PATH"
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-finestra-$T"
CD="${CD_FORZATO:-$RADICE/dist/exos.iso}"; [ -n "$ARCH" ] && CD="${CD_FORZATO:-$RADICE/dist/exos64.iso}"
rm -rf "$D"; mkdir -p "$D"
$T-gcc -O2 -o "$D/prova-finestra" tools/mesa-exos/prova-finestra.c -lEGL -lGLESv2 -lpthread || exit 1

. tools/mesa-exos/librerie-prova.sh
altri=$(librerie_prova "$D/prova-finestra" $T "$X" $BIT "$D") || exit 1

FOTO="$D/schermo.ppm"
# ExWin on console 5, back to the shell (alt-f1), the program in the
# background, then the graphic console again for the photograph
uscita=$(DISCO_MB=${DISCO_MB:-128} AMBIENTE="LD_LIBRARY_PATH=/disk ${AMBIENTE_PIU:-}" QEMU_PIU="-vga none -device VGA,edid=on,xres=1366,yres=768" ALTRI="$altri" \
         EXOS_ARCH=$ARCH EXOS_RAM=${EXOS_RAM:-1024M} CD_FORZATO="$CD" ATTESA=420 \
         PRIMA="exwin@14 key:alt-f1@2" SFONDO=1 SECONDI=2 \
         DOPO="key:alt-f5@${ATTESA_DISEGNO:-40} foto:$FOTO@3 key:alt-f1@2 sync@${ATTESA_FINE:-40}" \
         tools/exilla/esegui-in-exos.sh "$D/prova-finestra" ${TENUTA:-50})
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-finestra|finestra [0-9]|superficie|zona|fotogramm|dopo il|eglGetError|eglChooseConfig|libEGL|cannot open|undefined|FAULT|panic|PANIC"
esito=0
echo "$uscita" | grep -aq "prova-finestra: 0 NO" || esito=1

if [ -f "$FOTO" ]; then
    python3 -I - "$FOTO" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
import re
# P6, width height, 255, ONE blank, then RGB. ! Not d.split(maxsplit=4):
# the last piece loses its leading blanks too, and pixels whose bytes
# are 0x0a or 0x20 at the top left shortened the data (IndexError).
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", d)
w, h = int(m.group(1)), int(m.group(2)); px = d[m.end():]
rossi = blu = 0
for i in range(0, w * h * 3, 3):
    r, g, b = px[i], px[i + 1], px[i + 2]
    if r > 240 and g < 20 and b < 20: rossi += 1
    elif b > 240 and r < 20 and g < 20: blu += 1
print("      schermo %dx%d: %d pixel rossi, %d blu" % (w, h, rossi, blu))
sys.exit(0 if rossi > 5000 and blu > 20000 else 1)
PY
    if [ $? = 0 ]; then echo "  [OK] la foto dello schermo ha il triangolo rosso sul blu"
    else echo "  [NO] la foto dello schermo non ha il rosso e il blu della finestra"; esito=1; fi
    python3 tools/ppm2png.py "$FOTO" "$D/schermo.png" > /dev/null 2>&1 && rm -f "$FOTO" && echo "      $D/schermo.png"
else
    echo "  [NO] nessuna foto dello schermo"; esito=1
fi
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
