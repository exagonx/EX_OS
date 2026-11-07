#!/bin/bash
# =============================================================================
# tools/prova_menu_avvio.sh - the Start menu in groups (voce 80, pm 0.028)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_menu_avvio.sh            32 bit (dist/exos.iso)
#     EXOS_ARCH=x86_64 tools/prova_menu_avvio.sh   64 bit (dist/exos64.iso)
#
# ExWin from the CD (800x600), then with the mouse, photographing each step:
#   1  "Avvio": the menu, with the groups Applicazioni, Internet, Strumenti,
#      Multimedia, Giochi and their icons, and no "Applicazioni..." below
#      the line;
#   2  "Applicazioni...": the group opens beside it (File manager, Archivi,
#      Calctor, Pennello, Immagini);
#   3  Calctor: the menu closes and Calctor starts;
#   4  the menu again, then a click on the desktop: the menu closes;
#   5  the menu, "Impostazioni...", the Sistema tab: the button "Applicazioni
#      del menu..."; the Scrivania tab: the box "Icone accanto alle voci del
#      menu", ticked;
#   6  the box unticked, the menu again: entries with no icons.
# Three verdicts read from the photographs (a point inside the menu: grey
# when open, the desktop's colour when closed; the blue of the icon of
# «Applicazioni...» with and without the box), the rest is looking at them:
# they stay as PNG in cross_build/<machine>/prova-menu-avvio<64>/.
# Coordinates for 800x600 (the CD) and the menu of exwin/lib/applicazioni.txt
# (5 groups, no loose entries): the menu starts at y = 600 - 28 - 278 - 2.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
SUFF=""; CD="$PWD/dist/exos.iso"
if [ "$EXOS_ARCH" = x86_64 ]; then SUFF=64; CD="$PWD/dist/exos64.iso"; export EXOS_QEMU=qemu-system-x86_64; fi
D="$PWD/cross_build/$MACCHINA/prova-menu-avvio$SUFF"
# ! THE PHOTOGRAPHS GO THROUGH /tmp/exos: the project's path has the user's
# «@», which qemu_drive.py would take for the wait («cmd@seconds»).
T=/tmp/exos/menu-avvio$SUFF
rm -rf "$D" "$T"; mkdir -p "$D" "$T"
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
CPU=""; [ "$EXOS_ARCH" = x86_64 ] && CPU="-cpu qemu64"
export EXOS_ISTANZA=menuavvio$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-1024M}"
export EXOS_QEMU_EXTRA="$KVM $CPU"

passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do echo "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do echo "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}
casa() { for i in 1 2 3 4 5; do echo "mon:mouse_move -600 -600@0"; done; }
clic() { casa; passi_a "$1" "$2"; echo "mon:mouse_button 1;mouse_button 0@${3:-3}"; }

AVVIO="37 586"; APPLICAZIONI="100 307"; CALCTOR="300 359"; IMPOSTAZIONI="100 457"
{
    echo "exwin@14"; echo "key:alt-f5@3"
    clic $AVVIO;               echo "foto:$T/1-menu.ppm@1"
    clic $APPLICAZIONI;        echo "foto:$T/2-gruppo.ppm@1"
    clic $CALCTOR 6;           echo "foto:$T/3-calctor.ppm@1"
    clic $AVVIO;               echo "foto:$T/4-menu.ppm@1"
    clic 600 120;              echo "foto:$T/5-scrivania.ppm@1"
    clic $AVVIO
    clic $IMPOSTAZIONI 4;      echo "foto:$T/6-impostazioni.ppm@1"
    # the tabs and the box: where the window opens (EX_AUTO) on the CD's
    # 800x600, read from the photograph of step 6; the box is clicked on its
    # LABEL, a wider target than the 12-pixel square (QEMU's mouse is
    # relative and lands a few pixels off)
    IMP_SISTEMA="${IMP_SISTEMA:-240 63}"; IMP_SCRIVANIA="${IMP_SCRIVANIA:-145 63}"
    IMP_CASELLA="${IMP_CASELLA:-150 212}"
    if [ "$IMP_SISTEMA" != no ]; then
        clic $IMP_SISTEMA;     echo "foto:$T/7-sistema.ppm@1"
        clic $IMP_SCRIVANIA;   echo "foto:$T/8-scrivania.ppm@1"
        clic $IMP_CASELLA;     echo "foto:$T/9-casella.ppm@1"
        clic $AVVIO;           echo "foto:$T/10-senza-icone.ppm@1"
        clic $APPLICAZIONI;    echo "foto:$T/11-gruppo-senza-icone.ppm@1"
    fi
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 2400 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1

python3 -I - "$T" <<'PY'
import re, sys
d = sys.argv[1]
def px(n, x, y):
    b = open("%s/%s.ppm" % (d, n), "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", b)
    w = int(m.group(1)); i = m.end() + (y * w + x) * 3
    return "%02X%02X%02X" % (b[i], b[i + 1], b[i + 2])
ok = 0
X, Y = 200, 433
try:
    aperto, chiuso, calc = px("4-menu", X, Y), px("5-scrivania", X, Y), px("3-calctor", X, Y)
except Exception as e:
    print("  [NO] mancano le fotografie (%s)" % e); sys.exit(1)
if aperto != chiuso:
    print("  [OK] un clic sulla scrivania chiude il menu (%s -> %s)" % (aperto, chiuso)); ok += 1
else:
    print("  [NO] menu aperto e chiuso hanno lo stesso colore (%s): il menu non si e' visto o non si e' chiuso" % aperto)
if calc != aperto:
    print("  [OK] scelto Calctor, il menu non c'e' piu' (%s)" % calc); ok += 1
else:
    print("  [NO] dopo Calctor il menu e' ancora li' (%s)" % calc)
# the icon of «Applicazioni...» (blue): there with the box ticked, gone
# without; the entry's text is black, the button grey. Rows 302-312 only:
# rows 300 and 313 are the blue frame of the entry that has the focus
def blu(n):
    c = 0
    for y in range(302, 313):
        for x in range(40, 63):
            p = px(n, x, y); r, g, b = int(p[0:2], 16), int(p[2:4], 16), int(p[4:6], 16)
            if b > r + 60: c += 1
    return c
try:
    con, senza = blu("1-menu"), blu("10-senza-icone")
    if con > 20 and senza < 3:
        print("  [OK] le icone del menu: %d pixel dell'icona, %d con la casella tolta" % (con, senza)); ok += 1
    else:
        print("  [NO] le icone del menu: %d pixel con la casella, %d senza" % (con, senza))
except Exception as e:
    print("  [NO] manca la fotografia senza icone (%s)" % e)
sys.exit(0 if ok == 3 else 1)
PY
esito=$?
for p in "$T"/*.ppm; do
    [ -f "$p" ] || continue
    python3 tools/ppm2png.py "$p" "$D/$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"
done
echo "  fotografie: $D"
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
