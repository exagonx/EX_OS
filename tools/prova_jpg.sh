#!/bin/bash
# =============================================================================
# tools/prova_jpg.sh — il decodificatore JPEG di eximg contro ImageMagick
# (@JPEG-PROGRESSIVO, 30 settembre 2026)
#
# Sull'host, senza QEMU: compila tools/prove/jpgprova.c con lib/eximg/jpg.c,
# fa con ImageMagick la stessa immagine in piu' forme — baseline e
# progressiva, colore 4:2:0 e 4:4:4, grigio, misure che non sono multiple di
# 8, un intervallo di restart — e confronta i pixel con quelli che da'
# ImageMagick. Il progressivo e' la forma che il 30 settembre non si apriva:
# sfondi e foto del web.
#
#     bash tools/prova_jpg.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-jpg}"
mkdir -p "$D"
command -v magick > /dev/null || { echo "serve ImageMagick (magick)" >&2; exit 2; }
cc -O2 -Wall -I lib/eximg -o "$D/jpgprova" tools/prove/jpgprova.c lib/eximg/jpg.c || exit 1

# L'originale: sfumature, bordi netti e un po' di rumore, cosi' ogni fascia
# di coefficienti ha qualcosa da portare.
magick -size 203x151 gradient:'#2050d0-#f0c040' \
    \( -size 203x151 xc:none -fill '#c02020' -draw 'circle 100,75 100,20' \) -composite \
    -fill white -draw 'rectangle 20,20 60,50' -attenuate 0.3 +noise Gaussian "$D/orig.png"

fallite=0
prova() {   # nome, poi le opzioni di ImageMagick
    local nome=$1; shift
    magick "$D/orig.png" "$@" "$D/$nome.jpg" || { echo "NO  $nome: magick"; fallite=$((fallite+1)); return; }
    magick "$D/$nome.jpg" -depth 8 rgb:"$D/$nome.rgb"
    "$D/jpgprova" "$D/$nome.jpg" "$D/$nome.rgb" || fallite=$((fallite+1))
}
prova base420     -quality 85 -sampling-factor 2x2
prova base444     -quality 85 -sampling-factor 1x1
prova prog420     -quality 85 -sampling-factor 2x2 -interlace JPEG
prova prog444     -quality 85 -sampling-factor 1x1 -interlace JPEG
prova prog422     -quality 90 -sampling-factor 2x1 -interlace JPEG
prova proggrigio  -quality 85 -colorspace Gray -interlace JPEG
prova progq30     -quality 30 -sampling-factor 2x2 -interlace JPEG
prova progrestart -quality 85 -sampling-factor 2x2 -interlace JPEG -define jpeg:restart-interval=3
# CMYK (30 settembre 2026): ImageMagick lo scrive YCCK col marcatore Adobe,
# e il riferimento torna in sRGB.
cmyk() {   # nome, poi le opzioni
    local nome=$1; shift
    magick "$D/orig.png" -colorspace CMYK "$@" "$D/$nome.jpg" || { echo "NO  $nome: magick"; fallite=$((fallite+1)); return; }
    magick "$D/$nome.jpg" -colorspace sRGB -depth 8 rgb:"$D/$nome.rgb"
    "$D/jpgprova" "$D/$nome.jpg" "$D/$nome.rgb" || fallite=$((fallite+1))
}
cmyk cmyk         -quality 90
cmyk cmykprog     -quality 80 -interlace JPEG

echo "--- $fallite fallite"
[ $fallite -eq 0 ]
