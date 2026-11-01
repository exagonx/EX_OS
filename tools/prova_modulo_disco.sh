#!/bin/bash
# =============================================================================
# tools/prova_modulo_disco.sh — /dev/ext4.mod on an INSTALLED disk, and
# netupdate 0.031 bringing it back (kernel 0.263-0.264, 32 bit, QEMU)
#
#     tools/prova_modulo_disco.sh [dir]
#
# A. A disk installed from the CD (the floppy has only /rdboot and refuses
#    to install; tools/mkhd.sh): it boots, the kernel
#    loads the module, the ext2 root becomes writable and a file written on
#    it is there, outside, with e2fsck clean.
# B. A disk installed from the CD: /dev/ext4.mod and /bin/uname removed, the
#    netupdate register removed too (a machine updated by a netupdate older
#    than 0.031 never had the module in it), then `netupdate -check -yes`
#    against dist/netinst served by tools/prova_netupdate_http.py through
#    QEMU's guestfwd (no socket on the host). The module is marked
#    `essenziale` and must come back, byte for byte; uname is not and must
#    stay out. e2fsck clean.
# C. The disk of B with a module for another kernel, same size: it boots
#    writable from /rdboot/dev/ext4.mod and `netupdate -check -yes` replaces
#    the file, with no register and with one that says the right hash.
# Needs dist/exos.iso and dist/netinst (tools/mknetinst.sh).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$(pwd)
D="${1:-/tmp/exos-prove/modulo-disco}"
rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin
for f in dist/exos.iso dist/netinst/elenco.txt; do
    [ -f "$f" ] || { echo "manca $f" >&2; exit 1; }
done
grep -q "^dev/ext4.mod	.*	essenziale" dist/netinst/elenco.txt || {
    echo "dist/netinst/elenco.txt non segna dev/ext4.mod essenziale: rifai tools/mknetinst.sh" >&2; exit 1; }

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
pulito() { tr -d '\r' < "$1"; }

# The partition of a disk made by mkhd.sh starts at sector 2048.
partizione() { dd if="$1" of="$2" bs=512 skip=2048 status=none; }

disco() {   # $1 = image, $2 = cd, $3 = log
    sed "s|/tmp/exos-mkhd.log|$D/mkhd-$2.log|g" tools/mkhd.sh > "$D/mkhd-$2.sh"
    # part A's exports (EXOS_MARCA, the drive) must not reach the installer
    env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
        EXOS_ISTANZA=md$2 EXOS_SUPPORTO=$2 EXOS_HD="$1" bash "$D/mkhd-$2.sh" 128 > "$3" 2>&1
}
# The installed disk has the Italian keyboard: qemu_drive types US keys.
L=("root@2" "root@6" "keymap us@3")

# --- A. installed from the CD ------------------------------------------------
echo "=== A. disco installato dal CD ==="
A="$D/a.img"
if disco "$A" cd "$D/a-install.log"; then
    export EXOS_ISTANZA=mda EXOS_NO_FLOPPY=1 EXOS_RAM=64M EXOS_MARCA="utente:"
    export EXOS_QEMU_EXTRA="-drive file=$A,format=raw,if=ide"
    timeout 600 python3 tools/qemu_drive.py "${L[@]}" \
        "echo scritto-da-exos > /scritto.txt@3" "cat /scritto.txt@2" \
        > "$D/a.log" 2>&1
    cp /tmp/exos/serialmda.txt "$D/a-seriale.txt" 2>/dev/null
    pulito "$D/a-seriale.txt" | grep -q "ext4.mod 0.0[0-9]*: ext2[ ,]" \
        && si "il kernel ha caricato /dev/ext4.mod dal disco" \
        || no "il modulo non si e' caricato (vedi $D/a-seriale.txt)"
    pulito "$D/a.log" | grep -q "^scritto-da-exos" \
        && si "la radice ext2 e' scrivibile (cat rilegge il file)" \
        || no "il file sulla radice non si rilegge (vedi $D/a.log)"
    partizione "$A" "$D/a-p1.img"
    $SBIN/debugfs -R "stat /rdboot" "$D/a-p1.img" 2>&1 | grep -q "not found" \
        && si "install non ha copiato /rdboot sul disco" \
        || no "/rdboot e' finito sul disco"
    [ "$($SBIN/debugfs -R "cat /scritto.txt" "$D/a-p1.img" 2>/dev/null)" = scritto-da-exos ] \
        && si "fuori da EX-OS /scritto.txt c'e' ed e' giusto" \
        || no "fuori da EX-OS /scritto.txt non c'e'"
    $SBIN/e2fsck -fn "$D/a-p1.img" > "$D/a-e2fsck.log" 2>&1 \
        && si "e2fsck pulito" || no "e2fsck (vedi $D/a-e2fsck.log)"
else
    no "l'installazione dal CD non e' riuscita (vedi $D/a-install.log)"
fi

# --- B. netupdate brings the module back --------------------------------------
echo "=== B. netupdate -check -yes riporta il modulo ==="
B="$D/b.img"
if disco "$B" cd "$D/b-install.log"; then
    : > tools/httpino.log; rm -f tools/httpino.troncato
    printf 'exec python3 %s/tools/prova_netupdate_http.py %s/dist/netinst\n' \
        "$RADICE" "$RADICE" > "$D/http.sh"
    chmod +x "$D/http.sh"
    export EXOS_ISTANZA=mdb EXOS_NO_FLOPPY=1 EXOS_RAM=128M EXOS_MARCA="utente:"
    export EXOS_QEMU_EXTRA="-drive file=$B,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http.sh"
    timeout 1500 python3 tools/qemu_drive.py "${L[@]}" "modulo@3" \
        "rm /dev/ext4.mod@2" "rm /bin/uname@2" "rm /boot/netupdate.reg@2" \
        "echo tipo = HTTP > /boot/netupdate.cnf@2" \
        "echo url = 10.0.2.100 >> /boot/netupdate.cnf@2" \
        "netupdate -check -yes@~900" \
        "ls /dev/ext4.mod@2" "ls /bin/uname@2" "sync@3" \
        > "$D/b.log" 2>&1
    cp /tmp/exos/serialmdb.txt "$D/b-seriale.txt" 2>/dev/null
    pulito "$D/b.log" | grep -q "dev/ext4.mod \[manca: e' essenziale\]" \
        && si "-check elenca dev/ext4.mod fra i file da aggiornare (essenziale)" \
        || no "-check non elenca dev/ext4.mod come essenziale (vedi $D/b.log)"
    partizione "$B" "$D/b-p1.img"
    $SBIN/debugfs -R "stat /rdboot" "$D/b-p1.img" 2>&1 | grep -q "not found" \
        && si "install non ha copiato /rdboot sul disco" \
        || no "/rdboot e' finito sul disco"
    $SBIN/debugfs -R "dump /dev/ext4.mod $D/b-ext4.mod" "$D/b-p1.img" > /dev/null 2>&1
    cmp -s "$D/b-ext4.mod" build/iso-exos/dev/ext4.mod \
        && si "/dev/ext4.mod e' tornato, uguale a quello pubblicato" \
        || no "/dev/ext4.mod non e' tornato (vedi $D/b.log)"
    $SBIN/debugfs -R "stat /bin/uname" "$D/b-p1.img" 2>&1 | grep -q "not found\|File not found" \
        && si "/bin/uname (non essenziale) e' rimasto fuori" \
        || no "/bin/uname e' stato reinstallato senza -yesall"
    $SBIN/e2fsck -fn "$D/b-p1.img" > "$D/b-e2fsck.log" 2>&1 \
        && si "e2fsck pulito" || no "e2fsck (vedi $D/b-e2fsck.log)"
else
    no "l'installazione dal CD non e' riuscita (vedi $D/b-install.log)"
fi

# --- C. a stale module of the same size (kernel 0.293, netupdate 0.038) ---------
# The laptop of 30 October 2026: ext4.mod for another kernel, same 40920 bytes,
# and a register saying it was the new one.
vecchio() {   # puts a module for kernel 0.280 on the disk $B
    partizione "$B" "$D/c-p1.img"
    cp build/iso-exos/dev/ext4.mod "$D/c-ext4.mod"
    python3 tools/modulo-invecchia.py "$D/c-ext4.mod" 0.280
    printf 'rm /dev/ext4.mod\nwrite %s /dev/ext4.mod\n' "$D/c-ext4.mod" \
        | $SBIN/debugfs -w -f - "$D/c-p1.img" > "$D/c-debugfs.log" 2>&1
    dd if="$D/c-p1.img" of="$B" bs=512 seek=2048 conv=notrunc status=none
}
giro_c() {    # $1 = name, $2.. = commands before the update
    local n="$1"; shift
    vecchio
    EXOS_ISTANZA=mdc timeout 1500 python3 tools/qemu_drive.py "${L[@]}" \
        "cat /proc/mounts@3" "$@" "netupdate -check -yes@~900" "sync@3" \
        > "$D/c-$n.log" 2>&1
    cp /tmp/exos/serialmdc.txt "$D/c-$n-seriale.txt" 2>/dev/null
    pulito "$D/c-$n-seriale.txt" | grep -q " / ext2 rw" \
        && si "$n: con il modulo vecchio la radice e' in scrittura" \
        || no "$n: radice in sola lettura (vedi $D/c-$n.log)"
    partizione "$B" "$D/c-p1.img"
    $SBIN/debugfs -R "dump /dev/ext4.mod $D/c-dopo.mod" "$D/c-p1.img" > /dev/null 2>&1
    cmp -s "$D/c-dopo.mod" build/iso-exos/dev/ext4.mod \
        && si "$n: netupdate ha sostituito il modulo" \
        || no "$n: il modulo vecchio e' rimasto (vedi $D/c-$n.log)"
}
echo "=== C. modulo vecchio, stessa dimensione ==="
if [ -f "$B" ]; then
    export EXOS_ISTANZA=mdc EXOS_NO_FLOPPY=1 EXOS_RAM=128M EXOS_MARCA="utente:"
    giro_c "senza-registro" "rm /boot/netupdate.reg@2"
    giro_c "registro-bugiardo"     # the register now says the right hash
    # hwconfig writes the block of nv50.drv (hwconfig 0.009)
    partizione "$B" "$D/c-p1.img"
    printf 'write build/drivers-cd/nv50.drv /dev/nv50.drv\n' \
        | $SBIN/debugfs -w -f - "$D/c-p1.img" >> "$D/c-debugfs.log" 2>&1
    dd if="$D/c-p1.img" of="$B" bs=512 seek=2048 conv=notrunc status=none
    EXOS_ISTANZA=mdc timeout 300 python3 tools/qemu_drive.py "${L[@]}" \
        "hwconfig@6" "si@6" "sync@3" > "$D/c-nv50.log" 2>&1
    cp /tmp/exos/serialmdc.txt "$D/c-nv50-seriale.txt" 2>/dev/null
    partizione "$B" "$D/c-p1.img"
    $SBIN/debugfs -R "cat /boot/avvio.sh" "$D/c-p1.img" 2>/dev/null | grep -q "^nv50.drv -avvio" \
        && si "hwconfig ha scritto il blocco di nv50.drv" \
        || no "nessun blocco di nv50.drv in avvio.sh (vedi $D/c-nv50-seriale.txt)"
    $SBIN/e2fsck -fn "$D/c-p1.img" > "$D/c-e2fsck.log" 2>&1 \
        && si "e2fsck pulito" || no "e2fsck (vedi $D/c-e2fsck.log)"
fi

echo "=== $ok riusciti, $no no ==="
[ "$no" -eq 0 ]
