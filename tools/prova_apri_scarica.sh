#!/bin/bash
# =============================================================================
# tools/prova_apri_scarica.sh — EXBrowser davanti a un file che non e' una
# pagina: apri o scarica
#
# Chiesto il 23 settembre 2026: «quando il file ha estensione .zip o
# estensioni non web deve poter proporre se aprire o scaricare». Due casi,
# perche' sono le due strade del codice:
#
#   1. http://.../prova.zip — lo dice il NOME: la domanda arriva prima di
#      scaricare. Si sceglie «Apri con Archivi»: il file finisce in
#      $HOME/.app/exbrowser/scaricati/ e Archivi lo apre (fotografia);
#   2. http://.../dammi — il nome non dice niente, lo dice il server
#      (Content-Type: application/zip). Si sceglie «Scarica», si accetta il
#      posto proposto, e poi `zip -l` sul file dimostra che e' arrivato intero.
#
# I tasti: nel dialogo il fuoco sta su «Annulla», l'ultimo; Tab lo porta al
# primo pulsante, un secondo Tab al successivo.
#
# ! NIENTE pkill: il server Python lo ferma questo script, per numero.
# Vuole il sistema costruito:  make iso-exos
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-apri}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
rm -f "$D"/a*.ppm "$D"/a*.png

python3 - "$D" <<'PY'
import sys, zipfile
D = sys.argv[1]
z = zipfile.ZipFile(D + "/prova.zip", "w", zipfile.ZIP_DEFLATED)
z.writestr("leggimi.txt", "EX-OS: un archivio arrivato dalla rete.\n" * 200)
z.writestr("dati/numeri.bin", bytes((i * 7) % 256 for i in range(50000)))
z.close()
PY

python3 - "$D" > "$D/http.log" 2>&1 <<'PY' &
import http.server, sys
D = sys.argv[1]
dati = open(D + "/prova.zip", "rb").read()
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path not in ("/prova.zip", "/dammi"):
            self.send_response(404); self.end_headers(); return
        self.send_response(200)
        self.send_header("Content-Type", "application/zip")
        self.send_header("Content-Length", str(len(dati)))
        self.end_headers()
        self.wfile.write(dati)
http.server.HTTPServer(("0.0.0.0", 8000), H).serve_forever()
PY
P_HTTP=$!
sleep 1

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

EXOS_ISTANZA=apri EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M \
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide -netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    timeout 900 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/casa@1" "export HOME=/disk/casa@1" \
    "exwin@20" "key:alt-f1@2" \
    "/exwin/bin/exbrowser http://10.0.2.2:8000/prova.zip &@20" "key:alt-f5@3" \
    "foto:$D/a1_domanda.ppm@1" "key:tab@1" "key:ret@15" \
    "foto:$D/a2_archivi.ppm@2" \
    "key:alt-f1@2" \
    "/exwin/bin/exbrowser http://10.0.2.2:8000/dammi &@20" "key:alt-f5@3" \
    "foto:$D/a3_domanda.ppm@1" "key:tab@1" "key:tab@1" "key:ret@4" \
    "foto:$D/a4_dove.ppm@1" "key:ret@10" \
    "foto:$D/a5_fatto.ppm@1" \
    "key:alt-f1@2" \
    "echo ELENCO@1" "zip -l /disk/casa/dammi@6" \
    "ls /disk/casa/.app/exbrowser/scaricati@3" > "$D/exos.log" 2>&1

kill $P_HTTP 2>/dev/null
wait $P_HTTP 2>/dev/null
for f in "$D"/a*.ppm; do convert "$f" "${f%.ppm}.png" 2>/dev/null; done

L=$(sed 's/\x1b\[[0-9;]*m//g' "$D/exos.log" | tr -d '\000')
esito=0
controlla() {
    if printf '%s\n' "$L" | grep -aq "$2"; then echo "  si'  $1"
    else echo "  NO   $1"; esito=1; fi
}
controlla "«Apri» l'ha messo nella cartella dei file scaricati" "^prova.zip\|  prova.zip"
controlla "«Scarica» ha scritto un archivio intero (zip -l lo legge)" "numeri.bin"
echo "  le fotografie: $D/a*.png"
echo "ESITO: $([ $esito = 0 ] && echo BUONO || echo "qualcosa non va, vedi $D/exos.log")"
exit $esito
