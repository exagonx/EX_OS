#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_permessi.sh - users really apart (voce 111, kernel 0.355)
#
#     bash tools/prova_permessi.sh [dir]                         32 bit
#     EXOS_ARCH=x86_64 bash tools/prova_permessi.sh [dir]        64 bit
#
# ! ON A DISK INSTALLED FROM THE CD: from the CD there is no /root, no /home
# and no /tmp, and nothing can be written.
#
#   1  the disk installed from the CD (tools/mkhd.sh): root/root, mario/mario;
#   2  as installed: /root drwx------, /tmp drwxrwxrwt, /home/mario
#      drwx------ (install 0.013, ext4.mod 0.007);
#   3  permprova (tools/prove/permprova.c) as root, going down to uid 1000:
#      0 holes and 0 "too strict" against what Linux answers;
#   4  netupdate's check (tools/netinst/verifiche/sistema.sh) on a machine
#      left as before: /root 0755 and /tmp 0777 put right by itself (exit 2).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/permessi-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"
    GCC=${EXOS_FUORI}/cross_build/nasi5deb/exos-cross64/bin/x86_64-exos-gcc
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA=""
    GCC=${EXOS_FUORI}/cross_build/nasi5deb/exos-cross/bin/i386-exos-gcc
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -x "$GCC" ] || { echo "manca $GCC (tools/ambiente.sh -per prove)" >&2; exit 1; }
DEBUGFS=$(command -v debugfs || echo /sbin/debugfs)
[ -x "$DEBUGFS" ] || { echo "manca debugfs (e2fsprogs)" >&2; exit 1; }
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

"$GCC" -O2 -Wall -o "$D/permprova" tools/prove/permprova.c || { echo "permprova non compila" >&2; exit 1; }

echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=pm$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 256 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }
si "installato"

# the two files on the installed partition (it starts at sector 2048)
for f in "$D/permprova" tools/netinst/verifiche/sistema.sh; do
    "$DEBUGFS" -w -R "write $f $(basename "$f")" "$IMG?offset=1048576" > /dev/null 2>&1
done

echo "=== 2-4. dal disco ==="
A=("root@2" "root@6" "keymap us@3"
   "echo MODI; ls -l /; ls -l /home; echo FINE-MODI@4"
   "chmod 755 /permprova@2"
   "echo PROVA; /permprova; echo FINE-PROVA \$?@10"
   "rm -r /root/privata /permprova.p /tmp/permprova.d@3"
   "chmod 755 /root@2" "chmod 777 /tmp@2"
   "echo VERIFICA; sh /sistema.sh; echo FINE-VERIFICA \$?@8"
   "echo DOPO; ls -l /; echo FINE-DOPO@4"
   "sync@5")
EXOS_QEMU=$Q EXOS_ISTANZA=pm$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$KVM $EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
L=$(tr -d '\r' < "$D/qemu.log")

M=$(echo "$L" | sed -n '/^MODI/,/^FINE-MODI/p')
echo "$M" | grep -aq "^drwx------ .* root$"        && si "/root e' drwx------"       || no "/root non e' 0700"
echo "$M" | grep -aq "^drwxrwxrwt .* tmp$"         && si "/tmp e' drwxrwxrwt"        || no "/tmp non e' 1777"
echo "$M" | grep -aq "^drwx------ .* mario .* mario$" && si "/home/mario e' drwx------" || no "/home/mario non e' 0700"

P=$(echo "$L" | sed -n '/^PROVA/,/^FINE-PROVA/p')
echo "$P" | grep -a "\[SI\]\|\[NO\]" | sed 's/^/      /'
echo "$P" | grep -aq "permprova: 0 falle, 0 troppo stretti" && echo "$P" | grep -aq "^FINE-PROVA 0" \
    && si "permprova: 0 falle, 0 troppo stretti (uscita 0)" \
    || no "permprova: $(echo "$P" | grep -ao 'permprova: [0-9]* falle, [0-9]* troppo stretti' || echo 'non arrivata in fondo')"

V=$(echo "$L" | sed -n '/^VERIFICA/,/^FINE-VERIFICA/p')
echo "$V" | grep -v "^VERIFICA\|^FINE" | sed 's/^/      /'
echo "$V" | grep -aq "^FINE-VERIFICA 2" && si "la verifica di netupdate ha riparato (uscita 2)" \
    || no "la verifica di netupdate non ha detto 2 ($(echo "$V" | grep -a '^FINE-VERIFICA'))"
A2=$(echo "$L" | sed -n '/^DOPO/,/^FINE-DOPO/p')
echo "$A2" | grep -aq "^drwx------ .* root$" && echo "$A2" | grep -aq "^drwxrwxrwt .* tmp$" \
    && si "dopo la verifica /root 0700 e /tmp 1777" || no "dopo la verifica /root o /tmp non sono tornati"

echo "=== $ok riusciti, $no no ($ARCH) ==="
[ $no -eq 0 ]
