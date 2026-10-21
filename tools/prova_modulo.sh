#!/bin/bash
# =============================================================================
# tools/prova_modulo.sh — the kernel module /dev/ext4.mod (kernel 0.263), in
# QEMU, booting from the CD
#
#     tools/prova_modulo.sh [directory-di-lavoro]          32 bit
#     EXOS_ARCH=x86_64 tools/prova_modulo.sh [dir]         64 bit
#
# The disk has two partitions made by Linux (mke2fs of this machine):
#   p1  ext2 with Linux's defaults (ext_attr, resize_inode, dir_index,
#       filetype, sparse_super, large_file) and a directory of 300 files
#       that e2fsck -D has turned into a HASHED INDEX: EX-OS must write it
#       without leaving the index wrong;
#   p2  ext4 (extents, 64bit, flex_bg, metadata_csum, journal): read, and
#       written only once the module can (until then its bytes must not
#       change at all).
# Inside EX-OS: `modulo` lists ext4.mod; mkdir, cp, mv, echo >, rm, rmdir on
# p1; p2 read; a damaged module and one for another kernel version refused;
# the damaged one forced (SI) stops clean, because ext4.mod is registered
# already. Outside: e2fsck -fn clean on p1, the copied file identical, the
# new names there, p2 untouched.
#
# At 32 bit also: the floppy WITHOUT /dev/ext4.mod - the disk mounts
# read-only, writing is refused, and the screen says what to do.
#
# Vuole: dist/exos.iso (make iso-exos) o dist/exos64.iso (make ARCH=x86_64
# iso64), e2fsprogs (mke2fs, e2fsck, debugfs), sfdisk, mtools.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/modulo-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
P1=$((2048 * 512)); P1_SETT=65536            # 32 MB
P2=$(((2048 + 65536) * 512)); P2_SETT=98304  # 48 MB

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; BIN=build-64/bin; MOD=build-64/moduli/ext4.mod
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; BIN=build/bin; MOD=build/moduli/ext4.mod
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -f "$MOD" ] || { echo "manca $MOD" >&2; exit 1; }

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

# --- 1. the disk, made by Linux ---------------------------------------------
echo "=== 1. disco: p1 ext2 con indice hash, p2 ext4 ($ARCH) ==="
qemu-img create -f raw "$IMG" 96M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83\nstart=%d, size=%d, type=83\n' \
    $P1_SETT $((2048 + P1_SETT)) $P2_SETT | $SBIN/sfdisk "$IMG" > /dev/null 2>&1

mkdir -p "$D/albero1/grande" "$D/albero2/sotto"
for i in $(seq -w 1 300); do
    echo "file numero $i" > "$D/albero1/grande/un-nome-piuttosto-lungo-per-riempire-$i.txt"
done
echo "dal sistema ospite" > "$D/albero1/ospite.txt"
echo "ciao da ext4" > "$D/albero2/ciao.txt"
head -c 200000 /dev/urandom > "$D/albero2/sotto/dati.bin"

$SBIN/mke2fs -q -F -t ext2 -b 1024 -L prova1 -d "$D/albero1" -E offset=$P1 "$IMG" $((P1_SETT / 2)) \
    || { echo "mke2fs ext2 non riuscito" >&2; exit 1; }
$SBIN/mke2fs -q -F -t ext4 -L prova4 -d "$D/albero2" -E offset=$P2 "$IMG" $((P2_SETT / 2)) \
    || { echo "mke2fs ext4 non riuscito" >&2; exit 1; }
dd if="$IMG" of="$D/p1.img" bs=512 skip=2048 count=$P1_SETT status=none
$SBIN/e2fsck -fyD "$D/p1.img" > "$D/e2fsck-D.log" 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
$SBIN/debugfs -R "stat /grande" "$D/p1.img" 2>/dev/null | grep -q "Flags: 0x1000" \
    && ok "p1: /grande ha l'indice hash (dir_index)" || no "p1: /grande non e' indicizzata: la prova non prova"

# a damaged module (one byte of a message changed: the code is intact, the
# SHA-256 is not) and one built for another version (a right SHA-256)
cp "$MOD" "$D/rotto.mod"
python3 - "$D/rotto.mod" <<'PY'
import sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read())
i = d.find(b"ext2 writing"); d[i] ^= 0x20
open(p, "wb").write(d)
PY
python3 - "$MOD" "$D/altro.mod" <<'PY'
import sys, hashlib
tutto = open(sys.argv[1], "rb").read()
arch = tutto[-40:-32]                   # the trailer's architecture field
corpo = tutto[:-64] + b"EXOSMOD1" + b"0.001".ljust(16, b"\0") + arch
corpo += hashlib.sha256(corpo).digest()
open(sys.argv[2], "wb").write(corpo)
PY
dd if="$IMG" of="$D/p1.img" bs=512 skip=2048 count=$P1_SETT status=none
$SBIN/debugfs -w -R "write $D/rotto.mod rotto.mod" "$D/p1.img" > /dev/null 2>&1
$SBIN/debugfs -w -R "write $D/altro.mod altro.mod" "$D/p1.img" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
P2_PRIMA=$(dd if="$IMG" bs=512 skip=$((2048 + P1_SETT)) count=$P2_SETT status=none | sha256sum | cut -c1-64)

# --- 2. EX-OS from the CD -----------------------------------------------------
echo "=== 2. EX-OS dal CD: il modulo, ext2 scritto, ext4 letto, moduli sbagliati ==="
export EXOS_ISTANZA=mod$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
timeout 900 python3 tools/qemu_drive.py \
    "modulo@3" \
    "mount hd0p1 /disk@6" "mount@2" \
    "mkdir /disk/nuova@2" "cp /bin/ls /disk/nuova/ls@4" "mv /disk/nuova/ls /disk/ls2@3" \
    "echo scritto da ex-os > /disk/grande/zzz-nuovo.txt@3" \
    "rm /disk/grande/un-nome-piuttosto-lungo-per-riempire-007.txt@3" \
    "mkdir /disk/vuota@2" "rmdir /disk/vuota@2" \
    "cp /bin/ls /disk/grande/copia-di-ls@4" \
    "mount hd0p2 /e4@6" "cat /e4/ciao.txt@2" "ls /e4/sotto@2" \
    "mkdir /e4/nonsideve@3" \
    "modulo carica /disk/rotto.mod@4" \
    "modulo carica /disk/altro.mod@4" \
    "modulo carica -forza /disk/rotto.mod@4" "SI@4" \
    "modulo@3" \
    "umount /e4@3" "umount /disk@3" > "$D/2-cd.log" 2>&1
cp "/tmp/exos/serialmod$ARCH.txt" "$D/2-seriale.txt" 2>/dev/null
L="$D/2-cd.log"

grep -aq "^ext4.mod" "$L"                          && ok "modulo: ext4.mod caricato all'avvio" || no "modulo non elenca ext4.mod"
grep -aq "write module ext4.mod registered" "$L" "$D/2-seriale.txt" \
                                                   && ok "il kernel: modulo di scrittura registrato" || no "registrazione non vista"
grep -aq "ciao da ext4" "$L"                       && ok "ext4 letto (cat)" || no "ext4: ciao.txt non letto"
grep -aq "dati.bin" "$L"                           && ok "ext4: ls di una sottocartella" || no "ext4: ls /e4/sotto"
grep -aqi "is now read/write" "$L" "$D/2-seriale.txt" && ok "il CD: un ext2 diventato scrivibile" || true
grep -aq "rotto.mod non corrisponde" "$L"          && ok "modulo danneggiato rifiutato" || no "modulo danneggiato NON rifiutato"
grep -aq "altro.mod non corrisponde" "$L"          && ok "modulo di un'altra versione rifiutato" || no "modulo di un'altra versione NON rifiutato"
grep -aq "is built for kernel 0.001" "$L" "$D/2-seriale.txt" && ok "il kernel dice la versione trovata" || no "versione trovata non detta"
grep -aq "rotto.mod non e' un modulo per questo kernel, o si e' rifiutato" "$L" \
                                                   && ok "modulo forzato: si ferma pulito (scrittura gia' registrata)" || no "modulo forzato: esito inatteso"
[ "$(grep -ac "^ext4.mod" "$L")" -ge 2 ]          && ok "dopo i rifiuti, ext4.mod ancora unico caricato" || no "elenco finale dei moduli"

# --- 3. outside -----------------------------------------------------------------
echo "=== 3. dal sistema ospite ==="
dd if="$IMG" of="$D/p1.img" bs=512 skip=2048 count=$P1_SETT status=none
if $SBIN/e2fsck -fn "$D/p1.img" > "$D/e2fsck-p1.log" 2>&1; then
    ok "p1: e2fsck -fn pulito"
else
    no "p1: e2fsck trova errori (vedi $D/e2fsck-p1.log)"; sed -n 1,25p "$D/e2fsck-p1.log"
fi
$SBIN/debugfs -R "dump /ls2 $D/ls2" "$D/p1.img" > /dev/null 2>&1
cmp -s "$D/ls2" "$BIN/ls" && ok "/ls2 identico a $BIN/ls (cp + mv)" || no "/ls2 diverso da $BIN/ls"
$SBIN/debugfs -R "dump /grande/copia-di-ls $D/copia" "$D/p1.img" > /dev/null 2>&1
cmp -s "$D/copia" "$BIN/ls" && ok "copia nella cartella indicizzata identica" || no "copia nella cartella indicizzata diversa"
$SBIN/debugfs -R "cat /grande/zzz-nuovo.txt" "$D/p1.img" 2>/dev/null | grep -q "scritto da ex-os" \
    && ok "echo > file: il testo c'e'" || no "zzz-nuovo.txt"
$SBIN/debugfs -R "ls /grande" "$D/p1.img" 2>/dev/null | grep -q "riempire-007" \
    && no "il file cancellato c'e' ancora" || ok "rm: il file non c'e' piu'"
$SBIN/debugfs -R "ls /" "$D/p1.img" 2>/dev/null | grep -qw "vuota" && no "rmdir: vuota c'e' ancora" || ok "rmdir"
$SBIN/debugfs -R "ls /nuova" "$D/p1.img" 2>/dev/null | grep -q "ls " && no "mv: nuova/ls c'e' ancora" || ok "mv: tolto da /nuova"
P2_DOPO=$(dd if="$IMG" bs=512 skip=$((2048 + P1_SETT)) count=$P2_SETT status=none | sha256sum | cut -c1-64)
if grep -aq "features ext4.mod cannot write" "$L" "$D/2-seriale.txt"; then
    [ "$P2_PRIMA" = "$P2_DOPO" ] && ok "p2 (ext4, sola lettura): identica byte per byte" \
                                  || no "p2 cambiata pur essendo in sola lettura"
    grep -aq "nonsideve" "$L" && grep -aqi "sola lettura\|read-only" "$L" && ok "mkdir su ext4 rifiutato" || true
else
    dd if="$IMG" of="$D/p2.img" bs=512 skip=$((2048 + P1_SETT)) count=$P2_SETT status=none
    $SBIN/e2fsck -fn "$D/p2.img" > "$D/e2fsck-p2.log" 2>&1 && ok "p2 (ext4 scritto): e2fsck -fn pulito" \
        || { no "p2: e2fsck trova errori (vedi $D/e2fsck-p2.log)"; sed -n 1,25p "$D/e2fsck-p2.log"; }
fi

# --- 4. without the module (32 bit: the floppy) -------------------------------
if [ "$ARCH" = i386 ]; then
    echo "=== 4. dischetto SENZA /dev/ext4.mod: tutto in sola lettura ==="
    cp dist/floppy.img "$D/senza.img"
    mdel -i "$D/senza.img" ::/dev/ext4.mod
    unset EXOS_CDROM EXOS_NO_FLOPPY
    export EXOS_IMG="$D/senza.img" EXOS_ISTANZA=senza EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
    dd if="$IMG" bs=512 skip=2048 count=$P1_SETT status=none | sha256sum | cut -c1-64 > "$D/p1-prima.txt"
    timeout 400 python3 tools/qemu_drive.py "mount hd0p1 /disk@6" "mkdir /disk/no@3" "ls /disk@3" \
        > "$D/4-senza.log" 2>&1
    cp /tmp/exos/serialsenza.txt "$D/4-seriale.txt" 2>/dev/null
    grep -aq "ext4.mod is missing" "$D/4-senza.log" "$D/4-seriale.txt" \
        && ok "senza modulo: il kernel lo dice, col rimedio" || no "senza modulo: nessun messaggio"
    grep -aq "sola lettura" "$D/4-senza.log" && ok "senza modulo: montato in sola lettura" || no "senza modulo: montaggio non in sola lettura"
    [ "$(dd if="$IMG" bs=512 skip=2048 count=$P1_SETT status=none | sha256sum | cut -c1-64)" = "$(cat "$D/p1-prima.txt")" ] \
        && ok "senza modulo: p1 non toccata" || no "senza modulo: p1 cambiata"
fi

echo
[ $esito -eq 0 ] && echo "PROVA MODULO ($ARCH): TUTTO OK" || echo "PROVA MODULO ($ARCH): QUALCOSA NON VA (registri in $D)"
exit $esito
