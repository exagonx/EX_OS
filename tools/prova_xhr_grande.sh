#!/bin/bash
# =============================================================================
# tools/prova_xhr_grande.sh — una risposta XHR da 120 KB arriva intera anche
# con ExJs (@NAVMETA, 29 settembre 2026)
#
# Con ExJs una risposta diventa una stringa dell'arena del motore, che era di
# 96 KB: oltre, lo script riceveva la stringa VUOTA. Adesso l'arena si sceglie
# sulla memoria libera (js_arena in exbrowser.c): con 128 MB e' di 384 KB.
# Una pagina locale legge con XMLHttpRequest un file di 120000 byte e ne
# controlla la lunghezza. ! ExJs non ha throw: l'esito lo dice la RIGA
# dell'errore di `(0)()`, come in prova_date.sh — riga 5 se la lunghezza e'
# sbagliata, riga 6 se e' giusta.
#
#     tools/prova_xhr_grande.sh [directory-di-lavoro]   (default /tmp/exos-xhrgrande)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-xhrgrande}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/serialxhrgrande.txt

python3 -c "open('$D/grande.txt','w').write('x' * 120000)"
cat > "$D/pagina.html" <<'PAGINA'
<html><body><p>xhr grande</p><script>
var x = new XMLHttpRequest();
x.open('GET', 'grande.txt', false);
x.send();
if (x.responseText.length != 120000) (0)();
(0)();
</script></body></html>
PAGINA
printf 'motore = exjs\n' > "$D/imp.txt"

export EXOS_ISTANZA=xhrgrande EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
    timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
for f in pagina.html grande.txt; do "$DEBUGFS" -w -R "write $D/$f $f" "$OFF" > /dev/null 2>&1; done
for d in casa casa/.app casa/.app/exbrowser; do "$DEBUGFS" -w -R "mkdir $d" "$OFF" > /dev/null 2>&1; done
"$DEBUGFS" -w -R "write $D/imp.txt casa/.app/exbrowser/impostazioni.txt" "$OFF" > /dev/null 2>&1

EXOS_RAM=128M EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
    timeout 400 python3 tools/qemu_drive.py "mount hd0p1 /disk@6" "export HOME=/disk/casa@1" \
    "exwin@14" "key:alt-f1@2" "/exwin/bin/exbrowser /disk/pagina.html &@20" > "$D/1.log" 2>&1

n=$(tr -d '\r' < "$SER" | grep -o "riga [0-9]*: non e' una funzione" | head -1 | grep -o "[0-9]*")
case "$n" in
    6) echo "  [OK]  ExJs ha ricevuto i 120000 byte interi"; exit 0 ;;
    5) echo "  [NO]  ExJs ha ricevuto una lunghezza sbagliata (la stringa vuota?)"; exit 1 ;;
    *) echo "  [NO]  la pagina non ha detto niente"; tr -d '\r' < "$SER" | grep -i "javascript" | tail -3; exit 1 ;;
esac
