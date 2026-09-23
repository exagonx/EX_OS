#!/bin/bash
# =============================================================================
# tools/prova_download.sh — la finestra Download del toolkit, vista lavorare
#
# Chiesto il 23 settembre 2026: un gestore dei download con stato, percentuale
# e tempi medi, che si apre da solo quando parte uno scaricamento, e messo nel
# toolkit (lib/exdlg/scarichi.c) perche' lo usino anche i programmi di exide.
# Il primo cliente e' EXBrowser.
#
# Il server sull'host manda uno ZIP da 3 MB a circa 300 KB/s, con la sua
# Content-Length: abbastanza lento da fotografarlo a meta'.
#
#   1. EXBrowser su quello ZIP, «Apri con Archivi»;
#   2. la finestra Download si apre DA SOLA, e a meta' mostra percentuale,
#      velocita' e tempo che manca (fotografia);
#   3. alla fine dice «fatto ... media», e Archivi si apre da solo sul file:
#      e' la funzione di ritorno ex_scarichi_alla_fine;
#   4. dalla shell: il file c'e' e ha la misura giusta.
#
# ! NIENTE pkill: il server Python lo ferma questo script, per numero.
# Vuole il sistema costruito:  make iso-exos
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-download}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
rm -f "$D"/d*.ppm "$D"/d*.png

python3 - "$D" <<'PY'
import sys, zipfile, os
D = sys.argv[1]
z = zipfile.ZipFile(D + "/grande.zip", "w", zipfile.ZIP_STORED)
z.writestr("dati.bin", os.urandom(3 * 1024 * 1024))
z.writestr("leggimi.txt", "tre megabyte arrivati piano\n")
z.close()
PY
MISURA=$(stat -c %s "$D/grande.zip")

python3 - "$D" > "$D/http.log" 2>&1 <<'PY' &
import http.server, sys, time
D = sys.argv[1]
dati = open(D + "/grande.zip", "rb").read()
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/zip")
        self.send_header("Content-Length", str(len(dati)))
        self.end_headers()
        for i in range(0, len(dati), 32768):          # ~300 KB/s
            self.wfile.write(dati[i:i + 32768]); time.sleep(0.1)
http.server.HTTPServer(("0.0.0.0", 8000), H).serve_forever()
PY
P_HTTP=$!
sleep 1

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

EXOS_ISTANZA=dl EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M \
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide -netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    timeout 900 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/casa@1" "export HOME=/disk/casa@1" \
    "exwin@20" "key:alt-f1@2" \
    "/exwin/bin/exbrowser http://10.0.2.2:8000/grande.zip &@20" "key:alt-f5@3" \
    "key:tab@1" "key:ret@6" \
    "foto:$D/d1_a_meta.ppm@25" \
    "foto:$D/d2_finito.ppm@2" \
    "key:alt-f1@2" "echo MISURA@1" \
    "ls -l /disk/casa/.app/exbrowser/scaricati@3" > "$D/exos.log" 2>&1

kill $P_HTTP 2>/dev/null
wait $P_HTTP 2>/dev/null
for f in "$D"/d*.ppm; do convert "$f" "${f%.ppm}.png" 2>/dev/null; done

L=$(sed 's/\x1b\[[0-9;]*m//g' "$D/exos.log" | tr -d '\000')
esito=0
if printf '%s\n' "$L" | sed -n '/^MISURA/,$p' | grep -aq "$MISURA.*grande.zip\|grande.zip.*$MISURA"; then
    echo "  si'  il file c'e', e misura $MISURA byte come quello del server"
else
    echo "  NO   il file non c'e' o non ha la misura giusta ($MISURA)"; esito=1
fi
echo "  le fotografie (a meta', e finito con Archivi aperto): $D/d*.png"
echo "ESITO: $([ $esito = 0 ] && echo BUONO || echo "qualcosa non va, vedi $D/exos.log")"
exit $esito
