#!/bin/bash
# =============================================================================
# tools/prova_bordi.sh — border e padding del CSS in EXBrowser (@NAV-BORDER)
#
# Una pagina locale con quattro riquadri, ognuno col bordo di un colore che
# nella pagina non c'e' altrove, e il verdetto dai pixel della fotografia:
#
#   rosso    border: 4px solid, padding 10px, nell'attributo style
#   blu      solo border-bottom: 6px solid
#   verde    border: 3px e un colore, MA SENZA STILE: non deve vedersi
#   magenta  border: 5px dashed da una classe in <style> (si disegna pieno)
#
# Due riquadri misurano le unita' relative (@NAV-UNITA): un bordo di 0.5em
# dentro un testo da 2em deve essere spesso 15 pixel (15 x 2 x 0.5), uno di
# 1em su un testo da 10px deve esserne 10.
#
# In fondo una tabella con `border=1` e una cella gialla: non conta nel
# verdetto, e' li' perche' la fotografia mostri che le tabelle di prima non
# sono cambiate (gli sfondi ora azzerano il loro contorno).
#
# E una cosa che si misura: il testo del riquadro rosso non tocca il bordo —
# fra la riga rossa di sopra e il primo pixel scuro del testo c'e' il padding.
#
#     tools/prova_bordi.sh [directory-di-lavoro]   (default /tmp/exos-bordi)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-bordi}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

cat > "$D/bordi.html" <<'EOF'
<html><head><title>bordi</title>
<style> .v { border: 5px dashed #e020e0; margin-top: 12px } </style>
</head><body>
<div style="border: 4px solid #e02020; padding: 10px">riquadro rosso con padding</div>
<p style="border-bottom: 6px solid #2040e0">solo il bordo di sotto, blu</p>
<div style="border: 3px #20c040">senza stile: il verde non si vede</div>
<div class="v">magenta, tratteggiato ma disegnato pieno</div>
<div style="font-size: 2em; border-top: 0.5em solid #f08000">2em, bordo 0.5em</div>
<div style="font-size: 10px; border-top: 1em solid #00c0c0">10px, bordo 1em</div>
<table border="1"><tr><td bgcolor="#ffff80">cella gialla</td><td>tabella col border= di sempre</td></tr></table>
<p>fine</p>
</body></html>
EOF

export EXOS_ISTANZA=bordi EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/bordi.html bordi.html" "$OFF" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser /disk/bordi.html &@15" "key:alt-f5@4" \
    "foto:$D/bordi.ppm@1" > "$D/1.log" 2>&1

esito=0
ok() { echo "  [OK]  $*"; }
no() { echo "  [NO]  $*"; esito=1; }
[ -s "$D/bordi.ppm" ] || { no "manca la fotografia"; exit 1; }

python3 - "$D/bordi.ppm" > "$D/misure.txt" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read()
p = d.split(b"\n", 3)
w, h = map(int, p[1].split()); px = p[3]
def col(x, y): i = (y * w + x) * 3; return px[i], px[i+1], px[i+2]
def vicino(c, r, g, b): return abs(c[0]-r) < 20 and abs(c[1]-g) < 20 and abs(c[2]-b) < 20
cont = {"rosso": (0xE0,0x20,0x20), "blu": (0x20,0x40,0xE0), "verde": (0x20,0xC0,0x40), "magenta": (0xE0,0x20,0xE0)}
n = {k: 0 for k in cont}
for y in range(h):
    for x in range(w):
        c = col(x, y)
        for k, v in cont.items():
            if vicino(c, *v): n[k] += 1
for k in cont: print(k, n[k])
# Il padding: la prima riga rossa piena (il lato di sopra), poi quante righe
# si scendono, dentro il riquadro, prima del primo pixel scuro del testo.
top = None
for y in range(h):
    run = sum(1 for x in range(w) if vicino(col(x, y), 0xE0, 0x20, 0x20))
    if run > 200: top = y; break
spazio = -1
if top is not None:
    xs = [x for x in range(w) if vicino(col(x, top), 0xE0, 0x20, 0x20)]
    x0, x1 = min(xs) + 8, max(xs) - 8
    y = top
    while y < h and vicino(col(x0 - 4, y), 0xE0, 0x20, 0x20): y += 1   # lo spessore
    for yy in range(top + 1, min(h, top + 80)):
        if any(sum(col(x, yy)) < 200 for x in range(x0, x1)):
            spazio = yy - top; break
print("spazio", spazio)
# Lo spessore di un bordo di sopra: le righe di fila di quel colore, nella
# colonna in mezzo al suo tratto piu' lungo.
def spessore(r, g, b):
    best = None
    for y in range(h):
        xs = [x for x in range(w) if vicino(col(x, y), r, g, b)]
        if len(xs) > 200: best = (y, (min(xs) + max(xs)) // 2); break
    if not best: return 0
    y, x = best; k = 0
    while y + k < h and vicino(col(x, y + k), r, g, b): k += 1
    return k
print("arancio", spessore(0xF0, 0x80, 0x00))
print("azzurro", spessore(0x00, 0xC0, 0xC0))
EOF
cat "$D/misure.txt" | sed 's/^/        /'
val() { awk -v k="$1" '$1==k{print $2}' "$D/misure.txt"; }
[ "$(val rosso)" -gt 800 ]  && ok "il bordo rosso (attributo style) si vede" || no "rosso: $(val rosso) pixel"
[ "$(val blu)" -gt 600 ]    && ok "il solo bordo di sotto, blu, si vede" || no "blu: $(val blu) pixel"
[ "$(val verde)" -eq 0 ]    && ok "senza stile il bordo verde non c'e'" || no "verde: $(val verde) pixel, doveva essere zero"
[ "$(val magenta)" -gt 800 ] && ok "il bordo magenta da una classe si vede" || no "magenta: $(val magenta) pixel"
a=$(val arancio); z=$(val azzurro)
[ "$a" = 15 ] && ok "0.5em dentro un testo da 2em: 15 pixel (@NAV-UNITA)" || no "0.5em su 2em: $a pixel invece di 15"
[ "$z" = 10 ] && ok "1em su un testo da 10px: 10 pixel" || no "1em su 10px: $z pixel invece di 10"
s=$(val spazio)
[ "$s" -ge 12 ] && ok "il testo sta sotto il bordo piu' il padding ($s pixel dal bordo)" || no "il testo tocca il bordo: $s pixel"

echo ""
echo "  La fotografia e' $D/bordi.ppm"
exit $esito
