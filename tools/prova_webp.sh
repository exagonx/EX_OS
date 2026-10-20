#!/bin/bash
# =============================================================================
# tools/prova_webp.sh — il decodificatore WebP di eximg contro libwebp
# (@IMG-FORMATI, 30 settembre 2026)
#
# Sull'host, senza QEMU. ImageMagick (che usa libwebp) fa la stessa immagine in
# piu' forme — senza perdita con e senza alfa, con perdita a qualita' diverse,
# con perdita e alfa, una tavolozza di pochi colori, misure che non sono
# multiple di 16 — e la rilegge in RGBA; tools/prove/webpprova confronta.
#
#     bash tools/prova_webp.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-webp}"
mkdir -p "$D"
command -v magick > /dev/null || { echo "serve ImageMagick (magick)" >&2; exit 2; }
cc -O2 -Wall -I lib/eximg -o "$D/webpprova" tools/prove/webpprova.c \
    lib/eximg/webp.c lib/eximg/vp8.c || exit 1

# L'originale: sfumature, bordi netti, rumore, e un buco trasparente.
magick -size 203x151 gradient:'#2050d0-#f0c040' \
    \( -size 203x151 xc:none -fill '#c02020' -draw 'circle 100,75 100,20' \) -composite \
    -fill white -draw 'rectangle 20,20 60,50' -attenuate 0.3 +noise Gaussian "$D/orig.png"
magick "$D/orig.png" \( -size 203x151 xc:white -fill black -draw 'circle 150,110 150,80' \
    -blur 0x4 \) -alpha off -compose CopyOpacity -composite "$D/alfa.png"
magick -size 97x61 xc:'#e0e0e0' -fill '#205080' -draw 'rectangle 10,10 50,40' \
    -fill '#c03030' -draw 'circle 70,30 70,15' +dither -colors 6 "$D/pochi.png"

fallite=0
prova() {   # nome, sorgente, modo (esatto|perdita), poi le opzioni di ImageMagick
    local nome=$1 src=$2 modo=$3; shift 3
    magick "$D/$src" "$@" "$D/$nome.webp" || { echo "NO  $nome: magick"; fallite=$((fallite+1)); return; }
    magick "$D/$nome.webp" -depth 8 rgba:"$D/$nome.rgba"
    "$D/webpprova" "$D/$nome.webp" "$D/$nome.rgba" "$modo" || fallite=$((fallite+1))
}
prova ll          orig.png  esatto  -define webp:lossless=true
prova ll_alfa     alfa.png  esatto  -define webp:lossless=true
prova ll_pochi    pochi.png esatto  -define webp:lossless=true
prova ll_veloce   orig.png  esatto  -define webp:lossless=true -define webp:method=0
prova ll_lento    orig.png  esatto  -define webp:lossless=true -define webp:method=6
prova q80         orig.png  perdita -quality 80
prova q30         orig.png  perdita -quality 30
prova q95         orig.png  perdita -quality 95
prova q80_alfa    alfa.png  perdita -quality 80
prova q75_segm    orig.png  perdita -quality 75 -define webp:segments=4 -define webp:sns-strength=80
prova q70_sempl   orig.png  perdita -quality 70 -define webp:filter-type=0
prova q70_part    orig.png  perdita -quality 70 -define webp:partitions=3
# ! IN GRIGIO IL COLORE E' COSTANTE, e la differenza con libwebp (che
# interpola il colore, noi lo ripetiamo) sparisce: qui il VP8 dev'essere
# identico al bit, e lo e'.
prova grigio      orig.png  esatto  -colorspace Gray -quality 60
# Un'animazione: si legge il primo fotogramma.
magick -delay 20 "$D/orig.png" \( "$D/orig.png" -negate \) -loop 0 "$D/anim.webp"
magick "$D/anim.webp[0]" -depth 8 rgba:"$D/anim.rgba"
"$D/webpprova" "$D/anim.webp" "$D/anim.rgba" perdita || fallite=$((fallite+1))
echo "prova_webp: $fallite fallite"
[ "$fallite" -eq 0 ]
