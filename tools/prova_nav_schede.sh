#!/bin/bash
# =============================================================================
# tools/prova_nav_schede.sh — le schede di EXBrowser (@NAV-SCHEDE, 29/09/2026)
#
# a.html lancia «CARICATA-A» ogni volta che si carica, c.html «APERTA-C»:
# la seriale dice quali pagine si sono caricate e quante volte.
#
#   1. Ctrl+T apre una scheda nuova (la pagina iniziale): la barra compare;
#   2. Ctrl+Tab torna su a.html, che si RICARICA (le schede in sottofondo
#      tengono l'indirizzo, non la pagina): CARICATA-A due volte;
#   3. Tab + Invio sul collegamento target="_blank": c.html in una terza
#      scheda (APERTA-C);
#   4. Ctrl+W tre volte: le schede si chiudono, e l'ultima chiude la finestra
#      (la shell lo dice: «terminato: /exwin/bin/exbrowser ... (codice 0)»).
#
# ! GLI ERRORI DEI TIMER NON ARRIVANO SULLA SERIALE, quelli di uno script in
# linea si': a.html lancia subito, non da un setTimeout.
#
#     tools/prova_nav_schede.sh [directory-di-lavoro]   (default /tmp/exos-navschede)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-navschede}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialnavschede.txt

cat > "$D/a.html" <<'PAGINA'
<html><head><title>Pagina A</title></head><body>
<p><a href="c.html" target="_blank">apri c in una scheda</a></p>
<script>throw new Error("CARICATA-A");</script></body></html>
PAGINA
printf '<html><head><title>Pagina C</title></head><body><p>c</p><script>throw new Error("APERTA-C");</script></body></html>\n' > "$D/c.html"

export EXOS_ISTANZA=navschede EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
for f in a c; do "$DEBUGFS" -w -R "write $D/$f.html $f.html" "$OFF" > /dev/null 2>&1; done

timeout 500 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser /disk/a.html &@15" "key:alt-f5@3" "foto:$D/0-una.ppm@1" \
    "key:ctrl-t@10" "foto:$D/1-due.ppm@1" \
    "key:ctrl-tab@10" "foto:$D/2-tornata.ppm@1" \
    "key:tab@1" "key:ret@10" "foto:$D/3-tre.ppm@1" \
    "key:ctrl-w@6" "foto:$D/4-chiusa-c.ppm@1" "key:ctrl-w@6" "key:ctrl-w@6" \
    "foto:$D/5-niente.ppm@1" "key:alt-f1@2" "echo FINE@2" > "$D/1.log" 2>&1

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }
n=$(tr -d '\r' < "$SER" | grep -c "CARICATA-A")
[ "$n" -ge 2 ] && ok "tornando sulla scheda a.html si e' ricaricata ($n caricamenti)" \
    || no "a.html caricata $n volte (vedi $D/2-tornata.ppm)"
tr -d '\r' < "$SER" | grep -q "APERTA-C" && ok "target=_blank: c.html in una scheda nuova" \
    || no "c.html non si e' aperta (vedi $D/3-tre.ppm)"
if tr -d '\r' < "$D/1.log" | grep -q "terminato: /exwin/bin/exbrowser .*(codice 0)"; then
    ok "Ctrl+W sull'ultima scheda chiude la finestra"
else
    no "dopo tre Ctrl+W exbrowser non e' uscito (vedi $D/5-niente.ppm)"
fi
echo "  Le fotografie sono in $D"
exit $esito
