#!/bin/bash
# =============================================================================
# tools/prova_exide_autoaggiorna.sh — la voce «Autoaggiorna Proprieta» di exide
# (@EXIDE-PROPRIETA, 30 settembre 2026)
#
#   1. exide parte con la casa su un disco ext2; da tastiera (F10, due volte a
#      destra = Strumenti, otto in giu' = l'ultima voce, Invio) si accende;
#   2. il file $HOME/.app/exide/impostazioni.txt si legge dalla shell: deve
#      dire «autoaggiorna = 1»;
#   3. secondo avvio sullo stesso disco: la tendina riaperta deve mostrare la
#      spunta (3-spunta.ppm contro 1-menu.ppm: cambia solo il segno);
#   4. la larghezza del modulo si cambia nella casella e si esce con Tab:
#      l'elenco delle proprieta' deve mostrare il valore nuovo, senza Applica.
#
# ! DA TASTIERA E NON COL MOUSE: il mouse di QEMU e' relativo e arriva 10 px
# prima; le frecce nei menu sono esatte.
#
#     bash tools/prova_exide_autoaggiorna.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-autoagg}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)

export EXOS_ISTANZA=autoagg EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG" "$D"/*.ppm
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 400 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/casa@2" > "$D/0.log" 2>&1

avvia() {
    printf '%s\n' "mount hd0p1 /disk@6" "export HOME=/disk/casa@2" \
                  "exwin@20" "key:alt-f1@3" "/exwin/bin/exide &@12" "key:alt-f5@4"
}
strumenti() {
    echo "key:f10@1"; echo "key:right@1"; echo "key:right@1"
    for i in $(seq 1 8); do echo "key:down@0.3"; done
}

echo "=== 1-2. accesa da tastiera, e cosa resta sul disco ==="
{
    avvia
    strumenti
    echo "foto:$D/1-menu.ppm@1"
    echo "key:ret@2"
    echo "key:alt-f1@2"
    echo "cat /disk/casa/.app/exide/impostazioni.txt@3"
    echo "sync@2"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 500 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

ok=0; no=0
if tr -d '\r' < "$D/1.log" | grep -q "autoaggiorna = 1"; then
    echo "  [OK]  impostazioni.txt dice «autoaggiorna = 1»"; ok=$((ok+1))
else
    echo "  [NO]  impostazioni.txt non dice «autoaggiorna = 1» (vedi $D/1.log, $D/1-menu.ppm)"; no=$((no+1))
fi

echo "=== 3. secondo avvio: la spunta c'e' ancora ==="
{
    avvia
    strumenti
    echo "foto:$D/3-spunta.ppm@1"
    echo "key:esc@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 500 python3 tools/qemu_drive.py "${A[@]}" > "$D/3.log" 2>&1

# La spunta: le due fotografie hanno la stessa tendina sulla stessa voce, e
# il puntatore fermo nello stesso posto. Tolta la barra delle applicazioni
# (l'orologio cambia), la sola differenza deve essere il segno: qualche
# decina di pixel. ! La prima stesura cercava «la riga blu della voce» e
# trovava la barra del titolo.
python3 - "$D/1-menu.ppm" "$D/3-spunta.ppm" <<'PY' && { echo "  [OK]  al secondo avvio la voce ha la spunta, prima no"; ok=$((ok+1)); } || { echo "  [NO]  la spunta non si vede al secondo avvio (foto: $D/3-spunta.ppm)"; no=$((no+1)); }
import sys
def leggi(f):
    d = open(f, "rb").read(); p = d.split(b"\n", 3)
    w, h = map(int, p[1].split()); return w, h, p[3]
w, h, a = leggi(sys.argv[1]); _, _, b = leggi(sys.argv[2])
diversi = [(i // 3) for i in range(0, (h - 40) * w * 3, 3) if a[i:i+3] != b[i:i+3]]
n = len(diversi)
print("        pixel diversi sopra la barra delle applicazioni: %d" % n)
if n:
    xs = [q % w for q in diversi]; ys = [q // w for q in diversi]
    print("        nel riquadro (%d,%d)-(%d,%d)" % (min(xs), min(ys), max(xs), max(ys)))
sys.exit(0 if 5 <= n <= 200 and max(xs) - min(xs) < 16 else 1)
PY

echo "=== 4. accesa, uscire dalla casella applica la proprieta' ==="
# La riga «larghezza» dell'elenco delle proprieta' (632, 112) e poi la
# casella del valore (682, 422); 500 al posto di 400, e Tab per uscire.
# Si confrontano l'elenco dopo il clic nella casella (4b: il puntatore non e'
# piu' sull'elenco) e dopo Tab (4d): cambia solo se la proprieta' si e'
# applicata, cioe' «400» e' diventato «500».
{
    avvia
    for i in $(seq 1 8); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 65); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 11); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move 1 0@1"
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@2"
    for i in $(seq 1 5); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 31); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move 1 0@1"
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@2"
    echo "foto:$D/4b-casella.ppm@1"
    echo "key:end@1"; for i in $(seq 1 6); do echo "key:backspace@0.3"; done
    echo "500@n2"
    echo "key:tab@2"
    echo "foto:$D/4d-uscito.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 500 python3 tools/qemu_drive.py "${A[@]}" > "$D/4.log" 2>&1
python3 - "$D/4b-casella.ppm" "$D/4d-uscito.ppm" <<'PY' && { echo "  [OK]  uscendo dalla casella l'elenco delle proprieta' e' cambiato, senza Applica"; ok=$((ok+1)); } || { echo "  [NO]  l'elenco delle proprieta' non e' cambiato (foto: $D/4b-casella.ppm, $D/4d-uscito.ppm)"; no=$((no+1)); }
import sys
def leggi(f):
    d = open(f, "rb").read(); p = d.split(b"\n", 3)
    w, h = map(int, p[1].split()); return w, h, p[3]
w, h, a = leggi(sys.argv[1]); _, _, b = leggi(sys.argv[2])
n = sum(1 for y in range(70, 400) for x in range(612, 780)
        if a[(y*w+x)*3:(y*w+x)*3+3] != b[(y*w+x)*3:(y*w+x)*3+3])
print("        pixel cambiati nell'elenco delle proprieta': %d" % n)
sys.exit(0 if n >= 10 else 1)
PY

echo "  prova_exide_autoaggiorna: $ok OK, $no NO (fotografie in $D)"
[ "$no" -eq 0 ]
