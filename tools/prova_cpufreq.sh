#!/bin/bash
# =============================================================================
# tools/prova_cpufreq.sh - kernel 0.311 and /dev/cpufreq.drv in QEMU (voce 78)
#
#     tools/prova_cpufreq.sh            32 and 64 bit
#     tools/prova_cpufreq.sh 64         only one
#
# ! QEMU HAS NO P-STATES, so this is the proof that nothing breaks, not that
# the frequency changes: that one is on the laptop (AMD family 11h).
# Each architecture boots twice:
#   v: from the CD, the default CPU, with the hypervisor bit: the kernel must
#      say "macchina virtuale" and touch nothing; /boot is read-only there,
#      so -install must say errno 30;
#   n: from a disk installed from the CD (tools/mkhd.sh, as prova_expackage),
#      the same CPU WITHOUT the hypervisor bit: the code goes on to CPUID
#      8000_0007 and must say "non dichiara P-state" (no HwPstate, no EIST).
#      ! hwconfig copies a driver to /dev only if `<driver> -i` says it
#      serves here, so the installed disk must NOT have cpufreq.drv: that is
#      checked first, then debugfs puts it there (as prova_modulo_disco does
#      with nv50.drv) and -install writes the block, -remove takes it away.
# In both: `cpufreq.drv` describes, `-auto` is accepted without an error and
# without control, an unknown option is refused, -guarda runs, no [FAULT].
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D=/tmp/exos-prove/cpufreq; rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {    # $1 = 32|64, $2 = v|n
    local bit=$1 v=$2 iso q cpu log ser img L atteso drv
    local supporto entra
    if [ "$bit" = 64 ]; then iso=dist/exos64.iso; q=qemu-system-x86_64; cpu=qemu64; drv=build-64/drivers/cpufreq.drv
    else                     iso=dist/exos.iso;   q=qemu-system-i386;   cpu=pentium3; drv=build/drivers-cd/cpufreq.drv; fi
    [ "$v" = n ] && cpu="$cpu,-hypervisor"
    log="$D/$bit$v.log"; ser=/tmp/exos/serialcf$bit$v.txt; rm -f "$ser"
    echo "=== $bit bit, -cpu $cpu ==="
    supporto=(EXOS_CDROM=$iso); entra=("root@4" "root@6")
    if [ "$v" = n ]; then
        img="$D/hd$bit.img"
        sed "s|dist/exos.iso|$iso|g; s|/tmp/exos-mkhd.log|$D/mkhd$bit.log|g" tools/mkhd.sh > "$D/mkhd.sh"
        env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
            EXOS_QEMU=$q EXOS_ISTANZA=cfi$bit EXOS_SUPPORTO=cd EXOS_HD="$img" \
            bash "$D/mkhd.sh" 256 > "$D/install$bit.log" 2>&1 || {
            no "$bit$v: l'installazione dal CD non e' riuscita (vedi $D/install$bit.log)"; return; }
        dd if="$img" of="$D/p1.img" bs=512 skip=2048 status=none
        $SBIN/debugfs -R "stat /dev/cpufreq.drv" "$D/p1.img" 2>&1 \
            | grep -q "Inode:" && no "$bit$v: hwconfig ha copiato cpufreq.drv dove non serve" \
            || si "$bit$v: hwconfig non l'ha copiato (-i dice che qui non serve)"
        printf 'write %s /dev/cpufreq.drv\nsif /dev/cpufreq.drv mode 0100755\n' "$drv" \
            | $SBIN/debugfs -w -f - "$D/p1.img" > "$D/debugfs$bit.log" 2>&1
        dd if="$D/p1.img" of="$img" bs=512 seek=2048 conv=notrunc status=none
        supporto=(EXOS_MARCA="utente:")
        entra=("root@2" "root@6" "keymap us@3")
        cpu="$cpu -drive file=$img,format=raw,if=ide"
    fi
    env "${supporto[@]}" EXOS_QEMU=$q EXOS_ISTANZA=cf$bit$v EXOS_NO_FLOPPY=1 EXOS_RAM=512M \
        EXOS_QEMU_EXTRA="-cpu $cpu" \
        timeout 600 python3 tools/qemu_drive.py "${entra[@]}" \
        "dmesg | grep CPUFREQ@4" \
        "cpufreq.drv@4" \
        "cpufreq.drv -auto@4" \
        "cpufreq.drv -xyz@3" \
        "cpufreq.drv -guarda 2@5" \
        "cpufreq.drv -install@4" "cat /boot/avvio.sh@4" \
        "cpufreq.drv -remove@4" "cat /boot/avvio.sh@4" \
        "uname -a@2" "sync@3" > "$log" 2>&1
    L=$(tr -d '\r' < "$log")
    if [ "$v" = v ]; then atteso="macchina virtuale"; else atteso="non dichiara P-state\|non ha il CPUID esteso"; fi   # pentium3: no 8000_0007
    echo "$L" | grep -aq "CPUFREQ: nessun controllo" && si "$bit$v: dmesg dice che non c'e' controllo" \
        || no "$bit$v: nessuna riga CPUFREQ in dmesg (vedi $log)"
    echo "$L" | grep -aq "$atteso" && si "$bit$v: il motivo e' «$atteso»" || no "$bit$v: manca «$atteso»"
    echo "$L" | grep -aq "EX-OS .* 0.311" && si "$bit$v: kernel 0.311" || no "$bit$v: il kernel non e' 0.311"
    echo "$L" | grep -aq "il kernel dice di no\|non ha SYS_CPUFREQ" && no "$bit$v: -auto rifiutato con un errore" \
        || si "$bit$v: -auto senza errore"
    echo "$L" | grep -aq "opzione sconosciuta '-xyz'" && si "$bit$v: opzione sconosciuta rifiutata" || no "$bit$v: -xyz non rifiutata"
    if [ "$v" = n ]; then
        echo "$L" | grep -aq "^cpufreq.drv -auto$" && si "$bit$v: -install scrive il blocco" || no "$bit$v: il blocco non c'e' in avvio.sh"
        [ "$(echo "$L" | grep -ac '^cpufreq.drv -auto$')" = 1 ] && si "$bit$v: -remove lo toglie" || no "$bit$v: -remove non l'ha tolto"
    else
        echo "$L" | grep -aq "non riesco a scrivere /boot/avvio.sh (errno 30)" \
            && si "$bit$v: dal CD -install dice che /boot e' in sola lettura" || no "$bit$v: dal CD -install non dice errno 30"
    fi
    grep -aq "\[FAULT\]\|PANIC" "$ser" 2>/dev/null && no "$bit$v: [FAULT] o PANIC sulla seriale ($ser)" || si "$bit$v: nessun FAULT"
}

case "${1:-}" in
    32) giro 32 v; giro 32 n ;;
    64) giro 64 v; giro 64 n ;;
    *)  giro 32 v; giro 32 n; giro 64 v; giro 64 n ;;
esac
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
