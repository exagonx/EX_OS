#!/bin/bash
# =============================================================================
# tools/prova_extfs.sh — mkfs, chkdsk, disk and mount on ext2, ext3 and ext4,
# inside EX-OS, with Linux's e2fsck as the judge (kernel 0.292, mkfs 0.002,
# chkdsk 0.003)
#
#     tools/prova_extfs.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_extfs.sh [dir]   64 bit
#
# Run A: mkfs makes ext2, ext3, ext4 with 1 KB blocks, ext4 with 4 KB; disk,
# mount and /proc/mounts name them; files are written and read back; chkdsk
# finds nothing, chkdsk -r changes nothing. Outside: e2fsck clean everywhere.
# Run B: Linux damages two volumes (a bitmap, the free counts) and leaves a
# committed transaction in the journal of a third. chkdsk -r repairs and
# replays; e2fsck clean again, and the block from the journal is in place.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/extfs-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/disco.img"
SBIN=/usr/sbin
S1=131072; S2=131072; S3=131072; S4=1228800     # sectors: 64, 64, 64, 600 MB
O1=2048; O2=$((O1 + S1)); O3=$((O2 + S2)); O4=$((O3 + S3))

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }
fuori() { dd if="$IMG" of="$2" bs=512 skip="$1" count="$3" status=none; }
dentro() { dd if="$2" of="$IMG" bs=512 seek="$1" conv=notrunc status=none; }

qemu-img create -f raw "$IMG" $(((O4 + S4) / 2048 + 1))M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\n' \
    $O1 $S1 $O2 $S2 $O3 $S3 $O4 $S4 | $SBIN/sfdisk "$IMG" > /dev/null 2>&1

export EXOS_ISTANZA=xf$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"

echo "=== 1. EX-OS: mkfs, mount, file, chkdsk ($ARCH) ==="
passi=("mkfs -t ext2 -f -L due hd0p1@~120" "mkfs -t ext3 -f -L tre hd0p2@~120"
       "mkfs -t ext4 -f -L quattro hd0p3@~120" "mkfs -t ext4 -f -L grande hd0p4@~300"
       "disk@4")
for v in 1:a 2:b 3:c 4:d; do
    n=${v%:*}; v=${v#*:}
    passi+=("mount hd0p$n /$v@6" "mount@3" "cat /proc/mounts@3"
            "mkdir /$v/nuova@3" "cp /bin/ls /$v/nuova/ls@5" "echo scritto su $v > /$v/eco.txt@3"
            "cat /$v/eco.txt@3" "/$v/nuova/ls /$v@4" "umount /$v@5")
done
for p in 1 2 3 4; do passi+=("chkdsk hd0p$p@~120" "chkdsk -r hd0p$p@~120"); done
timeout 1500 python3 tools/qemu_drive.py "${passi[@]}" > "$D/a.log" 2>&1
L=$(tr -d '\r' < "$D/a.log")

for t in "Filesystem ext2 creato" "Filesystem ext3 creato"; do
    grep -q "$t" <<<"$L" && ok "mkfs: $t" || no "mkfs: manca '$t' (vedi $D/a.log)"
done
[ "$(grep -c 'Filesystem ext4 creato' <<<"$L")" -eq 2 ] && ok "mkfs: due ext4 creati" \
    || no "mkfs: ext4 (vedi $D/a.log)"
for f in ext2 ext3 ext4; do
    grep -q "hd0p[0-9] .*$f\|$f .*hd0p" <<<"$L" && ok "mount o disk nominano $f" \
        || no "nessuno nomina $f (vedi $D/a.log)"
    grep -q "^hd0p[0-9] /[a-d] $f rw" <<<"$L" && ok "/proc/mounts dice $f" \
        || no "/proc/mounts non dice $f (vedi $D/a.log)"
done
[ "$(grep -c 'Nessun problema trovato' <<<"$L")" -eq 8 ] \
    && ok "chkdsk e chkdsk -r: nessun problema, 8 volte su 8" \
    || no "chkdsk: $(grep -c 'Nessun problema trovato' <<<"$L") volte su 8 (vedi $D/a.log)"

for n in 1 2 3 4; do
    eval "fuori \$O$n $D/p$n.img \$S$n"
    if $SBIN/e2fsck -fn "$D/p$n.img" > "$D/e2fsck-a$n.txt" 2>&1; then
        ok "p$n: e2fsck pulito ($(tail -1 "$D/e2fsck-a$n.txt" | cut -c1-44))"
    else
        no "p$n: e2fsck (vedi $D/e2fsck-a$n.txt)"
    fi
    c=$($SBIN/debugfs -R "cat /eco.txt" "$D/p$n.img" 2>/dev/null | grep -c scritto)
    [ "$c" = 1 ] && ok "p$n: il file scritto da EX-OS si rilegge da Linux" \
        || no "p$n: eco.txt non si rilegge"
    cmp -s <($SBIN/debugfs -R "cat /nuova/ls" "$D/p$n.img" 2>/dev/null) \
           "$([ "$ARCH" = x86_64 ] && echo build-64 || echo build)/iso-exos/bin/ls" \
        && ok "p$n: /nuova/ls identico byte per byte" || no "p$n: /nuova/ls diverso"
done

echo "=== 2. Linux rovina, EX-OS ripara ==="
python3 - "$D" <<'PY'
import struct,sys
D=sys.argv[1]
def geo(f):
    f.seek(1024); s=f.read(1024)
    u=lambda o:struct.unpack('<I',s[o:o+4])[0]
    return 1024<<u(24), u(20), (struct.unpack('<H',s[254:256])[0] or 32)
# p3: one bit of the block bitmap of group 0
with open(D+'/p3.img','r+b') as f:
    bs,fd,dsz=geo(f); f.seek((fd+1)*bs); bb=struct.unpack('<I',f.read(4))[0]
    f.seek(bb*bs+700); v=f.read(1)[0]; f.seek(bb*bs+700); f.write(bytes([v^1]))
    f.seek(1024); s=f.read(44); ib=struct.unpack('<I',s[40:44])[0]
    f.seek(bb*bs+bs+(ib//8)+8); v=f.read(1)[0]; f.seek(bb*bs+bs+(ib//8)+8); f.write(bytes([v&0xFE]))
# p4: the free blocks in the superblock
with open(D+'/p4.img','r+b') as f:
    f.seek(1024+12); v=struct.unpack('<I',f.read(4))[0]; f.seek(1024+12); f.write(struct.pack('<I',v+11))
PY
B=$($SBIN/debugfs -R "bmap /eco.txt 0" "$D/p2.img" 2>/dev/null | tr -dc 0-9)
python3 -c "import sys; sys.stdout.buffer.write(b'DAL GIORNALE\n'+bytes(1024-13))" > "$D/nuovo.bin"
printf 'jo\njw -b %s %s\njc\nq\n' "$B" "$D/nuovo.bin" | $SBIN/debugfs -w "$D/p2.img" > "$D/jw.log" 2>&1
[ $(( $(od -An -tu4 -j1120 -N4 "$D/p2.img") & 4 )) -ne 0 ] \
    && ok "p2: debugfs ha lasciato needs_recovery" || no "p2: niente needs_recovery"
for n in 2 3 4; do eval "dentro \$O$n $D/p$n.img"; done

timeout 900 python3 tools/qemu_drive.py "chkdsk -r hd0p2@~120" "chkdsk -r hd0p3@~120" \
    "chkdsk -r hd0p4@~120" "chkdsk hd0p2@~120" "chkdsk hd0p3@~120" "chkdsk hd0p4@~120" \
    > "$D/b.log" 2>&1
L=$(tr -d '\r' < "$D/b.log")
grep -q "giornale applicato: [1-9]" <<<"$L" && ok "p2: chkdsk -r ha applicato il giornale" \
    || no "p2: giornale non applicato (vedi $D/b.log)"
[ "$(grep -c 'Nessun problema trovato' <<<"$L")" -ge 3 ] \
    && ok "dopo la riparazione: nessun problema" || no "dopo la riparazione (vedi $D/b.log)"
for n in 2 3 4; do
    eval "fuori \$O$n $D/p$n.img \$S$n"
    $SBIN/e2fsck -fn "$D/p$n.img" > "$D/e2fsck-b$n.txt" 2>&1 \
        && ok "p$n: e2fsck pulito dopo chkdsk -r" || no "p$n: e2fsck (vedi $D/e2fsck-b$n.txt)"
done
$SBIN/debugfs -R "cat /eco.txt" "$D/p2.img" 2>/dev/null | grep -q "DAL GIORNALE" \
    && ok "p2: il blocco del giornale e' al suo posto" || no "p2: eco.txt non viene dal giornale"

echo
[ $esito = 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA: vedi $D"
exit $esito
