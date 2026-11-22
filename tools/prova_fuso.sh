#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_fuso.sh - the time zone (kernel 0.340): time() in UTC
#
#     tools/prova_fuso.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_fuso.sh [dir]   64 bit
#
# QEMU's CMOS clock in local time, as on a real PC (tools/qemu_drive.py,
# -rtc base=localtime), and no /boot/fuso.cfg: the kernel assumes UTC+1 with
# European summer time. tools/prove/fusoprova.c checks time() against Linux,
# localtime/mktime/strftime at the two edges of summer time, tzset and
# gettimeofday, and a new file on ext2 and on FAT dated in UTC; then
# `date -fuso:+5:30,no` must reach localtime at once. From outside, debugfs
# must read the ext2 mtime as UTC and mdir the FAT time as local time.
# Then the same CD with a /boot/fuso.cfg (+5:30, no summer time, put in by
# xorriso): the kernel must read it at boot, before any program runs.
# PASS: every "FUSO" line with 0, "FUSO fine 0" each run, the checks outside.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/fuso-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=${EXOS_FUORI}/cross_build/nasi5deb
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
S1=65536; S2=131072; O1=2048; O2=$((O1 + S1))   # sectors: 32 and 64 MB

if [ "$ARCH" = x86_64 ]; then
    ISO=${FUSO_ISO:-dist/exos64.iso}; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64 $KVM"
else
    ISO=${FUSO_ISO:-dist/exos.iso}; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M EXTRA="$KVM"
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
command -v mdir > /dev/null || { echo "manca mdir (mtools)" >&2; exit 1; }
$GCC -O2 tools/prove/fusoprova.c -o "$D/albero/fusoprova" || exit 1
chmod 755 "$D/albero/fusoprova"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

qemu-img create -f raw "$IMG" $(((O2 + S2) / 2048 + 1))M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=c\n' \
    $O1 $S1 $O2 $S2 | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
fakeroot $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -d "$D/albero" "$D/p1.img" $((S1 / 2)) || exit 1
dd if=/dev/zero of="$D/p2.img" bs=512 count=$S2 status=none
$SBIN/mkfs.fat -F 32 "$D/p2.img" > /dev/null || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=$O1 conv=notrunc status=none
dd if="$D/p2.img" of="$IMG" bs=512 seek=$O2 conv=notrunc status=none

HOST=$(date +%s)
echo "=== $ARCH ${KVM:+(KVM)}, $ISO ==="
EXOS_ISTANZA=fuso$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide,index=0" \
    timeout 600 python3 tools/qemu_drive.py \
    "mount hd0p1 /a@5" "mount hd0p2 /b@5" \
    "/a/fusoprova base $HOST /a /b@30" \
    "date -fuso:+5:30,no@5" "/a/fusoprova zona 330 0@10" \
    "date -fuso:+1,ue@5" "/a/fusoprova zona 60 1@10" \
    "umount /b@5" "umount /a@5" "echo FINE@1" > "$D/q.log" 2>&1
cp "/tmp/exos/serialfuso$ARCH.txt" "$D/seriale.txt" 2>/dev/null

righe=$(grep -a '^FUSO [a-z0-9-]* [01] ' "$D/q.log")
[ -n "$righe" ] || no "nessuna riga FUSO (vedi $D/q.log)"
while read -r r; do
    [ -z "$r" ] && continue
    set -- $r
    if [ "$3" = 0 ]; then ok "$r"; else no "$r"; fi
done <<< "$righe"
[ "$(grep -ac '^FUSO fine 0' "$D/q.log")" -ge 3 ] || no "manca FUSO fine 0 (tre giri)"
grep -aq "FUSO: /boot/fuso.cfg non c'e'" "$D/seriale.txt" "$D/q.log" \
    && ok "kernel: senza /boot/fuso.cfg vale UTC+1 con l'ora legale" || no "kernel: manca la riga FUSO dell'avvio"
grep -aq 'FAULT\|PANIC' "$D/q.log" && no "FAULT o PANIC nel log"

# from outside: ext2 keeps UTC, FAT keeps local time
dd if="$IMG" of="$D/p1.img" bs=512 skip=$O1 count=$S1 status=none
dd if="$IMG" of="$D/p2.img" bs=512 skip=$O2 count=$S2 status=none
m=$($SBIN/debugfs -R "stat nuovo.txt" "$D/p1.img" 2>/dev/null | sed -n 's/^ *mtime: *0x\([0-9a-f]*\).*/\1/p' | head -1)
m=$((16#${m:-0}))
if [ "$m" -ge "$HOST" ] && [ "$m" -le $((HOST + 600)) ]; then ok "ext2: mtime $m e' UTC (Linux all'avvio: $HOST)"
else no "ext2: mtime $m, Linux all'avvio $HOST"; fi
riga=$(MTOOLS_SKIP_CHECK=1 mdir -i "$D/p2.img" -a :: 2>/dev/null | grep -ai 'nuovo')
quando=$(echo "$riga" | grep -oE '[0-9]{4}-[0-9]{2}-[0-9]{2} +[0-9]{1,2}:[0-9]{2}' | head -1)
f=$(date -d "$quando" +%s 2>/dev/null || echo 0)       # read as Linux's local time
if [ "$f" -ge $((HOST - 120)) ] && [ "$f" -le $((HOST + 600)) ]; then ok "FAT: '$quando' e' l'ora locale di Linux"
else no "FAT: '$riga' (Linux all'avvio: $(date -d @$HOST '+%F %R'))"; fi
$SBIN/e2fsck -fn "$D/p1.img" > "$D/e2fsck.log" 2>&1 && ok "ext2: e2fsck pulito" || no "ext2: e2fsck (vedi $D/e2fsck.log)"
$SBIN/fsck.fat -n "$D/p2.img" > "$D/fsckfat.log" 2>&1 && ok "FAT32: fsck.fat pulito" || no "FAT32: fsck.fat (vedi $D/fsckfat.log)"

# the zone file read at boot
if command -v xorriso > /dev/null; then
    printf '# prova_fuso.sh\nminuti = +330\nlegale = no\n' > "$D/fuso.cfg"
    xorriso -indev "$ISO" -outdev "$D/fuso.iso" -boot_image any replay \
        -map "$D/fuso.cfg" /boot/fuso.cfg -commit > "$D/xorriso.log" 2>&1 \
        || no "xorriso: non riesco a fare il CD col fuso (vedi $D/xorriso.log)"
    EXOS_ISTANZA=fuso2$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$D/fuso.iso" \
        EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide,index=0" \
        timeout 300 python3 tools/qemu_drive.py \
        "mount hd0p1 /a@5" "/a/fusoprova zona 330 0@10" "date -fuso@5" "umount /a@5" \
        "echo FINE@1" > "$D/q2.log" 2>&1
    cp "/tmp/exos/serialfuso2$ARCH.txt" "$D/seriale2.txt" 2>/dev/null
    grep -aq 'FUSO: UTC+5:30, ora legale mai (/boot/fuso.cfg)' "$D/seriale2.txt" "$D/q2.log" \
        && ok "kernel: legge /boot/fuso.cfg all'avvio (UTC+5:30, niente ora legale)" \
        || no "kernel: manca 'FUSO: UTC+5:30' all'avvio (vedi $D/seriale2.txt)"
    r=$(grep -a '^FUSO zona-nuova [01] ' "$D/q2.log")
    case "$r" in "FUSO zona-nuova 0 "*) ok "dopo l'avvio: $r" ;; *) no "dopo l'avvio: ${r:-nessuna riga (vedi $D/q2.log)}" ;; esac
    grep -aq 'FAULT\|PANIC' "$D/q2.log" && no "FAULT o PANIC nel log del CD col fuso"
else
    echo "  (manca xorriso: il fuso letto all'avvio non si prova)"
fi

[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
