#!/bin/bash
# =============================================================================
# tools/prova_montaggi.sh - eleven volumes mounted at once (kernel 0.343,
# voce 50: the mount tables grow)
#
#     tools/prova_montaggi.sh            32 bit
#     tools/prova_montaggi.sh 64         64 bit
#
# Until 0.342 the VFS had 6 mount slots, ext2 4, FAT 4: prova_extfs met
# "nessuno slot di montaggio libero" at its fourth ext volume. Here one disk
# with an extended partition and eleven logical ones, made on Linux: 3 ext2,
# 3 ext4 (journal, metadata_csum: the write module's state for each volume),
# 5 FAT16. From the CD (root cd0, /rdboot, /proc already take three slots)
# all eleven are mounted together, a file is written on each and read back,
# all are unmounted, two are mounted again (slots used again), and Linux's
# e2fsck and fsck.fat must find every volume clean, with the file in it.
# ISO 9660 beyond two would need three CD drives: not here.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=${1:-32}
D=/tmp/exos-prove/montaggi$BIT; rm -rf "$D"; mkdir -p "$D"
IMG="$D/disco.img"
SBIN=/usr/sbin
if [ "$BIT" = 64 ]; then ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M
else                     ISO=dist/exos.iso;   export EXOS_RAM=256M; fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

TIPI=(ext2 ext2 ext2 ext4 ext4 ext4 fat fat fat fat fat)
N=${#TIPI[@]}
qemu-img create -f raw "$IMG" $((N * 40 + 8))M > /dev/null
{
    printf 'label: dos\nunit: sectors\n\nstart=2048, type=5\n'
    for ((i = 0; i < N; i++)); do printf 'size=%d, type=83\n' $((36 * 2048)); done
} | $SBIN/sfdisk "$IMG" > "$D/sfdisk.log" 2>&1 || { no "sfdisk (vedi $D/sfdisk.log)"; exit 1; }

# the logical partitions are 5, 6, ...: their starts as sfdisk placed them
mapfile -t INIZI < <($SBIN/sfdisk -d "$IMG" | sed -n 's/^.*img\([0-9]*\) : start= *\([0-9]*\),.*/\1 \2/p' | awk '$1 >= 5 {print $2}')
[ ${#INIZI[@]} = $N ] || { no "sfdisk ha fatto ${#INIZI[@]} logiche invece di $N"; exit 1; }
for ((i = 0; i < N; i++)); do
    off=${INIZI[$i]}; byte=$((off * 512)); p=$((i + 5))
    case ${TIPI[$i]} in
        ext2) $SBIN/mke2fs -q -F -t ext2 -L e$p -E offset=$byte "$IMG" 32M 2>> "$D/mkfs.log" ;;
        ext4) $SBIN/mke2fs -q -F -t ext4 -L e$p -E offset=$byte "$IMG" 32M 2>> "$D/mkfs.log" ;;
        fat)  $SBIN/mkfs.fat -F 16 -n F$p --offset=$off "$IMG" $((32 * 1024)) > /dev/null ;;
    esac
done

export EXOS_ISTANZA=mt$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
passi=("disk@4")
for ((i = 0; i < N; i++)); do p=$((i + 5)); passi+=("mount hd0p$p /m$p@4"); done
passi+=("cat /proc/mounts@3")
for ((i = 0; i < N; i++)); do
    p=$((i + 5))
    passi+=("echo volume $p > /m$p/eco.txt@2" "cat /m$p/eco.txt@2")
done
passi+=("sync@4")
for ((i = 0; i < N; i++)); do p=$((i + 5)); passi+=("umount /m$p@3"); done
passi+=("mount hd0p9 /di-nuovo@4" "mount hd0p15 /ancora@4" "cat /proc/mounts@3"
        "cat /di-nuovo/eco.txt@2" "cat /ancora/eco.txt@2"
        "umount /di-nuovo@3" "umount /ancora@3" "uname -a@2")
timeout 1200 python3 tools/qemu_drive.py "${passi[@]}" > "$D/qemu.log" 2>&1
L=$(tr -d '\r' < "$D/qemu.log")
SER=/tmp/exos/serialmt$BIT.txt

KV=$(sed -n 's/^#define EXOS_VERSION *"\(.*\)"/\1/p' kernel/include/version.h)
echo "$L" | grep -aq "EX-OS .* $KV" && si "kernel $KV" || no "il kernel non e' $KV (vedi $D/qemu.log)"
m=$(echo "$L" | grep -ac "^hd0p[0-9]* /m[0-9]* ")
[ "$m" = $N ] && si "/proc/mounts: $N volumi del disco montati insieme" || no "/proc/mounts: $m volumi su $N"
echo "$L" | grep -aq "slot di montaggio\|nessun montaggio libero\|niente memoria per un altro" \
    && no "un montaggio rifiutato per mancanza di posto" || si "nessun montaggio rifiutato"
for ((i = 0; i < N; i++)); do
    p=$((i + 5))
    echo "$L" | grep -aq "^volume $p$" || { no "/m$p (${TIPI[$i]}): il file non si rilegge"; continue; }
done
r=$(echo "$L" | grep -ac "^volume [0-9]*$")
[ "$r" -ge $N ] && si "$N file scritti e riletti (ext2, ext4, FAT)" || no "riletti $r file su $N"
echo "$L" | grep -aq "^hd0p9 /di-nuovo " && echo "$L" | grep -aq "^hd0p15 /ancora " \
    && si "rimontati dopo lo smontaggio (posti riusati)" || no "il rimontaggio non compare in /proc/mounts"
[ "$(echo "$L" | grep -ac '^volume 9$\|^volume 15$')" -ge 4 ] && si "i file ci sono ancora dopo il rimontaggio" \
    || no "dopo il rimontaggio i file non si rileggono"
grep -aq "\[FAULT\]\|PANIC" "$SER" 2>/dev/null && no "[FAULT] o PANIC sulla seriale ($SER)" || si "nessun FAULT"

echo "=== Linux giudica i volumi ==="
for ((i = 0; i < N; i++)); do
    p=$((i + 5)); off=${INIZI[$i]}
    dd if="$IMG" of="$D/p$p.img" bs=512 skip="$off" count=$((32 * 2048)) status=none
    case ${TIPI[$i]} in
        ext*) if $SBIN/e2fsck -fn "$D/p$p.img" > "$D/fsck$p.log" 2>&1; then
                  t=$($SBIN/debugfs -R "cat /eco.txt" "$D/p$p.img" 2>/dev/null)
                  [ "$t" = "volume $p" ] && si "p$p ${TIPI[$i]}: e2fsck pulito, eco.txt giusto" \
                      || no "p$p ${TIPI[$i]}: eco.txt dice '$t'"
              else no "p$p ${TIPI[$i]}: e2fsck trova errori ($D/fsck$p.log)"; fi ;;
        fat)  if $SBIN/fsck.fat -n "$D/p$p.img" > "$D/fsck$p.log" 2>&1; then
                  t=$(MTOOLS_SKIP_CHECK=1 mtype -i "$D/p$p.img" ::/ECO.TXT 2>/dev/null | tr -d '\r')
                  [ "$t" = "volume $p" ] && si "p$p fat: fsck.fat pulito, eco.txt giusto" \
                      || no "p$p fat: eco.txt dice '$t'"
              else no "p$p fat: fsck.fat trova errori ($D/fsck$p.log)"; fi ;;
    esac
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
