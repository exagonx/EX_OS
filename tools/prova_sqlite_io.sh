#!/bin/bash
# =============================================================================
# tools/prova_sqlite_io.sh - SQLite and its system calls on a real disk, in QEMU
#
#     tools/prova_sqlite_io.sh [ext2|ext3|ext4] [threads] [rounds]
#     EXOS_ARCH=i386 tools/prova_sqlite_io.sh ...
#
# Builds tools/prove/sqliteio.c with Firefox's own SQLite and its options, and
# runs it on a 1 GB disk made by EX-OS's mkfs (4 KB blocks, like the laptop).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
FS="${1:-ext4}"; FILI="${2:-4}"; GIRI="${3:-30}"
ARCH="${EXOS_ARCH:-x86_64}"
D="${EXOS_DIR:-/tmp/exos-prove/sqlite-io-$ARCH-$FS}"
rm -rf "$D"; mkdir -p "$D"
X=cross_build/nasi5deb
SQ=firefox-main/third_party/sqlite3/src
if [ "$ARCH" = x86_64 ]; then
    GCC=$X/exos-cross64/bin/x86_64-exos-gcc; ISO=dist/exos64.iso
    export EXOS_QEMU=qemu-system-x86_64 EXTRA="-cpu qemu64"
else
    GCC=$X/exos-cross/bin/i386-exos-gcc; ISO=dist/exos.iso; EXTRA=""
fi
"$GCC" -O1 -w -I$SQ -DSQLITE_THREADSAFE=1 -DSQLITE_SECURE_DELETE=1 \
    -DSQLITE_DEFAULT_PAGE_SIZE=32768 -DSQLITE_MAX_DEFAULT_PAGE_SIZE=32768 \
    -DSQLITE_DEFAULT_JOURNAL_SIZE_LIMIT=1572864 -DSQLITE_OMIT_LOAD_EXTENSION \
    -DSQLITE_OMIT_DEPRECATED -DSQLITE_TEMP_STORE=2 -DHAVE_USLEEP=1 \
    tools/prove/sqliteio.c $SQ/sqlite3.c -lpthread -o "$D/sqliteio" || exit 1
IMG="$D/hd.img"
qemu-img create -f raw "$IMG" 1G > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /usr/sbin/sfdisk "$IMG" > /dev/null 2>&1
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=sqio$FS EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" EXOS_RAM=1024M
export EXOS_QEMU_EXTRA="$KVM $EXTRA -drive file=$IMG,format=raw,if=ide"
timeout 500 python3 tools/qemu_drive.py "mkfs -t $FS -f -L prova hd0p1@~200" > "$D/mkfs.log" 2>&1
/usr/sbin/debugfs -w -R "write $D/sqliteio sqliteio" "$IMG?offset=1048576" > "$D/debugfs.log" 2>&1
timeout 1500 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "dmesg > /disk/prima.txt@3" \
    "mkdir /disk/p@2" "/disk/sqliteio /disk/p $FILI $GIRI > /disk/esito.txt@~1200" \
    "dmesg > /disk/dopo.txt@3" "umount /disk@5" "echo FINE-PROVA@2" > "$D/esegui.log" 2>&1
/usr/sbin/debugfs -R "cat esito.txt" "$IMG?offset=1048576" 2>/dev/null > "$D/esito.txt"
/usr/sbin/debugfs -R "cat dopo.txt" "$IMG?offset=1048576" 2>/dev/null > "$D/dopo.txt"
dd if="$IMG" of="$D/p.img" bs=1M skip=1 status=none
/usr/sbin/e2fsck -fn "$D/p.img" > "$D/e2fsck.log" 2>&1; echo "e2fsck esito $?"
cat "$D/esito.txt"
grep -aE 'WARN|ERROR' "$D/dopo.txt" | grep -avE 'firma sconosciuta|ATA:|CFG:|sistema di base|PASSO' | tail -20
