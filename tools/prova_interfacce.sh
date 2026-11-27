#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_interfacce.sh [32|64] - the named interfaces (libc of 0.373):
# getifaddrs, if_nameindex and family, SIOCGIF* (Qt's QNetworkInterface)
#
#     tools/prova_interfacce.sh        32 bit (dist/exos.iso)
#     tools/prova_interfacce.sh 64     64 bit (dist/exos64.iso)
#
# QEMU's user network (10.0.0.0/24, DHCP from .15) and an ne2k with MAC
# 52:54:00:ab:cd:ef; netdetect, dhcp, then tools/prove/interfprova (static,
# libc.a made here from lib/libc.c): lo 127.0.0.1/8, eth0 10.0.0.15/24 with
# broadcast 10.0.0.255 and that MAC, the same through every function.
# Results in /tmp/exos-prove/interfacce-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
M="$(hostname | tr 'A-Z' 'a-z')"
D=/tmp/exos-prove/interfacce-$BIT
SBIN=/usr/sbin
rm -rf "$D"; mkdir -p "$D/albero" "$D/lib" /tmp/exos
if [ $BIT = 64 ]; then
    CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
    GCC="$EXOS_FUORI/cross_build/$M/exos-cross64/bin/x86_64-exos-gcc"
    CF="-m64 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
else
    CD="$PWD/dist/exos.iso"; CPU=""; unset EXOS_QEMU
    GCC="$EXOS_FUORI/cross_build/$M/exos-cross/bin/i386-exos-gcc"
    CF="-m32 -march=pentium-mmx -mtune=pentium-mmx -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
fi
[ -f "$CD" ] || { echo "manca $CD" >&2; exit 1; }
gcc $CF -w -Ilib/include -c lib/libc.c -o "$D/lib/libc.o" && ar rcs "$D/lib/libc.a" "$D/lib/libc.o" || exit 1
"$GCC" -O2 -static -Ilib/include -L"$D/lib" tools/prove/interfprova.c -o "$D/albero/interfprova" || exit 1
IMG=$D/hd.img
$SBIN/mke2fs -q -t ext2 -b 1024 -L interf -d "$D/albero" "$IMG" 16M || exit 1

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=interf$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15 -device ne2k_pci,netdev=n1,mac=52:54:00:ab:cd:ef"
SER=/tmp/exos/serialinterf$BIT.txt; rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "netdetect -c@12" "dhcp@8" "mount -r hd0 /disk@5" \
    "/disk/interfprova@6" "echo FINE-INTERF@2" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

echo "=== interfacce ($BIT bit) ==="
grep -a "^IFA \|^NI \|^N2I \|^I2N \|^CONF \|^GIF " "$D/seriale.txt" | sed 's/^/        /'
python3 -I - "$D/seriale.txt" <<'FINE'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
r = [l for l in s.split("\n") if l.split()[:1] and l.split()[0] in ("IFA","NI","N2I","I2N","CONF","GIF","INTERF")]
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
def ha(riga, testo): (ok if riga in r else no)(testo + ": " + riga)
MAC = "52:54:00:ab:cd:ef"
ha("IFA lo packet ind=00:00:00:00:00:00 masc=- bc=- flag=49", "lo, la voce AF_PACKET")
ha("IFA lo inet ind=127.0.0.1 masc=255.0.0.0 bc=- flag=49", "lo, 127.0.0.1/8")
ha("IFA eth0 packet ind=%s masc=- bc=- flag=1043" % MAC, "eth0, il MAC della scheda")
ha("IFA eth0 inet ind=10.0.0.15 masc=255.255.255.0 bc=10.0.0.255 flag=1043", "eth0, indirizzo del DHCP")
ha("NI 1 lo", "if_nameindex: lo"); ha("NI 2 eth0", "if_nameindex: eth0")
ha("N2I eth0=2 lo=1 xx=0", "if_nametoindex")
ha("I2N 1=lo 2=eth0 9=NULL", "if_indextoname")
ha("CONF n=2 lo=127.0.0.1 eth0=10.0.0.15", "SIOCGIFCONF")
ha("GIF eth0 flag=1043 ind=10.0.0.15 masc=255.255.255.0 bc=10.0.0.255 mtu=1500 idx=2 hw=%s" % MAC, "SIOCGIF* di eth0")
ha("GIF xx 19", "un nome che non c'e': ENODEV")
(ok if "INTERF FINE" in r else no)("il programma arriva in fondo")
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
FINE
E=$?
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
