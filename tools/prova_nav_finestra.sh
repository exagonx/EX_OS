#!/bin/bash
# =============================================================================
# tools/prova_nav_finestra.sh — EXBrowser apre le pagine in una finestra nuova
# (@NAV-FINESTRA, 29 settembre 2026)
#
# a.html ha un collegamento target="_blank" a b.html, un pulsante che chiama
# open('d.html'), e uno script che chiama open('c.html') MENTRE SI CARICA.
# Ogni pagina di arrivo lancia un errore col suo nome, che finisce sulla
# seriale: e' cosi' che si sa quale finestra si e' aperta.
#
#   1. Tab + Invio sul collegamento: si apre b.html (APERTA-B: dal 29
#      settembre in una SCHEDA nuova, come Firefox; la finestra nuova la
#      danno Shift+clic e il tasto destro), e c.html NO —
#      una finestra che si apre da sola, senza un clic, e' bloccata;
#   2. Tab Tab + Invio sul pulsante, con QuickJS: si apre d.html;
#   3. lo stesso con ExJs (motore = exjs nel profilo).
#
#     tools/prova_nav_finestra.sh [directory-di-lavoro]   (default /tmp/exos-navfin)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-navfin}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialnavfin.txt

cat > "$D/a.html" <<'PAGINA'
<html><body><p><a href="b.html" target="_blank">una finestra nuova</a></p>
<p><button onclick="open('d.html')">apri d</button></p>
<script>open('c.html');</script></body></html>
PAGINA
for x in b c d; do
    X=$(echo $x | tr a-z A-Z)
    printf '<html><body><p>pagina %s</p><script>throw new Error("APERTA-%s");</script></body></html>\n' "$x" "$X" > "$D/$x.html"
done
printf 'motore = exjs\n' > "$D/imp.txt"

export EXOS_ISTANZA=navfin EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
# ! 64 MB, COME LE ALTRE PROVE DEL NAVIGATORE. Il navigatore che a 32 MB ogni
# tanto non caricava una libreria era una corsa nel kernel (due processi che
# caricavano la stessa libreria insieme), corretta nel kernel 0.224: da li'
# questa prova passa anche a 32 MB (29 settembre 2026). Si resta a 64 per
# misurare quel che la prova cerca e non la memoria.
export EXOS_RAM=64M
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
for f in a b c d; do "$DEBUGFS" -w -R "write $D/$f.html $f.html" "$OFF" > /dev/null 2>&1; done
for d in casa casa/.app casa/.app/exbrowser; do "$DEBUGFS" -w -R "mkdir $d" "$OFF" > /dev/null 2>&1; done
"$DEBUGFS" -w -R "write $D/imp.txt casa/.app/exbrowser/impostazioni.txt" "$OFF" > /dev/null 2>&1

giro() {    # nome, poi i tasti
    local n=$1; shift
    timeout 500 python3 tools/qemu_drive.py \
        "mount hd0p1 /disk@6" "$@" > "$D/$n.log" 2>&1
    tr -d '\r' < "$SER" > "$D/$n.ser"
}
giro 1 "exwin@14" "key:alt-f1@2" "/exwin/bin/exbrowser /disk/a.html &@15" "key:alt-f5@3" \
       "key:tab@1" "key:ret@15" "foto:$D/1.ppm@1"
giro 2 "exwin@14" "key:alt-f1@2" "/exwin/bin/exbrowser /disk/a.html &@15" "key:alt-f5@3" \
       "key:tab@1" "key:tab@1" "key:ret@15" "foto:$D/2.ppm@1"
giro 3 "export HOME=/disk/casa@1" "exwin@14" "key:alt-f1@2" \
       "/exwin/bin/exbrowser /disk/a.html &@15" "key:alt-f5@3" \
       "key:tab@1" "key:tab@1" "key:ret@15" "foto:$D/3.ppm@1"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }
grep -q "APERTA-B" "$D/1.ser" && ok "target=_blank: b.html aperta (in una scheda nuova)" \
    || no "target=_blank: b.html non si e' aperta (vedi $D/1.ppm)"
! grep -q "APERTA-C" "$D/1.ser" "$D/2.ser" "$D/3.ser" \
    && ok "open() mentre la pagina si carica: bloccata" \
    || no "open() senza un clic ha aperto c.html"
grep -q "APERTA-D" "$D/2.ser" && ok "open() da un pulsante, QuickJS: d.html" \
    || no "open() da un pulsante, QuickJS: niente (vedi $D/2.ppm)"
grep -q "APERTA-D" "$D/3.ser" && ok "open() da un pulsante, ExJs: d.html" \
    || no "open() da un pulsante, ExJs: niente (vedi $D/3.ppm)"
exit $esito
