#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_frammenti.sh [dir] - writing on a FRAGMENTED volume (ext4.mod
# 0.009, voce 123)
#
#     tools/prova_frammenti.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_frammenti.sh [dir]   64 bit
#     ! dir outside the repository (default /tmp/exos-prove/frammenti-<arch>)
#
# Until 0.008 a write on a group full of one-block holes reserved nothing and
# paid a bitmap and a descriptor write for every block. Now the reservation
# is the first free run long enough, or else the free blocks wherever they
# are. This makes exactly that volume, four times (ext2 4 KB, ext2 1 KB,
# ext3, ext4), on the host: 600 files of one block, a filler that leaves 260
# free blocks in a row, then the odd files deleted - 300 one-block holes.
# Inside EX-OS: cp of a file of 384 blocks and an append of 50 (more than the
# run: the holes must be used). Outside: e2fsck -fn clean, the copy identical
# byte for byte, and some of its blocks in the holes (the scattered path was
# really taken).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/frammenti-$ARCH}"
case "$(realpath -m "$D")" in "$(realpath .)"*) echo "dir dentro il repository: no" >&2; exit 1;; esac
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
S=65536                                     # sectors per partition: 32 MB
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; export EXOS_RAM=256M; EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }
inizio() { echo $((2048 + ($1 - 1) * S)); }

# --- the four volumes, made on the host ------------------------------------------
echo "=== 1. quattro volumi frammentati, fatti da Linux ($ARCH) ==="
TIPI=("ext2 4096" "ext2 1024" "ext3 4096" "ext4 4096")
qemu-img create -f raw "$IMG" $(((2048 + 4 * S) / 2048 + 1))M > /dev/null
{ printf 'label: dos\nunit: sectors\n\n'
  for n in 1 2 3 4; do printf 'start=%d, size=%d, type=83\n' "$(inizio $n)" $S; done; } \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
for n in 1 2 3 4; do
    set -- ${TIPI[$((n-1))]}; T=$1; B=$2
    A="$D/albero$n"; P="$D/p$n.img"; mkdir -p "$A"
    for i in $(seq -w 1 600); do head -c "$B" /dev/urandom > "$A/f$i"; done
    head -c $((384 * B)) /dev/urandom > "$A/grande.bin"
    head -c $((50 * B)) /dev/urandom > "$A/pezzo.bin"
    cp "$A/grande.bin" "$D/grande$n.bin"; cp "$A/pezzo.bin" "$D/pezzo$n.bin"
    rm -f "$P"
    $SBIN/mke2fs -q -t "$T" -b "$B" -L fram$n -d "$A" "$P" $((S / 2))k || exit 1
    rm -rf "$A"
    # the filler: all the free space but 260 blocks (fewer than the copy: the holes are needed)
    LIB=$($SBIN/dumpe2fs -h "$P" 2>/dev/null | sed -n 's/^Free blocks: *//p')
    # ! not zeros: debugfs write makes a zero block a hole, and the filler
    # took no space at all (the first run of this test)
    tr '\0' '\377' < /dev/zero | head -c $(((LIB - 260 - 16) * B)) > "$D/riempi.bin"
    $SBIN/debugfs -w -R "write $D/riempi.bin riempi.bin" "$P" > /dev/null 2>&1
    rm -f "$D/riempi.bin"
    # the odd ones go: one-block holes
    for i in $(seq -w 1 2 600); do echo "rm f$i"; done | $SBIN/debugfs -w -f - "$P" > /dev/null 2>&1
    $SBIN/e2fsck -fy "$P" > /dev/null 2>&1
    LIB=$($SBIN/dumpe2fs -h "$P" 2>/dev/null | sed -n 's/^Free blocks: *//p')
    $SBIN/e2fsck -fn "$P" > /dev/null 2>&1 && ok "p$n $T $B: $LIB blocchi liberi, 300 buchi da un blocco" \
        || no "p$n: il volume fatto qui non e' pulito"
    dd if="$P" of="$IMG" bs=512 seek="$(inizio $n)" conv=notrunc status=none
done

# --- EX-OS writes ------------------------------------------------------------------
echo "=== 2. EX-OS: cp di 384 blocchi e 50 in coda, sui quattro ==="
export EXOS_ISTANZA=fram$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$PWD/$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
passi=()
for n in 1 2 3 4; do
    passi+=("mount hd0p$n /v@8" "cp /v/grande.bin /v/copia.bin@~120"
            "cat /v/pezzo.bin >> /v/copia.bin@~60" "ls -l /v/copia.bin@3" "umount /v@6")
done
timeout 1800 python3 tools/qemu_drive.py "${passi[@]}" > "$D/exos.log" 2>&1
cp "/tmp/exos/serialfram$ARCH.txt" "$D/seriale.txt" 2>/dev/null
grep -q "ext4.mod 0.0[0-9]*" "$D/seriale.txt" && ok "$(grep -o 'ext4.mod 0.0[0-9]*' "$D/seriale.txt" | head -1) caricato" \
    || no "ext4.mod non caricato (vedi $D/seriale.txt)"
grep -aq "READ-ONLY\|sola lettura" "$D/seriale.txt" && no "un volume in sola lettura (vedi $D/seriale.txt)"

# --- outside: e2fsck, the bytes, the holes -----------------------------------------
echo "=== 3. fuori: e2fsck, i byte, i buchi usati ==="
for n in 1 2 3 4; do
    set -- ${TIPI[$((n-1))]}; T=$1; B=$2
    P="$D/p$n.img"
    dd if="$IMG" of="$P" bs=512 skip="$(inizio $n)" count=$S status=none
    if $SBIN/e2fsck -fn "$P" > "$D/fsck$n.txt" 2>&1; then ok "p$n $T $B: e2fsck pulito"
    else no "p$n $T $B: e2fsck trova errori (vedi $D/fsck$n.txt)"; fi
    cat "$D/grande$n.bin" "$D/pezzo$n.bin" > "$D/atteso$n.bin"
    $SBIN/debugfs -R "dump copia.bin $D/copia$n.bin" "$P" > /dev/null 2>&1
    if cmp -s "$D/atteso$n.bin" "$D/copia$n.bin"; then ok "p$n: copia.bin identica ($(stat -c %s "$D/copia$n.bin") byte)"
    else no "p$n: copia.bin diversa da grande+pezzo"; fi
    # how many pieces: more than the run alone means the holes were used
    PEZZI=$($SBIN/debugfs -R "blocks copia.bin" "$P" 2>/dev/null | tr ' ' '\n' | grep . | sort -n |
            awk 'NR>1 && $1!=p+1 {c++} {p=$1} END {print c+1}')
    [ "${PEZZI:-0}" -gt 20 ] && ok "p$n: copia.bin in $PEZZI pezzi, i buchi usati" \
        || no "p$n: copia.bin in ${PEZZI:-?} pezzi: i buchi non sono stati usati"
done
[ $esito = 0 ] && echo "=== TUTTO A POSTO ($ARCH) ===" || echo "=== QUALCOSA NON VA ($ARCH) ==="
exit $esito
