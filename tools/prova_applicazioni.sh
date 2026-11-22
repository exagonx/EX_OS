#!/bin/bash
# =============================================================================
# tools/prova_applicazioni.sh - Impostazioni > Applicazioni (voce 109, pm 0.029)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     bash tools/prova_applicazioni.sh [dir]            32 bit (dist/exos.iso)
#     EXOS_ARCH=x86_64 bash tools/prova_applicazioni.sh [dir]   64 bit
#
# ! ON A DISK INSTALLED FROM THE CD, NOT ON THE CD: the choices go to
# /boot/tipi.cfg, and from the CD /boot cannot be written (the tab says
# «Non riesco a scrivere /boot/tipi.cfg», as it must).
#
#   1  the disk installed from the CD (tools/mkhd.sh);
#   2  from the disk, ExWin, Avvio > Impostazioni..., the tab Applicazioni:
#      the group Immagini, ".bmp  immagini" first and chosen;
#   3  "Scegli il programma...": the dialog with "immagini" in the box; the
#      name rubbed out, "exeditor", Enter; the console: /boot/tipi.cfg says
#      "bmp | /exwin/bin/exeditor";
#   4  "Predefinito": /boot/tipi.cfg no longer names bmp;
#   5  the box Tipo, the group Indirizzi: "http://" in the list.
# Buttons and entries are found by their text on the screen (punta:), not
# by position. The photographs stay as PNG in the test's directory.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$(pwd)
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/applicazioni-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=ap$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 256 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

echo "=== 2. la scheda Applicazioni, dal disco ==="
# the photographs go through /tmp/exos: a path with «@» would be a wait
T=/tmp/exos/applicazioni-$ARCH
rm -rf "$T"; mkdir -p "$T"
clic() { echo "mon:mouse_button 1; mouse_button 0@${1:-3}"; }
A=("root@2" "root@6" "keymap us@3" "rm -f /boot/tipi.cfg@2"
   "exwin@25" "key:alt-f5@3"
   "punta:Avvio@1" "$(clic 3)"
   "punta:Impostazioni...@1" "$(clic 4)"   "foto:$T/1-impostazioni.ppm@1"
   "punta:Applicazioni|0,0,0,0,0,800,60@1" "$(clic 3)" "foto:$T/2-applicazioni.ppm@1"
   "punta:Scegli il programma...@1" "$(clic 4)" "foto:$T/3-dialogo.ppm@1")
for i in 1 2 3 4 5 6 7 8 9 10; do A+=("key:backspace@0.2"); done
A+=("exeditor@4" "foto:$T/4-scelto.ppm@1"
    "key:alt-f1@3" "echo SCELTA; cat /boot/tipi.cfg; echo FINE-SCELTA@3" "key:alt-f5@3"
    "punta:Predefinito@1" "$(clic 3)" "foto:$T/5-predefinito.ppm@1"
    "key:alt-f1@3" "echo DOPO; cat /boot/tipi.cfg; echo FINE-DOPO@3" "key:alt-f5@3"
    "punta:Immagini@1" "$(clic 3)" "foto:$T/6-tipo.ppm@1"
    "punta:Indirizzi@1" "$(clic 3)" "foto:$T/7-indirizzi.ppm@1"
    "key:alt-f1@3" "sync@5")
EXOS_QEMU=$Q EXOS_ISTANZA=ap$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$KVM $EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 1200 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
L=$(tr -d '\r' < "$D/qemu.log")

grep -aq "PUNTA: 'Applicazioni' in" "$D/qemu.log" && si "la scheda Applicazioni c'e'" \
    || no "la scheda Applicazioni non si trova sullo schermo"
grep -aq "PUNTA: 'Scegli il programma...' in" "$D/qemu.log" && si "il pulsante Scegli il programma... c'e'" \
    || no "il pulsante Scegli il programma... non si trova"
S1=$(echo "$L" | sed -n '/^SCELTA/,/^FINE-SCELTA/p')
echo "$S1" | grep -aq "^bmp | /exwin/bin/exeditor" && si "scelto exeditor: /boot/tipi.cfg dice bmp | /exwin/bin/exeditor" \
    || no "dopo la scelta /boot/tipi.cfg non dice bmp | /exwin/bin/exeditor"
S2=$(echo "$L" | sed -n '/^DOPO/,/^FINE-DOPO/p')
[ -n "$S2" ] && ! echo "$S2" | grep -aq "^bmp |" && si "Predefinito: /boot/tipi.cfg non nomina piu' bmp" \
    || no "dopo Predefinito /boot/tipi.cfg nomina ancora bmp (o non si e' letto)"

python3 -I - "$T" "$RADICE/tools" <<'PY'
import re, sys
d, attrezzi = sys.argv[1], sys.argv[2]
sys.path.insert(0, attrezzi)
import trova_testo
# ! THE CHOSEN ROW IS LIGHT TEXT ON GREY, and the glyph reader does not see
# it: the checks count the rows that are NOT chosen. Immagini has 7 rows,
# ".bmp" first and chosen: 6 more say "immagini" before the choice and after
# Predefinito, 6 too after choosing exeditor for .bmp - and none of them
# says "(scelto)". So: "immagini" 6 times everywhere, ".png" there, and
# "https://" (second row of Indirizzi) there.
def quante(n, testo):
    b = open("%s/%s.ppm" % (d, n), "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", b)
    return len(trova_testo.trova(int(m.group(1)), int(m.group(2)), b[m.end():], testo, tutti=True))
esito = 0
for n, testo, deve, cosa in (
        ("2-applicazioni", ".png", 1, "la lista di Immagini ha .png"),
        ("2-applicazioni", "immagini", 6, "Immagini: le 6 righe non scelte dicono immagini"),
        ("4-scelto", "immagini", 6, "dopo la scelta di .bmp le altre 6 righe dicono ancora immagini"),
        ("5-predefinito", "immagini", 6, "dopo Predefinito le 6 righe dicono immagini"),
        ("7-indirizzi", "https://", 1, "il gruppo Indirizzi ha https://")):
    try:
        v = quante(n, testo)
    except Exception as e:
        print("  [NO] %s: manca la fotografia (%s)" % (cosa, e)); esito = 1; continue
    if v == deve: print("  [OK] %s" % cosa)
    else: print("  [NO] %s (%d volte, attese %d)" % (cosa, v, deve)); esito = 1
sys.exit(esito)
PY
[ $? = 0 ] && ok=$((ok+5)) || no=$((no+1))
grep -a "\[FAULT\]" /tmp/exos/serialap$ARCH.txt > /dev/null 2>&1 && no "un [FAULT] sulla seriale" || si "nessun [FAULT]"

for p in "$T"/*.ppm; do
    [ -f "$p" ] || continue
    python3 tools/ppm2png.py "$p" "$D/$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"
done
rm -f "$IMG"
echo "  fotografie: $D"
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
