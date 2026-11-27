#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_ppoll.sh [32|64] - ppoll (kernel 0.373, SYS_PPOLL)
#
#     tools/prova_ppoll.sh        32 bit (dist/exos.iso)
#     tools/prova_ppoll.sh 64     64 bit (dist/exos64.iso)
#
# tools/prove/ppollprova (static, with a libc.a made here from lib/libc.c):
# a pipe, the timeout, a pending signal the mask lets in (EINTR at once, the
# old mask back after), one sent by a child during the wait, a mask that
# keeps it out, and a UDP socket in the list (the libc's mixed rounds).
# Results in /tmp/exos-prove/ppoll-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
M="$(hostname | tr 'A-Z' 'a-z')"
D=/tmp/exos-prove/ppoll-$BIT
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
"$GCC" -O2 -static -Ilib/include -L"$D/lib" tools/prove/ppollprova.c -o "$D/albero/ppollprova" || exit 1
IMG=$D/hd.img
$SBIN/mke2fs -q -t ext2 -b 1024 -L ppoll -d "$D/albero" "$IMG" 16M || exit 1

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=ppoll$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialppoll$BIT.txt; rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "mount -r hd0 /disk@5" "/disk/ppollprova@14" \
    "echo FINE-PPOLL@2" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

echo "=== ppoll ($BIT bit) ==="
grep -a "^PPOLL " "$D/seriale.txt" | sed 's/^/        /'
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
    if len(p) >= 2 and p[0] == "PPOLL":
        v[p[1]] = dict(x.split("=") for x in p[2:] if "=" in x)
def c(nome, cond, testo):
    d = v.get(nome)
    if d is None: no("%s: nessuna riga" % nome); return
    try: b = cond(d)
    except Exception: b = False
    (ok if b else no)("%s: %s %s" % (nome, testo, d))
I = lambda d, k: int(d[k])
c("pipe", lambda d: I(d,"r") == 1 and I(d,"in") == 1, "un byte nella pipe -> 1, POLLIN")
c("scade", lambda d: I(d,"r") == 0 and 240 <= I(d,"ms") <= 400, "niente in 250 ms -> 0")
c("pendente", lambda d: I(d,"eintr") == 1 and I(d,"ms") < 100 and I(d,"presi") == 1 and I(d,"bloccato") == 1,
  "segnale gia' pendente, la maschera lo lascia: EINTR subito, gestore, maschera di prima")
c("figlio", lambda d: I(d,"eintr") == 1 and 250 <= I(d,"ms") <= 1500 and I(d,"presi") == 1 and I(d,"bloccato") == 1,
  "segnale dal figlio dopo 300 ms: EINTR")
c("tenuto", lambda d: I(d,"r") == 0 and I(d,"presi") == 0 and I(d,"pendente") == 1 and I(d,"ms") >= 290,
  "la maschera lo tiene fuori: scadenza, resta pendente")
c("presa", lambda d: I(d,"eintr") == 1 and I(d,"ms") < 200 and I(d,"presi") == 1 and I(d,"bloccato") == 1,
  "con un socket UDP nella lista: EINTR subito")
(ok if "PPOLL FINE" in s else no)("il programma arriva in fondo")
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
FINE
E=$?
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
