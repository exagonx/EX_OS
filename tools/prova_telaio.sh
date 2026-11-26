#!/bin/bash
# =============================================================================
# tools/prova_telaio.sh [32|64] [directory] - the frame of a window while it
# lives (wserver 0.020 WIN_MSG_TELAIO, exwin.so 0.028 ex_window_frame,
# voce 121), in QEMU
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ExWin from the CD, then `finestreprova -s 15` (see its head): a C83296
# window at 120,100 goes through five steps, a photograph each, and the
# colour of the screen is read where each thing must, or must not, be:
#   1. normal: the window and its title bar, the taskbar at the bottom
#   2. full screen: all four corners are the window, the taskbar too
#   3. a green EX_TOPMOST window opened now: above the full-screen one
#   4. normal again: at 120,100 with its title bar, the taskbar back
#   5. no frame: the window where it was, no title bar
# The serial line says EXM_SIZE as big as the screen, and back.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT="${1:-32}"
D="${2:-/tmp/exos-prove/telaio-$BIT}"
rm -rf "$D"; mkdir -p "$D"
if [ "$BIT" = 64 ]; then
    export EXOS_CDROM=dist/exos64.iso EXOS_QEMU=qemu-system-x86_64
else
    export EXOS_CDROM=dist/exos.iso
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
export EXOS_ISTANZA=telaio$BIT EXOS_NO_FLOPPY=1 EXOS_RAM="${EXOS_RAM:-512M}"
SER=/tmp/exos/serialtelaio$BIT.txt
rm -f "$SER"

timeout 400 python3 tools/qemu_drive.py \
    "exwin@15" "key:alt-f1@2" "/exwin/bin/finestreprova -s 15 &@1" "key:alt-f5@4" \
    "foto:$D/passo1.ppm@13" "foto:$D/passo2.ppm@13" "foto:$D/passo3.ppm@13" \
    "foto:$D/passo4.ppm@13" "foto:$D/passo5.ppm@14" "key:alt-f1@2" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" > "$D/seriale.txt" 2>/dev/null

echo "=== telaio delle finestre ($BIT bit) ==="
grep -a "TELAIO" "$D/seriale.txt" | sed 's/^/        /'
python3 -I - "$D" "$D/seriale.txt" <<'PY'
import sys
d, ser = sys.argv[1], sys.argv[2]
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1

def ppm(p):
    b = open(p, "rb").read()
    campi, i = [], 0
    while len(campi) < 4:
        while b[i:i+1].isspace(): i += 1
        j = i
        while not b[j:j+1].isspace(): j += 1
        campi.append(b[i:j]); i = j
    return int(campi[1]), int(campi[2]), b[i+1:]

FIN, VERDE, BARRA = (200, 50, 150), (0, 192, 0), (0x31, 0x4D, 0x8B)
NOMI = {FIN: "la finestra", VERDE: "la verde", BARRA: "la barra del titolo"}
def attesi(n, w, h):
    angoli = [(2, 2), (w - 3, 2), (2, h - 3), (w - 3, h - 3)]
    if n in (1, 4):
        return [(200, 180, FIN, True), (125, 105, FIN, True), (115, 95, FIN, False),
                (200, 90, BARRA, True), (w - 3, h - 3, FIN, False), (2, 2, FIN, False)]
    if n == 2:
        return [(x, y, FIN, True) for x, y in angoli]
    if n == 3:
        return [(380, 250, VERDE, True)] + [(x, y, FIN, True) for x, y in angoli]
    return [(200, 180, FIN, True), (125, 105, FIN, True), (200, 90, BARRA, False),
            (w - 3, h - 3, FIN, False)]

dims = None
for n in range(1, 6):
    try:
        w, h, px = ppm("%s/passo%d.ppm" % (d, n))
    except Exception as e:
        no("passo %d: manca la fotografia (%s)" % (n, e)); continue
    dims = (w, h)
    for x, y, c, deve in attesi(n, w, h):
        k = (y * w + x) * 3
        v = tuple(px[k:k + 3])
        vicino = all(abs(a - b) <= 8 for a, b in zip(v, c))
        t = "passo %d (%d,%d): %s%s (%02x%02x%02x)" % (n, x, y, "" if deve else "non ", NOMI[c], *v)
        ok(t) if vicino == deve else no(t)

s = open(ser, "rb").read().decode("latin-1") if dims else ""
if dims:
    pieno = "TELAIO misura %dx%d" % dims
    ok("EXM_SIZE a schermo intero: " + pieno) if pieno in s else no("manca '%s' sulla seriale" % pieno)
    ok("EXM_SIZE di nuovo 360x240") if s.count("TELAIO misura 360x240") >= 1 else no("non torna a 360x240")
ok("TELAIO fine") if "TELAIO fine" in s else no("manca TELAIO fine")
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
R=$?
for p in "$D"/*.ppm; do
    [ -f "$p" ] && python3 tools/ppm2png.py "$p" "${p%.ppm}.png" > /dev/null 2>&1 && rm -f "$p"
done
echo "  fotografie: $D"
[ $R = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $R
