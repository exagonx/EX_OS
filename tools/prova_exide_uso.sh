#!/bin/bash
# =============================================================================
# tools/prova_exide_uso.sh — come si usa exide col mouse (4 ottobre 2026,
# chiesto dall'utente)
#
#   1. il pannello degli strumenti ha le miniature, e un clic su «Casella
#      testo» lo arma;
#   2. un clic sulla maschera posa il controllo, e il mouse TORNA PUNTATORE: un
#      secondo clic sul vuoto non ne mette un altro;
#   3. un doppio clic su un pulsante apre il sorgente col cursore DENTRO la sua
#      funzione: una lettera battuta subito finisce fra le graffe;
#   4. il sorgente NON E' MODALE: con lui aperto un clic sulla maschera sceglie
#      un altro controllo, e Ctrl+S dal disegnatore salva anche l'editor.
#
# Parte dal disco di prova_exide_genera.sh (un progetto con tutti i controlli):
# se non c'e', lo fa fare a lei.
#
#     tools/prova_exide_uso.sh [directory-di-lavoro]   (default /tmp/exos-ide-uso)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ide-uso}"
BASE=/tmp/exos-ide-genera/prova-hd.img
mkdir -p "$D"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
[ -f "$BASE" ] || bash tools/prova_exide_genera.sh > "$D/0-genera.log" 2>&1
[ -f "$BASE" ] || { echo "NON RIUSCITO: prova_exide_genera.sh non ha lasciato il disco" >&2; exit 1; }
cp "$BASE" "$D/prova-hd.img"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=ideuso
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

# Il mouse di QEMU e' relativo e si ferma 9 pixel prima: vedi prova_giochi.sh.
vai() {
    local dx=$(( $1 + 9 )) dy=$(( $2 + 9 )) i d rx ry
    for i in 1 2 3 4 5; do echo "mon:mouse_move -600 -600@0"; done
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do echo "mon:mouse_move 10 10@0.04"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do echo "mon:mouse_move 10 0@0.04"; done
    for ((i = 10; i <= ry; i += 10)); do echo "mon:mouse_move 0 10@0.04"; done
    echo "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@1"
}
CLIC="mon:mouse_button 1;mouse_button 0@2"

{
    printf '%s\n' "mount hd0p1 /disk@6" "export HOME=/disk/casa@2" "exwin@20" "key:alt-f1@3" \
                  "/exwin/bin/exide /disk/nuovo &@14" "key:alt-f5@4" "foto:$D/1-aperto.ppm@1"
    vai 60 145;  echo "$CLIC"; echo "foto:$D/2-armato.ppm@1"     # «Casella testo»
    vai 560 420; echo "$CLIC"                                    # sul vuoto: si posa
    vai 590 300; echo "$CLIC"; echo "foto:$D/3-posato.ppm@1"     # di nuovo sul vuoto
    vai 230 121                                                  # Button1
    echo "mon:mouse_button 1;mouse_button 0;mouse_button 1;mouse_button 0@6"
    echo "X@n1"; echo "foto:$D/4-editor.ppm@1"
    vai 320 118; echo "$CLIC"; echo "foto:$D/5-sotto.ppm@1"      # Label1, sotto l'editor
    echo "key:ctrl-s@6"; echo "foto:$D/6-salvato.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 1500 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1

"$DEBUGFS" -R "cat nuovo/src/finestra.dis" "$OFF" > "$D/finestra.dis" 2>/dev/null
"$DEBUGFS" -R "cat nuovo/src/finestra.c"   "$OFF" > "$D/finestra.c"   2>/dev/null

colore() {      # quanti pixel di quel colore nel rettangolo
    python3 - "$@" <<'FINEPY'
import sys
f, x0, y0, x1, y1, r, g, b = sys.argv[1], *map(int, sys.argv[2:])
d = open(f, "rb").read().split(b"\n", 3)
w = int(d[1].split()[0]); px = d[3]
print(sum(1 for y in range(y0, y1) for x in range(x0, x1)
          if px[(y * w + x) * 3:(y * w + x) * 3 + 3] == bytes((r, g, b))))
FINEPY
}

echo "=== 1. il pannello degli strumenti ==="
# Le miniature stanno su quadratini grigi dentro un pannello bianco; la riga
# armata e' blu (48, 90, 138).
g=$(colore "$D/1-aperto.ppm" 12 72 50 410 192 192 192)
[ "${g:-0}" -gt 3000 ] && ok "le miniature ci sono ($g pixel)" || no "niente miniature nel pannello ($g pixel)"
b=$(colore "$D/2-armato.ppm" 50 137 160 155 48 90 138)
[ "${b:-0}" -gt 500 ] && ok "«Casella testo» e' armato" || no "il clic sul pannello non ha armato lo strumento ($b)"

echo "=== 2. posato uno, il mouse torna puntatore ==="
n=$(grep -c "^c textbox TextBox2 " "$D/finestra.dis")
t=$(grep -c "^c textbox " "$D/finestra.dis")
[ "$n" = 1 ] && ok "TextBox2 e' sulla maschera" || no "TextBox2 non c'e' in finestra.dis"
[ "$t" = 3 ] && ok "il secondo clic non ne ha messo un altro (3 caselle: 2 di prima e TextBox2)" \
             || no "le caselle di testo sono $t, dovevano essere 3"
b=$(colore "$D/3-posato.ppm" 50 74 160 92 48 90 138)
[ "${b:-0}" -gt 500 ] && ok "nel pannello e' scelto il Puntatore" || no "il Puntatore non e' scelto ($b)"

echo "=== 3. il doppio clic porta DENTRO la funzione ==="
if sed -n '/^void Button1_Click(void)/,/^}/p' "$D/finestra.c" | grep -q "^    X"; then
    ok "la lettera battuta e' fra le graffe di Button1_Click"
else
    no "la lettera non e' dentro Button1_Click:"; sed -n '/Button1: Click/,/^}/p' "$D/finestra.c" | sed 's/^/        /'
fi

echo "=== 4. il sorgente non e' modale ==="
# Con l'editor aperto, il clic su Label1 l'ha scelta: nella casella del valore
# (sotto l'elenco delle proprieta') c'e' il suo nome, non piu' Button1. Lo
# dice finestra.c: Ctrl+S dal DISEGNATORE ha salvato anche l'editor.
grep -q "^    X" "$D/finestra.c" && ok "Ctrl+S dal disegnatore ha salvato anche l'editor" \
                              || no "il testo dell'editor non e' stato salvato col progetto"
d=$(python3 - "$D/4-editor.ppm" "$D/5-sotto.ppm" <<'FINEPY'
import sys
a, b = (open(p, "rb").read().split(b"\n", 3) for p in sys.argv[1:3])
w = int(a[1].split()[0])
print(sum(1 for y in range(74, 150) for x in range(612, 778)
          if a[3][(y * w + x) * 3:(y * w + x) * 3 + 3] != b[3][(y * w + x) * 3:(y * w + x) * 3 + 3]))
FINEPY
)
[ "${d:-0}" -gt 100 ] && ok "le proprieta' sono cambiate col clic sotto l'editor ($d pixel)" \
                      || no "il disegnatore non risponde con l'editor aperto ($d pixel)"

grep -a "\[FAULT\]" /tmp/exos/serialideuso.txt > /dev/null 2>&1 && no "un [FAULT] sulla seriale" || ok "nessun [FAULT]"

echo
[ $esito = 0 ] && echo "  prova_exide_uso: TUTTO BENE" \
               || echo "  prova_exide_uso: qualcosa non va (fotografie in $D)"
exit $esito
