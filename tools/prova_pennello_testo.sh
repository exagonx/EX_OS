#!/bin/bash
# =============================================================================
# tools/prova_pennello_testo.sh — lo strumento Testo di Pennello (@PAINT)
#
# Un PNG bianco 320x200 aperto dalla riga di comando; T sceglie il Testo, ]
# porta lo spessore al massimo (36 pixel); un clic nella tela, «EXOS» e
# Invio; Ctrl+S salva. Fuori da QEMU il PNG deve avere la scritta: pixel
# scuri in un riquadro alto fra 20 e 40 pixel e largo piu' di 50. Poi Ctrl+Z,
# e la fotografia della tela deve tornare senza scritta.
#
#     tools/prova_pennello_testo.sh [directory-di-lavoro]   (default /tmp/exos-ptesto)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ptesto}"
mkdir -p "$D"
rm -f "$D"/*.ppm "$D"/uscita.png
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

magick -size 320x200 xc:white "$D/bianca.png"
export EXOS_ISTANZA=ptesto EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/bianca.png bianca.png" "$OFF" > /dev/null 2>&1

{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/pennello /disk/bianca.png &@8"; echo "key:alt-f5@3"
    echo "key:t@1"; echo "key:bracket_right@1"; echo "key:bracket_right@1"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 19); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 9); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@3"
    echo "foto:$D/1-dialogo.ppm@1"
    echo "EXOS@3"
    echo "foto:$D/2-testo.ppm@1"
    echo "key:ctrl-s@4"
    echo "foto:$D/3-salvato.ppm@1"
    echo "key:ctrl-z@2"
    echo "foto:$D/4-annullato.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

"$DEBUGFS" -R "dump bianca.png $D/uscita.png" "$OFF" > /dev/null 2>&1
esito=0
if [ -s "$D/uscita.png" ]; then
    b=$(magick "$D/uscita.png" -threshold 50% -negate -trim -format "%w %h" info: 2>/dev/null)
    w=${b% *}; h=${b#* }
    if [ -n "$w" ] && [ "$w" -gt 50 ] && [ "$h" -ge 20 ] && [ "$h" -le 40 ]; then
        echo "  [OK]  la scritta e' nei pixel del PNG salvato (${w}x${h})"
    else echo "  [NO]  nel PNG salvato la scritta non c'e' o ha misure strane ($b)"; esito=1; fi
else echo "  [NO]  il PNG non e' stato salvato"; esito=1; fi
scuri() { python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read(); c = d.split(b"\n", 3); W, H = map(int, c[1].split()); px = c[3]
print(sum(1 for y in range(60, 400) for x in range(140, 480) if max(px[(y*W+x)*3:(y*W+x)*3+3]) < 60))
EOF
}
s2=$(scuri "$D/2-testo.ppm"); s4=$(scuri "$D/4-annullato.ppm")
echo "        pixel scuri nella tela: col testo $s2, dopo Ctrl+Z $s4"
if [ "$s2" -gt 200 ] && [ "$s4" -lt $((s2 / 4)) ]; then echo "  [OK]  Ctrl+Z toglie la scritta"
else echo "  [NO]  Ctrl+Z non toglie la scritta"; esito=1; fi
exit $esito
