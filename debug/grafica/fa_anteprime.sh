#!/bin/sh
# debug/grafica/fa_anteprime.sh — rifa' i PNG dei temi e delle animazioni
# DAL CODICE C: compila extema.c, extanim.c e i tema_NN_*.c sull'host insieme
# ad anteprima.c / anim_anteprima.c, poi converte con PIL (python3-pil).
#   sh fa_anteprime.sh            dalla cartella debug/grafica
set -e
QUI=$(cd "$(dirname "$0")" && pwd)
RADICE=$(cd "$QUI/../.." && pwd)
TMP=${TMPDIR:-/tmp}/exos-anteprime.$$
mkdir -p "$TMP/inc"
trap 'rm -rf "$TMP"' EXIT
printf '#include <stdint.h>\n' > "$TMP/inc/kernel.h"     # per font8x16.c
FONT="$RADICE/kernel/arch/x86/font8x16.c"
cd "$QUI"
gcc -O2 -Wall -Wextra -I. -Icolori -I"$TMP/inc" -o "$TMP/anteprima" anteprima.c extema.c tema_*.c colori/excolori.c "$FONT"
for i in 0 1 2 3 4 5 6 7 8 9; do
    f=$(ls tema_$(printf %02d $((i + 1)))_*.c)
    "$TMP/anteprima" "$i" "$TMP/t.ppm" > /dev/null
    python3 -I -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], optimize=True)' "$TMP/t.ppm" "${f%.c}.png"
    if "$TMP/anteprima" "$i" "$TMP/s.ppm" scrivania > /dev/null 2>&1 && [ -s "$TMP/s.ppm" ]; then
        python3 -I -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], optimize=True)' "$TMP/s.ppm" "${f%.c}_scrivania.png"
    fi
    rm -f "$TMP/s.ppm"
    echo "${f%.c}.png"
done
if [ -f anim_anteprima.c ]; then
    gcc -O2 -Wall -Wextra -I. -I"$TMP/inc" -Icolori -o "$TMP/anim" anim_anteprima.c extanim.c anim_[0-9]*.c extema.c tema_*.c "$FONT"
    mkdir -p "$TMP/fot"
    for a in $("$TMP/anim" elenco); do
        rm -f "$TMP"/fot/*.ppm
        "$TMP/anim" "$a" "$TMP/fot" > /dev/null
        python3 -I "$QUI/anim_png.py" "$TMP/fot" "$a"
    done
fi

# gli schemi di colore del tema 01 e il pannello Colori di pm
cd "$QUI"
gcc -O2 -Wall -Wextra -I. -Icolori -I"$TMP/inc" -o "$TMP/anteprima_c" anteprima.c extema.c tema_*.c colori/excolori.c "$FONT"
for s in colori/schemi/*.colori; do
    b=$(basename "$s" .colori)
    "$TMP/anteprima_c" "$s" "$TMP/c.ppm" > /dev/null
    python3 -I -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], optimize=True)' "$TMP/c.ppm" "colori/schema_$b.png"
    echo "colori/schema_$b.png"
done
gcc -O2 -Wall -Wextra -I. -Icolori -I"$TMP/inc" -o "$TMP/pannello" colori/anteprima_pannello.c colori/colori_anteprima.c colori/excolori.c extema.c tema_*.c "$FONT"
"$TMP/pannello" colori/schemi/01_classico.colori 8 "$TMP/p.ppm"
python3 -I -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], optimize=True)' "$TMP/p.ppm" colori/pm_pannello_colori.png
"$TMP/pannello" colori/schemi/03_oceano.colori 6 "$TMP/p.ppm"
python3 -I -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2], optimize=True)' "$TMP/p.ppm" colori/pm_pannello_colori_oceano.png
echo colori/pm_pannello_colori.png
