#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_stat.sh - stat() and fstat() tell what the volume keeps
# (kernel 0.339: Stat with mode, owner, links and Unix times)
#
#     tools/prova_stat.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_stat.sh [dir]   64 bit
#
# One disk made by Linux: p1 ext2 (f751 0751 1234:56 with fixed times and a
# hard link, a directory 0710 with a subdirectory, a symbolic link), p2
# FAT32; a CD made with xorriso, tempo.txt dated 1000000000. Inside EX-OS
# tools/prove/statprova.c compares; then e2fsck -fn and debugfs from outside
# must see the chmod 0640 that EX-OS made.
# PASS: every "STAT" line with 0, "STAT fine 0", clean checks, no FAULT.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/stat-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero" "$D/cd"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=${EXOS_FUORI}/cross_build/nasi5deb
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
S1=65536; S2=131072; O1=2048; O2=$((O1 + S1))   # sectors: 32 and 64 MB

if [ "$ARCH" = x86_64 ]; then
    ISO=${STAT_ISO:-dist/exos64.iso}; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64 $KVM"
else
    ISO=${STAT_ISO:-dist/exos.iso}; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M EXTRA="$KVM"
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -x $SBIN/mkfs.fat ] || { echo "manca mkfs.fat (dosfstools)" >&2; exit 1; }
$GCC -O2 tools/prove/statprova.c -o "$D/albero/statprova" || exit 1
chmod 755 "$D/albero/statprova"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

printf 'ciao\n' > "$D/cd/tempo.txt"
touch -d @1000000000 "$D/cd/tempo.txt"
TZ=UTC xorriso -as mkisofs -quiet -J -o "$D/cd.iso" "$D/cd" || exit 1

qemu-img create -f raw "$IMG" $(((O2 + S2) / 2048 + 1))M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=c\n' \
    $O1 $S1 $O2 $S2 | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
# owners exist only inside fakeroot: the tree and mke2fs in one session
fakeroot sh -c "
    cd '$D/albero' &&
    printf 'ciao\n' > f751 && chmod 0751 f751 && chown 1234:56 f751 &&
    ln f751 due &&
    mkdir -p d/s && chmod 0710 d &&
    ln -s f751 lnk &&
    touch -m -d @1000000000 f751 && touch -a -d @1100000000 f751 &&
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -d '$D/albero' '$D/p1.img' $((S1 / 2))" || exit 1
dd if=/dev/zero of="$D/p2.img" bs=512 count=$S2 status=none
$SBIN/mkfs.fat -F 32 -n STAT "$D/p2.img" > /dev/null || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=$O1 conv=notrunc status=none
dd if="$D/p2.img" of="$IMG" bs=512 seek=$O2 conv=notrunc status=none

echo "=== $ARCH ${KVM:+(KVM)}, $ISO ==="
EXOS_ISTANZA=stat$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide,index=0 -drive file=$D/cd.iso,if=ide,index=3,media=cdrom" \
    timeout 600 python3 tools/qemu_drive.py \
    "mount hd0p1 /a@5" "mount hd0p2 /b@5" "mount cd1 /c@5" \
    "/a/statprova /a /b /c@30" "umount /c@5" "umount /b@5" "umount /a@5" "echo FINE@1" > "$D/q.log" 2>&1

righe=$(grep -a '^STAT [a-z0-9-]* [01] ' "$D/q.log")
[ -n "$righe" ] || no "nessuna riga STAT (vedi $D/q.log)"
while read -r r; do
    [ -z "$r" ] && continue
    set -- $r
    if [ "$3" = 0 ]; then ok "$r"; else no "$r"; fi
done <<< "$righe"
grep -aq '^STAT fine 0' "$D/q.log" || no "manca STAT fine 0"
grep -aq 'FAULT\|PANIC' "$D/q.log" && no "FAULT o PANIC nel log"

dd if="$IMG" of="$D/p1.img" bs=512 skip=$O1 count=$S1 status=none
dd if="$IMG" of="$D/p2.img" bs=512 skip=$O2 count=$S2 status=none
$SBIN/e2fsck -fn "$D/p1.img" > "$D/e2fsck.log" 2>&1 && ok "ext2: e2fsck pulito" || no "ext2: e2fsck (vedi $D/e2fsck.log)"
$SBIN/fsck.fat -n "$D/p2.img" > "$D/fsckfat.log" 2>&1 && ok "FAT32: fsck.fat pulito" || no "FAT32: fsck.fat (vedi $D/fsckfat.log)"
m=$($SBIN/debugfs -R "stat f751" "$D/p1.img" 2>/dev/null | grep -o 'Mode: *[0-7]*' | grep -o '[0-7]*$')
[ "$m" = 0640 ] && ok "debugfs: f751 ha modo 0640, come chmod dentro EX-OS" || no "debugfs: f751 ha modo '$m' invece di 0640"

[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
