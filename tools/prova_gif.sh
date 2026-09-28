#!/bin/bash
# =============================================================================
# tools/prova_gif.sh — le GIF animate di eximg contro ImageMagick, sull'host
# (@NAV-GIF, 28 settembre 2026)
#
# ImageMagick scrive animazioni scelte per i casi che si sbagliano — fotogrammi
# piu' piccoli della tela e spostati, trasparenza, e i tre smaltimenti (none,
# background, previous) — e con `-coalesce` da' i fotogrammi COMPOSTI, cioe'
# quel che un browser deve mostrare a ogni passo. I nostri devono essere
# uguali: stessa alfa, e stessi colori dove l'alfa e' piena. Si fanno due giri
# interi, perche' il ricominciare da capo e' un pezzo di codice suo.
#
# E una GIF ferma (un fotogramma solo) NON deve aprirsi come animazione.
#
#     tools/prova_gif.sh [directory-di-lavoro]   (default /tmp/exos-gif)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-gif}"
rm -rf "$D"
mkdir -p "$D/shim"
command -v magick >/dev/null || { echo "serve ImageMagick (magick)"; exit 1; }

printf '#include <stdlib.h>\n#include <string.h>\n' > "$D/shim/libc.h"
gcc -O1 -Wall -Wno-unused-result -I "$D/shim" -I lib/eximg -I lib/exzip \
    tools/prove/scrivi_prova.c lib/eximg/scrivi.c lib/exzip/deflate.c \
    lib/eximg/eximg.c lib/eximg/bmp.c lib/eximg/png.c lib/eximg/jpg.c \
    lib/eximg/gif.c lib/eximg/ico.c lib/eximg/inflate.c \
    -o "$D/prova" || exit 1

FALLITI=0
male() { echo "  *** FALLITO: $*"; FALLITI=$((FALLITI + 1)); }

prova() {   # nome, poi gli argomenti di ImageMagick che fanno l'animazione
    local nome="$1"; shift
    local f="$D/$nome.gif" dir="$D/$nome"
    mkdir -p "$dir/suoi"
    magick "$@" -loop 0 "$f" || { male "$nome: ImageMagick non la scrive"; return; }
    local nf; nf=$(magick identify "$f" | wc -l)
    magick "$f" -coalesce "$dir/suoi/s%d.png"
    if ! "$D/prova" anima "$f" "$dir" $((nf * 2)) > "$dir/ritardi.txt"; then
        male "$nome: $(cat "$dir/ritardi.txt")"; return
    fi
    local k ok=1
    for ((k = 0; k < nf * 2; k++)); do
        local s=$((k % nf))
        magick "$dir/suoi/s$s.png" -alpha set -depth 8 rgba:"$dir/s$s.rgba"
        python3 - "$dir/f$k.rgba" "$dir/s$s.rgba" <<'PY' || { male "$nome: fotogramma $k (suo $s) diverso"; ok=0; }
import sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
if len(a) != len(b): sys.exit(1)
for i in range(0, len(a), 4):
    oa = a[i+3] >= 128; ob = b[i+3] >= 128
    if oa != ob: sys.exit(1)
    if oa and a[i:i+3] != b[i:i+3]: sys.exit(1)
PY
    done
    [ $ok = 1 ] && echo "  $nome: $nf fotogrammi, due giri, uguali  (ritardi: $(awk '{printf "%s ", $3}' "$dir/ritardi.txt" | cut -c1-40))"
}

# 1. fotogrammi piccoli e spostati su una tela, smaltimento none: si accumulano
prova spostati -dispose none -delay 10 -size 40x30 xc:red \
    \( -size 10x10 xc:blue -repage 40x30+5+5 \) \( -size 10x10 xc:lime -repage 40x30+22+12 \)
# 2. smaltimento background: il pezzo di prima si cancella al trasparente
prova sfondo -dispose none -delay 20 -size 40x30 xc:none -dispose background \
    \( -size 12x12 xc:blue -repage 40x30+2+2 \) \( -size 12x12 xc:orange -repage 40x30+20+10 \)
# 3. smaltimento previous: si torna a com'era prima del fotogramma
prova precedente -dispose none -delay 5 -size 40x30 xc:yellow -dispose previous \
    \( -size 8x8 xc:blue -repage 40x30+4+4 \) \( -size 8x8 xc:red -repage 40x30+24+14 \)
# 4. trasparenza dentro un fotogramma, e il ritardo 0 che vale 100 ms
prova trasparente -dispose none -delay 0 -size 30x30 xc:green \
    \( -size 30x30 xc:none -fill magenta -draw 'rectangle 5,5 14,24' \) \
    \( -size 30x30 xc:none -fill cyan -draw 'rectangle 15,5 24,24' \)

# 5. una GIF ferma non e' un'animazione
magick -size 20x20 xc:blue "$D/ferma.gif"
if "$D/prova" anima "$D/ferma.gif" "$D" 1 > /dev/null; then male "una GIF di un fotogramma si e' aperta come animazione"
else echo "  ferma: un fotogramma solo, non e' un'animazione (eximg_carica basta)"; fi

echo
if [ "$FALLITI" -eq 0 ]; then echo "prova_gif: TUTTO BENE"; else echo "prova_gif: $FALLITI FALLITI"; fi
exit $((FALLITI != 0))
