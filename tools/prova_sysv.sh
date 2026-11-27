#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_sysv.sh [32|64] - System V IPC (kernel 0.373, voce 85):
# shmget/shmat/shmctl, semget/semop/semtimedop/semctl, ftok
#
#     tools/prova_sysv.sh        32 bit (dist/exos.iso)
#     tools/prova_sysv.sh 64     64 bit (dist/exos64.iso)
#
# tools/prove/sysvprova (static, libc.a made here from lib/libc.c) on an
# ext2 disk: every step prints SYSV <step> OK or NO, see the program.
# Results in /tmp/exos-prove/sysv-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
M="$(hostname | tr 'A-Z' 'a-z')"
D=/tmp/exos-prove/sysv-$BIT
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
"$GCC" -O2 -static -Ilib/include -L"$D/lib" tools/prove/sysvprova.c -o "$D/albero/sysvprova" || exit 1
IMG=$D/hd.img
$SBIN/mke2fs -q -t ext2 -b 1024 -L sysv -d "$D/albero" "$IMG" 16M || exit 1

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=sysv$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialsysv$BIT.txt; rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "mount hd0 /disk@5" "/disk/sysvprova /disk@20" \
    "echo FINE-SYSV@2" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

echo "=== sysv ($BIT bit) ==="
grep -a "^SYSV " "$D/seriale.txt" | sed 's/^/        /'
python3 -I - "$D/seriale.txt" <<'FINE'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
passi = ["shm-stat", "shm-figlio", "ftok-excl", "sem-exec", "ping-pong", "tutto-o-niente",
         "scade", "zero", "undo", "getall", "rimosso"]
r = {}
for l in s.split("\n"):
    p = l.split()
    if len(p) == 3 and p[0] == "SYSV" and p[2] in ("OK", "NO"): r[p[1]] = p[2]
for x in passi: (ok if r.get(x) == "OK" else no)("%s: %s" % (x, r.get(x, "nessuna riga")))
(ok if "SYSV FINE" in s else no)("il programma arriva in fondo")
(ok if "ERRORE" not in s else no)("nessun ERRORE")
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
FINE
E=$?
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
