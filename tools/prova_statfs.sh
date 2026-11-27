#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_statfs.sh [32|64] - statfs, fstatfs, statvfs (kernel 0.373,
# voce 103)
#
#     tools/prova_statfs.sh        32 bit (dist/exos.iso)
#     tools/prova_statfs.sh 64     64 bit (dist/exos64.iso)
#
# Gecko 161 (Thunderbird) checks a font file's volume with statfs().f_type.
# A disk with an ext2 and a FAT16 partition (32 MB: too few clusters for a
# FAT32, which the kernel rightly refuses) made on the host; inside EX-OS
# tools/prove/statfsprova on /disk (ext2), /mnt (FAT), / (the CD: booting
# from it, it is the root) and /proc:
#   f_type 0xEF53, 0x4d44, 0x9660, 0x9fa0; ext2 blocks and free blocks the
#   same as dumpe2fs says (the disk is not written in between); FAT with
#   blocks; the CD and /proc read-only; fstatfs and statvfs agreeing.
# Results in /tmp/exos-prove/statfs-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
M="$(hostname | tr 'A-Z' 'a-z')"
if [ $BIT = 64 ]; then
    CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
    GCC="$EXOS_FUORI/cross_build/$M/exos-cross64/bin/x86_64-exos-gcc"
else
    CD="$PWD/dist/exos.iso"; CPU=""; unset EXOS_QEMU
    GCC="$EXOS_FUORI/cross_build/$M/exos-cross/bin/i386-exos-gcc"
fi
[ -f "$CD" ] || { echo "manca $CD" >&2; exit 1; }
D=/tmp/exos-prove/statfs-$BIT
SBIN=/usr/sbin
rm -rf "$D"; mkdir -p "$D/albero" /tmp/exos
"$GCC" -O2 tools/prove/statfsprova.c -o "$D/albero/statfsprova" || exit 1
IMG=$D/hd.img
qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\nstart=67584, size=65536, type=6\n' \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
$SBIN/mke2fs -q -t ext2 -b 1024 -L statfs -d "$D/albero" "$D/p1.img" 32M || exit 1
$SBIN/mkfs.fat -F 16 -n STATFS -C "$D/p2.img" 32768 > /dev/null || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
dd if="$D/p2.img" of="$IMG" bs=512 seek=67584 conv=notrunc status=none
BLOCCHI=$($SBIN/dumpe2fs -h "$D/p1.img" 2>/dev/null | sed -n 's/^Block count: *//p')
LIBERI=$($SBIN/dumpe2fs -h "$D/p1.img" 2>/dev/null | sed -n 's/^Free blocks: *//p')
rm -f "$D/p1.img" "$D/p2.img"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=statfs$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialstatfs$BIT.txt; rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "mount -r hd0p1 /disk@5" "mount hd0p2 /mnt@5" \
    "/disk/statfsprova /disk /disk/statfsprova /mnt / /proc@5" "echo FINE-STATFS@2" \
    > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

echo "=== statfs ($BIT bit) ==="
grep -a "^STATFS \|^FSTATFS \|^STATVFS " "$D/seriale.txt" | sed 's/^/        /'
python3 -I - "$D/seriale.txt" "$BLOCCHI" "$LIBERI" <<'PY'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
blocchi, liberi = int(sys.argv[2]), int(sys.argv[3])
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
v, fs, sv = {}, {}, {}
for l in s.split("\n"):
    p = l.split()
    if len(p) >= 3 and p[0] == "STATFS" and p[2] != "ERRORE":
        v[p[1]] = dict(x.split("=") for x in p[2:])
    elif len(p) == 3 and p[0] == "FSTATFS": fs[p[1]] = p[2]
    elif len(p) == 3 and p[0] == "STATVFS": sv[p[1]] = p[2]
def tipo(path, atteso, nome):
    d = v.get(path)
    if not d: no("%s: statfs non risponde" % path); return None
    (ok if d["tipo"] == atteso else no)("%s: f_type %s (%s, atteso %s)" % (path, d["tipo"], nome, atteso))
    return d
d = tipo("/disk", "ef53", "ext2")
if d:
    (ok if int(d["blocchi"]) == blocchi and int(d["liberi"]) == liberi else no)(
        "/disk: %s blocchi, %s liberi (dumpe2fs: %d, %d)" % (d["blocchi"], d["liberi"], blocchi, liberi))
    (ok if d["ro"] == "1" else no)("/disk montato -r: ro=%s" % d["ro"])
f = tipo("/disk/statfsprova", "ef53", "un file sull'ext2")
f = tipo("/mnt", "4d44", "FAT16")
if f: (ok if int(f["blocchi"]) > 0 and int(f["liberi"]) > 0 else no)("/mnt: %s blocchi, %s liberi" % (f["blocchi"], f["liberi"]))
c = tipo("/", "9660", "ISO 9660, il CD")
if c: (ok if c["ro"] == "1" else no)("/ (il CD) in sola lettura: ro=%s" % c["ro"])
tipo("/proc", "9fa0", "/proc")
(ok if fs.get("/disk/statfsprova") == "UGUALE" else no)("fstatfs di un file aperto = statfs: %s" % fs.get("/disk/statfsprova"))
diversi = [k for k, x in sv.items() if x != "UGUALE"]
(ok if sv and not diversi else no)("statvfs uguale a statfs su %d percorsi%s" % (len(sv), (": diversi " + ", ".join(diversi)) if diversi else ""))
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
E=$?
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
