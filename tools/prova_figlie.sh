#!/bin/bash
# =============================================================================
# tools/prova_figlie.sh [32|64] [directory] - child windows of ExWin
# (wserver 0.016, WIN_MSG_CREA_FIGLIA, voce 105 step 5), in QEMU
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ExWin from the CD, then `finestreprova -f 15` (see its head): a grey window
# 300x200 at 100,100, a red child at 20,30, a blue child of the red one, a
# green child half outside. Four photographs, one per step, and the colour
# of the screen at the points where each window must, or must not, be:
#   1. as made: the children inside the parent, the green one cut at its edge
#   2. the parent moved to 300,150: the children with it, nothing left behind
#   3. the red one bigger and moved inside the parent, its blue child with it
#   4. the parent closed: the children gone with it
# and on the serial line "FIGLIE fine 0" (a child of nobody refused).
# ! 15 s A STEP: the wait of qemu_drive.py comes after each command, and a
# command to QEMU's monitor costs about 2 s more; with 8 s the pictures
# came one step late (21 November 2026).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT="${1:-32}"
D="${2:-/tmp/exos-prove/figlie-$BIT}"
rm -rf "$D"; mkdir -p "$D"
if [ "$BIT" = 64 ]; then
    export EXOS_CDROM=dist/exos64.iso EXOS_QEMU=qemu-system-x86_64
else
    export EXOS_CDROM=dist/exos.iso
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
export EXOS_ISTANZA=figlie$BIT EXOS_NO_FLOPPY=1 EXOS_RAM="${EXOS_RAM:-512M}"
SER=/tmp/exos/serialfiglie$BIT.txt
rm -f "$SER"

timeout 400 python3 tools/qemu_drive.py \
    "exwin@15" "key:alt-f1@2" "/exwin/bin/finestreprova -f 15 &@1" "key:alt-f5@2" \
    "foto:$D/passo1.ppm@13" "foto:$D/passo2.ppm@13" "foto:$D/passo3.ppm@13" \
    "foto:$D/passo4.ppm@6" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" > "$D/seriale.txt" 2>/dev/null

echo "=== finestre figlie ($BIT bit) ==="
grep -a "FIGLIE" "$D/seriale.txt" | sed 's/^/        /'
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
        if b[i:i+1] == b"#":
            while b[i:i+1] != b"\n": i += 1
            continue
        j = i
        while not b[j:j+1].isspace(): j += 1
        campi.append(b[i:j]); i = j
    w, h = int(campi[1]), int(campi[2])
    return w, h, b[i+1:]

GRIGIO, ROSSO, BLU, VERDE = (128, 128, 128), (255, 0, 0), (0, 0, 255), (0, 255, 0)
NOMI = {GRIGIO: "grigio", ROSSO: "rosso", BLU: "blu", VERDE: "verde"}
# step: [(x, y, colour, True = must be it / False = must not)]
ATTESI = {
    1: [(115, 115, GRIGIO, True), (200, 200, ROSSO, True), (150, 155, BLU, True),
        (375, 275, VERDE, True), (405, 275, VERDE, False), (375, 305, VERDE, False)],
    2: [(315, 165, GRIGIO, True), (400, 250, ROSSO, True), (350, 205, BLU, True),
        (575, 325, VERDE, True), (200, 200, ROSSO, False), (150, 155, BLU, False),
        (375, 275, VERDE, False)],
    3: [(500, 280, ROSSO, True), (380, 275, BLU, True), (330, 200, GRIGIO, True),
        (575, 325, VERDE, True), (350, 205, BLU, False)],
    4: [(500, 280, ROSSO, False), (380, 275, BLU, False), (575, 325, VERDE, False),
        (330, 200, GRIGIO, False)],
}
for passo, punti in ATTESI.items():
    p = "%s/passo%d.ppm" % (d, passo)
    try:
        w, h, px = ppm(p)
    except (OSError, ValueError, IndexError):
        no("passo %d: niente fotografia (%s)" % (passo, p)); continue
    sbagli = []
    for x, y, c, deve in punti:
        k = (y * w + x) * 3
        v = tuple(px[k:k+3])
        if (v == c) != deve:
            sbagli.append("%d,%d e' %s, %s %s" % (x, y, v, "doveva essere" if deve else "non doveva essere", NOMI[c]))
    if sbagli:
        no("passo %d: %s (%s)" % (passo, "; ".join(sbagli), p))
    else:
        ok("passo %d: %d punti come devono (%s)" % (passo, len(punti), p))
s = open(ser, encoding="latin-1", errors="replace").read() if True else ""
if "FIGLIE di nessuno 0" in s: ok("una figlia di una finestra che non c'e' e' rifiutata")
else: no("la figlia di nessuno non e' stata rifiutata")
if "FIGLIE fine 0" in s: ok("finestreprova -f: ogni risposta del server come doveva")
else: no("finestreprova -f non dice FIGLIE fine 0 (%s)" % ser)
if "[FAULT]" in s: no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
