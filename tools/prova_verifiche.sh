#!/bin/bash
# =============================================================================
# tools/prova_verifiche.sh -- netupdate 0.044's checks (le verifiche), in
# QEMU, on a disk installed from the CD (5 November 2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_verifiche.sh                      64 bit
#     EXOS_ARCH=i386 tools/prova_verifiche.sh       32 bit
#
# The repository is dist/netinst_64 (dist/netinst), made with the scripts of
# tools/netinst/verifiche; served by tools/prova_netupdate_http.py through
# QEMU's guestfwd at 10.0.2.100, from a copy made of hard links.
#   A. the repository as it is: -verifica (sistema: a posto), -install:valkey
#      (its check runs), /exos/valkey/dati taken away and made again by
#      -verifica:valkey (exit 2), -noverifica, the log /boot/netupdate.ver;
#   B. versione.txt says another system (0.999): sistema.sh asks for a reboot
#      (exit 1, "C'E' DA FARE A MANO");
#   C. a script changed on the server after verifiche.txt: not run.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$PWD
ARCH="${EXOS_ARCH:-x86_64}"
D="${1:-/tmp/exos-prove/verifiche-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; REPO=dist/netinst_64
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA="";            REPO=dist/netinst
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
grep -q '^verifiche' "$REPO/versione.txt" || { echo "$REPO non ha verifiche: make netinst" >&2; exit 1; }

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=vf$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 256 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

# ! THE SERVED NAME IS THE REPOSITORY'S: netupdate at 64 bits adds _64 to the url
# by itself and asks for /netinst_64 (6/11/2026: 404 on every run).
NOME=$(basename "$REPO")
# the three servers: copies of the repository made of links (no space).
# ! HARD LINKS ONLY ON THE SAME FILESYSTEM: /tmp is a tmpfs here, and on
# 6/11/2026 every `cp -al` failed and the three servers were empty. Symbolic
# links work anywhere; sed -i and rm below replace the link, not the original.
for g in a b c; do
    mkdir -p "$D/srv-$g"
    cp -al "$RADICE/$REPO" "$D/srv-$g/$NOME" 2>/dev/null || {
        rm -rf "$D/srv-$g/$NOME"; cp -as "$RADICE/$REPO" "$D/srv-$g/$NOME"; } || {
        echo "non riesco a copiare $REPO in $D/srv-$g" >&2; exit 1; }
done
# B: another system version, versione.txt still the root of trust
sed -i 's/^versione  = .*/versione  = 0.999/' "$D/srv-b/$NOME/versione.txt"
rm "$D/srv-b/$NOME/versione.txt.tmp" 2>/dev/null
# C: sistema.sh changed after its fingerprint went in verifiche.txt
rm "$D/srv-c/$NOME/verifiche/sistema.sh"
{ cat "$RADICE/$REPO/verifiche/sistema.sh"; echo "echo MODIFICATO-SUL-SERVER"; } > "$D/srv-c/$NOME/verifiche/sistema.sh"

L=("root@2" "root@6" "keymap us@3")
gira() {    # $1 = run, then the commands
    local giro=$1; shift
    printf 'exec python3 %s/tools/prova_netupdate_http.py %s\n' "$RADICE" "$D/srv-$giro" > "$D/http-$giro.sh"
    chmod +x "$D/http-$giro.sh"
    EXOS_QEMU=$Q EXOS_ISTANZA=vf$ARCH$giro EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http-$giro.sh" \
        timeout 1800 python3 tools/qemu_drive.py "${L[@]}" "$@" > "$D/$giro.log" 2>&1
    # ! ONLY THE SERIAL, WITHOUT COLOURS: qemu_drive.py writes every "--- invio"
    # line at the top, not next to the output, so the pieces are cut on the
    # shell prompts ("> command") of the serial (6/11/2026).
    tr -d '\r' < "$D/$giro.log" | sed 's/\x1b\[[0-9;]*m//g' \
        | sed -n '/^=== seriale dal prompt/,$p' > "$D/$giro.txt"
}
CNF=("echo tipo = HTTP > /boot/netupdate.cnf@2" "echo url = 10.0.2.100/netinst >> /boot/netupdate.cnf@2")

echo "=== 2. A: il repository com'e' ==="
gira a "${CNF[@]}" \
    "netupdate -verifica@~120" \
    "netupdate -install:valkey -yes@~400" \
    "rm -r /exos/valkey/dati@5" \
    "netupdate -verifica:valkey@~120" \
    "ls -a /exos/valkey@4" \
    "netupdate -verifica -noverifica@~60" \
    "cat /boot/netupdate.ver@4" "sync@5"
A="$D/a.txt"
sed -n '/> netupdate -verifica$/,/> netupdate -install/p' "$A" > "$D/a1.txt"
grep -aq "^  sistema *a posto" "$D/a1.txt" && si "A: -verifica: sistema a posto" || no "A: -verifica non dice 'sistema a posto' ($D/a1.txt)"
grep -aq "Tutto a posto" "$D/a1.txt" && si "A: -verifica: Tutto a posto" || no "A: -verifica senza 'Tutto a posto'"
grep -aq "valkey.sh" "$D/a1.txt" && no "A: la verifica di valkey e' partita senza valkey installato" || si "A: valkey non installato, la sua verifica non parte"
sed -n '/> netupdate -install:valkey/,/> rm -r/p' "$A" > "$D/a2.txt"
grep -aq -- "-- valkey.sh (valkey)" "$D/a2.txt" && grep -aq "^  valkey *a posto" "$D/a2.txt" \
    && si "A: -install:valkey esegue la sua verifica: a posto" || no "A: dopo -install:valkey niente verifica a posto ($D/a2.txt)"
sed -n '/> netupdate -verifica:valkey/,/> ls/p' "$A" > "$D/a3.txt"
grep -aq "creata /exos/valkey/dati" "$D/a3.txt" && grep -aq "^  valkey *ha sistemato da sola" "$D/a3.txt" \
    && si "A: senza dati/ la verifica la rifa' e dice 'ha sistemato da sola'" || no "A: dati/ non rifatta ($D/a3.txt)"
grep -aq "sistema.sh" "$D/a3.txt" && no "A: -verifica:valkey ha eseguito anche sistema" || si "A: -verifica:valkey esegue solo valkey"
sed -n '/> ls -a \/exos\/valkey/,/> netupdate -verifica -noverifica/p' "$A" | grep -aq "dati" \
    && si "A: /exos/valkey/dati c'e' di nuovo" || no "A: /exos/valkey/dati non c'e'"
sed -n '/> netupdate -verifica -noverifica/,/> cat/p' "$A" > "$D/a4.txt"
grep -aq "^Verifiche" "$D/a4.txt" && no "A: -noverifica le ha eseguite lo stesso" || si "A: -noverifica non esegue niente"
sed -n '/> cat \/boot\/netupdate.ver/,$p' "$A" > "$D/a5.txt"
[ "$(grep -ac "	valkey	valkey.sh	" "$D/a5.txt")" -ge 2 ] && grep -aq "	sistema	sistema.sh	0" "$D/a5.txt" \
    && si "A: /boot/netupdate.ver registra ogni verifica" || no "A: /boot/netupdate.ver incompleto ($D/a5.txt)"

echo "=== 3. B: il server dice un altro sistema ==="
gira b "netupdate -verifica@~120"
grep -aq "Gira il kernel .*, ma il sistema installato e' il 0.999" "$D/b.txt" && grep -aq "^  sistema *C'E' DA FARE A MANO" "$D/b.txt" \
    && si "B: kernel diverso dal sistema: 'riavvia', da fare a mano" || no "B: sistema.sh non chiede il riavvio ($D/b.txt)"
grep -aq "chied[a-z]* qualcosa a te" "$D/b.txt" && si "B: il riassunto lo ripete" || no "B: manca 'chiede qualcosa a te'"

echo "=== 4. C: uno script cambiato sul server ==="
gira c "netupdate -verifica@~120"
grep -aq "MODIFICATO-SUL-SERVER" "$D/c.txt" && no "C: lo script cambiato e' stato eseguito" || si "C: lo script cambiato non gira"
grep -aq "sistema.sh (sistema): non arrivata intera" "$D/c.txt" && si "C: lo dice ('non arrivata intera')" || no "C: non dice niente ($D/c.txt)"

for g in a b c; do
    grep -a "\[FAULT\]\|KERNEL PANIC" /tmp/exos/serialvf$ARCH$g.txt > /dev/null 2>&1 && no "$g: un [FAULT] sulla seriale"
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
