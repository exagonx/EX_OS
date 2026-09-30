#!/bin/bash
# =============================================================================
# tools/prova_img_nav.sh — i formati nuovi nel navigatore e in Immagini
# (@IMG-FORMATI, 30 settembre 2026)
#
# Una pagina su fondo giallo con quattro immagini di colori pieni:
#   - un WebP con perdita, verde;
#   - un WebP senza perdita, magenta, con 10 pixel di bordo TRASPARENTE;
#   - un PNG con un cerchio rosso su fondo TRASPARENTE;
#   - un JPEG CMYK (YCCK, col marcatore Adobe), azzurro.
# Sulla fotografia si contano i pixel di ogni colore. ! E IL NERO: prima i
# pixel trasparenti di PNG e WebP uscivano neri, ora devono prendere il giallo
# della pagina.
# Poi Immagini apre il WebP: la sua fotografia non deve dire «Non si legge».
#
#     bash tools/prova_img_nav.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-imgnav}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

magick -size 60x40 xc:'#20c020' -quality 85 "$D/verde.webp"
magick -size 60x60 xc:none -fill '#c020c0' -draw 'rectangle 10,10 49,49' \
    -define webp:lossless=true "$D/magenta.webp"
magick -size 60x60 xc:none -fill '#e02020' -draw 'circle 30,30 30,5' "$D/rosso.png"
magick -size 60x40 xc:'#00c0c0' -colorspace CMYK -quality 90 "$D/azzurro.jpg"
cat > "$D/img.html" <<'HTML'
<html><body style="background:#e0d040">
<p><img src="verde.webp"> <img src="magenta.webp"> <img src="rosso.png"> <img src="azzurro.jpg"></p>
</body></html>
HTML

export EXOS_ISTANZA=imgnav EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG" "$D"/*.ppm
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
for f in img.html verde.webp magenta.webp rosso.png azzurro.jpg; do
    "$DEBUGFS" -w -R "write $D/$f $f" "$OFF" > /dev/null 2>&1
done

timeout 600 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser /disk/img.html &@20" "key:alt-f5@4" "foto:$D/1-pagina.ppm@1" \
    "key:alt-f1@2" "/exwin/bin/immagini /disk/magenta.webp &@10" "key:alt-f5@4" \
    "foto:$D/2-immagini.ppm@1" > "$D/1.log" 2>&1

python3 - "$D/1-pagina.ppm" "$D/2-immagini.ppm" <<'PY'
import sys
def leggi(f):
    d = open(f, "rb").read(); p = d.split(b"\n", 3)
    w, h = map(int, p[1].split()); return w, h, p[3]
def conta(f, rgb, tol, y0=60, y1=560):
    w, h, px = leggi(f); n = 0
    for y in range(y0, min(y1, h)):
        for x in range(w):
            i = (y * w + x) * 3
            if all(abs(px[i + k] - rgb[k]) <= tol for k in range(3)): n += 1
    return n
ok = 0; no = 0
def controlla(cosa, cond, n):
    global ok, no
    print("  [%s]  %s (%d pixel)" % ("OK" if cond else "NO", cosa, n))
    if cond: ok += 1
    else: no += 1
p = sys.argv[1]
n = conta(p, (0x20, 0xc0, 0x20), 24); controlla("il WebP con perdita, verde", n > 1800, n)
n = conta(p, (0xc0, 0x20, 0xc0), 16); controlla("il WebP senza perdita, magenta", n > 1400, n)
n = conta(p, (0xe0, 0x20, 0x20), 16); controlla("il PNG, il cerchio rosso", n > 1500, n)
n = conta(p, (0x00, 0xc0, 0xc0), 24); controlla("il JPEG CMYK, azzurro", n > 1800, n)
n = conta(p, (0, 0, 0), 12, 70, 200); controlla("niente nero attorno: la trasparenza prende il giallo", n < 60, n)
n = conta(p, (0xe0, 0xd0, 0x40), 8); controlla("il giallo della pagina c'e'", n > 5000, n)
n = conta(sys.argv[2], (0xc0, 0x20, 0xc0), 16, 0, 600); controlla("Immagini apre il WebP", n > 1400, n)
print("  prova_img_nav: %d OK, %d NO" % (ok, no))
sys.exit(1 if no else 0)
PY
