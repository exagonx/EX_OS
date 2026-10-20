#!/bin/bash
# =============================================================================
# tools/prova_rotella.sh — la rotella del mouse scorre la pagina (@EXBROWSER-HTML5)
#
# La catena intera: kbd.drv chiede al mouse PS/2 il modo IntelliMouse
# (quattro byte, il quarto e' la rotella), wserver manda WIN_EV_ROTELLA alla
# finestra sotto il puntatore, il toolkit ne fa EXM_ROTELLA, EXBrowser scorre.
#
# Una pagina lunga, il puntatore portato sopra la pagina, poi cinque scatti in
# giu' e cinque in su dal monitor di QEMU. Tre fotografie:
#   1 prima, 2 dopo i cinque in giu', 3 dopo i cinque in su.
# Passa se la 2 e' DIVERSA dalla 1 sopra la pagina (ha scorso) e la 3 e'
# UGUALE alla 1 (e' tornata esattamente dov'era). Le righe dell'orologio in
# basso (582-591) non contano.
#
# ! NEL MONITOR DI QEMU `mouse_move 0 0 1` E' LA ROTELLA IN SU (WHEEL_UP, che
# il PS/2 emulato manda come dz NEGATIVO): per scendere si scrive -1. Il
# primo giro della prova l'aveva al contrario, e la pagina — gia' in cima —
# restava ferma ai cinque «in giu'» e scendeva ai cinque «in su». Il verso
# del driver e' quello di Linux: positivo verso chi usa il mouse.
#
# Si guarda anche la seriale: kbd.drv dice «con la rotella» se il mouse ha
# accettato il modo IntelliMouse.
#
#     tools/prova_rotella.sh [directory-di-lavoro]   (default /tmp/exos-rotella)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-rotella}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialrotella.txt

{
    echo "<html><body><h1>La rotella</h1>"
    for i in $(seq 1 150); do echo "<p>Riga numero $i della pagina lunga, da scorrere con la rotella.</p>"; done
    echo "</body></html>"
} > "$D/lunga.html"

export EXOS_ISTANZA=rotella EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/lunga.html lunga.html" "$OFF" > /dev/null 2>&1

{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser /disk/lunga.html &@15"; echo "key:alt-f5@4"
    # Il puntatore in alto a sinistra, poi a passi di dieci verso il centro
    # della pagina: il mouse di QEMU e' relativo e perde i passi lunghi.
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 40); do echo "mon:mouse_move 10 8@0"; done
    echo "foto:$D/1.ppm@2"
    for i in $(seq 1 5); do echo "mon:mouse_move 0 0 -1@1"; done
    echo "foto:$D/2.ppm@2"
    for i in $(seq 1 5); do echo "mon:mouse_move 0 0 1@1"; done
    echo "foto:$D/3.ppm@2"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

esito=0
if tr -d '\r' < "$SER" | grep -q "mouse PS/2: .*con la rotella"; then
    echo "  [OK]  kbd.drv: il mouse ha accettato il modo IntelliMouse"
else
    echo "  [NO]  kbd.drv non dice «con la rotella»: $(tr -d '\r' < "$SER" | grep -o 'mouse PS/2: .*' | head -1)"; esito=1
fi

diverse() {   # a.ppm b.ppm -> righe di pixel diverse fra 60 e 575
    python3 - "$1" "$2" <<'EOF'
import sys
def leggi(p):
    d = open(p, "rb").read(); c = d.split(b"\n", 3)
    w, h = map(int, c[1].split()); return w, h, c[3]
w, h, a = leggi(sys.argv[1]); _, _, b = leggi(sys.argv[2])
r = w * 3
print(sum(1 for y in range(60, min(h, 576)) if a[y*r:(y+1)*r] != b[y*r:(y+1)*r]))
EOF
}
for k in 1 2 3; do [ -s "$D/$k.ppm" ] || { echo "  [NO]  manca la fotografia $k"; exit 1; }; done
d12=$(diverse "$D/1.ppm" "$D/2.ppm"); d13=$(diverse "$D/1.ppm" "$D/3.ppm")
if [ "$d12" -gt 50 ]; then echo "  [OK]  cinque scatti in giu': la pagina ha scorso ($d12 righe di pixel cambiate)"
else echo "  [NO]  cinque scatti in giu': la pagina e' ferma ($d12 righe cambiate)"; esito=1; fi
if [ "$d13" -eq 0 ]; then echo "  [OK]  cinque scatti in su: la pagina e' tornata esattamente dov'era"
else echo "  [NO]  cinque scatti in su: $d13 righe diverse dalla prima fotografia"; esito=1; fi
echo "        fotografie in $D"
exit $esito
