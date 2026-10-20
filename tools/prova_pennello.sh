#!/bin/bash
# =============================================================================
# tools/prova_pennello.sh — il programma di disegno di ExWin (@PAINT)
#
# Il verdetto lo danno i FILE SALVATI, non lo schermo, e non dipende da dove
# cade il tratto: il mouse di QEMU e' relativo e perde passi (vedi
# tools/prova_archivi.sh), quindi la prova non pretende un punto preciso.
#
#   1. sul disco di prova si mette un JPG (scritto da ImageMagick);
#   2. `pennello /disk/foto.jpg` lo apre (eximg.so, dalla riga di comando);
#   3. col mouse si tira un tratto a mano libera, poi da tastiera si sceglie il
#      rettangolo pieno (Shift+R) e se ne tira uno; Ctrl+Z e Ctrl+Y;
#   4. Ctrl+S: il JPG non si scrive, Pennello lo dice e propone .png ->
#      /disk/uscita.png; poi File > Salva con nome -> /disk/uscita.bmp.
#
# Fuori, con debugfs e ImageMagick:
#   - i due file esistono e sono della misura del JPG;
#   - PNG e BMP hanno ESATTAMENTE gli stessi pixel: sono la stessa immagine
#     scritta da due codificatori diversi e letta da un decodificatore che
#     non e' nostro;
#   - rispetto al JPG decodificato da eximg (sull'host, con scrivi_prova)
#     qualcosa e' cambiato, ma non tutto: il disegno c'e', e il resto della
#     foto e' rimasto com'era — pixel per pixel, perche' il JPG si decodifica
#     una volta sola, dentro EX-OS, e si salva senza perdite.
#
#     tools/prova_pennello.sh [directory-di-lavoro]   (default /tmp/exos-pennello)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs, ImageMagick
# e tools/prova_scrivi.sh gia' passato (per scrivi_prova).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-pennello}"
mkdir -p "$D"
rm -f "$D"/*.ppm "$D"/uscita.*
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

# Il decodificatore JPG di eximg, sull'host: serve a sapere che pixel ha visto
# Pennello (un JPG non ha UNA decodifica giusta al pixel).
SP="$D/scrivi"
tools/prova_scrivi.sh "$SP" > "$D/scrivi.log" 2>&1 || { echo "tools/prova_scrivi.sh non passa: vedi $D/scrivi.log"; exit 1; }

magick -size 320x200 gradient:skyblue-seagreen -fill orange -draw 'circle 160,100 160,40' \
       -quality 90 "$D/foto.jpg"
"$SP/scrivi_prova" leggi "$D/foto.jpg" "$D/foto.rgba" > /dev/null || { echo "eximg non legge il JPG di prova"; exit 1; }

export EXOS_ISTANZA=pennello EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/foto.jpg foto.jpg" "$OFF" > /dev/null 2>&1

# Un tratto: dal punto dove si e', giu', a passi da dieci col bottone premuto.
tratto() {  # passi dx dy
    echo "mon:mouse_button 1@0"
    for ((i = 0; i < $1; i++)); do echo "mon:mouse_move $2 $3@0"; done
    echo "mon:mouse_button 0@1"
}
{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/pennello /disk/foto.jpg &@8"; echo "key:alt-f5@3"
    echo "foto:$D/1-aperta.ppm@1"
    # Il puntatore nell'angolo in alto a sinistra, poi dentro la FOTO, non
    # solo dentro la tela: la tela e' grande quanto la finestra, ma la foto
    # e' 320x200 nel suo angolo. La prima finestra il server la mette in alto
    # a sinistra, la tela comincia a (136, 46) e (190, 90) cade nella foto
    # anche se qualche passo del mouse si perde.
    # ! LA PRIMA VOLTA IL TRATTO PARTIVA DA (330, 260): dentro la tela, sotto
    #   la foto, e non disegnava niente.
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 19); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 9); do echo "mon:mouse_move 0 10@0"; done
    tratto 12 10 4
    echo "foto:$D/2-tratto.ppm@1"
    echo "key:shift-r@1"
    for i in $(seq 1 6); do echo "mon:mouse_move -10 0@0"; done
    tratto 5 10 8
    echo "foto:$D/3-rettangolo.ppm@1"
    echo "key:ctrl-z@2"
    echo "foto:$D/4-annullato.ppm@1"
    echo "key:ctrl-y@2"
    echo "key:ctrl-s@3"
    echo "foto:$D/5-avviso.ppm@1"
    echo "key:ret@3"
    echo "foto:$D/6-salva.ppm@1"
    for i in $(seq 1 40); do echo "key:backspace@0"; done
    echo "/disk/uscita.png@6"
    echo "foto:$D/7-salvata.ppm@1"
    # File > Salva con nome: F10 apre File, la quarta voce.
    echo "key:f10@1"; echo "key:down@1"; echo "key:down@1"; echo "key:down@1"
    echo "foto:$D/8-menu.ppm@1"
    echo "key:ret@3"
    for i in $(seq 1 40); do echo "key:backspace@0"; done
    echo "/disk/uscita.bmp@6"
    echo "foto:$D/9-bmp.ppm@1"
    echo "key:alt-f1@2"; echo "ls -l /disk@3"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

"$DEBUGFS" -R "dump /uscita.png $D/uscita.png" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -R "dump /uscita.bmp $D/uscita.bmp" "$OFF" > /dev/null 2>&1

esito=0
ok()  { echo "  [OK]  $*"; }
no()  { echo "  [NO]  $*"; esito=1; }

for f in png bmp; do
    if [ -s "$D/uscita.$f" ]; then ok "uscita.$f salvato ($(stat -c%s "$D/uscita.$f") byte)"
    else no "uscita.$f non c'e'"; fi
done
if [ -s "$D/uscita.png" ] && [ -s "$D/uscita.bmp" ]; then
    m1=$(magick identify -format '%wx%h' "$D/uscita.png" 2>/dev/null)
    m2=$(magick identify -format '%wx%h' "$D/uscita.bmp" 2>/dev/null)
    [ "$m1" = "320x200" ] && ok "il PNG e' 320x200 come il JPG" || no "il PNG e' $m1, non 320x200"
    [ "$m2" = "320x200" ] && ok "il BMP e' 320x200 come il JPG" || no "il BMP e' $m2, non 320x200"
    magick "$D/uscita.png" -depth 8 -alpha off rgb:"$D/png.rgb"
    magick "$D/uscita.bmp" -depth 8 -alpha off rgb:"$D/bmp.rgb"
    magick -size 320x200 -depth 8 rgba:"$D/foto.rgba" -alpha off rgb:"$D/foto.rgb"
    cmp -s "$D/png.rgb" "$D/bmp.rgb" && ok "PNG e BMP hanno gli stessi pixel" || no "PNG e BMP sono diversi"
    diversi=$(cmp -l "$D/foto.rgb" "$D/png.rgb" 2>/dev/null | awk '{print int(($1-1)/3)}' | sort -u | wc -l)
    echo "        pixel cambiati rispetto al JPG: $diversi su 64000"
    if [ "$diversi" -gt 20 ] && [ "$diversi" -lt 32000 ]; then ok "il disegno c'e', e il resto della foto e' intatto"
    else no "pixel cambiati fuori misura ($diversi): o non si e' disegnato, o la foto si e' rovinata"; fi
fi
grep -q "uscita.png" "$D/1.log" && ok "ls -l dentro EX-OS vede i file" || echo "  [--]  ls -l non si legge nel log"

echo ""
echo "  Le fotografie sono in $D/*.ppm"
exit $esito
