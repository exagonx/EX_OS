#!/bin/bash
# =============================================================================
# tools/prova_png.sh — il lettore PNG di eximg contro ImageMagick, sull'host
# (@IMMAGINI, 28 settembre 2026)
#
# ImageMagick scrive la STESSA immagine in ogni combinazione che la specifica
# ammette — grigio a 1/2/4/8/16 bit, tavolozza a 1/2/4/8, RGB, grigio+alfa e
# RGBA a 8 e 16 — con e senza interlacciamento Adam7, su tre misure scelte
# per essere scomode (1x1: sei passate di Adam7 vuote; 3x5; 37x23: righe che
# non finiscono a byte pieno). Il nostro lettore deve dare i suoi pixel.
#
# ! L'ALFA NON SI CONFRONTA: eximg la butta via apposta (vedi png.c). Si
# confrontano gli RGB grezzi, cioe' senza fondere l'alfa su nessuno sfondo.
# ! A 16 BIT SI AMMETTE UNO DI DIFFERENZA: noi prendiamo il byte alto,
# ImageMagick arrotonda (v*255/65535), e i due differiscono al piu' di uno.
#
#     tools/prova_png.sh [directory-di-lavoro]   (default /tmp/exos-png)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-png}"
rm -rf "$D"
mkdir -p "$D/shim"
command -v magick >/dev/null || { echo "serve ImageMagick (magick)"; exit 1; }

printf '#include <stdlib.h>\n#include <string.h>\n' > "$D/shim/libc.h"
gcc -O1 -Wall -Wno-unused-result -I "$D/shim" -I lib/eximg -I lib/exzip \
    tools/prove/scrivi_prova.c lib/eximg/scrivi.c lib/exzip/deflate.c \
    lib/eximg/eximg.c lib/eximg/bmp.c lib/eximg/png.c lib/eximg/jpg.c \
    lib/eximg/gif.c lib/eximg/ico.c lib/eximg/inflate.c \
    -o "$D/leggi" || exit 1

FATTI=0; FALLITI=0
prova() {   # nome misura tipo-colore profondita' interlacciamento [colori]
    local n="$1" m="$2" ct="$3" bd="$4" il="$5" col="$6" f="$D/$1.png" opz=()
    [ -n "$col" ] && opz+=(-colors "$col")
    [ "$ct" = 0 ] || [ "$ct" = 4 ] && opz+=(-colorspace Gray)
    [ "$ct" = 4 ] || [ "$ct" = 6 ] && opz+=(-alpha set -channel A -evaluate set 70% +channel)
    magick -size "$m" plasma:fractal -seed 7 "${opz[@]}" \
        -define png:color-type="$ct" -define png:bit-depth="$bd" \
        -interlace "$il" "$f" 2>/dev/null
    FATTI=$((FATTI + 1))
    local veri; veri=$(python3 -c "import sys; d=open(sys.argv[1],'rb').read(); print(d[25], d[24], 'adam7' if d[28] else '')" "$f")
    if ! "$D/leggi" leggi "$f" "$D/$n.nostro.rgba" > /dev/null; then
        echo "  *** FALLITO: $n ($veri): eximg non lo legge"; FALLITI=$((FALLITI + 1)); return
    fi
    magick "$f" -alpha off -depth 8 rgb:"$D/$n.suo.rgb"
    if python3 - "$D/$n.nostro.rgba" "$D/$n.suo.rgb" "$bd" <<'EOF'
import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
tol = 1 if sys.argv[3] == "16" else 0
n = len(b) // 3
if len(a) != n * 4: sys.exit(1)
for i in range(n):
    for k in range(3):
        if abs(a[i*4+k] - b[i*3+k]) > tol: sys.exit(1)
EOF
    then echo "  $n: uguale  ($veri)"
    else echo "  *** FALLITO: $n ($veri): pixel diversi"; FALLITI=$((FALLITI + 1)); fi
}

for m in 1x1 3x5 37x23; do
    for il in None PNG; do
        s="${m}-${il}"
        prova "g1-$s"   "$m" 0 1  "$il"
        prova "g2-$s"   "$m" 0 2  "$il"
        prova "g4-$s"   "$m" 0 4  "$il"
        prova "g8-$s"   "$m" 0 8  "$il"
        prova "g16-$s"  "$m" 0 16 "$il"
        prova "p1-$s"   "$m" 3 1  "$il" 2
        prova "p2-$s"   "$m" 3 2  "$il" 4
        prova "p4-$s"   "$m" 3 4  "$il" 16
        prova "p8-$s"   "$m" 3 8  "$il" 200
        prova "rgb8-$s" "$m" 2 8  "$il"
        prova "rgb16-$s" "$m" 2 16 "$il"
        prova "ga8-$s"  "$m" 4 8  "$il"
        prova "ga16-$s" "$m" 4 16 "$il"
        prova "rgba8-$s" "$m" 6 8 "$il"
        prova "rgba16-$s" "$m" 6 16 "$il"
    done
done

# ! LA TAVOLOZZA A 1 BIT SI CHIEDE A PARTE: su un'immagine sfumata ImageMagick
# sceglie da se' 2 bit anche chiedendone 1. Due colori pieni la fanno a 1 —
# ed e' il caso che il visualizzatore ha trovato per primo (una PNG di un
# colore solo). Qui si controlla anche che il file SIA davvero a 1 bit.
for il in None PNG; do
    f="$D/p1vero-$il.png"
    magick -size 37x23 xc:'#E02020' -fill white -draw 'rectangle 0,0 18,11' \
        -define png:bit-depth=1 -define png:color-type=3 -interlace "$il" "$f"
    FATTI=$((FATTI + 1))
    prof=$(python3 -c "import sys; print(open(sys.argv[1],'rb').read()[24])" "$f")
    if [ "$prof" != 1 ]; then echo "  *** FALLITO: p1vero-$il non e' a 1 bit ($prof)"; FALLITI=$((FALLITI + 1)); continue; fi
    "$D/leggi" leggi "$f" "$D/p1vero.nostro.rgba" > /dev/null &&
    magick "$f" -alpha off -depth 8 rgb:"$D/p1vero.suo.rgb" &&
    python3 - "$D/p1vero.nostro.rgba" "$D/p1vero.suo.rgb" <<'EOF' && echo "  p1vero-$il: uguale  (3 1)" || { echo "  *** FALLITO: p1vero-$il"; FALLITI=$((FALLITI + 1)); }
import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
n = len(b) // 3
sys.exit(0 if len(a) == n * 4 and all(a[i*4+k] == b[i*3+k] for i in range(n) for k in range(3)) else 1)
EOF
done

echo
if [ "$FALLITI" -eq 0 ]; then echo "prova_png: $FATTI su $FATTI, TUTTO BENE"
else echo "prova_png: $FALLITI FALLITI su $FATTI"; fi
exit $((FALLITI != 0))
