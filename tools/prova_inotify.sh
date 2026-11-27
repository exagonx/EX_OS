#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_inotify.sh [32|64] - inotify (kernel 0.373, voce 85)
#
#     tools/prova_inotify.sh        32 bit (dist/exos.iso)
#     tools/prova_inotify.sh 64     64 bit (dist/exos64.iso)
#
# A disk with an ext2 and a FAT16 partition; tools/prove/inotprova (static,
# libc.a made here from lib/libc.c) on /disk (ext2) and /mnt (FAT): every
# step prints INO <step> OK or NO - create, write, close, chmod, rename with
# its cookie and the watch that follows, mkdir/rmdir, unlink with
# DELETE_SELF and IGNORED, FIONREAD, poll, a read too short, a blocking read
# and a poll woken by another process, rm_watch, the errors, FAT. Then
# e2fsck and fsck.fat on the partitions.
# Results in /tmp/exos-prove/inotify-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
M="$(hostname | tr 'A-Z' 'a-z')"
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
D=/tmp/exos-prove/inotify-$BIT
SBIN=/usr/sbin
rm -rf "$D"; mkdir -p "$D/albero" "$D/lib" /tmp/exos
gcc $CF -w -Ilib/include -c lib/libc.c -o "$D/lib/libc.o" && ar rcs "$D/lib/libc.a" "$D/lib/libc.o" || exit 1
"$GCC" -O2 -static -Ilib/include -L"$D/lib" tools/prove/inotprova.c -o "$D/albero/inotprova" || exit 1
IMG=$D/hd.img
qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\nstart=67584, size=65536, type=6\n' \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
$SBIN/mke2fs -q -t ext2 -b 1024 -L inotify -d "$D/albero" "$D/p1.img" 32M || exit 1
$SBIN/mkfs.fat -F 16 -n INOTIFY -C "$D/p2.img" 32768 > /dev/null || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
dd if="$D/p2.img" of="$IMG" bs=512 seek=67584 conv=notrunc status=none
rm -f "$D/p1.img" "$D/p2.img"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=inotify$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialinotify$BIT.txt; rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "mount hd0p2 /mnt@5" \
    "/disk/inotprova /disk /mnt@15" "umount /mnt@3" "umount /disk@3" "echo FINE-INOTIFY@2" \
    > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

dd if="$IMG" of="$D/p1.img" bs=512 skip=2048 count=65536 status=none
dd if="$IMG" of="$D/p2.img" bs=512 skip=67584 count=65536 status=none
$SBIN/e2fsck -fn "$D/p1.img" > "$D/e2fsck.txt" 2>&1; E2=$?
$SBIN/fsck.fat -n "$D/p2.img" > "$D/fsckfat.txt" 2>&1; EF=$?
echo "=== inotify ($BIT bit) ==="
grep -a "^INO " "$D/seriale.txt" | sed 's/^/        /'
python3 -I - "$D/seriale.txt" "$E2" "$EF" <<'PY'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
passi = ["init", "vuota-eagain", "enoent", "guarda", "stessa-wd", "fionread-poll", "crea",
         "onlydir-enotdir", "guarda-file", "attrib", "rinomina", "segue-il-nome", "cartella",
         "cancella", "corta-einval", "bloccante", "poll-attende", "poll-evento", "togli",
         "togli-einval", "fat"]
righe = {}
for l in s.split("\n"):
    p = l.split()
    if len(p) == 3 and p[0] == "INO" and p[2] in ("OK", "NO"): righe[p[1]] = p[2]
for x in passi:
    (ok if righe.get(x) == "OK" else no)("%s: %s" % (x, righe.get(x, "nessuna riga")))
(ok if "INO FINE" in s else no)("il programma arriva in fondo")
(ok if sys.argv[2] == "0" else no)("e2fsck sulla ext2: %s" % sys.argv[2])
(ok if sys.argv[3] == "0" else no)("fsck.fat sulla FAT16: %s" % sys.argv[3])
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
E=$?
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
