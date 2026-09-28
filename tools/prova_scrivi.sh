#!/bin/bash
# =============================================================================
# tools/prova_scrivi.sh — i codificatori BMP e PNG e il lettore BMP di eximg,
# provati sull'host (@PAINT, 28 settembre 2026)
#
# Tre giri, contro due decodificatori:
#
#   1. tools/prove/scrivi_prova.c scrive le immagini di prova in PNG e BMP e
#      le rilegge con eximg_carica (il NOSTRO lettore): pixel per pixel;
#   2. ImageMagick legge gli stessi file (NON e' nostro: una CRC sbagliata in
#      modo simmetrico da noi a lui non torna) e i pixel devono essere quelli
#      attesi;
#   3. ImageMagick scrive BMP nelle forme che si trovano in giro — tavolozza a
#      1, 4 e 8 bit, 16 bit, 32 bit, RLE8, RLE4, l'intestazione OS/2 e quella
#      V5 — e il nostro lettore deve dare i suoi stessi pixel.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-scrivi}"
rm -rf "$D"
mkdir -p "$D/shim" "$D/img"
command -v magick >/dev/null || { echo "serve ImageMagick (magick)"; exit 1; }

printf '#include <stdlib.h>\n#include <string.h>\n' > "$D/shim/libc.h"
gcc -O1 -Wall -Wno-unused-result -I "$D/shim" -I lib/eximg -I lib/exzip \
    tools/prove/scrivi_prova.c lib/eximg/scrivi.c lib/exzip/deflate.c \
    lib/eximg/eximg.c lib/eximg/bmp.c lib/eximg/png.c lib/eximg/jpg.c \
    lib/eximg/gif.c lib/eximg/ico.c lib/eximg/inflate.c \
    -o "$D/scrivi_prova" || exit 1

FALLITI=0
male() { echo "  *** FALLITO: $*"; FALLITI=$((FALLITI + 1)); }

echo "=== 1. scritti e riletti da eximg ==="
"$D/scrivi_prova" genera "$D/img" || FALLITI=$((FALLITI + 1))

echo "=== 2. i nostri file letti da ImageMagick ==="
for r in "$D"/img/img*.rgba; do
    b="${r%.rgba}"
    magick "$b.png" -depth 8 rgba:"$b.png.rgba" 2>"$b.err" || { male "$b.png non si apre: $(head -c 200 "$b.err")"; continue; }
    cmp -s "$r" "$b.png.rgba" || male "$(basename "$b").png: pixel diversi per ImageMagick"
    # Il BMP non porta l'alfa: si confrontano i soli RGB.
    magick "$b.bmp" -depth 8 rgb:"$b.bmp.rgb" 2>"$b.err" || { male "$b.bmp non si apre"; continue; }
    magick -size "$(magick identify -format '%wx%h' "$b.png")" -depth 8 rgba:"$r" rgb:"$b.atteso.rgb"
    cmp -s "$b.atteso.rgb" "$b.bmp.rgb" || male "$(basename "$b").bmp: pixel diversi per ImageMagick"
done
# E pngcheck di ImageMagick: un pezzo con la CRC sbagliata lo fa lamentare.
for p in "$D"/img/img*.png; do
    magick identify -regard-warnings "$p" >/dev/null 2>&1 || male "$(basename "$p"): ImageMagick si lamenta"
done

echo "=== 3. BMP scritti da ImageMagick letti da eximg ==="
magick -size 37x23 gradient:red-blue -fill yellow -draw 'circle 18,11 18,3' "$D/src.png"
prova_bmp() {   # nome, opzioni di ImageMagick
    local n="$1"; shift
    magick "$D/src.png" "$@" "$D/$n.bmp" || { male "$n: ImageMagick non lo scrive"; return; }
    magick "$D/$n.bmp" -depth 8 -alpha off rgb:"$D/$n.suo.rgb"
    "$D/scrivi_prova" leggi "$D/$n.bmp" "$D/$n.nostro.rgba" >/dev/null || { male "$n: eximg non lo legge"; return; }
    magick -size 37x23 -depth 8 rgba:"$D/$n.nostro.rgba" -alpha off rgb:"$D/$n.nostro.rgb"
    cmp -s "$D/$n.suo.rgb" "$D/$n.nostro.rgb" && echo "  $n: uguale" || male "$n: pixel diversi"
}
prova_bmp b24   -type TrueColor -define bmp:format=bmp3
prova_bmp p8    -colors 200 -type Palette -define bmp:format=bmp3
prova_bmp p4    -colors 16 -type Palette -define bmp:format=bmp3
prova_bmp p1    -monochrome -define bmp:format=bmp3
prova_bmp b32   -type TrueColorAlpha -define bmp:format=bmp4
prova_bmp v5    -type TrueColor -define bmp:format=bmp5
prova_bmp b16   -define bmp:subtype=RGB565
prova_bmp b555  -define bmp:subtype=RGB555
prova_bmp os2   -type TrueColor -define bmp:format=bmp2
prova_bmp rle8  -colors 200 -type Palette -compress RLE -define bmp:format=bmp3
prova_bmp rle4  -colors 16 -type Palette -compress RLE -define bmp:format=bmp3

echo
if [ "$FALLITI" -eq 0 ]; then echo "prova_scrivi: TUTTO BENE"; else echo "prova_scrivi: $FALLITI FALLITI"; fi
exit $((FALLITI != 0))
