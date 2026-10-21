#!/bin/bash
# =============================================================================
# tools/prova_ext3.sh — ext3 written by /dev/ext4.mod 0.003, with its journal
# (@EXT3-GIORNALE), in QEMU, booting from the CD
#
#     tools/prova_ext3.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_ext3.sh [dir]    64 bit
#
# The disk, made by Linux (mke2fs -t ext3 of this machine):
#   p1  ext3, 4 KB blocks          written by EX-OS
#   p2  ext3, 1 KB blocks          written by EX-OS, then a "crash"
#   p3  ext3, 1 KB blocks, left by debugfs with THREE COMMITTED TRANSACTIONS
#       in the journal and needs_recovery set: /vittima.txt rewritten
#       ("contenuto B"), /vittima2.txt rewritten and then REVOKED (it must
#       stay as it was) - EX-OS must replay it the way Linux would
#   p4  the same as p3, mounted with -r: nothing may change, not one byte
#
# Run A: p4 read-only (no replay, the old contents), p3 replayed, then on p1
# and p2 mkdir, cp, mv, echo >, rm, rmdir and tools/prove/discovel.c (write,
# read back every byte, rewrite in place, read again), umount.
# Outside: e2fsck -fn clean everywhere, needs_recovery off, files identical,
# p4 identical byte for byte. Then OUR journal replayed by Linux's e2fsck:
# the last transaction EX-OS committed on p1 is put back in the journal
# (journal superblock and needs_recovery, as after a crash) and e2fsck must
# recover it and find the volume clean.
# Run B: p2 written and QEMU stopped without umount: e2fsck recovers it and
# the volume is clean, with what was written.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/ext3-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
S1=196608; S2=131072; S3=32768; S4=32768        # sectors: 96, 64, 16, 16 MB
O1=2048; O2=$((O1 + S1)); O3=$((O2 + S2)); O4=$((O3 + S3))

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc; LS=build-64/iso-exos/bin/ls
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc; LS=build/iso-exos/bin/ls
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/discovel.c -o "$D/discovel" || exit 1

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }
fuori() { dd if="$IMG" of="$2" bs=512 skip="$1" count="$3" status=none; }      # start, file, count
dentro() { dd if="$2" of="$IMG" bs=512 seek="$1" conv=notrunc status=none; }

# --- the disk -----------------------------------------------------------------
echo "=== 1. disco: due ext3 da scrivere, due con un giornale da riapplicare ($ARCH) ==="
qemu-img create -f raw "$IMG" $(((O4 + S4) / 2048 + 1))M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\n' \
    $O1 $S1 $O2 $S2 $O3 $S3 $O4 $S4 | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
mkdir -p "$D/albero" "$D/rec"
cp "$D/discovel" "$D/albero/discovel"
echo "da togliere" > "$D/albero/da-togliere.txt"
$SBIN/mke2fs -q -F -t ext3 -b 4096 -L ext3-4k -d "$D/albero" -E offset=$((O1 * 512)) "$IMG" $((S1 / 8)) || exit 1
$SBIN/mke2fs -q -F -t ext3 -b 1024 -L ext3-1k -d "$D/albero" -E offset=$((O2 * 512)) "$IMG" $((S2 / 2)) || exit 1

echo "contenuto A vecchio" > "$D/rec/vittima.txt"
echo "seconda vittima originale" > "$D/rec/vittima2.txt"
$SBIN/mke2fs -q -F -t ext3 -b 1024 -L ripristino -d "$D/rec" "$D/p3.img" $((S3 / 2)) || exit 1
B1=$($SBIN/debugfs -R "bmap /vittima.txt 0" "$D/p3.img" 2>/dev/null)
B2=$($SBIN/debugfs -R "bmap /vittima2.txt 0" "$D/p3.img" 2>/dev/null)
(printf 'contenuto B dal giornale\n'; head -c 1024 /dev/zero) | head -c 1024 > "$D/b.bin"
(printf 'NON deve comparire\n'; head -c 1024 /dev/zero) | head -c 1024 > "$D/c.bin"
printf 'jo\njw -b %s %s\njw -b %s %s\njw -r %s\njc\n' "$B1" "$D/b.bin" "$B2" "$D/c.bin" "$B2" \
    | $SBIN/debugfs -w -f - "$D/p3.img" > "$D/debugfs-jw.log" 2>&1
$SBIN/dumpe2fs -h "$D/p3.img" 2>/dev/null | grep -q needs_recovery \
    && ok "p3: debugfs ha lasciato needs_recovery e tre transazioni" \
    || no "p3: debugfs non ha lasciato un giornale da riapplicare (vedi $D/debugfs-jw.log)"
dentro $O3 "$D/p3.img"
dentro $O4 "$D/p3.img"
P4_PRIMA=$(dd if="$IMG" bs=512 skip=$O4 count=$S4 status=none | sha256sum | cut -c1-64)

# --- run A ----------------------------------------------------------------------
echo "=== 2. EX-OS: -r senza ripristino, ripristino, scritture su 4 KB e 1 KB ==="
export EXOS_ISTANZA=e3$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
passi=("modulo@3"
       "mount -r hd0p4 /q@6" "cat /q/vittima.txt@2" "umount /q@3"
       "mount hd0p3 /r@8" "cat /r/vittima.txt@2" "cat /r/vittima2.txt@2"
       "echo dopo il ripristino > /r/dopo.txt@3" "umount /r@3"
       "mount hd0p1 /a@8" "mount hd0p2 /b@8" "mount@2")
for v in a b; do
    passi+=("mkdir /$v/nuova@2" "cp /bin/ls /$v/nuova/ls@4" "mv /$v/nuova/ls /$v/ls2@3"
            "echo scritto da ex-os > /$v/eco.txt@3" "mkdir /$v/vuota@2" "rmdir /$v/vuota@2"
            "rm /$v/da-togliere.txt@3"
            "/$v/discovel /$v/t4k.bin 8 4096@~240" "/$v/discovel /$v/t64k.bin 8 65536@~240")
done
passi+=("umount /a@4" "umount /b@4")
timeout 1800 python3 tools/qemu_drive.py "${passi[@]}" > "$D/a.log" 2>&1
cp "/tmp/exos/seriale3$ARCH.txt" "$D/a-seriale.txt" 2>/dev/null
L=$(tr -d '\r' < "$D/a.log")

tr -d '\r' < "$D/a-seriale.txt" | grep -q "ext4.mod 0.0[0-9]*: ext2,* ext3\( and ext4\)* writing\|ext4.mod 0.0[0-9]*: ext2 and ext3 writing" \
    && ok "ext4.mod caricato, scrive ext2 ed ext3" || no "il modulo non dice ext3 (vedi $D/a-seriale.txt)"
grep -A3 "cat /q/vittima.txt" <<<"$L" | grep -q "contenuto A vecchio" \
    && ok "mount -r: niente ripristino, si legge il vecchio contenuto" \
    || no "mount -r: non legge il vecchio contenuto"
[ "$(dd if="$IMG" bs=512 skip=$O4 count=$S4 status=none | sha256sum | cut -c1-64)" = "$P4_PRIMA" ] \
    && ok "mount -r: p4 identica byte per byte" || no "mount -r: p4 e' cambiata"
grep -A3 "cat /r/vittima.txt" <<<"$L" | grep -q "contenuto B dal gior" \
    && ok "p3: il giornale scritto da debugfs e' stato riapplicato" \
    || no "p3: vittima.txt non ha il contenuto del giornale"
grep -A3 "cat /r/vittima2.txt" <<<"$L" | grep -q "seconda vittima originale" \
    && ok "p3: il blocco revocato non e' stato riapplicato" \
    || no "p3: il blocco revocato e' stato riapplicato"
grep -q "DISCOVEL ERRORI" <<<"$L" && no "discovel: byte riletti diversi"
[ "$(grep -c '^DISCOVEL' <<<"$L")" -eq 16 ] \
    && ok "discovel: 16 misure, nessun byte sbagliato" || no "discovel: mancano misure (vedi $D/a.log)"
grep '^DISCOVEL' <<<"$L" | sed 's/^/        /'

controlla() {   # $1 name, $2 image, $3 1 = with the written files
    local n="$1" f="$2"
    if $SBIN/e2fsck -fn "$f" > "$D/e2fsck-$n.log" 2>&1; then ok "$n: e2fsck pulito"
    else no "$n: e2fsck (vedi $D/e2fsck-$n.log)"; fi
    $SBIN/dumpe2fs -h "$f" 2>/dev/null | grep -q needs_recovery \
        && no "$n: needs_recovery ancora acceso" || ok "$n: needs_recovery spento"
    if [ "$3" = 1 ]; then
        $SBIN/debugfs -R "dump /ls2 $D/$n-ls2" "$f" > /dev/null 2>&1
        cmp -s "$D/$n-ls2" "$LS" && ok "$n: /ls2 identico a /bin/ls" || no "$n: /ls2 diverso"
        [ "$($SBIN/debugfs -R "cat /eco.txt" "$f" 2>/dev/null)" = "scritto da ex-os" ] \
            && ok "$n: /eco.txt giusto" || no "$n: /eco.txt"
        $SBIN/debugfs -R "stat /da-togliere.txt" "$f" 2>&1 | grep -q "not found" \
            && ok "$n: /da-togliere.txt tolto" || no "$n: /da-togliere.txt c'e' ancora"
    fi
}
fuori $O1 "$D/p1.img" $S1; controlla p1 "$D/p1.img" 1
fuori $O2 "$D/p2.img" $S2; controlla p2 "$D/p2.img" 1
fuori $O3 "$D/p3-dopo.img" $S3; controlla p3 "$D/p3-dopo.img" 0
[ "$($SBIN/debugfs -R "cat /dopo.txt" "$D/p3-dopo.img" 2>/dev/null)" = "dopo il ripristino" ] \
    && ok "p3: scritto dopo il ripristino" || no "p3: /dopo.txt"

# --- our journal, recovered by Linux's e2fsck -------------------------------------
echo "=== 3. il giornale scritto da EX-OS, riapplicato da e2fsck ==="
cp "$D/p1.img" "$D/p1-crash.img"
JB=$($SBIN/debugfs -R "bmap <8> 0" "$D/p1-crash.img" 2>/dev/null)
python3 - "$D/p1-crash.img" "$JB" 4096 <<'PY'
import sys, struct
f, jb, bs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
d = bytearray(open(f, "rb").read())
o = jb * bs
first, seq = struct.unpack(">II", d[o + 0x14:o + 0x1C])
# the last transaction EX-OS committed starts at s_first with s_sequence - 1
d[o + 0x18:o + 0x20] = struct.pack(">II", seq - 1, first)
inc = struct.unpack("<I", d[1024 + 96:1024 + 100])[0] | 0x4      # needs_recovery
d[1024 + 96:1024 + 100] = struct.pack("<I", inc)
open(f, "wb").write(d)
print("        transazione %d, dal blocco %d del giornale" % (seq - 1, first))
PY
$SBIN/debugfs -R "logdump" "$D/p1-crash.img" > "$D/logdump.txt" 2>&1
grep -q "commit block" "$D/logdump.txt" \
    && ok "debugfs logdump legge descrittore e commit di EX-OS" \
    || no "logdump non trova una transazione completa (vedi $D/logdump.txt)"
$SBIN/e2fsck -fy "$D/p1-crash.img" > "$D/e2fsck-recupero.log" 2>&1
grep -q "recovering journal" "$D/e2fsck-recupero.log" \
    && ok "e2fsck riapplica la transazione di EX-OS" || no "e2fsck non riapplica (vedi $D/e2fsck-recupero.log)"
$SBIN/e2fsck -fn "$D/p1-crash.img" > "$D/e2fsck-dopo-recupero.log" 2>&1 \
    && ok "dopo il recupero e2fsck pulito" || no "dopo il recupero (vedi $D/e2fsck-dopo-recupero.log)"

# --- run B: no umount ------------------------------------------------------------
echo "=== 4. scritture su p2 e QEMU fermato senza umount ==="
export EXOS_ISTANZA=e3b$ARCH
timeout 600 python3 tools/qemu_drive.py "mount hd0p2 /b@8" \
    "echo prima del crash > /b/crash.txt@3" "mkdir /b/cartella-crash@3" \
    "cp /bin/ls /b/cartella-crash/ls@4" > "$D/b.log" 2>&1
fuori $O2 "$D/p2-crash.img" $S2
$SBIN/dumpe2fs -h "$D/p2-crash.img" 2>/dev/null | grep -q needs_recovery \
    && ok "p2: needs_recovery acceso (montato e non smontato)" || no "p2: needs_recovery spento senza umount"
$SBIN/e2fsck -fy "$D/p2-crash.img" > "$D/e2fsck-p2-y.log" 2>&1
$SBIN/e2fsck -fn "$D/p2-crash.img" > "$D/e2fsck-p2-crash.log" 2>&1 \
    && ok "p2: dopo e2fsck -fy pulito" || no "p2 (vedi $D/e2fsck-p2-crash.log)"
[ "$($SBIN/debugfs -R "cat /crash.txt" "$D/p2-crash.img" 2>/dev/null)" = "prima del crash" ] \
    && ok "p2: /crash.txt c'e'" || no "p2: /crash.txt perso"
$SBIN/debugfs -R "dump /cartella-crash/ls $D/p2-crash-ls" "$D/p2-crash.img" > /dev/null 2>&1
cmp -s "$D/p2-crash-ls" "$LS" && ok "p2: il file copiato prima del crash e' intero" \
    || no "p2: il file copiato prima del crash non e' intero"
exit $esito
