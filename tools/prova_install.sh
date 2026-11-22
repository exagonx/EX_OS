#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_install.sh - `<driver> -install` / `-remove` in QEMU (voce 24)
#
#     tools/prova_install.sh            32 and 64 bit
#     tools/prova_install.sh 64         only one
#
# A disk installed from the CD (tools/mkhd.sh, as prova_cpufreq), then debugfs
# puts in /dev the drivers hwconfig did not copy (no hardware for them in
# QEMU), and the machine boots from the disk with an Intel HDA:
#   - xhci.drv -install writes its block, -remove takes it away;
#   - ehci.drv: the line hwconfig wrote counts (-install says "gia'"),
#     -remove comments it out;
#   - the disk has hwconfig's chain (e1000): pcnet.drv goes BEFORE ip.drv
#     with its marker, e1000.drv says "gia'"; pcnet -remove takes line and
#     marker, e1000 -remove comments hwconfig's line out;
#   - with no chain (an avvio.sh of just `pci.drv &`) ne2k.drv writes a block
#     with card, ip.drv and dhcp, and no second pci.drv;
#   - hdaudio.drv: audio = /dev/hdaudio.drv in [modules] of kernel.cfg;
#     sb.drv -remove leaves it (not its own); hdaudio.drv -remove takes it;
#   - mouseser.drv -install com1, pci.drv -install (says who starts it);
#   - no [FAULT].
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D=/tmp/exos-prove/install; rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {    # $1 = 32|64
    local bit=$1 iso q cpu dr log ser img
    if [ "$bit" = 64 ]; then iso=dist/exos64.iso; q=qemu-system-x86_64; cpu=qemu64; dr=${EXOS_FUORI}/build-64/drivers
    else                     iso=dist/exos.iso;   q=qemu-system-i386;   cpu=pentium3; dr=${EXOS_FUORI}/build/drivers; fi
    log="$D/$bit.log"; ser=/tmp/exos/serialin$bit.txt; rm -f "$ser"
    echo "=== $bit bit ==="
    img="$D/hd$bit.img"
    sed "s|dist/exos.iso|$iso|g; s|/tmp/exos-mkhd.log|$D/mkhd$bit.log|g" tools/mkhd.sh > "$D/mkhd.sh"
    env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
        EXOS_QEMU=$q EXOS_ISTANZA=ins$bit EXOS_SUPPORTO=cd EXOS_HD="$img" \
        bash "$D/mkhd.sh" 256 > "$D/install$bit.log" 2>&1 || {
        no "$bit: l'installazione dal CD non e' riuscita (vedi $D/install$bit.log)"; return; }
    dd if="$img" of="$D/p1.img" bs=512 skip=2048 status=none
    : > "$D/debugfs$bit.cmd"
    for n in xhci pcnet e1000 ne2k hdaudio sb mouseser ehci pci; do
        f=$dr/$n.drv; [ -f "$f" ] || f=${dr}-cd/$n.drv
        $SBIN/debugfs -R "stat /dev/$n.drv" "$D/p1.img" 2>&1 | grep -q "Inode:" && continue
        printf 'write %s /dev/%s.drv\nsif /dev/%s.drv mode 0100755\n' "$f" $n $n >> "$D/debugfs$bit.cmd"
    done
    $SBIN/debugfs -w -f "$D/debugfs$bit.cmd" "$D/p1.img" > "$D/debugfs$bit.log" 2>&1
    dd if="$D/p1.img" of="$img" bs=512 seek=2048 conv=notrunc status=none
    EXOS_MARCA="utente:" EXOS_QEMU=$q EXOS_ISTANZA=in$bit EXOS_NO_FLOPPY=1 EXOS_RAM=512M \
        EXOS_QEMU_EXTRA="-cpu $cpu -drive file=$img,format=raw,if=ide -device intel-hda -device hda-duplex" \
        timeout 600 python3 tools/qemu_drive.py "root@2" "root@6" "keymap us@3" \
        "echo PRIMA@1" "cat /boot/avvio.sh@4" \
        "xhci.drv -install@4" "echo XHCI-SU@1" "cat /boot/avvio.sh@4" \
        "xhci.drv -remove@4" "echo XHCI-GIU@1" "cat /boot/avvio.sh@4" \
        "ehci.drv -install@4" "ehci.drv -remove@4" "echo EHCI-GIU@1" "cat /boot/avvio.sh@4" \
        "pcnet.drv -install@4" "e1000.drv -install@4" "echo RETE-SU@1" "cat /boot/avvio.sh@4" \
        "pcnet.drv -remove@4" "echo PCNET-GIU@1" "cat /boot/avvio.sh@4" \
        "e1000.drv -remove@4" "echo E1000-GIU@1" "cat /boot/avvio.sh@4" \
        "cp /boot/avvio.sh /boot/avvio.sav@3" "echo 'pci.drv &' > /boot/avvio.sh@3" \
        "ne2k.drv -install@4" "echo NUDO@1" "cat /boot/avvio.sh@4" \
        "rm /boot/avvio.sh@2" "cp /boot/avvio.sav /boot/avvio.sh@3" \
        "hdaudio.drv -install@5" "sb.drv -remove@4" "echo AUDIO-SU@1" "cat /boot/kernel.cfg@4" \
        "hdaudio.drv -remove@4" "echo AUDIO-GIU@1" "cat /boot/kernel.cfg@4" \
        "mouseser.drv -install com1@4" "echo MOUSE-SU@1" "cat /boot/avvio.sh@4" \
        "mouseser.drv -remove@4" "pci.drv -install@3" "echo FINE@1" \
        "uname -a@2" "sync@3" > "$log" 2>&1
    L=$(tr -d '\r' < "$log")
    tratto() { echo "$L" | awk -v m="$1" 'index($0,"echo " m) && /ex-os/ {s=1; next} s && /ex-os.*echo [A-Z]/ {exit} s' | grep -av '^$'; }
    tratto XHCI-SU | grep -aqx "xhci.drv &" && si "$bit: xhci -install scrive il blocco" || no "$bit: il blocco di xhci non c'e'"
    tratto XHCI-SU | grep -aqx "# -install xhci.drv" && si "$bit: con l'intestazione che hwconfig riporta avanti" || no "$bit: manca '# -install xhci.drv'"
    tratto XHCI-GIU | grep -aq "xhci.drv" && no "$bit: xhci -remove non l'ha tolto" || si "$bit: xhci -remove lo toglie"
    echo "$L" | grep -aq "ehci.drv: gia' in /boot/avvio.sh (riga [0-9]*" && si "$bit: ehci -install vede la riga di hwconfig" || no "$bit: ehci -install non dice 'gia''"
    tratto EHCI-GIU | grep -aqx "# tolto da \`ehci.drv -remove\`: ehci.drv -avvio &" && si "$bit: ehci -remove commenta la riga di hwconfig" || no "$bit: la riga di ehci non e' commentata"
    tratto RETE-SU | grep -ax -A1 "pcnet.drv &" | grep -aqx "ip.drv &" && si "$bit: pcnet va subito prima di ip.drv" || no "$bit: pcnet non e' prima di ip.drv"
    tratto RETE-SU | grep -aqx "# \`pcnet.drv -remove\` toglie la riga qui sotto." && si "$bit: col suo segno" || no "$bit: manca il segno di pcnet"
    echo "$L" | grep -aq "e1000.drv: gia' in /boot/avvio.sh (riga [0-9]*" && si "$bit: e1000 -install vede la catena di hwconfig" || no "$bit: e1000 -install non dice 'gia''"
    tratto PCNET-GIU | grep -aq "pcnet" && no "$bit: pcnet -remove ha lasciato qualcosa" || si "$bit: pcnet -remove toglie riga e segno"
    tratto PCNET-GIU | grep -aqx "e1000.drv &" && si "$bit: e e1000 resta" || no "$bit: e1000 e' sparito con pcnet"
    tratto E1000-GIU | grep -aqx "# tolto da \`e1000.drv -remove\`: e1000.drv &" && si "$bit: e1000 -remove commenta la riga di hwconfig" || no "$bit: la riga di e1000 non e' commentata"
    tratto NUDO | grep -aqx "# -install ne2k.drv" && si "$bit: senza catena ne2k scrive il blocco" || no "$bit: senza catena niente blocco di ne2k"
    [ "$(tratto NUDO | grep -ax -A1 'ne2k.drv &' | grep -acx 'ip.drv &')" = 1 ] && tratto NUDO | grep -aqx "dhcp" \
        && si "$bit: il blocco ha scheda, ip.drv e dhcp" || no "$bit: il blocco di ne2k non ha la catena"
    [ "$(tratto NUDO | grep -acx 'pci.drv &')" = 1 ] && si "$bit: e non ripete pci.drv" || no "$bit: pci.drv due volte"
    tratto AUDIO-SU | grep -aq "^audio *= /dev/hdaudio.drv" && si "$bit: hdaudio -install scrive audio = in kernel.cfg" || no "$bit: niente audio = in kernel.cfg"
    echo "$L" | grep -aq "sb.drv: non era in /boot/kernel.cfg" && si "$bit: sb -remove lascia stare quello di hdaudio" || no "$bit: sb -remove non dice 'non era'"
    tratto AUDIO-GIU | grep -aq "hdaudio" && no "$bit: hdaudio -remove ha lasciato qualcosa" || si "$bit: hdaudio -remove toglie la voce e il commento"
    tratto MOUSE-SU | grep -aqx "mouseser.drv com1 &" && si "$bit: mouseser -install com1" || no "$bit: niente 'mouseser.drv com1 &'"
    echo "$L" | grep -aq "pci.drv: niente da installare: lo accende /boot/avvio.sh" && si "$bit: pci -install dice chi lo accende" || no "$bit: pci -install non lo dice"
    grep -aq "\[FAULT\]\|PANIC" "$ser" 2>/dev/null && no "$bit: [FAULT] o PANIC sulla seriale ($ser)" || si "$bit: nessun FAULT"
}

case "${1:-}" in
    32) giro 32 ;;
    64) giro 64 ;;
    *)  giro 32; giro 64 ;;
esac
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
