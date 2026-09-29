#!/bin/bash
# =============================================================================
# tools/prova_biscotti_disco.sh — i biscotti sopravvivono al navigatore
# (@NAVMETA: «la persistenza dei biscotti non e' provata dentro EX-OS»)
#
# Una casa su ext2 ($HOME), un server Python sull'host (10.0.2.2 da QEMU):
#   1. un EXBrowser apre /metti, che risponde con un biscotto persistente
#      (Max-Age): il navigatore lo scrive in $HOME/.app/exbrowser/biscotti.txt;
#   2. un SECONDO EXBrowser, nato dopo, apre /guarda: il biscotto deve
#      arrivare al server, letto dal file e non dalla memoria del primo;
#   3. e il file, fuori da QEMU, deve contenerlo (debugfs).
#
#     tools/prova_biscotti_disco.sh [directory-di-lavoro]   (default /tmp/exos-bisdisco)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs, python3.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-bisdisco}"
mkdir -p "$D"
rm -f "$D"/ricevute.txt "$D"/biscotti.txt
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
PORTA=8766
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

cat > "$D/servi.py" <<'SERVER'
import http.server, sys
D = sys.argv[1]
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        with open(D + "/ricevute.txt", "a") as f:
            f.write("=== %s Cookie: %s\n" % (self.path, self.headers.get("Cookie", "(nessuno)")))
        corpo = b"<html><body><p>pagina " + self.path.encode() + b"</p></body></html>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        if self.path.startswith("/metti"):
            self.send_header("Set-Cookie", "prova=42; Max-Age=86400; Path=/")
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)
    def log_message(self, *a): pass
http.server.HTTPServer(("0.0.0.0", int(sys.argv[2])), H).serve_forever()
SERVER
python3 "$D/servi.py" "$D" $PORTA &
SERVER_PID=$!
sleep 1

export EXOS_ISTANZA=bisdisco EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
    timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "mkdir casa" "$OFF" > /dev/null 2>&1

EXOS_QEMU_EXTRA="-netdev user,id=n1 -device ne2k_pci,netdev=n1 -drive file=$IMG,format=raw,if=ide" \
    timeout 600 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "export HOME=/disk/casa@1" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser http://10.0.2.2:$PORTA/metti &@30" "key:alt-f1@2" \
    "/exwin/bin/exbrowser http://10.0.2.2:$PORTA/guarda &@30" "key:alt-f1@2" \
    "sync@3" > "$D/1.log" 2>&1
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

"$DEBUGFS" -R "dump casa/.app/exbrowser/biscotti.txt $D/biscotti.txt" "$OFF" > /dev/null 2>&1
echo "=== il server ha ricevuto ==="; sed 's/^/    /' "$D/ricevute.txt" 2>/dev/null
echo "=== biscotti.txt ==="; sed 's/^/    /' "$D/biscotti.txt" 2>/dev/null
esito=0
if grep -q "prova" "$D/biscotti.txt" 2>/dev/null; then echo "  [OK]  il biscotto e' scritto nel file della casa"
else echo "  [NO]  biscotti.txt non contiene il biscotto"; esito=1; fi
if grep -q "^=== /guarda Cookie: .*prova=42" "$D/ricevute.txt" 2>/dev/null; then
    echo "  [OK]  il secondo navigatore l'ha rimandato (letto dal file)"
else echo "  [NO]  il secondo navigatore non ha mandato il biscotto"; esito=1; fi
exit $esito
