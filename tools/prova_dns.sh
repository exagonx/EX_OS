#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_dns.sh [32|64] - getaddrinfo with a DNS that loses answers
# (voce 132)
#
#     tools/prova_dns.sh        32 bit (dist/exos.iso)
#     tools/prova_dns.sh 64     64 bit (dist/exos64.iso)
#
# The user, 26/11/2026: in Exilla a site needs several tries before its name
# resolves. The libc's resolver gave up at the first timeout. Here a DNS of
# ours, on the host, does what a real network does now and then:
#   perso*  the first question is not answered (a lost datagram);
#   lento*  every answer comes after 2.5 s (more than the first wait);
#   nx*     NXDOMAIN at once (a name that does not exist: no retries);
#   anything else: 10.9.8.7 at once.
# EX-OS asks it with tools/prove/risolvi (getaddrinfo, as Firefox) after
# `ipcfg -d 10.0.0.2`. perso and lento must resolve, nx must say "nome
# sconosciuto" quickly, and the DNS must see a second question only for
# perso and lento.
# ! PORT 53 ON THE HOST: QEMU's network sends 10.0.0.2:53 to the host's
# 127.0.0.1:53, which a user cannot open. Everything runs in a namespace of
# its own (unshare -rn), where the user is root of an empty network.
# Results in /tmp/exos-prove/dns-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
R="$PWD"
BIT=32; [ "${1:-}" = 64 ] && BIT=64
if [ $BIT = 64 ]; then
    CD="$R/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
    GCC="$EXOS_FUORI/cross_build/$(hostname | tr 'A-Z' 'a-z')/exos-cross64/bin/x86_64-exos-gcc"
else
    CD="$R/dist/exos.iso"; CPU=""; unset EXOS_QEMU
    GCC="$EXOS_FUORI/cross_build/$(hostname | tr 'A-Z' 'a-z')/exos-cross/bin/i386-exos-gcc"
fi
[ -f "$CD" ] || { echo "manca $CD" >&2; exit 1; }
[ -x "$GCC" ] || { echo "manca $GCC" >&2; exit 1; }
D=/tmp/exos-prove/dns-$BIT
rm -rf "$D"; mkdir -p "$D/disco" /tmp/exos
"$GCC" -O2 tools/prove/risolvi.c -o "$D/disco/risolvi" -lpthread || exit 1
IMG=$D/hd.img
/sbin/mke2fs -q -t ext2 -L dns -d "$D/disco" "$D/part.img" 16M || exit 1
qemu-img create -f raw "$IMG" 18M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/part.img" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$D/part.img"

cat > "$D/finto_dns.py" <<'PY'
import socket, sys, threading, time
log = open(sys.argv[1], "w", buffering=1)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 53))
visti = {}
def nome(q):
    p, parti = 12, []
    while q[p]:
        parti.append(q[p + 1:p + 1 + q[p]].decode("latin-1")); p += q[p] + 1
    return ".".join(parti), p + 5
def risposta(q, fine, nx):
    r = bytearray(q[:fine])
    r[2] = 0x81; r[3] = 0x83 if nx else 0x80
    r[6:8] = b"\0\0" if nx else b"\0\1"; r[8:12] = b"\0\0\0\0"
    if not nx: r += b"\xc0\x0c\0\1\0\1\0\0\0\x3c\0\4" + bytes([10, 9, 8, 7])
    return bytes(r)
def manda_dopo(r, da, t):
    time.sleep(t); s.sendto(r, da)
while True:
    q, da = s.recvfrom(1500)
    n, fine = nome(q)
    visti[n] = visti.get(n, 0) + 1
    log.write("DNS %s %d %d\n" % (n, visti[n], (q[0] << 8) | q[1]))
    if n.startswith("perso") and visti[n] == 1: continue
    r = risposta(q, fine, n.startswith("nx"))
    if n.startswith("lento"): threading.Thread(target=manda_dopo, args=(r, da, 2.5)).start()
    else: s.sendto(r, da)
PY

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
cat > "$D/dentro.sh" <<EOF
ip link set lo up
python3 -I "$D/finto_dns.py" "$D/dns.log" &
sleep 1
export EXOS_ISTANZA=dns$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15 -device ne2k_pci,netdev=n1"
rm -f /tmp/exos/serialdns$BIT.txt
timeout 900 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "ipcfg -d 10.0.0.2@3" \
    "mount hd0p1 /disk@5" \
    "/disk/risolvi 1 perso1.prova.lan lento1.prova.lan nx1.prova.lan altro1.prova.lan perso2.prova.lan@60" \
    "echo FINE-DNS@2" > "$D/qemu.log" 2>&1
kill %1 2>/dev/null
EOF
unshare -rn bash "$D/dentro.sh"
tr -d '\r' < "/tmp/exos/serialdns$BIT.txt" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

echo "=== risoluzione con un DNS che perde risposte ($BIT bit) ==="
grep -a "^RISOLVI " "$D/seriale.txt" | sed 's/^/        /'
python3 -I - "$D/seriale.txt" "$D/dns.log" <<'PY'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
d = open(sys.argv[2]).read().split("\n")
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
righe = {}
for l in s.split("\n"):
    p = l.split()
    if len(p) >= 4 and p[0] == "RISOLVI" and p[1] not in righe: righe[p[1]] = p
def guarda(n, atteso, entro):
    p = righe.get(n)
    if not p: no("%s: nessuna risposta" % n); return
    ms = int(p[2]); r = " ".join(p[3:])
    if r.startswith(atteso) and ms <= entro: ok("%s: %s in %d ms" % (n, r, ms))
    else: no("%s: %s in %d ms (atteso %s entro %d ms)" % (n, r, ms, atteso, entro))
guarda("perso1.prova.lan", "OK 10.9.8.7", 4000)
guarda("perso2.prova.lan", "OK 10.9.8.7", 4000)
guarda("lento1.prova.lan", "OK 10.9.8.7", 4000)
guarda("nx1.prova.lan", "NO -2", 1000)
guarda("altro1.prova.lan", "OK 10.9.8.7", 1000)
dom = {}
ids = {}
for l in d:
    p = l.split()
    if len(p) == 4 and p[0] == "DNS":
        dom[p[1]] = max(dom.get(p[1], 0), int(p[2])); ids.setdefault(p[1], set()).add(p[3])
ok("il DNS ha visto la domanda ripetuta di perso1") if dom.get("perso1.prova.lan", 0) >= 2 else no("perso1: domande %r" % dom.get("perso1.prova.lan"))
ok("nx1 chiesto una volta sola in fila") if dom.get("nx1.prova.lan", 0) <= 2 else no("nx1 chiesto %r volte" % dom.get("nx1.prova.lan"))
tutti = [i for v in ids.values() for i in v]
ok("identificativi diversi: %d su %d" % (len(set(tutti)), len(tutti))) if len(set(tutti)) >= len(tutti) - 1 \
    else no("identificativi ripetuti: %d diversi su %d" % (len(set(tutti)), len(tutti)))
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
E=$?
echo "  registro del DNS: $D/dns.log"
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
