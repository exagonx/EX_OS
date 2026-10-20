#!/bin/bash
# =============================================================================
# tools/prova_giochi.sh — i quattro giochi di ExWin (@GIOCHI, 3 ottobre 2026)
#
#   1. EXKlondike, EXSpider, EXMajong ed EXGO si aprono, ciascuno con la sua
#      finestra e la barra dei menu disegnata;
#   2. EXKlondike: lo Spazio gira il mazzo (gli scarti si riempiono);
#   3. EXSpider: D distribuisce una fila dal mazzo (le colonne si allungano);
#   4. EXGO: un clic al centro mette una pietra nera e il computer risponde
#      con una bianca;
#   5. nessun [FAULT] sulla seriale.
#
# La logica (le regole del go, la tartaruga del mahjong) si prova sull'host
# meglio che qui: qui si guarda che i programmi girino dentro EX-OS.
#
#     tools/prova_giochi.sh [directory-di-lavoro]   (default /tmp/exos-giochi)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-giochi}"
mkdir -p "$D"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

export EXOS_ISTANZA=giochi
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
SER=/tmp/exos/serialgiochi.txt

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

# Il mouse di QEMU e' relativo: dall'angolo in alto a sinistra, a passi da
# 10 pixel ogni 50 ms. Si ferma 9 pixel prima del bersaglio (misurato in
# prova_archivi.sh), e qui se ne tiene conto.
passi_a() {
    local dx=$(( $1 + 9 )) dy=$(( $2 + 9 )) i d rx ry
    for i in 1 2 3 4 5; do echo "mon:mouse_move -600 -600@0"; done
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do echo "mon:mouse_move 10 10@0.05"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do echo "mon:mouse_move 10 0@0.05"; done
    for ((i = 10; i <= ry; i += 10)); do echo "mon:mouse_move 0 10@0.05"; done
    echo "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@1"
}
apri() { printf '%s\n' "key:alt-f1@2" "/exwin/bin/$1 &@12" "key:alt-f5@5"; }

{
    echo "exwin@16"
    apri exklondike; echo "foto:$D/kl-1.ppm@1"; echo "key:spc@2"; echo "foto:$D/kl-2.ppm@1"; echo "key:ctrl-q@3"
    apri exspider;   echo "foto:$D/sp-1.ppm@1"; echo "key:d@3";   echo "foto:$D/sp-2.ppm@1"; echo "key:ctrl-q@3"
    apri exmajong;   echo "foto:$D/mj-1.ppm@1"; echo "key:ctrl-q@3"
    apri exgo;       echo "foto:$D/go-1.ppm@1"
    passi_a 252 292
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@5"; echo "foto:$D/go-2.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1

# Quanti pixel di un colore (con tolleranza) ci sono in un rettangolo.
conta() {
    python3 - "$@" <<'FINEPY'
import sys
f, x0, y0, x1, y1, r, g, b, tol = sys.argv[1], *map(int, sys.argv[2:])
d = open(f, "rb").read().split(b"\n", 3)
w = int(d[1].split()[0]); px = d[3]
n = 0
for y in range(y0, y1):
    for x in range(x0, x1):
        i = (y * w + x) * 3
        if abs(px[i] - r) <= tol and abs(px[i + 1] - g) <= tol and abs(px[i + 2] - b) <= tol:
            n += 1
print(n)
FINEPY
}
# Due fotografie diverse in un rettangolo?
diverse() {
    python3 - "$@" <<'FINEPY'
import sys
a, b = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
x0, y0, x1, y1 = map(int, sys.argv[3:])
w = int(a.split(b"\n", 3)[1].split()[0])
pa, pb = a.split(b"\n", 3)[3], b.split(b"\n", 3)[3]
n = sum(1 for y in range(y0, y1) for x in range(x0, x1)
        if pa[(y * w + x) * 3:(y * w + x) * 3 + 3] != pb[(y * w + x) * 3:(y * w + x) * 3 + 3])
print(n)
FINEPY
}

echo "=== 1. le finestre si aprono ==="
for g in kl sp mj go; do
    f="$D/$g-1.ppm"
    [ -f "$f" ] || { no "$g: manca la fotografia"; continue; }
    # Il tavolo (verde per le carte, marrone per il mahjong, legno per il go)
    # al centro dello schermo: non e' il blu della scrivania.
    if [ "$(conta "$f" 380 280 420 320 32 64 96 6)" -lt 800 ]; then ok "$g: la finestra c'e'"
    else no "$g: al centro c'e' ancora la scrivania"; fi
done

echo "=== 2. EXKlondike: lo Spazio gira il mazzo ==="
n=$(diverse "$D/kl-1.ppm" "$D/kl-2.ppm" 90 40 180 140)
[ "${n:-0}" -gt 500 ] && ok "gli scarti si sono riempiti ($n pixel cambiati)" || no "gli scarti non cambiano"

echo "=== 3. EXSpider: D distribuisce una fila ==="
n=$(diverse "$D/sp-1.ppm" "$D/sp-2.ppm" 20 150 760 260)
[ "${n:-0}" -gt 2000 ] && ok "le colonne si sono allungate ($n pixel cambiati)" || no "le colonne non cambiano"

echo "=== 4. EXGO: una pietra nera, e il computer risponde ==="
neri=$(conta "$D/go-2.ppm" 2 42 502 542 30 30 30 12)
bianchi=$(conta "$D/go-2.ppm" 2 42 502 542 242 242 238 10)
[ "${neri:-0}" -gt 300 ] && ok "la pietra nera c'e' ($neri pixel)" || no "nessuna pietra nera ($neri pixel)"
[ "${bianchi:-0}" -gt 200 ] && ok "il bianco ha risposto ($bianchi pixel)" || no "il bianco non ha risposto ($bianchi pixel)"

echo "=== 5. la seriale ==="
grep -a "\[FAULT\]" "$SER" > /dev/null 2>&1 && no "un [FAULT] sulla seriale (vedi $SER)" || ok "nessun [FAULT]"

echo
[ $esito = 0 ] && echo "  prova_giochi: TUTTO BENE" || echo "  prova_giochi: qualcosa non va (fotografie in $D)"
exit $esito
