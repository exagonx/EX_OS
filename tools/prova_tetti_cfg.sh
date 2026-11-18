#!/bin/bash
# =============================================================================
# tools/prova_tetti_cfg.sh - kernel.cfg without ceilings (voce 35, kernel 0.347)
#
#     tools/prova_tetti_cfg.sh          32 and 64 bit
#     tools/prova_tetti_cfg.sh 64       only one
#
# A disk installed from the CD (tools/mkhd.sh, as prova_install.sh); debugfs
# takes its /boot/kernel.cfg and gives it back with, at the end, 9 KB of
# comments, a [modules] of 20 drivers that do not exist and an [env] of 40
# variables, the last one TETTO_ULTIMA. Up to 0.346 the file was read in
# 8 KB (the end was lost, with an ERROR), [modules] kept 16 and [env] 32.
# Then the machine boots from the disk:
#   - dmesg has no "LETTO SOLO L'INIZIO";
#   - the 20th fake driver was tried (its name in dmesg);
#   - env[] in dmesg reaches TETTO_ULTIMA, and so does /bin/env of a shell
#     if the system exports [env] to it;
#   - the shell works (the real modules before the fake ones started);
#   - no [FAULT] or PANIC.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D=/tmp/exos-prove/tetticfg; rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {    # $1 = 32|64
    local bit=$1 iso q cpu log ser img i
    if [ "$bit" = 64 ]; then iso=dist/exos64.iso; q=qemu-system-x86_64; cpu=qemu64
    else                     iso=dist/exos.iso;   q=qemu-system-i386;   cpu=pentium3; fi
    log="$D/$bit.log"; ser=/tmp/exos/serialtc$bit.txt; rm -f "$ser"
    echo "=== $bit bit ==="
    img="$D/hd$bit.img"
    sed "s|dist/exos.iso|$iso|g; s|/tmp/exos-mkhd.log|$D/mkhd$bit.log|g" tools/mkhd.sh > "$D/mkhd.sh"
    env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
        EXOS_QEMU=$q EXOS_ISTANZA=tci$bit EXOS_SUPPORTO=cd EXOS_HD="$img" \
        bash "$D/mkhd.sh" 256 > "$D/install$bit.log" 2>&1 || {
        no "$bit: l'installazione dal CD non e' riuscita (vedi $D/install$bit.log)"; return; }
    dd if="$img" of="$D/p1.img" bs=512 skip=2048 status=none
    $SBIN/debugfs -R "dump /boot/kernel.cfg $D/kernel$bit.cfg" "$D/p1.img" > /dev/null 2>&1
    [ -s "$D/kernel$bit.cfg" ] || { no "$bit: /boot/kernel.cfg non c'e' sul disco"; return; }
    {
        cat "$D/kernel$bit.cfg"
        echo
        for i in $(seq 1 120); do
            echo "# riempitivo della prova dei tetti (voce 35), riga $i di 120: oltre gli 8 KB di prima"
        done
        echo "[modules]"
        for i in $(seq -w 1 20); do echo "tettofinto$i = /dev/tettofinto$i.drv"; done
        echo "[env]"
        for i in $(seq -w 1 39); do echo "TETTO_$i = valore$i"; done
        echo "TETTO_ULTIMA = si"
    } > "$D/kernel$bit.nuovo"
    echo "    kernel.cfg di prova: $(wc -c < "$D/kernel$bit.nuovo") byte"
    printf 'rm /boot/kernel.cfg\nwrite %s /boot/kernel.cfg\n' "$D/kernel$bit.nuovo" > "$D/debugfs$bit.cmd"
    $SBIN/debugfs -w -f "$D/debugfs$bit.cmd" "$D/p1.img" > "$D/debugfs$bit.log" 2>&1
    dd if="$D/p1.img" of="$img" bs=512 seek=2048 conv=notrunc status=none
    EXOS_MARCA="utente:" EXOS_QEMU=$q EXOS_ISTANZA=tc$bit EXOS_NO_FLOPPY=1 EXOS_RAM=512M \
        EXOS_QEMU_EXTRA="-cpu $cpu -drive file=$img,format=raw,if=ide" \
        timeout 600 python3 tools/qemu_drive.py "root@2" "root@6" "keymap us@3" \
        "uname -a@2" "dmesg > /tmp/dm.txt@4" \
        "echo CFG-INIZIO@1" "grep -c \"LETTO SOLO\" /tmp/dm.txt@3" \
        "grep -c tettofinto20 /tmp/dm.txt@3" "grep \"TETTO_ULTIMA\" /tmp/dm.txt@3" \
        "echo CFG-FINE@1" "sync@3" > "$log" 2>&1
    L=$(tr -d '\r' < "$log")
    echo "$L" | grep -aq "EX-OS .*0\.34[7-9]" && si "$bit: gira il kernel 0.347" || no "$bit: non gira la 0.347 (uname)"
    tratto() { echo "$L" | awk 'index($0,"echo CFG-INIZIO") && /ex-os/ {s=1; next} s && /echo CFG-FINE/ {exit} s'; }
    tratto | grep -a -A1 "grep -c \"LETTO SOLO\"" | tail -1 | grep -aqx "0" \
        && si "$bit: kernel.cfg letto tutto (niente LETTO SOLO L'INIZIO)" || no "$bit: kernel.cfg letto a meta'"
    tratto | grep -a -A1 "grep -c tettofinto20" | tail -1 | grep -aq "^[1-9]" \
        && si "$bit: il ventesimo modulo in piu' e' stato tentato" || no "$bit: [modules] tagliato"
    tratto | grep -aq "env\[[0-9]*\]: TETTO_ULTIMA = si" \
        && si "$bit: [env] arriva alla quarantesima variabile in piu'" || no "$bit: [env] tagliato"
    grep -aq "\[FAULT\]\|PANIC" "$ser" 2>/dev/null && no "$bit: [FAULT] o PANIC sulla seriale ($ser)" || si "$bit: nessun FAULT"
}

case "${1:-}" in
    32) giro 32 ;;
    64) giro 64 ;;
    *)  giro 32; giro 64 ;;
esac
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
