#!/bin/bash
# =============================================================================
# tools/prova_impostazioni.sh - every tab of Impostazioni shows only its own
# controls (pm 0.031, 25 November 2026)
#
# ! THE SCREEN SAVER SAT OVER EVERY TAB: the tabs kept 16 controls each, it
# went in Scrivania as the 17th to 21st, and those were never hidden. Now it
# is in Video under a line, and the tabs keep 32.
#
#   1  from the CD, ExWin, Avvio > Impostazioni...: the tab Video has the
#      modes and, below, "Salvaschermo:" and "Spegni lo schermo:";
#   2  Scrivania, Sistema, Applicazioni: none says "Salvaschermo:" or
#      "Spegni lo schermo:", and each has its own text;
#   3  no "troppi controlli" on the serial line, no [FAULT].
# Tabs found by their text (punta:), photographs as PNG in the directory.
#
#     tools/prova_impostazioni.sh [CARTELLA]     (EXOS_ARCH=x86_64 for 64 bit)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$(pwd)
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/impostazioni-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
CD=dist/exos.iso
if [ "$ARCH" = x86_64 ]; then CD=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64; fi
[ -f "$CD" ] || { echo "manca $CD" >&2; exit 1; }
export EXOS_ISTANZA=impost$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M

# the photographs go through /tmp/exos: a path with «@» would be a wait
T=/tmp/exos/impostazioni-$ARCH
rm -rf "$T"; mkdir -p "$T"
clic() { echo "mon:mouse_button 1; mouse_button 0@${1:-3}"; }
A=("exwin@20"
   "punta:Avvio@1" "$(clic 3)"
   "punta:Impostazioni...@1" "$(clic 4)" "foto:$T/1-video.ppm@1"
   "punta:Scrivania@1" "$(clic 3)" "foto:$T/2-scrivania.ppm@1"
   "punta:Sistema@1" "$(clic 3)" "foto:$T/3-sistema.ppm@1"
   "punta:Applicazioni@1" "$(clic 3)" "foto:$T/4-applicazioni.ppm@1"
   "punta:Video@1" "$(clic 3)" "foto:$T/5-video.ppm@1")
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1

python3 -I - "$T" "$RADICE/tools" <<'PY'
import re, sys
d, attrezzi = sys.argv[1], sys.argv[2]
sys.path.insert(0, attrezzi)
import trova_testo
def c(n, testo):
    b = open("%s/%s.ppm" % (d, n), "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", b)
    return len(trova_testo.trova(int(m.group(1)), int(m.group(2)), b[m.end():], testo, tutti=True))
esito = 0
prove = []
for n in ("1-video", "5-video"):
    prove += [(n, "Salvaschermo:", True), (n, "Spegni lo schermo:", True), (n, "Prova...", True)]
for n, suo in (("2-scrivania", "Icone accanto alle voci del menu"),
               ("3-sistema", "Orologio nella barra"),
               ("4-applicazioni", "Predefinito")):
    prove += [(n, suo, True), (n, "Salvaschermo:", False), (n, "Spegni lo schermo:", False),
              (n, "Prova...", False)]
for n, testo, deve in prove:
    try:
        v = c(n, testo) > 0
    except Exception as e:
        print("  [NO] %s: manca la fotografia (%s)" % (n, e)); esito = 1; continue
    cosa = "%s: %s%r" % (n, "" if deve else "niente ", testo)
    if v == deve: print("  [OK] " + cosa)
    else: print("  [NO] " + cosa); esito = 1
sys.exit(esito)
PY
esito=$?
S=/tmp/exos/serialimpost$ARCH.txt
grep -aq "troppi controlli" "$S" 2>/dev/null && { echo "  [NO] pm: troppi controlli in una scheda"; esito=1; } \
    || echo "  [OK] nessuna scheda oltre SCH_MAX"
grep -aq "\[FAULT\]" "$S" 2>/dev/null && { echo "  [NO] un [FAULT] sulla seriale"; esito=1; } \
    || echo "  [OK] nessun [FAULT]"
for p in "$T"/*.ppm; do
    [ -f "$p" ] || continue
    python3 tools/ppm2png.py "$p" "$D/$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"
done
echo "  fotografie: $D"
[ $esito = 0 ] && echo "ESITO: TUTTO A POSTO" || echo "ESITO: QUALCOSA NON VA"
exit $esito
