#!/bin/bash
# =============================================================================
# tools/prova_certi_aggiunti.sh — un certificato aggiunto da EXBrowser vale
# per tutti
#
# Chiesto il 23 settembre 2026: poter aggiungere certificati scaricandoli. La
# prova segue la strada vera, dall'inizio alla fine:
#
#   1. sull'host: una CA di prova, un server https firmato da lei (sul
#      10.0.2.2 che la rete di QEMU fa corrispondere a questa macchina) e il
#      file della CA servito in http;
#   2. in EX-OS `scarica` su quel server deve FALLIRE: la CA non e' nel
#      magazzino;
#   3. da EXBrowser, File > Aggiungi un certificato..., pilotato da tastiera:
#      l'indirizzo del .pem, l'avviso con l'impronta, «Mi fido»;
#   4. lo stesso `scarica` deve RIUSCIRE — ed e' un altro programma: il
#      certificato sta nella casa della persona, non nel navigatore.
#
# ! IL NOME NEL CERTIFICATO E' «10.0.2.2» COME NOME DNS, oltre che come IP:
# excert confronta solo i nomi DNS (0x82), e un sito si raggiunge per nome.
#
# ! NIENTE pkill: qemu_drive.py si ripulisce da solo; i due server Python li
# ferma questo script, per numero di processo.
#
# Vuole il sistema costruito:  make iso-exos
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-certi}"
mkdir -p "$D/www"
IMG="$D/prova-hd.img"
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)

# --- 1. la CA di prova e il server ---------------------------------------------
IERI=$(date -u -d yesterday +%Y%m%d%H%M%SZ)
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$D/ca.key" -out "$D/www/ca.pem" \
    -subj "/CN=EX-OS prova CA/O=EX-OS" -days 30 -not_before "$IERI" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
openssl req -newkey rsa:2048 -nodes -keyout "$D/sito.key" -out "$D/sito.csr" \
    -subj "/CN=10.0.2.2" 2>/dev/null
printf 'basicConstraints=critical,CA:FALSE\nsubjectAltName=DNS:10.0.2.2,IP:10.0.2.2\n' > "$D/sito.ext"
openssl x509 -req -in "$D/sito.csr" -CA "$D/www/ca.pem" -CAkey "$D/ca.key" \
    -CAcreateserial -days 30 -not_before "$IERI" -sha256 -extfile "$D/sito.ext" \
    -out "$D/sito.pem" 2>/dev/null

python3 -m http.server 8000 --bind 0.0.0.0 --directory "$D/www" > "$D/http.log" 2>&1 &
P_HTTP=$!
python3 - "$D" > "$D/https.log" 2>&1 <<'PY' &
import http.server, ssl, sys
D = sys.argv[1]
c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
c.minimum_version = ssl.TLSVersion.TLSv1_3
c.load_cert_chain(D + "/sito.pem", D + "/sito.key")
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        b = b"<html><body>certificato aggiunto: funziona</body></html>"
        self.send_response(200); self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(b))); self.end_headers()
        self.wfile.write(b)
s = http.server.HTTPServer(("0.0.0.0", 8443), H)
s.socket = c.wrap_socket(s.socket, server_side=True)
s.serve_forever()
PY
P_HTTPS=$!
sleep 1

# --- 2-4. EX-OS -------------------------------------------------------------------
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

B=()
for i in 1 2 3 4 5 6 7 8; do B+=("key:backspace@0"); done

EXOS_ISTANZA=crta EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M \
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide -netdev user,id=n1 -device ne2k_pci,netdev=n1" \
    timeout 900 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/casa@1" \
    "export HOME=/disk/casa@1" \
    "echo PRIMA@1" "scarica -i https://10.0.2.2:8443/@25" \
    "exwin@20" "key:alt-f1@2" \
    "/exwin/bin/exbrowser about:blank &@20" "key:alt-f5@3" \
    "key:f10@2" "key:down,down,down,down@2" "foto:$D/c1_menu.ppm@1" \
    "key:ret@3" "${B[@]}" "http://10.0.2.2:8000/ca.pem@15" \
    "foto:$D/c2_chi.ppm@1" "key:ret@3" \
    "foto:$D/c3_fidarsi.ppm@1" "key:tab@1" "key:ret@3" \
    "foto:$D/c4_fatto.ppm@1" "key:ret@2" \
    "key:alt-f1@2" "echo DOPO@1" "scarica -i https://10.0.2.2:8443/@25" \
    "cat /disk/casa/.app/exhttp/certi.pem@3" > "$D/exos.log" 2>&1

kill $P_HTTP $P_HTTPS 2>/dev/null
wait $P_HTTP $P_HTTPS 2>/dev/null

for f in "$D"/c*.ppm; do convert "$f" "${f%.ppm}.png" 2>/dev/null; done

# --- l'esito --------------------------------------------------------------------
L=$(sed 's/\x1b\[[0-9;]*m//g' "$D/exos.log" | tr -d '\000')
prima=$(printf '%s\n' "$L" | sed -n '/^PRIMA/,/^DOPO/p' | grep -a "^scarica:" | head -1)
dopo=$(printf '%s\n' "$L" | sed -n '/^DOPO/,$p' | grep -a "^scarica:" | head -1)
esito=0
echo "  prima: $prima"
echo "  dopo:  $dopo"
case "$prima" in *"radice non e' nel magazzino"*) echo "  si'  prima era rifiutato per la radice";;
                 *) echo "  NO   prima doveva essere rifiutato per la radice"; esito=1;; esac
case "$dopo" in "scarica: 200"*) echo "  si'  dopo si apre, e da un altro programma";;
                *) echo "  NO   dopo doveva aprirsi"; esito=1;; esac
if printf '%s\n' "$L" | grep -aq "^# EX-OS prova CA (EX-OS)"; then
    echo "  si'  il file personale ha il certificato, col suo nome"
else
    echo "  NO   il file personale non ha il certificato"; esito=1
fi
echo "  le fotografie dei dialoghi: $D/c*.png"
echo "ESITO: $([ $esito = 0 ] && echo BUONO || echo "qualcosa non va, vedi $D/exos.log")"
exit $esito
