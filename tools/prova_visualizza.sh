#!/bin/bash
# =============================================================================
# tools/prova_visualizza.sh — the file manager's Opzioni > Visualizzazione and
# the sheet beside the pointer while rows are dragged (voce 114, filemgr
# 0.016, exwin.so 0.027)
#
#   1. filemgr.cfg with nascosti = 0, estensione = 0: the list shows "nota"
#      and not "nota.txt", and no ".nascosto";
#   2. the same with 1 and 1: "nota.txt" and ".nascosto" are there;
#   3. the menu opens the window: Opzioni, Visualizzazione..., the photo
#      has its two choices;
#   4. one row pressed and dragged out of the list (onto the tree): a white
#      sheet with a grey border beside the pointer;
#   5. two rows marked (space) and dragged: three sheets one over the other;
#      released on the tree, the sheet goes away.
#
# The disk is made here with the files already in it (mke2fs -d, as
# prova_salvaschermo): the CD is read only, /tmp included.
#
#     tools/prova_visualizza.sh [CARTELLA]     (EXOS_ARCH=x86_64 for 64 bit)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-visualizza}"
mkdir -p "$D"
rm -f "$D"/*.ppm

CD=dist/exos.iso
if [ "$EXOS_ARCH" = x86_64 ]; then CD=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64; fi
[ -f "$CD" ] || { echo "manca $CD" >&2; exit 1; }
export EXOS_ISTANZA=visualizza EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD"

# the disk: /prova with a hidden file, a text and two more; HOME in /casa
disco() {   # NASCOSTI ESTENSIONE
    local G="$D/disco" PART="$D/part.img"
    rm -rf "$G" "$D/hd.img" "$PART"
    mkdir -p "$G/casa/.exwin/config" "$G/prova"
    printf '# filemgr\nalbero = 200\nnascosti = %s\nestensione = %s\n' "$1" "$2" \
        > "$G/casa/.exwin/config/filemgr.cfg"
    echo segreto > "$G/prova/.nascosto"
    echo testo > "$G/prova/nota.txt"
    echo uno > "$G/prova/alfa.txt"
    echo due > "$G/prova/beta.txt"
    fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L vis -d "$G" "$PART" 8M || exit 1
    qemu-img create -f raw "$D/hd.img" 10M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$D/hd.img" > /dev/null 2>&1
    dd if="$PART" of="$D/hd.img" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"; sync "$D/hd.img"
}
export EXOS_QEMU_EXTRA="-drive file=$D/hd.img,format=raw,if=ide"
INIZIO=("mount hd0p1 /disk@5" "export HOME=/disk/casa@1" "exwin@14" "key:alt-f1@2"
        "/exwin/bin/filemgr /disk/prova &@8" "key:alt-f5@4")

c() { python3 tools/trova_testo.py "$1" "$2" > /dev/null 2>&1; }
esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

# --- 1 and 2: what the list shows ---
disco 0 0
timeout 300 python3 tools/qemu_drive.py "${INIZIO[@]}" "foto:$D/1-senza.ppm@1" > "$D/1.log" 2>&1
disco 1 1
timeout 300 python3 tools/qemu_drive.py "${INIZIO[@]}" "foto:$D/2-con.ppm@1" \
    "punta:Opzioni@1" "mon:mouse_button 1@1" "mon:mouse_button 0@2" \
    "punta:Visualizzazione@1" "mon:mouse_button 1@1" "mon:mouse_button 0@3" \
    "foto:$D/3-finestra.ppm@1" "key:esc@2" > "$D/2.log" 2>&1

echo "=== 1. nascosti e estensione spenti ==="
if c "$D/1-senza.ppm" "nota" && ! c "$D/1-senza.ppm" "nota.txt"; then ok "si vede \"nota\", non \"nota.txt\""; else no "l'estensione (vedi $D/1-senza.ppm)"; fi
if ! c "$D/1-senza.ppm" ".nascosto"; then ok "\".nascosto\" non c'e'"; else no "\".nascosto\" si vede"; fi
echo "=== 2. accesi ==="
if c "$D/2-con.ppm" "nota.txt"; then ok "si vede \"nota.txt\""; else no "manca \"nota.txt\" (vedi $D/2-con.ppm)"; fi
if c "$D/2-con.ppm" ".nascosto"; then ok "si vede \".nascosto\""; else no "manca \".nascosto\""; fi
echo "=== 3. Opzioni > Visualizzazione ==="
if c "$D/3-finestra.ppm" "Mostra i file e le cartelle nascosti" && c "$D/3-finestra.ppm" "Mostra l'estensione"; then
    ok "la finestra ha le due scelte"
else
    no "la finestra non si e' aperta (vedi $D/3-finestra.ppm)"
fi

# --- 4 and 5: the sheet ---
# the front sheet: 20x26, border 128,128,128, white inside; molti = the
# left borders of the two behind it, 4 and 8 px to the left
fogli() {
    python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
def p(x, y): i = (y * w + x) * 3; return (px[i], px[i+1], px[i+2])
G, B = (128, 128, 128), (255, 255, 255)
for y in range(0, h - 26):
    for x in range(8, w - 20):
        if p(x, y) != G or p(x + 19, y + 25) != G: continue
        if any(p(x + i, y) != G or p(x + i, y + 25) != G for i in range(20)): continue
        if any(p(x, y + j) != G or p(x + 19, y + j) != G for j in range(26)): continue
        if p(x + 2, y + 2) != B or p(x + 17, y + 23) != B: continue
        dietro = all(p(x - 4, y + 4 + j) == G for j in range(26)) and \
                 all(p(x - 8, y + 8 + j) == G for j in range(26))
        print("3" if dietro else "1"); sys.exit(0)
print("0")
EOF
}
disco 1 1
timeout 300 python3 tools/qemu_drive.py "${INIZIO[@]}" \
    "punta:alfa.txt@1" "mon:mouse_button 1@1" \
    "mon:mouse_move -40 0@0" "mon:mouse_move -60 0@0" "mon:mouse_move -60 0@0" "mon:mouse_move -60 0@1" \
    "foto:$D/4-uno.ppm@1" "mon:mouse_move 0 -30@0" "mon:mouse_button 0@2" "foto:$D/4-lasciato.ppm@1" \
    > "$D/4.log" 2>&1
timeout 300 python3 tools/qemu_drive.py "${INIZIO[@]}" \
    "key:tab@1" "key:down@1" "key:spc@1" "key:spc@1" \
    "punta:alfa.txt@1" "mon:mouse_button 1@1" \
    "mon:mouse_move -40 0@0" "mon:mouse_move -60 0@0" "mon:mouse_move -60 0@0" "mon:mouse_move -60 0@1" \
    "foto:$D/5-tre.ppm@1" "mon:mouse_button 0@2" "foto:$D/5-lasciato.ppm@1" \
    > "$D/5.log" 2>&1
echo "=== 4. una riga trascinata ==="
n=$(fogli "$D/4-uno.ppm"); [ "$n" = 1 ] && ok "un foglio accanto al puntatore" || no "fogli: $n (vedi $D/4-uno.ppm)"
n=$(fogli "$D/4-lasciato.ppm"); [ "$n" = 0 ] && ok "lasciato: il foglio se n'e' andato" || no "dopo il rilascio fogli: $n"
echo "=== 5. due righe segnate e trascinate ==="
n=$(fogli "$D/5-tre.ppm"); [ "$n" = 3 ] && ok "tre fogli sovrapposti" || no "fogli: $n (vedi $D/5-tre.ppm)"
n=$(fogli "$D/5-lasciato.ppm"); [ "$n" = 0 ] && ok "lasciato: i fogli se ne sono andati" || no "dopo il rilascio fogli: $n"

[ $esito = 0 ] && echo "ESITO: TUTTO A POSTO" || echo "ESITO: QUALCOSA NON VA (foto in $D)"
exit $esito
