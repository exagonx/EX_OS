#!/bin/bash
# =============================================================================
# tools/prova_salvaschermo.sh — the screen savers of ExWin (voce 116)
#
#   1. salvaschermo cerchio, started by hand: after a few seconds the screen
#      is the saver's (the taskbar is covered, coloured pixels where the
#      circle went); one mouse move and the desktop is back;
#   2. salvaschermo scritta: the same, with the text;
#   3. pm starts it by itself: pm.cfg says Cerchio after 1 minute, nobody
#      touches anything for 75 s, the photo must be the saver's.
#
# What counts is the photo: the bar's grey (212,208,200, the classic theme)
# in the last rows of the screen, and how many pixels are neither black nor
# that grey.
#
#     tools/prova_salvaschermo.sh [CARTELLA]     (EXOS_ARCH=x86_64 for 64 bit)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-salvaschermo}"
mkdir -p "$D"
rm -f "$D"/*.ppm

CD=dist/exos.iso
if [ "$EXOS_ARCH" = x86_64 ]; then CD=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64; fi
[ -f "$CD" ] || { echo "manca $CD" >&2; exit 1; }
export EXOS_ISTANZA=salvasch EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD"

# "barra colorati": grey pixels in the bar's rows, coloured pixels anywhere
conta() {
    python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
barra = colorati = 0
for y in range(0, h, 2):
    for x in range(0, w, 2):
        i = (y * w + x) * 3
        c = (px[i], px[i + 1], px[i + 2])
        if y >= h - 24 and c == (212, 208, 200): barra += 1
        if c != (0, 0, 0) and c != (212, 208, 200) and max(c) - min(c) > 60: colorati += 1
print(barra, colorati)
EOF
}

esito=0
giudica() {     # NAME FOTO_DURANTE FOTO_DOPO
    local b1 c1 b2 c2
    read -r b1 c1 <<< "$(conta "$2")"
    read -r b2 c2 <<< "$(conta "$3")"
    echo "  durante: barra $b1, colorati $c1   dopo: barra $b2"
    if [ "${b1:-999}" -lt 20 ] && [ "${c1:-0}" -gt 200 ]; then
        echo "  [OK]  $1: lo schermo e' del salvaschermo, la barra coperta"
    else
        echo "  [NO]  $1: lo schermo non e' del salvaschermo"; esito=1
    fi
    if [ "${b2:-0}" -gt 200 ]; then
        echo "  [OK]  $1: dopo il mouse la scrivania e' tornata"
    else
        echo "  [NO]  $1: dopo il mouse la scrivania non e' tornata"; esito=1
    fi
}

for tipo in cerchio scritta; do
    {
        # ! FROM ExWin's TERMINAL, already in graphics: launched from the text
        # console, the Alt+F5 that goes back to the desktop is a key, and the
        # saver rightly ended at once (25/11, the first run of this test)
        echo "exwin@10"
        echo "key:alt-f1@2"
        echo "/exwin/bin/term &@6"
        echo "key:alt-f5@4"
        echo "/exwin/bin/salvaschermo $tipo Prova@8"
        echo "foto:$D/$tipo-durante.ppm@1"
        echo "mon:mouse_move 30 20@3"
        echo "foto:$D/$tipo-dopo.ppm@1"
    } > "$D/args-$tipo.txt"
    mapfile -t A < "$D/args-$tipo.txt"
    timeout 300 python3 tools/qemu_drive.py "${A[@]}" > "$D/$tipo.log" 2>&1
    echo "=== $tipo, lanciato a mano ==="
    giudica "$tipo" "$D/$tipo-durante.ppm" "$D/$tipo-dopo.ppm"
done

# ! THE CD IS READ ONLY, /root and /tmp included: the profile comes on a
# small disk made here, with pm.cfg already in it (mke2fs -d, as the Firefox
# tests do), mounted on /disk and used as HOME
G="$D/disco"; IMG="$D/hd.img"; PART="$D/part.img"
rm -rf "$G" "$IMG" "$PART"; mkdir -p "$G/casa/.exwin/config"
printf 'salvaschermo = Cerchio\nsalvaschermo_minuti = 1\n' > "$G/casa/.exwin/config/pm.cfg"
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L salva -d "$G" "$PART" 8M || exit 1
qemu-img create -f raw "$IMG" 10M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"; sync "$IMG"
{
    echo "mount hd0p1 /disk@5"
    echo "export HOME=/disk/casa@1"
    echo "exwin@80"
    echo "foto:$D/pm-durante.ppm@1"
    echo "mon:mouse_move 30 20@3"
    echo "foto:$D/pm-dopo.ppm@1"
} > "$D/args-pm.txt"
mapfile -t A < "$D/args-pm.txt"
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
    timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/pm.log" 2>&1
echo "=== pm lo lancia dopo un minuto fermo ==="
giudica "pm" "$D/pm-durante.ppm" "$D/pm-dopo.ppm"

[ $esito = 0 ] && echo "ESITO: TUTTO A POSTO" || echo "ESITO: QUALCOSA NON VA (foto in $D)"
exit $esito
