#!/bin/bash
# =============================================================================
# tools/prova_intestazioni.sh — setRequestHeader e gli headers di fetch
# arrivano davvero al server (@NAVMETA, 28 settembre 2026)
#
# Fino al 28 settembre le intestazioni di uno script si prendevano e si
# buttavano. Qui un server Python sull'host (10.0.2.2 da dentro QEMU) serve
# una pagina che fa un XHR e una fetch con intestazioni sue, e SCRIVE SU UN
# FILE le intestazioni che riceve: il verdetto e' quel file.
#   - X-Prova: ciao                (setRequestHeader)
#   - Content-Type: application/json, e UNA sola volta (sostituisce il nostro)
#   - Host: cattivo                -> NON deve arrivare (vietata)
#   - X-A: b\r\nX-Iniettata: 1     -> NON deve diventare un'intestazione
#   - X-Fetch: si                  (headers di fetch, oggetto semplice)
#
#     tools/prova_intestazioni.sh [directory-di-lavoro]   (default /tmp/exos-intest)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos) e python3.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-intest}"
mkdir -p "$D"
rm -f "$D"/ricevute.txt
PORTA=8765

cat > "$D/pagina.html" <<'PAGINA'
<html><body><p>intestazioni</p><script>
var x = new XMLHttpRequest();
x.open('POST', '/eco?xhr', false);
x.setRequestHeader('X-Prova', 'ciao');
x.setRequestHeader('Content-Type', 'application/json');
x.setRequestHeader('Host', 'cattivo');
x.setRequestHeader('X-A', 'b\r\nX-Iniettata: 1');
x.send('{"a":1}');
fetch('/eco?fetch', { headers: { 'X-Fetch': 'si' } }).then(function () {
  throw new Error('INTESTAZIONI-FATTE');
});
</script></body></html>
PAGINA

cat > "$D/servi.py" <<'SERVER'
import http.server, sys
D = sys.argv[1]
class H(http.server.BaseHTTPRequestHandler):
    def registra(self):
        with open(D + "/ricevute.txt", "a") as f:
            f.write("=== %s %s\n" % (self.command, self.path))
            for k, v in self.headers.items(): f.write("%s: %s\n" % (k, v))
    def rispondi(self, corpo, tipo="text/plain"):
        self.send_response(200)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)
    def do_GET(self):
        if self.path.startswith("/pagina"):
            self.rispondi(open(D + "/pagina.html", "rb").read(), "text/html")
        else:
            self.registra(); self.rispondi(b"ok")
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(n)
        self.registra(); self.rispondi(b"ok")
    def log_message(self, *a): pass
http.server.HTTPServer(("0.0.0.0", int(sys.argv[2])), H).serve_forever()
SERVER
python3 "$D/servi.py" "$D" $PORTA &
SERVER_PID=$!
sleep 1

EXOS_ISTANZA=intest EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M \
EXOS_QEMU_EXTRA="-netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    timeout 600 python3 tools/qemu_drive.py "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser http://10.0.2.2:$PORTA/pagina.html &@40" \
    > "$D/1.log" 2>&1
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo "=== le intestazioni ricevute ==="
sed 's/^/    /' "$D/ricevute.txt" 2>/dev/null
esito=0
xhr=$(awk '/^=== [A-Z]+ \/eco\?xhr/{f=1;next} /^===/{f=0} f' "$D/ricevute.txt" 2>/dev/null)
fet=$(awk '/^=== GET \/eco\?fetch/{f=1;next} /^===/{f=0} f' "$D/ricevute.txt" 2>/dev/null)
si() { if [ "$1" = 1 ]; then echo "  [OK]  $2"; else echo "  [NO]  $2"; esito=1; fi; }
si "$(grep -q '^=== POST /eco?xhr' "$D/ricevute.txt" && echo 1)" "l'XHR in POST arriva come POST"
si "$(echo "$xhr" | grep -qxi 'X-Prova: ciao' && echo 1)" "setRequestHeader arriva (X-Prova: ciao)"
si "$([ "$(echo "$xhr" | grep -ci '^Content-Type:')" = 1 ] && echo "$xhr" | grep -qxi 'Content-Type: application/json' && echo 1)" \
   "il Content-Type della pagina sostituisce il nostro, una volta sola"
si "$(echo "$xhr" | grep -qi '^Host: cattivo' || echo 1)" "Host non si puo' cambiare"
si "$([ -n "$xhr" ] && ! echo "$xhr" | grep -qi '^X-Iniettata' && echo 1)" "un a capo dentro un valore non inietta un'intestazione"
si "$(echo "$fet" | grep -qxi 'X-Fetch: si' && echo 1)" "gli headers di fetch arrivano (X-Fetch: si)"
exit $esito
