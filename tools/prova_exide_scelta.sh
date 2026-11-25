#!/bin/bash
# =============================================================================
# tools/prova_exide_scelta.sh - more controls at once in exide (exide 0.029,
# debug/implementazione_EXIDE002 passo 2)
#
#   1. a click on the empty form, Ctrl+A: every control is chosen;
#   2. five right arrows: the whole set moves by the same step;
#   3. Canc removes the set, Ctrl+Z puts it back in one step;
#   4. Ctrl+S: in finestra.dis every control has moved by the same dx (1..5,
#      less only if the group touched the right edge), y unchanged, none
#      lost or added.
#
# Starts from the disk of prova_exide_genera.sh (a project with every
# control): if it is not there, it has it made.
#
#     tools/prova_exide_scelta.sh [directory]        (default /tmp/exos-ide-scelta)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ide-scelta}"
BASE=/tmp/exos-ide-genera/prova-hd.img
mkdir -p "$D"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
[ -f "$BASE" ] || bash tools/prova_exide_genera.sh > "$D/0-genera.log" 2>&1
[ -f "$BASE" ] || { echo "NON RIUSCITO: prova_exide_genera.sh non ha lasciato il disco" >&2; exit 1; }
cp "$BASE" "$D/prova-hd.img"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

"$DEBUGFS" -R "cat nuovo/src/finestra.dis" "$OFF" > "$D/prima.dis" 2>/dev/null

export EXOS_ISTANZA=idescelta
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

# QEMU's mouse is relative and stops 9 pixels short: see prova_exide_uso.sh.
vai() {
    local dx=$(( $1 + 9 )) dy=$(( $2 + 9 )) i d rx ry
    for i in 1 2 3 4 5; do echo "mon:mouse_move -600 -600@0"; done
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do echo "mon:mouse_move 10 10@0.04"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do echo "mon:mouse_move 10 0@0.04"; done
    for ((i = 10; i <= ry; i += 10)); do echo "mon:mouse_move 0 10@0.04"; done
    echo "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@1"
}
CLIC="mon:mouse_button 1;mouse_button 0@2"

{
    printf '%s\n' "mount hd0p1 /disk@6" "export HOME=/disk/casa@2" "exwin@20" "key:alt-f1@3" \
                  "/exwin/bin/exide /disk/nuovo &@14" "key:alt-f5@4" "foto:$D/1-aperto.ppm@1"
    vai 590 300; echo "$CLIC"                                    # the empty form
    echo "key:ctrl-a@2"; echo "foto:$D/2-tutti.ppm@1"
    for i in 1 2 3 4 5; do echo "key:right@0.3"; done
    echo "foto:$D/3-spostati.ppm@1"
    echo "key:delete@2"; echo "foto:$D/4-tolti.ppm@1"
    echo "key:ctrl-z@2"; echo "foto:$D/5-rimessi.ppm@1"
    echo "key:ctrl-s@6"; echo "foto:$D/6-salvato.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 1500 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1

"$DEBUGFS" -R "cat nuovo/src/finestra.dis" "$OFF" > "$D/dopo.dis" 2>/dev/null

echo "=== i controlli spostati insieme ==="
python3 - "$D/prima.dis" "$D/dopo.dis" <<'FINEPY'
import sys
# controls by form: Ctrl+A chooses only the form open in the designer (the
# first one, "principale"); the others must not move at all
def ctrl(p):
    r, f = {}, None
    for l in open(p, errors="replace"):
        c = l.split()
        if len(c) >= 2 and c[0] == "F": f = c[1]
        if len(c) >= 9 and c[0] == "c":
            r.setdefault(f, {})[c[2]] = (int(c[4]), int(c[5]), int(c[6]), int(c[7]))
    return r
tutti_a, tutti_b = ctrl(sys.argv[1]), ctrl(sys.argv[2])
if not tutti_a:
    print("  [NO]  prima.dis non ha controlli"); sys.exit(1)
prima = next(iter(tutti_a))
for f in tutti_a:
    if f != prima and tutti_a[f] != tutti_b.get(f):
        print("  [NO]  la maschera %s, non aperta, e' cambiata" % f); sys.exit(1)
a, b = tutti_a[prima], tutti_b.get(prima, {})
if set(a) != set(b):
    print("  [NO]  i controlli non sono gli stessi: %s / %s" % (sorted(a), sorted(b))); sys.exit(1)
print("  [OK]  gli stessi %d controlli in %s: Canc e Ctrl+Z li hanno rimessi tutti; le altre maschere ferme" % (len(a), prima))
dx = {b[k][0] - a[k][0] for k in a}
dy = {b[k][1] - a[k][1] for k in a}
mis = all(a[k][2:] == b[k][2:] for k in a)
if len(dx) == 1 and 1 <= dx.pop() <= 5 and dy == {0} and mis:
    print("  [OK]  tutti spostati dello stesso passo a destra, y e misure uguali")
    sys.exit(0)
print("  [NO]  spostamenti: dx %s dy %s misure uguali %s" % (sorted({b[k][0] - a[k][0] for k in a}), sorted(dy), mis))
sys.exit(1)
FINEPY
[ $? -eq 0 ] || esito=1
grep -aq "FAULT\|PANIC" "$D/qemu.log" && no "FAULT o PANIC sulla seriale" || ok "nessun FAULT"
echo "  foto da guardare: $D/2-tutti.ppm (maniglie piene sul primo, vuote sugli altri)"
[ $esito -eq 0 ] && echo "=== RIUSCITO ===" || echo "=== NON RIUSCITO ==="
exit $esito
