#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_partenza_shell.sh [32|64] - programs start in the user's
# profile, the shell has history and editing in a terminal, the file manager
# opens a shell in the folder shown (the user, 26/11/2026)
#
#     tools/prova_partenza_shell.sh        32 bit (dist/exos.iso)
#     tools/prova_partenza_shell.sh 64     64 bit (dist/exos64.iso)
#
# From the CD with an ext2 disk on /disk (the CD's /tmp is read-only),
# HOME=/disk/casa, ExWin; by name on the screen (punta:):
#   1. Avvio > Strumenti > Terminale: `pwd > menu.txt` (relative: the file
#      lands where the shell started) - must be /disk/casa;
#   2. in that terminal: "cho ok >> st.txt", Home, "e", Enter; Up, Enter;
#      Up, ten times Left, "2", Enter - st.txt must say ok, ok, ok2;
#   3. Avvio > Applicazioni > File manager: /disk/casa on the screen;
#   4. right button on the list: "Apri una shell qui" in the menu;
#   5. into /disk/casa/sotto (double click), File > Apri una shell qui (F10,
#      three times down, Enter): `pwd > /disk/casa/file.txt` must be
#      /disk/casa/sotto.
# The console prints the three files at the end; photographs in
# /tmp/exos-prove/partenza-<bit>.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=32; [ "${1:-}" = 64 ] && BIT=64
if [ $BIT = 64 ]; then
    CD=dist/exos64.iso; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
else
    CD=dist/exos.iso; CPU=""; unset EXOS_QEMU
fi
[ -f "$CD" ] || { echo "manca $CD" >&2; exit 1; }
D=/tmp/exos-prove/partenza-$BIT
rm -rf "$D"; mkdir -p "$D" /tmp/exos
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=partenza$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$PWD/$CD" EXOS_RAM="${EXOS_RAM:-1024M}"
IMG=$D/hd.img
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialpartenza$BIT.txt; rm -f "$SER"

clic()  { echo "mon:mouse_button 1;mouse_button 0@${1:-3}"; }
A=("keymap us@3" "mkfs -t ext2 -L prova hd0p1@4" "si@40" "mount hd0p1 /disk@6"
   "mkdir /disk/casa@2" "mkdir /disk/casa/sotto@2" "export HOME=/disk/casa@1"
   "exwin@14" "key:alt-f5@3"
   "punta:Avvio@1" "$(clic 3)" "punta:Strumenti@1" "$(clic 3)" "punta:Terminale@1" "$(clic 6)"
   "foto:$D/1-terminale.ppm@1"
   "pwd > menu.txt@3"
   "cho ok >> st.txt@n1" "key:home@1" "e@n1" "key:ret@3"
   "key:up@1" "key:ret@3"
   "key:up@1")
for i in 1 2 3 4 5 6 7 8 9 10; do A+=("key:left@0.3"); done
A+=("2@n1" "key:ret@3" "foto:$D/2-cronologia.ppm@1"
    "punta:Avvio@1" "$(clic 3)" "punta:Applicazioni|0,0,0,0,280,230,600@1" "$(clic 3)"
    "punta:File manager@1" "$(clic 6)" "foto:$D/3-filemgr.ppm@1"
    "punta:/disk/casa@1"
    "punta:sotto@1" "mon:mouse_button 2;mouse_button 0@2" "foto:$D/4-destro.ppm@1"
    "punta:Apri una shell qui@1" "key:esc@2"
    "punta:sotto@1" "mon:mouse_button 1;mouse_button 0;mouse_button 1;mouse_button 0@3"
    "foto:$D/5-sotto.ppm@1"
    "key:f10@1" "key:down@0.5" "key:down@0.5" "key:down@0.5" "key:ret@6"
    "foto:$D/6-shell-qui.ppm@1"
    "pwd > /disk/casa/file.txt@3"
    "key:alt-f1@2"
    "echo MENU; cat /disk/casa/menu.txt@2" "echo ST; cat /disk/casa/st.txt@2"
    "echo FILE; cat /disk/casa/file.txt@2" "echo FINE-PARTENZA@2")
timeout 1200 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"
for p in "$D"/*.ppm; do
    [ -f "$p" ] && python3 tools/ppm2png.py "$p" "${p%.ppm}.png" > /dev/null 2>&1 && rm -f "$p"
done

echo "=== partenza, cronologia, shell qui ($BIT bit) ==="
python3 -I - "$D/seriale.txt" "$D/qemu.log" <<'PY'
import sys
s = open(sys.argv[1], "rb").read().decode("latin-1")
q = open(sys.argv[2], "rb").read().decode("latin-1")
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1
def dopo(chiave, fino):
    i = s.rfind("\n" + chiave + "\n")
    if i < 0: return []
    j = s.find(fino, i + len(chiave) + 2)
    return [r.strip() for r in s[i + len(chiave) + 2: j if j > 0 else None].split("\n")
            if r.strip() and not r.strip().startswith(("ex-os", "echo", "["))]
m = dopo("MENU", "ST\n")
ok("il Terminale del menu parte in /disk/casa") if "/disk/casa" in m else no("Terminale del menu: %r" % m)
st = dopo("ST", "FILE\n")
ok("cronologia e modifica: %s" % ", ".join(st)) if st == ["ok", "ok", "ok2"] \
    else no("cronologia e modifica: st.txt = %r (atteso ok, ok, ok2)" % st)
ok("il file manager mostra /disk/casa") if "PUNTA: '/disk/casa' in" in q else no("/disk/casa non e' sullo schermo del file manager")
ok("tasto destro: 'Apri una shell qui'") if "PUNTA: 'Apri una shell qui' in" in q else no("manca 'Apri una shell qui' nel menu del tasto destro")
f = dopo("FILE", "FINE-PARTENZA")
ok("File > Apri una shell qui: la shell in /disk/casa/sotto") if "/disk/casa/sotto" in f \
    else no("File > Apri una shell qui: %r (attesa /disk/casa/sotto)" % f)
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
E=$?
echo "  fotografie: $D"
[ $E = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit) ==="
exit $E
