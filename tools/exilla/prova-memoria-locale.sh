#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/prova-memoria-locale.sh - localStorage, cookies, indexedDB in
# Exilla (voce 131)
#
#     tools/exilla/prova-memoria-locale.sh                 64 bit (dist/exos64.iso)
#     EXOS_ARCH=i386 tools/exilla/prova-memoria-locale.sh  32 bit
#
# The user, 26/11/2026: logging into the Firefox account shows "Local storage
# and cookies are required - local storage or cookies are still disabled".
# A page served over HTTP by the host (10.0.0.2:8000 from the guest, an
# origin like the accounts page's) tries document.cookie, localStorage,
# sessionStorage, indexedDB and navigator.storage, and dump()s each answer
# (MEMORIA <what> OK|NO <error>). Firefox runs with MOZ_LOG for the
# QuotaManager, LocalStorage and the storage underneath: their own reason
# lands in the log. The page loads twice: the second time localStorage and
# the cookie must still hold what the first put there.
# Results in cross_build/<macchina>/costruzione-prova[64]/memoria-locale/.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${EXOS_ARCH:-x86_64}" = x86_64 ]; then
    SUFF=64; CD="${CD:-$R/dist/exos64.iso}"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
else
    SUFF="";  CD="${CD:-$R/dist/exos.iso}";   CPU=""; unset EXOS_QEMU
fi
P="${EXOS_FUORI}/cross_build/exilla-obj/gecko$SUFF/dist/firefox"
D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/memoria-locale"
G="$OUT/disco"; IMG="$OUT/hd.img"; PART="$OUT/part.img"; W="$OUT/www"
PORTA="${PORTA:-8000}"
[ -x "$P/firefox" ] || { echo "manca $P/firefox: prima tools/exilla/gecko-costruisci.sh package" >&2; exit 1; }
ss -ltn 2>/dev/null | grep -q ":$PORTA " && { echo "la porta $PORTA dell'host e' occupata: PORTA=<altra>" >&2; exit 2; }
rm -rf "$OUT"; mkdir -p "$G/font" "$G/p" "$G/c" "$W" /tmp/exos
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
cp tools/exilla/exos-media.js "$G/firefox/defaults/pref/" 2>/dev/null
{
    echo 'pref("datareporting.policy.firstRunURL", "");'
    echo 'pref("browser.dom.window.dump.enabled", true);'
    echo 'pref("browser.shell.checkDefaultBrowser", false);'
    echo 'pref("browser.startup.homepage_override.mstone", "ignore");'
} > "$G/firefox/defaults/pref/memoria.js"
cat > "$W/memoria.html" <<'EOF'
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Memoria</title></head>
<body style="background:#d8f0d8"><h1>MEMORIA LOCALE</h1><pre id="r"></pre>
<script>
function d(t) { dump("MEMORIA " + t + "\n"); document.getElementById("r").textContent += t + "\n"; }
function err(e) { return (e && (e.name + ": " + e.message)) || String(e); }
var giro = location.hash === "#2" ? 2 : 1;
d("GIRO " + giro + " " + location.origin);
d("cookieEnabled " + navigator.cookieEnabled);
try {
  if (giro === 1) document.cookie = "prova=uno; path=/; max-age=3600";
  d("cookie " + (document.cookie.indexOf("prova=uno") >= 0 ? "OK" : "NO [" + document.cookie + "]"));
} catch (e) { d("cookie NO " + err(e)); }
try {
  var ls = window.localStorage;
  if (giro === 1) ls.setItem("prova", "uno");
  d("localStorage " + (ls.getItem("prova") === "uno" ? "OK" : "NO letto " + ls.getItem("prova")));
} catch (e) { d("localStorage NO " + err(e)); }
try {
  sessionStorage.setItem("s", "1");
  d("sessionStorage " + (sessionStorage.getItem("s") === "1" ? "OK" : "NO"));
} catch (e) { d("sessionStorage NO " + err(e)); }
var fatti = 0;
function fine() { if (++fatti === 2) { d("FINE " + giro); if (giro === 1) setTimeout(function () { location.hash = "#2"; location.reload(); }, 1500); } }
try {
  var r = indexedDB.open("prova", 1);
  r.onupgradeneeded = function () { r.result.createObjectStore("s"); };
  r.onsuccess = function () { d("indexedDB OK"); r.result.close(); fine(); };
  r.onerror = function () { d("indexedDB NO " + err(r.error)); fine(); };
} catch (e) { d("indexedDB NO " + err(e)); fine(); }
if (navigator.storage && navigator.storage.estimate)
  navigator.storage.estimate().then(function (s) { d("storage OK quota " + s.quota + " usato " + s.usage); fine(); },
                                    function (e) { d("storage NO " + err(e)); fine(); });
else { d("storage NO manca navigator.storage"); fine(); }
</script></body></html>
EOF
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 256 ))
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L memoria -d "$G" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"; rm -rf "$G"; sync "$IMG"

( cd "$W" && exec python3 -m http.server "$PORTA" --bind 0.0.0.0 ) > "$OUT/http.log" 2>&1 &
HTTP=$!
trap 'kill $HTTP 2>/dev/null' EXIT
sleep 1

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=memoria$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15 -device ne2k_pci,netdev=n1"
SER=/tmp/exos/serialmemoria$SUFF.txt; rm -f "$SER"
A=("netdetect -c@12" "dhcp@8" "mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
for v in HOME=/disk/c LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font \
         MOZ_LOG=QuotaManager:5,LocalStorage:5,mozStorage:3,IndexedDB:3 MOZ_LOG_FILE=/disk/c/moz.log; do
    A+=("export $v@1")
done
A+=("exwin@14" "key:alt-f1@2"
    "/disk/firefox/firefox -no-remote -profile /disk/p http://10.0.0.2:$PORTA/memoria.html &@${ATTESA:-360}"
    "key:alt-f5@3" "foto:$OUT/pagina.ppm@2"
    "key:alt-f1@2" "echo FINE-MEMORIA@2")
timeout 1500 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale.txt"
python3 tools/ppm2png.py "$OUT/pagina.ppm" "$OUT/pagina.png" > /dev/null 2>&1 && rm -f "$OUT/pagina.ppm"
# outside, from the disk: the profile's storage/ and Firefox's own log
dd if="$IMG" of="$OUT/part.img" bs=1M skip=1 status=none && rm -f "$IMG"
/sbin/debugfs -R "ls -l /p" "$OUT/part.img" > "$OUT/profilo.txt" 2>&1
/sbin/debugfs -R "rdump /p/storage $OUT" "$OUT/part.img" > /dev/null 2>&1
find "$OUT/storage" -ls > "$OUT/storage.txt" 2>/dev/null || echo "nessuna cartella storage" > "$OUT/storage.txt"
/sbin/debugfs -R "dump /c/moz.log.moz_log $OUT/moz.log" "$OUT/part.img" > /dev/null 2>&1
rm -f "$OUT/part.img"

echo "=== localStorage, cookie, indexedDB in Exilla ($([ -n "$SUFF" ] && echo 64 || echo 32) bit) ==="
grep -a "^MEMORIA" "$OUT/seriale.txt" | sed 's/^/        /'
python3 -I - "$OUT/seriale.txt" <<'PY'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
r = [l.strip() for l in s.split("\n") if l.startswith("MEMORIA ")]
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
giro, per = 0, {1: {}, 2: {}}
for l in r:
    p = l.split(" ", 3)
    if p[1] == "GIRO": giro = int(p[2]); continue
    if giro and len(p) >= 3: per[giro][p[1]] = " ".join(p[2:])
for g in (1, 2):
    if not per[g]: no("giro %d: la pagina non ha detto niente" % g); continue
    # navigator.storage: only in a secure context (https), not over this http
    for k in ("cookie", "localStorage", "sessionStorage", "indexedDB"):
        v = per[g].get(k, "manca")
        (ok if v.startswith("OK") else no)("giro %d %s: %s" % (g, k, v))
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
E=$?
echo "  registro di Firefox: $OUT/moz.log; lo storage del profilo: $OUT/storage.txt"
[ $E = 0 ] && echo "=== TUTTO A POSTO ===" || echo "=== QUALCOSA NON VA ==="
exit $E
