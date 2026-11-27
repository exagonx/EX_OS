#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_tempi.sh [32|64] - the dates of a file (kernel 0.373, SYS_TEMPI,
# ext4.mod 0.010)
#
#     tools/prova_tempi.sh        32 bit (dist/exos.iso)
#     tools/prova_tempi.sh 64     64 bit (dist/exos64.iso)
#
# tools/prove/tempiprova (linked static with a libc.a made here from
# lib/libc.c: the tree's libc, whatever the sysroot has) on an ext2 and a
# FAT16 partition: utimensat, UTIME_OMIT, utime, utimes, futimens(NULL),
# a directory, EINVAL. Then on the host: debugfs reads atime and mtime of
# tempi.txt on the ext2 disk, fsck.fat and e2fsck find both volumes clean.
# Results in /tmp/exos-prove/tempi-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
M="$(hostname | tr 'A-Z' 'a-z')"
D=/tmp/exos-prove/tempi-$BIT
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
"$GCC" -O2 -static -Ilib/include -L"$D/lib" tools/prove/tempiprova.c -o "$D/albero/tempiprova" || exit 1
IMG=$D/hd.img
qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\nstart=67584, size=65536, type=6\n' \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
$SBIN/mke2fs -q -t ext2 -b 1024 -L tempi -d "$D/albero" "$D/p1.img" 32M || exit 1
$SBIN/mkfs.fat -F 16 -n TEMPI -C "$D/p2.img" 32768 > /dev/null || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
dd if="$D/p2.img" of="$IMG" bs=512 seek=67584 conv=notrunc status=none
rm -f "$D/p1.img" "$D/p2.img"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=tempi$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialtempi$BIT.txt; rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "mount hd0p2 /mnt@5" \
    "echo EXT2-INIZIO@1" "/disk/tempiprova /disk@6" "echo FAT-INIZIO@1" "/disk/tempiprova /mnt@6" \
    "umount /mnt@3" "umount /disk@3" "echo FINE-TEMPI@2" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"
dd if="$IMG" of="$D/p1.img" bs=512 skip=2048 count=65536 status=none
dd if="$IMG" of="$D/p2.img" bs=512 skip=67584 count=65536 status=none
$SBIN/debugfs -R "stat /tempi.txt" "$D/p1.img" > "$D/debugfs.txt" 2>&1
$SBIN/e2fsck -fn "$D/p1.img" > "$D/e2fsck.txt" 2>&1; E2=$?
$SBIN/fsck.fat -n "$D/p2.img" > "$D/fsckfat.txt" 2>&1; FF=$?
MTIME_HOST=$(MTOOLS_SKIP_CHECK=1 mdir -i "$D/p2.img" ::/TEMPI.TXT 2>/dev/null | grep -i "tempi" | head -1)

echo "=== date dei file ($BIT bit) ==="
grep -a "^TEMPI \|EXT2-INIZIO\|FAT-INIZIO" "$D/seriale.txt" | grep -v "echo" | sed 's/^/        /'
python3 -I - "$D/seriale.txt" "$D/debugfs.txt" "$E2" "$FF" <<'PY'
import sys, re
s = open(sys.argv[1], "rb").read().decode("latin-1")
dbg = open(sys.argv[2], "rb").read().decode("latin-1")
e2, ff = sys.argv[3], sys.argv[4]
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
righe = [l for l in s.split("\n") if l.startswith("TEMPI ") or l.endswith("-INIZIO") and not l.startswith("echo")]
parti, cur = {}, None
for l in righe:
    if l.strip() in ("EXT2-INIZIO", "FAT-INIZIO"): cur = l.strip()[:-7]; parti[cur] = {}; continue
    if cur is None: continue
    p = l.split()
    parti[cur][p[1]] = dict(x.split("=") for x in p[2:] if "=" in x) if len(p) > 2 and "=" in p[2] else p[2:]
for fs in ("EXT2", "FAT"):
    v = parti.get(fs)
    if not v: no("%s: nessuna riga" % fs); continue
    fat = fs == "FAT"
    def giorno(x): return int(x) // 86400
    def vicino(a, b, tol): return abs(int(a) - int(b)) <= tol
    attesi = [("fisso", 1000000000, 1234567890), ("omit", None, 1300000000),
              ("utime", 1100000000, 1200000000), ("utimes", 1400000000, 1500000000)]
    prima_a = None
    for nome, a, m in attesi:
        d = v.get(nome)
        if not isinstance(d, dict) or "m" not in d: no("%s %s: %s" % (fs, nome, d)); continue
        if a is None: a = prima_a
        prima_a = a
        # FAT: mtime to 2 s, the access a day (read back as its midnight)
        bm = vicino(d["m"], m, 2) if fat else int(d["m"]) == m
        ba = (giorno(d["a"]) == giorno(a) or vicino(d["a"], a, 86400)) if fat else int(d["a"]) == a
        (ok if bm and ba else no)("%s %s: a=%s m=%s (attesi %s, %s)" % (fs, nome, d["a"], d["m"], a, m))
    d = v.get("futimens")
    if isinstance(d, dict) and "ora" in d:
        (ok if vicino(d["m"], d["ora"], 3) else no)("%s futimens(NULL): m=%s, adesso %s" % (fs, d["m"], d["ora"]))
    else: no("%s futimens: %s" % (fs, d))
    d = v.get("dir")
    (ok if isinstance(d, dict) and vicino(d.get("m", 0), 1222222222, 2 if fat else 0) else no)("%s una directory: %s" % (fs, d))
    (ok if v.get("einval") == ["1"] else no)("%s tv_nsec fuori misura -> EINVAL: %s" % (fs, v.get("einval")))
    (ok if "FINE" in v else no)("%s il programma arriva in fondo" % fs)
m = re.search(r"mtime: 0x([0-9a-f]+)", dbg); a = re.search(r"atime: 0x([0-9a-f]+)", dbg)
(ok if m and int(m.group(1), 16) == 1234567890 and a and int(a.group(1), 16) == 1000000000 else no)(
    "sul disco (debugfs): atime %s mtime %s" % (a and int(a.group(1), 16), m and int(m.group(1), 16)))
(ok if e2 == "0" else no)("e2fsck pulito (%s)" % e2)
(ok if ff == "0" else no)("fsck.fat pulito (%s)" % ff)
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
E=$?
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
