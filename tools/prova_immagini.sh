#!/bin/bash
# =============================================================================
# tools/prova_immagini.sh — il visualizzatore di immagini (@IMMAGINI)
#
# Sul disco di prova quattro immagini di colori pieni e diversi, cosi' dalla
# fotografia si sa QUALE si vede contando i pixel di quel colore — senza
# dipendere da dove il server ha messo la finestra:
#
#   1-rosso.png   1600x1000   piu' grande dello schermo: va ADATTATA
#   2-blu.jpg      300x200
#   3-verde.bmp    200x150
#   4-magenta.gif  120x120    per il file manager
#   nota.txt                  NON e' un'immagine: la cartella la salta
#
#   1. `immagini /disk/1-rosso.png`: rosso, e meno di una finestra intera;
#   2. Pag giu': blu, verde, magenta; Pag giu' ancora: resta magenta (e' l'ultima);
#   3. Home: di nuovo rosso; «1» (grandezza vera): il rosso RIEMPIE di piu';
#   4. dal file manager, Tab (il fuoco parte sull'albero) / Fine / su / Invio
#      su 4-magenta.gif: si apre il
#      visualizzatore, perche' /exwin/lib/tipi.txt lo dice.
#
#     tools/prova_immagini.sh [directory-di-lavoro]   (default /tmp/exos-immagini)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs, ImageMagick.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-immagini}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

magick -size 1600x1000 xc:'#E02020' "$D/1-rosso.png"
magick -size 300x200   xc:'#2040E0' -quality 95 "$D/2-blu.jpg"
magick -size 200x150   xc:'#20C040' -type TrueColor "BMP3:$D/3-verde.bmp"
magick -size 120x120   xc:'#E020E0' "$D/4-magenta.gif"
echo "non sono un'immagine" > "$D/nota.txt"

# ! LA PRIMA VOLTA LA PNG ROSSA NON SI VEDEVA, e la colpa sembrava la memoria
# (32 MB, e una 1600x1000 da decodificare). Non lo era: a 64 MB falliva lo
# stesso. ImageMagick salva un colore solo come tavolozza da 1 BIT, ed eximg
# leggeva solo gli 8 — vedi lib/eximg/png.c e tools/prova_png.sh. Con quello
# sistemato la prova passa anche coi 32 MB di sempre.
export EXOS_ISTANZA=immagini EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
for f in 1-rosso.png 2-blu.jpg 3-verde.bmp 4-magenta.gif nota.txt; do
    "$DEBUGFS" -w -R "write $D/$f $f" "$OFF" > /dev/null 2>&1
done

timeout 600 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/immagini /disk/1-rosso.png &@10" "key:alt-f5@3" \
    "foto:$D/1-rosso.ppm@1" \
    "key:pgdn@3" "foto:$D/2-blu.ppm@1" \
    "key:pgdn@3" "foto:$D/3-verde.ppm@1" \
    "key:pgdn@3" "foto:$D/4-magenta.ppm@1" \
    "key:pgdn@3" "foto:$D/4-ancora-magenta.ppm@1" \
    "key:home@3" "foto:$D/5-rosso.ppm@1" \
    "key:1@3" "foto:$D/6-vero.ppm@1" \
    "key:ctrl-q@3" \
    "key:alt-f1@2" "/exwin/bin/filemgr /disk &@8" "key:alt-f5@3" \
    "foto:$D/7-filemgr.ppm@1" \
    "key:tab@1" "key:end@1" "key:up@1" "key:ret@8" \
    "foto:$D/8-da-filemgr.ppm@1" > "$D/1.log" 2>&1

# Quanti pixel di un colore, con una tolleranza (il JPG non e' esatto).
conta() {   # foto rrggbb
    python3 - "$1" "$2" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read()
p = d.split(b"\n", 3)
w, h = map(int, p[1].split()); px = p[3]
c = bytes.fromhex(sys.argv[2]); n = 0
for i in range(0, w * h * 3, 3):
    if abs(px[i]-c[0]) < 24 and abs(px[i+1]-c[1]) < 24 and abs(px[i+2]-c[2]) < 24: n += 1
print(n)
EOF
}

esito=0
ok() { echo "  [OK]  $*"; }
no() { echo "  [NO]  $*"; esito=1; }
for f in 1-rosso 2-blu 3-verde 4-magenta 4-ancora-magenta 5-rosso 6-vero 7-filemgr 8-da-filemgr; do
    [ -s "$D/$f.ppm" ] || { no "manca la fotografia $f"; }
done

r1=$(conta "$D/1-rosso.ppm" E02020);  b2=$(conta "$D/2-blu.ppm" 2040E0)
g3=$(conta "$D/3-verde.ppm" 20C040);  m4=$(conta "$D/4-magenta.ppm" E020E0)
m4b=$(conta "$D/4-ancora-magenta.ppm" E020E0)
r5=$(conta "$D/5-rosso.ppm" E02020);  r6=$(conta "$D/6-vero.ppm" E02020)
m7=$(conta "$D/7-filemgr.ppm" E020E0); m8=$(conta "$D/8-da-filemgr.ppm" E020E0)
echo "        rosso $r1, blu $b2, verde $g3, magenta $m4/$m4b, rosso $r5 -> al 100% $r6, magenta $m7 -> $m8"

[ "$r1" -gt 100000 ] && [ "$r1" -lt 480000 ] && ok "la PNG grande si vede, adattata alla finestra" || no "la PNG grande: $r1 pixel rossi"
[ "$b2" -gt 50000 ] && [ "$b2" -lt 70000 ] && ok "Pag giu': il JPG blu, a grandezza vera (300x200)" || no "il JPG blu: $b2 pixel"
[ "$g3" -gt 25000 ] && [ "$g3" -lt 35000 ] && ok "Pag giu': il BMP verde (200x150); nota.txt saltato" || no "il BMP verde: $g3 pixel"
[ "$m4" -gt 13000 ] && [ "$m4" -lt 16000 ] && ok "Pag giu': la GIF magenta (120x120)" || no "la GIF magenta: $m4 pixel"
[ "$m4b" = "$m4" ] && ok "Pag giu' sull'ultima: resta li'" || no "dopo l'ultima e' cambiato qualcosa ($m4 -> $m4b)"
[ "$r5" = "$r1" ] && ok "Home: di nuovo la prima, adattata come prima" || no "Home: $r5 pixel rossi invece di $r1"
[ "$r6" -gt "$r5" ] && ok "«1»: al 100% il rosso riempie di piu' ($r5 -> $r6)" || no "«1» non ha ingrandito ($r5 -> $r6)"
[ "$m7" -lt 1000 ] && [ "$m8" -gt 13000 ] && ok "dal file manager la GIF si apre nel visualizzatore (tipi.txt)" || no "dal file manager: magenta $m7 -> $m8"

echo ""
echo "  Le fotografie sono in $D/*.ppm"
exit $esito
