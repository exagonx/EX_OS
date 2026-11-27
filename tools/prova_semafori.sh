#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_semafori.sh [32|64] - semaphores between processes (kernel
# 0.373, SYS_ATTESA_DORMI_COMUNE / _SVEGLIA_COMUNE; sem_open, pshared)
#
#     tools/prova_semafori.sh        32 bit (dist/exos.iso)
#     tools/prova_semafori.sh 64     64 bit (dist/exos64.iso)
#
# tools/prove/semprova (static, libc.a made here from lib/libc.c): a named
# semaphore between it and a process it execs, 1000 posts and waits across
# the two (a lost wake-up hangs it), one with pshared in memory a fork
# shares, the timeout, EEXIST, the same name twice. See the program.
# Results in /tmp/exos-prove/semafori-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
M="$(hostname | tr 'A-Z' 'a-z')"
D=/tmp/exos-prove/semafori-$BIT
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
"$GCC" -O2 -static -Ilib/include -L"$D/lib" tools/prove/semprova.c -o "$D/albero/semprova" || exit 1
IMG=$D/hd.img
$SBIN/mke2fs -q -t ext2 -b 1024 -L sem -d "$D/albero" "$IMG" 16M || exit 1

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=sem$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialsem$BIT.txt; rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "mount -r hd0 /disk@5" "/disk/semprova@20" \
    "echo FINE-SEM@2" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

echo "=== semafori ($BIT bit) ==="
grep -a "^SEM " "$D/seriale.txt" | sed 's/^/        /'
python3 -I - "$D/seriale.txt" <<'FINE'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
v = {}
for l in s.split("\n"):
    p = l.split()
    if len(p) >= 2 and p[0] == "SEM":
        v[p[1]] = dict(x.split("=") for x in p[2:] if "=" in x) if len(p) > 2 and "=" in p[2] else p[2:]
def c(nome, cond, testo):
    d = v.get(nome)
    if d is None: no("%s: nessuna riga" % nome); return
    try: b = cond(d)
    except Exception: b = False
    (ok if b else no)("%s: %s %s" % (nome, testo, d))
I = lambda d, k: int(d[k])
c("stesso", lambda d: d == ["1"], "lo stesso nome due volte: lo stesso sem_t")
c("eexist", lambda d: d == ["1"], "O_CREAT|O_EXCL su uno che c'e': EEXIST")
c("nome", lambda d: I(d,"r") == 0 and 250 <= I(d,"ms") <= 1500, "sem_open fra due processi, post dopo 300 ms")
c("mille", lambda d: I(d,"presi") == 1000 and I(d,"resto") == 0, "1000 post dall'altro processo, 1000 wait")
c("pshared", lambda d: I(d,"r") == 0 and 150 <= I(d,"ms") <= 1500, "pshared in memoria MAP_SHARED, post del figlio")
c("scade", lambda d: I(d,"etimedout") == 1 and 190 <= I(d,"ms") <= 450, "sem_timedwait senza post: ETIMEDOUT")
c("valore", lambda d: d == ["2"], "due post: sem_getvalue 2")
(ok if "SEM FINE" in s else no)("il programma arriva in fondo")
(ok if "ERRORE" not in "".join(l for l in s.split("\n") if l.startswith("SEM")) else no)("nessun ERRORE")
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
FINE
E=$?
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
