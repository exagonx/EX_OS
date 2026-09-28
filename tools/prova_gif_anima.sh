#!/bin/bash
# =============================================================================
# tools/prova_gif_anima.sh — le GIF animate si muovono in EXBrowser (@NAV-GIF)
#
# Una pagina con una GIF di due fotogrammi, uno tutto rosso e uno tutto blu,
# 600 ms l'uno, e quattro fotografie a circa un secondo di distanza — nel
# navigatore e poi nel visualizzatore Immagini. Se
# l'animazione gira, almeno una fotografia ha il rosso e almeno una il blu;
# se la GIF e' ferma (com'era prima) sono tutte rosse.
#
# La composizione dei fotogrammi — smaltimento, trasparenza, posizione — si
# prova sull'host contro ImageMagick: tools/prova_gif.sh. Qui si prova solo
# che il navigatore la faccia girare.
#
#     tools/prova_gif_anima.sh [directory-di-lavoro]   (default /tmp/exos-gifanima)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs, ImageMagick.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-gifanima}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

magick -delay 60 -size 120x80 xc:'#E02020' xc:'#2040E0' -loop 0 "$D/anima.gif"
# ! UNA GIF DIVERSA PER IL VISUALIZZATORE, verde e magenta: quando lo si apre
# il navigatore e' ancora a schermo con la sua, e contando il rosso e il blu
# il visualizzatore passerebbe la prova anche da fermo.
magick -delay 60 -size 120x80 xc:'#20C040' xc:'#E020E0' -loop 0 "$D/anima2.gif"
cat > "$D/anima.html" <<'EOF'
<html><body><p>una GIF animata:</p><img src="anima.gif"></body></html>
EOF

export EXOS_ISTANZA=gifanima EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
for f in anima.gif anima2.gif anima.html; do "$DEBUGFS" -w -R "write $D/$f $f" "$OFF" > /dev/null 2>&1; done

timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser /disk/anima.html &@15" "key:alt-f5@4" \
    "foto:$D/1.ppm@0" "foto:$D/2.ppm@0" "foto:$D/3.ppm@0" "foto:$D/4.ppm@0" \
    "key:alt-f1@2" "/exwin/bin/immagini /disk/anima2.gif &@10" "key:alt-f5@4" \
    "foto:$D/5.ppm@0" "foto:$D/6.ppm@0" "foto:$D/7.ppm@0" "foto:$D/8.ppm@0" \
    > "$D/1.log" 2>&1

conta() {   # foto rrggbb
    python3 - "$1" "$2" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read(); p = d.split(b"\n", 3)
w, h = map(int, p[1].split()); px = p[3]; c = bytes.fromhex(sys.argv[2])
print(sum(1 for i in range(0, w*h*3, 3) if abs(px[i]-c[0]) < 24 and abs(px[i+1]-c[1]) < 24 and abs(px[i+2]-c[2]) < 24))
EOF
}
esito=0
verdetto() {   # chi, colore1, colore2, poi le quattro foto
    local chi="$1" c1="$2" c2="$3" uno=0 due=0 k r b; shift 3
    for k in "$@"; do
        [ -s "$D/$k.ppm" ] || { echo "  [NO]  manca la fotografia $k"; esito=1; continue; }
        r=$(conta "$D/$k.ppm" "$c1"); b=$(conta "$D/$k.ppm" "$c2")
        echo "        foto $k: $c1 $r, $c2 $b"
        [ "$r" -gt 5000 ] && uno=$((uno + 1))
        [ "$b" -gt 5000 ] && due=$((due + 1))
    done
    if [ "$uno" -ge 1 ] && [ "$due" -ge 1 ]; then echo "  [OK]  $chi: la GIF si muove ($c1 in $uno foto, $c2 in $due)"
    else echo "  [NO]  $chi: la GIF e' ferma ($c1 in $uno foto, $c2 in $due)"; esito=1; fi
}
verdetto EXBrowser E02020 2040E0 1 2 3 4
# Il visualizzatore adatta alla finestra solo le immagini piu' grandi: la GIF
# 120x80 si vede a grandezza vera.
verdetto Immagini 20C040 E020E0 5 6 7 8

echo ""
echo "  Le fotografie sono in $D/*.ppm"
exit $esito
