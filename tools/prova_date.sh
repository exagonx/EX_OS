#!/bin/bash
# =============================================================================
# tools/prova_date.sh — Date nel navigatore, coi due motori (@EXJS-LACUNE)
#
# La stessa pagina due volte: con QuickJS (il motore predefinito) e con ExJs
# (una casa su ext2 con "motore = exjs" nelle impostazioni). Lo script della
# pagina controlla da se' le cose che contano — l'anno vero dall'orologio del
# sistema, `b - a` fra due Date che non va all'indietro, toISOString, JSON,
# valueOf — e finisce LANCIANDO un errore col suo esito: e' l'unica cosa di
# una pagina che arriva sulla seriale (console.log va nella barra di stato).
#
# ! L'ANNO VERO E' LA PROVA DELL'OROLOGIO: senza exjs_orologio_metti ExJs
# direbbe 1970, e le prove sull'host non se ne accorgono perche' la'
# l'orologio e' finto per forza.
#
# Il comportamento fine (campi, traboccamenti, formati, lettura delle date)
# si prova sull'host: make prova-exjs.
#
#     tools/prova_date.sh [directory-di-lavoro]   (default /tmp/exos-date)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-date}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialdate.txt

# ! EXJS NON HA ANCORA throw, e il suo errore non dice il nome di niente:
# dice la RIGA. La pagina per ExJs chiama percio' `(0)()` — «non e' una
# funzione» — sulla riga del controllo che fallisce, e alla riga 13 se sono
# passati tutti. QuickJS invece dice riga 0 ma porta il messaggio: per lui si
# lancia un Error con l'esito scritto dentro.
pagina() {   # nome-file etichetta
    local fine="throw new Error(no.length ? 'DATE-$2-NO ' + no.join(', ') : 'DATE-$2-OK ' + y);"
    local q="no.push"
    if [ "$2" = EXJS ]; then fine="(0)();"; q="(0)();//"; fi
    cat > "$D/$1" <<PAGINA
<html><body><p>Date, motore $2</p><script>
var no = [], a = new Date(), s = 0, i, b, y = a.getFullYear();
for (i = 0; i < 30000; i++) s += i;
b = new Date();
if (!(y >= 2026 && y < 2100)) $q('anno ' + y);
if (!(b - a >= 0)) $q('b-a ' + (b - a));
if (!(Date.now() > 1.7e12)) $q('now ' + Date.now());
if (new Date(0).toISOString() != '1970-01-01T00:00:00.000Z') $q('iso');
if (JSON.stringify({d: new Date(0)}) != '{"d":"1970-01-01T00:00:00.000Z"}') $q('json');
if (({valueOf: function () { return 5; }}) * 2 != 10) $q('valueOf');
if (Date.UTC(2000, 0, 1) != 946684800000) $q('UTC');
if (new Date(2024, 1, 29).getDate() != 29) $q('29 febbraio');
$fine
</script></body></html>
PAGINA
}
pagina q.html QJS
pagina e.html EXJS
printf 'motore = exjs\n' > "$D/imp.txt"

export EXOS_ISTANZA=date EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
for f in q.html e.html; do "$DEBUGFS" -w -R "write $D/$f $f" "$OFF" > /dev/null 2>&1; done
for d in casa casa/.app casa/.app/exbrowser; do "$DEBUGFS" -w -R "mkdir $d" "$OFF" > /dev/null 2>&1; done
"$DEBUGFS" -w -R "write $D/imp.txt casa/.app/exbrowser/impostazioni.txt" "$OFF" > /dev/null 2>&1

timeout 500 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser /disk/q.html &@15" "key:alt-f5@3" "foto:$D/q.ppm@0" \
    "key:alt-f1@2" "export HOME=/disk/casa@1" \
    "/exwin/bin/exbrowser /disk/e.html &@15" "key:alt-f5@3" "foto:$D/e.ppm@0" \
    > "$D/1.log" 2>&1

esito=0
r=$(tr -d '\r' < "$SER" | grep -o "DATE-QJS-[A-Z]* [^\"']*" | head -1)
case "$r" in
    DATE-QJS-OK*) echo "  [OK]  QuickJS: $r" ;;
    "")           echo "  [NO]  QuickJS: la pagina non ha detto niente (foto: $D/q.ppm)"; esito=1 ;;
    *)            echo "  [NO]  QuickJS: $r"; esito=1 ;;
esac
righe=(- - - - anno b-a Date.now toISOString JSON valueOf Date.UTC "29 febbraio")
n=$(tr -d '\r' < "$SER" | grep -o "riga [0-9]*: non e' una funzione" | head -1 | grep -o "[0-9]*")
if [ "$n" = 13 ]; then echo "  [OK]  ExJs: tutti i controlli passati (anno, b-a, Date.now, ISO, JSON, valueOf, UTC)"
elif [ -z "$n" ]; then echo "  [NO]  ExJs: la pagina non ha detto niente (foto: $D/e.ppm)"; esito=1
else echo "  [NO]  ExJs: fallisce il controllo della riga $n (${righe[$n]:-?})"; esito=1; fi
exit $esito
