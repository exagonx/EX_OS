#!/bin/bash
# =============================================================================
# tools/prova_cursore.sh — il clic mette il cursore dove si clicca, nelle
# caselle delle pagine di EXBrowser (@NAV-CURSORE)
#
# Una casella con «abcdefghij» e, accanto, un pulsante largo il cui gestore
# del clic fa `throw` col valore della casella. L'errore JavaScript EXBrowser
# lo scrive sulla seriale («exbrowser: javascript, riga 1: ... VALORE[...]»),
# ed e' li' che si legge il verdetto.
# ! COL PRIMO GIRO IL GESTORE ERA UN onclick= SU UNA PAGINA SENZA <script>, e
# sulla seriale non arrivava niente: il motore JavaScript si apriva solo per
# gli <script>. Era un difetto del navigatore, corretto in 0.013 e provato da
# tools/prova_onclick.sh. Qui resta addEventListener.
#
# La prova clicca verso il primo terzo del testo, batte «X» e clicca il
# pulsante. ! IL MOUSE DI QEMU E' RELATIVO E PERDE PASSI, quindi non si
# pretende una lettera precisa: la X deve stare DENTRO la parola — non in
# testa, non in fondo. Prima del 28 settembre 2026 finiva sempre in fondo
# («abcdefghijX»), qualunque punto si cliccasse.
#
#     tools/prova_cursore.sh [directory-di-lavoro]   (default /tmp/exos-cursore)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-cursore}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialcursore.txt

cat > "$D/cursore.html" <<'EOF'
<html><body>
<input id="q" value="abcdefghij" size="10"> <button id="b">Premi qui il pulsante di prova, largo apposta</button>
<script>
document.getElementById('b').addEventListener('click', function () {
    throw new Error('VALORE[' + document.getElementById('q').value + ']');
});
</script>
</body></html>
EOF

export EXOS_ISTANZA=cursore EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/cursore.html cursore.html" "$OFF" > /dev/null 2>&1

# La prima finestra il server la mette in alto a sinistra; la pagina comincia
# a circa (10, 80), e la casella (dieci caratteri) arriva a x = 100.
{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/exbrowser /disk/cursore.html &@15"; echo "key:alt-f5@4"
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 4); do echo "mon:mouse_move 10 0@0"; done      # x = 40: dopo la terza/quarta lettera
    for i in $(seq 1 9); do echo "mon:mouse_move 0 10@0"; done      # y = 90
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@1"
    echo "foto:$D/1-clic.ppm@1"
    echo "key:shift-x@1"
    echo "foto:$D/2-scritto.ppm@1"
    for i in $(seq 1 16); do echo "mon:mouse_move 10 0@0"; done     # x = 200: sul pulsante
    echo "mon:mouse_button 1@0"; echo "mon:mouse_button 0@2"
    echo "foto:$D/3-pulsante.ppm@1"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

esito=0
ok() { echo "  [OK]  $*"; }
no() { echo "  [NO]  $*"; esito=1; }
v=$(tr -d '\r' < "$SER" | grep -o 'VALORE\[[^]]*\]' | tail -1 | sed 's/VALORE\[\(.*\)\]/\1/')
echo "        il valore arrivato allo script: «$v»"
if [ -z "$v" ]; then
    no "lo script non ha scritto il valore (il clic sul pulsante non e' arrivato?)"
else
    p=$(python3 -c "import sys; print(sys.argv[1].find('X'))" "$v")
    rest=$(echo "$v" | tr -d X)
    [ "$rest" = "abcdefghij" ] && ok "il testo di prima e' intatto, piu' una X" || no "il testo e' cambiato: «$v»"
    if [ "$p" -ge 1 ] && [ "$p" -le 9 ]; then ok "la X sta dove si e' cliccato, dentro la parola (dopo $p lettere)"
    else no "la X sta in posizione $p: il cursore non e' andato dove si e' cliccato"; fi
fi

echo ""
echo "  Le fotografie sono in $D/*.ppm"
exit $esito
