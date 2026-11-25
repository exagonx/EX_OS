#!/bin/bash
# =============================================================================
# tools/prova_root_video.sh - an ordinary user changes the resolution from
# Impostazioni > Video, giving the password as with sudo (pm 0.031, exdlg
# ex_dlg_password, 25 November 2026)
#
# ! THE DESKTOP RUNS AS ITS USER since kernel 0.359, and svga.drv writes
# /boot/stage2.bin and /boot/kernel.cfg, which are root's: "1280x1024 non e'
# stato impostato", "accesso negato" (the .253, user graziano).
#
#   1  the disk installed from the CD (tools/mkhd.sh): root/root, mario/mario,
#      mario in /boot/amministratori;
#   2  mario, ExWin, Avvio > Impostazioni..., the row 640x480, Applica: the
#      window "Cambiare la risoluzione" asks mario's password;
#   3  a wrong one: "Password sbagliata", kernel.cfg untouched;
#   4  Applica again, "mario": "Risoluzione impostata su 640x480", and
#      kernel.cfg says svga = 640x480;
#   5  pm is mario again afterwards (no "non riesco a tornare"), no [FAULT].
# Tabs, rows and buttons found by their text (punta:), photographs as PNG.
#
#     bash tools/prova_root_video.sh [dir]              32 bit (dist/exos.iso)
#     EXOS_ARCH=x86_64 bash tools/prova_root_video.sh   64 bit
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$(pwd)
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/rootvideo-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=rv$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 256 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

echo "=== 2-5. mario cambia la risoluzione ==="
T=/tmp/exos/rootvideo-$ARCH
rm -rf "$T"; mkdir -p "$T"
clic() { echo "mon:mouse_button 1; mouse_button 0@${1:-3}"; }
A=("mario@2" "mario@6" "keymap us@3"
   "echo PRIMA; cat /boot/amministratori; grep -a svga /boot/kernel.cfg; echo FINE-PRIMA@3"
   "exwin@25" "key:alt-f5@3"
   "punta:Avvio@1" "$(clic 3)"
   "punta:Impostazioni...@1" "$(clic 4)"
   "punta:640x480@1" "$(clic 2)"
   "punta:Applica|0,0,0,0,60,800,320@1" "$(clic 4)" "foto:$T/1-chiede.ppm@1"
   "sbagliata@6" "foto:$T/2-sbagliata.ppm@1" "key:ret@2"
   "key:alt-f1@3" "echo DOPO-NO; grep -a svga /boot/kernel.cfg; echo FINE-DOPO-NO@3" "key:alt-f5@3"
   "punta:Applica|0,0,0,0,60,800,320@1" "$(clic 4)"
   "mario@8" "foto:$T/3-impostata.ppm@1" "key:ret@2"
   "key:alt-f1@3"
   "echo DOPO-SI; grep -a svga /boot/kernel.cfg; dmesg | grep -a 'tornare'; echo FINE-DOPO-SI@4")
EXOS_QEMU=$Q EXOS_ISTANZA=rv$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$KVM $EXTRA -drive file=$IMG,format=raw,if=ide -nic none" \
    timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
L=$(tr -d '\r' < "$D/qemu.log")

echo "$L" | sed -n '/^PRIMA/,/^FINE-PRIMA/p' | grep -aq '^mario' \
    && si "mario e' in /boot/amministratori" || no "mario non e' in /boot/amministratori"
P0=$(echo "$L" | sed -n '/^PRIMA/,/^FINE-PRIMA/p' | grep -a '^svga' | head -1)
P1=$(echo "$L" | sed -n '/^DOPO-NO/,/^FINE-DOPO-NO/p' | grep -a '^svga' | head -1)
P2=$(echo "$L" | sed -n '/^DOPO-SI/,/^FINE-DOPO-SI/p' | grep -a '^svga' | head -1)
[ "$P1" = "$P0" ] && si "password sbagliata: kernel.cfg come prima (${P0:-senza voce svga})" \
    || no "password sbagliata ma kernel.cfg cambiato: '$P0' -> '$P1'"
echo "$P2" | grep -aq '640x480' && si "password giusta: kernel.cfg dice $P2" \
    || no "password giusta ma kernel.cfg dice '${P2}'"
echo "$L" | sed -n '/^DOPO-SI/,/^FINE-DOPO-SI/p' | grep -aq 'non riesco a tornare' \
    && no "pm non e' tornato all'utente" || si "pm torna a mario dopo svga.drv"

python3 -I - "$T" "$RADICE/tools" <<'PY'
import re, sys
d, attrezzi = sys.argv[1], sys.argv[2]
sys.path.insert(0, attrezzi)
import trova_testo
def c(n, testo):
    b = open("%s/%s.ppm" % (d, n), "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", b)
    return len(trova_testo.trova(int(m.group(1)), int(m.group(2)), b[m.end():], testo, tutti=True))
esito = 0
for n, testo, cosa in (("1-chiede", "Serve root.", "la finestra chiede la password di mario"),
                       ("2-sbagliata", "Password sbagliata", "password sbagliata: lo dice"),
                       ("3-impostata", "Risoluzione impostata", "password giusta: risoluzione impostata")):
    try:
        v = c(n, testo)
    except Exception as e:
        print("  [NO] %s: manca la fotografia (%s)" % (cosa, e)); esito = 1; continue
    print("  [%s] %s" % ("OK" if v else "NO", cosa))
    if not v: esito = 1
sys.exit(esito)
PY
[ $? = 0 ] && ok=$((ok+3)) || no=$((no+1))
grep -a "\[FAULT\]" /tmp/exos/serialrv$ARCH.txt > /dev/null 2>&1 && no "un [FAULT] sulla seriale" || si "nessun [FAULT]"

for p in "$T"/*.ppm; do
    [ -f "$p" ] || continue
    python3 tools/ppm2png.py "$p" "$D/$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"
done
rm -f "$IMG"
echo "  fotografie: $D"
echo "=== $ok riusciti, $no no ($ARCH) ==="
[ $no = 0 ]
