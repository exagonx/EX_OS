#!/bin/bash
# =============================================================================
# tools/prova_expackage.sh — netupdate 0.041 and expackage, in QEMU, on a disk
# installed from the CD (3 November 2026)
#
#     tools/prova_expackage.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_expackage.sh [dir]    64 bit
#
# The repository is dist/netinst (dist/netinst_64 at 64 bit), served by
# tools/prova_netupdate_http.py through QEMU's guestfwd at 10.0.2.100.
# Run A, the shell:
#   - -install:list -tab: every package of catalogo.txt, the last line
#     FINE<TAB>n (the catalogue has more than the 32 netupdate 0.040 read);
#   - -install:list -p: stops at a screen, q stops the list;
#   - -install:nasm -yes and -remove:nasm -yes (nasm: 3 MB, no `vuole`), no question asked, the
#     register says so;
#   - -set:http <address>/ writes tipo = HTTP and the address without the
#     slash.
# Run B, the desktop: expackage shows the list; a click and Enter on fb
# mark it and its chain ([d] base: photo b2-dipendenze), a second Enter
# takes the mark away; a click and Enter on nasm mark it, Applica and Procedi
# install it; the register says nasm.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$(pwd)
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/expackage-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; SRV=dist; REPO=netinst
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA="";            SRV=dist; REPO=netinst
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
CAT="$SRV/$REPO$([ "$ARCH" = x86_64 ] && echo _64)/catalogo.txt"
N_CAT=$(grep -c '^\[' "$CAT")
# apache's `vuole` as the catalogue writes them: five at 32 bit (more than the
# four netupdate 0.040 kept), four at 64
V_APACHE=$(awk '/^\[/{f=($0=="[apache]")} f&&/^vuole/{sub(/^vuole *= */,""); printf "%s%s", n++?",":"", $0}' "$CAT")

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
pulito() { tr -d '\r' < "$1"; }

echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
# ! THE DISK GOES IN $D THROUGH EXOS_HD (tools/mkhd.sh reads it since 30
# October 2026); the old `IMG=` substitution no longer matched, and the
# install landed on dischi/hd.img (seen on 3 November 2026).
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=xp$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 256 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

L=("root@2" "root@6" "keymap us@3")
gira() {    # $1 = run, then the commands
    local giro=$1; shift
    printf 'exec python3 %s/tools/prova_netupdate_http.py %s\n' "$RADICE" "$RADICE/$SRV" > "$D/http-$giro.sh"
    chmod +x "$D/http-$giro.sh"
    EXOS_QEMU=$Q EXOS_ISTANZA=xp$ARCH$giro EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http-$giro.sh" \
        timeout 1800 python3 tools/qemu_drive.py "${L[@]}" "$@" > "$D/$giro.log" 2>&1
}

echo "=== 2. A: netupdate dalla shell ==="
gira a \
    "echo tipo = HTTP > /boot/netupdate.cnf@2" \
    "echo url = 10.0.2.100/netinst >> /boot/netupdate.cnf@2" \
    "netupdate -install:list -tab@~120" \
    "netupdate -install:list -p@~60" "@3" "q@~10" \
    "netupdate -install:nasm -yes@~300" \
    "netupdate -registro@~10" \
    "netupdate -remove:nasm -yes@~60" \
    "netupdate -registro@~10" \
    "netupdate -set:http 10.0.2.100/netinst/@~10" \
    "cat /boot/netupdate.cnf@3" "sync@5"
A=$(pulito "$D/a.log")
echo "$A" | grep -aq "FINE.$N_CAT" \
    && si "A: -tab da' tutti i $N_CAT pacchetti del catalogo (FINE $N_CAT)" \
    || no "A: -tab non arriva a FINE $N_CAT (vedi $D/a.log)"
echo "$A" | grep -aq "PKG.exilla" && si "A: exilla, l'ultimo del catalogo, c'e'" || no "A: exilla non c'e' nell'elenco"
echo "$A" | grep -aq "PKG.apache.*	$V_APACHE	" \
    && si "A: apache ha tutte le sue vuole ($V_APACHE)" || no "A: apache non ha le vuole del catalogo ($V_APACHE)"
echo "$A" | grep -aq "piu' di 32 pacchetti" && no "A: dice ancora «piu' di 32 pacchetti»" \
    || si "A: niente «piu' di 32 pacchetti»"
echo "$A" | grep -aq -- "-- Invio per continuare, q per smettere --" \
    && si "A: -p si ferma a schermo pieno" || no "A: -p non si ferma"
echo "$A" | grep -aq "Procedo?" && no "A: con -yes ha chiesto «Procedo?»" \
    || si "A: con -yes non chiede"
# the first -registro (after the install) names nasm, the second one not
R1=$(echo "$A" | sed -n '/invio .netupdate -registro/,/invio .netupdate -remove/p')
R2=$(echo "$A" | sed -n '/invio .netupdate -remove/,/invio .netupdate -set/p' | sed -n '/Installato su questa/,$p')
echo "$R1" | grep -aq "^  nasm " && si "A: dopo -install:nasm il registro nomina nasm" \
    || no "A: il registro non nomina nasm dopo l'installazione"
echo "$R2" | grep -aq "^  nasm " && no "A: dopo -remove:nasm il registro nomina ancora nasm" \
    || si "A: dopo -remove:nasm il registro non lo nomina piu'"
echo "$A" | grep -aq "Tolgo nasm" && si "A: -remove:nasm -yes l'ha tolto" || no "A: -remove:nasm non e' partito"
echo "$A" | grep -aq "url *= 10.0.2.100/netinst$" && echo "$A" | grep -aq "tipo *= HTTP" \
    && si "A: -set:http scrive tipo HTTP e l'indirizzo senza la barra" \
    || no "A: -set:http non ha scritto quel che doveva"

echo "=== 3. B: expackage sulla scrivania ==="
gira b \
    "exwin@25" "key:alt-f1@3" \
    "/exwin/bin/expackage &@25" "key:alt-f5@4" "foto:$D/b1-elenco.ppm@1" \
    "punta:FreeBASIC|-200,0@1" "mon:mouse_button 1; mouse_button 0@2" "key:ret@3" \
    "foto:$D/b2-dipendenze.ppm@1" "key:ret@3" \
    "punta:nasm@1" "mon:mouse_button 1; mouse_button 0@2" "key:ret@3" \
    "foto:$D/b2-segnato.ppm@1" \
    "punta:Applica@1" "mon:mouse_button 1; mouse_button 0@3" "foto:$D/b3-conferma.ppm@1" \
    "punta:Procedi@1" "mon:mouse_button 1; mouse_button 0@90" "foto:$D/b4-fatto.ppm@1" \
    "key:alt-f1@3" "netupdate -registro@~10" "sync@5"
B=$(pulito "$D/b.log")
for f in b1-elenco b2-dipendenze b2-segnato b3-conferma b4-fatto; do
    [ -s "$D/$f.ppm" ] && command -v convert > /dev/null && convert "$D/$f.ppm" "$D/$f.png" 2> /dev/null
done
echo "$B" | grep -aq "VAI\|punta" > /dev/null
echo "$B" | sed -n '/invio .netupdate -registro/,$p' | grep -aq "^  nasm " \
    && si "B: expackage ha installato nasm (il registro lo nomina)" \
    || no "B: dopo expackage il registro non nomina nasm (fotografie in $D)"
grep -a "\[FAULT\]" /tmp/exos/serialxp${ARCH}b.txt > /dev/null 2>&1 && no "B: un [FAULT] sulla seriale" \
    || si "B: nessun [FAULT]"

echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
