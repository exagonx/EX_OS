#!/bin/bash
# =============================================================================
# tools/prova_netupdate_app.sh -- netupdate 0.050's archive by application
# (app.txt, voce 107), in QEMU, on a disk installed from the CD (20/11/2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_netupdate_app.sh [dir]                     64 bit
#     EXOS_ARCH=i386 tools/prova_netupdate_app.sh [dir]      32 bit
#
# The repository is dist/netinst_64 (dist/netinst) made by mknetinst with
# app.txt, served by tools/prova_netupdate_http.py through QEMU's guestfwd.
#   A. as it is: -list, -list:<pezzo>, -install:nano (with a menu entry put
#      in its block for the test), then -check -yes: nano has the same build,
#      "non le guardo", nothing to do; the register has nano's build.
#   B. nano with a newer build and one file whose sha256 on the server is
#      wrong: -list says "aggiornabile", -check -yes retries it by itself
#      `tentativi` times and writes /boot/netupdate.err.
#   C. nano wants fmt too (newer build again, right files): -check says what
#      is missing and the failures of B first; -check -yesall installs fmt;
#      /boot/netupdate.err goes; the register has nano's new build.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$PWD
ARCH="${EXOS_ARCH:-x86_64}"
D="${1:-/tmp/exos-prove/netupdate-app-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; REPO=dist/netinst_64
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA="";            REPO=dist/netinst
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
grep -q '^app ' "$REPO/versione.txt" || { echo "$REPO non ha app.txt: make netinst" >&2; exit 1; }
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=na$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 512 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

# ! THE SERVED NAME IS THE REPOSITORY'S: at 64 bits netupdate adds _64 itself
NOME=$(basename "$REPO")
for g in a b c; do
    mkdir -p "$D/srv-$g"
    cp -as "$RADICE/$REPO" "$D/srv-$g/$NOME" || { echo "non copio $REPO" >&2; exit 1; }
done
# a block of app.txt changed: the link replaced by a file, versione.txt along
cambia_app() {   # $1 = server, $2 = python code on `t` (app.txt) and `lst` (app/nano.txt)
    python3 - "$D/srv-$1/$NOME" "$2" <<'PY'
import hashlib, os, re, sys
r, codice = sys.argv[1], sys.argv[2]
def leggi(p): return open(p, encoding="utf-8").read()
def scrivi(p, s):
    os.remove(p); open(p, "w", encoding="utf-8").write(s)
t, lst = leggi(r + "/app.txt"), leggi(r + "/app/nano.txt")
def blocco(t, pid, f):
    i = t.index("[%s]" % pid); j = t.find("\n[", i + 1); j = len(t) if j < 0 else j + 1
    return t[:i] + f(t[i:j]) + t[j:]
exec(codice)
scrivi(r + "/app/nano.txt", lst)
imp = hashlib.sha256(lst.encode()).hexdigest()
t = blocco(t, "nano", lambda b: re.sub(r"(?m)^elimpronta *=.*$", "elimpronta = " + imp, b))
scrivi(r + "/app.txt", t)
v = leggi(r + "/versione.txt")
v = re.sub(r"(?m)^app *=.*$", "app       = " + hashlib.sha256(t.encode()).hexdigest(), v)
scrivi(r + "/versione.txt", v)
PY
}
MENU='t = blocco(t, "nano", lambda b: b.rstrip("\n") + "\nmenu = Strumenti/Nano di prova | /exos/bin/nano | /exwin/icone/testo.ico\ngrafica = si\n")'
cambia_app a "$MENU"
cambia_app b "$MENU
t = blocco(t, 'nano', lambda b: re.sub(r'(?m)^build *=.*$', 'build      = 299901010000', b))
c = lst.splitlines(); i = next(k for k, x in enumerate(c) if x.startswith('exos/bin/nano\t'))
f = c[i].split('\t'); f[2] = '0' * 64; c[i] = '\t'.join(f); lst = '\n'.join(c) + '\n'"
cambia_app c "$MENU
t = blocco(t, 'nano', lambda b: re.sub(r'(?m)^build *=.*$', 'build      = 299902020000\nvuole = fmt', b))"

L=("root@2" "root@6" "keymap us@3")
gira() {    # $1 = run, then the commands
    local giro=$1; shift
    printf 'exec python3 %s/tools/prova_netupdate_http.py %s\n' "$RADICE" "$D/srv-$giro" > "$D/http-$giro.sh"
    chmod +x "$D/http-$giro.sh"
    EXOS_QEMU=$Q EXOS_ISTANZA=na$ARCH$giro EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
    EXOS_QEMU_EXTRA="$EXTRA $KVM -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http-$giro.sh" \
        timeout 1800 python3 tools/qemu_drive.py "${L[@]}" "$@" > "$D/$giro.log" 2>&1
    tr -d '\r' < "$D/$giro.log" | sed 's/\x1b\[[0-9;]*m//g' \
        | sed -n '/^=== seriale dal prompt/,$p' > "$D/$giro.txt"
}
pezzo() {   # $1 = file, $2 = command that starts the piece, $3 = the next one
    local a=${2//\//\\/} z=${3//\//\\/}
    sed -n "/> $a/,/> $z/p" "$1"
}
blocco() {  # $1 = file with `cat /boot/netupdate.reg`, $2 = package: its block
    awk -v b="[$2]" '$0 == b { f = 1; print; next } f && /^\[/ { f = 0 } f' "$1"
}
CNF=("echo tipo = HTTP > /boot/netupdate.cnf@2" "echo url = 10.0.2.100/netinst >> /boot/netupdate.cnf@2")

echo "=== 2. A: il repository com'e' ==="
gira a "${CNF[@]}" "netupdate -version@3" \
    "netupdate -list@~120" "netupdate -list:nan@~120" \
    "netupdate -install:nano -yes@~400" \
    "netupdate -check -yes@~400" "echo SECONDO GIRO@1" "netupdate -check -yes@~400" \
    "cat /boot/netupdate.reg@6" "grep Nano /exwin/lib/applicazioni.txt@3" "sync@5"
A="$D/a.txt"
grep -aqE "netupdate 0\.0(5[0-9]|[6-9][0-9])" "$A" && si "A: netupdate 0.050 o dopo" || no "A: netupdate prima della 0.050 ($A)"
pezzo "$A" "netupdate -list$" "netupdate -list:nan" > "$D/a1.txt"
[ "$(grep -ac '^  [a-z0-9-]* *\(installato\|no\|aggiornabile\) ' "$D/a1.txt")" -ge 20 ] \
    && si "A: -list mostra le applicazioni del server" || no "A: -list ($D/a1.txt)"
grep -aq "^  vlc .* VLC 3\.0\.24" "$D/a1.txt" && si "A: -list dice la versione (VLC 3.0.24)" || no "A: -list senza versione ($D/a1.txt)"
pezzo "$A" "netupdate -list:nan" "netupdate -install" > "$D/a2.txt"
grep -aq "^  nano " "$D/a2.txt" && [ "$(grep -ac '^  [a-z0-9-]* *\(installato\|no\|aggiornabile\) ' "$D/a2.txt")" = 1 ] \
    && si "A: -list:nan mostra solo nano" || no "A: -list:nan ($D/a2.txt)"
# ! THE SECOND -check: since 0.051 sistema is always looked at, and the first
# one brings the files of sistema that a disk installed from the CD lacks
# (help.txt, the drivers...). The second must find nothing, stage2.bin
# included (0.052).
pezzo "$A" "echo SECONDO GIRO" "cat /boot/netupdate.reg" > "$D/a3.txt"
grep -aq "con la stessa versione e build del server" "$D/a3.txt" && si "A: -check: le app con la stessa build non si guardano" \
    || no "A: -check non salta le app uguali ($D/a3.txt)"
grep -aq "Niente da fare" "$D/a3.txt" && si "A: -check: niente da fare" || no "A: -check ha trovato da fare ($D/a3.txt)"
blocco "$A" nano > "$D/a4.txt"
grep -aq "^build *= *2[0-9]\{11\}" "$D/a4.txt" && grep -aq "^versione *= *9\.3" "$D/a4.txt" \
    && si "A: il registro ha versione e build di nano" || no "A: registro senza build ($D/a4.txt)"
grep -aq "Strumenti/Nano di prova" "$D/a.txt" && si "A: -install mette la voce nel menu" || no "A: voce di menu assente"

echo "=== 3. B: una build nuova con un file guasto sul server ==="
gira b "netupdate -list:nano@~120" "netupdate -check -yes@~600" "cat /boot/netupdate.err@3" "sync@5"
B="$D/b.txt"
pezzo "$B" "netupdate -list:nano" "netupdate -check" | grep -aq "^  nano *aggiornabile" \
    && si "B: -list dice nano aggiornabile" || no "B: nano non e' aggiornabile ($B)"
grep -aq "ritento da solo (3 di 3)" "$B" && si "B: ritenta da solo tre volte" || no "B: non ritenta da solo ($B)"
pezzo "$B" "cat /boot/netupdate.err" "sync" | grep -aq "^exos/bin/nano	" \
    && si "B: /boot/netupdate.err nomina exos/bin/nano" || no "B: /boot/netupdate.err ($B)"

echo "=== 4. C: nano vuole fmt ==="
# ! -check WITHOUT -yes ASKS: "no" answers it, or the next command would
gira c "netupdate -check@~300" "no@~30" "no@~10" "netupdate -check -yesall@~900" \
    "netupdate -list:fmt@~120" "cat /boot/netupdate.err@3" "cat /boot/netupdate.reg@6" "echo FINE-C@2" "sync@5"
C="$D/c.txt"
pezzo "$C" "netupdate -check$" "netupdate -check -yesall" > "$D/c1.txt"
# fmt wants cpp, cpp wants base: a disk from the CD has none of them
grep -aq "L'ultima volta non sono arrivati" "$D/c1.txt" && si "C: -check dice prima i file di B" || no "C: non li dice ($D/c1.txt)"
grep -aq "servono anche:.* fmt" "$D/c1.txt" && si "C: -check dice che manca fmt" || no "C: non dice fmt ($D/c1.txt)"
pezzo "$C" "netupdate -check -yesall" "netupdate -list:fmt" | grep -aq "installo fmt" \
    && si "C: -yesall installa fmt" || no "C: -yesall non installa fmt ($C)"
pezzo "$C" "netupdate -list:fmt" "cat /boot" | grep -aq "^  fmt *installato" \
    && si "C: fmt installato" || no "C: fmt non installato ($C)"
pezzo "$C" "cat /boot/netupdate.err" "cat /boot/netupdate.reg" | grep -aq "^exos/bin/nano	" \
    && no "C: /boot/netupdate.err e' rimasto" || si "C: /boot/netupdate.err tolto"
blocco "$C" nano | grep -aq "^build *= *299902020000" \
    && si "C: il registro ha la build nuova di nano" || no "C: build di nano non aggiornata ($C)"

for g in a b c; do
    grep -a "\[FAULT\]\|KERNEL PANIC" /tmp/exos/serialna$ARCH$g.txt > /dev/null 2>&1 && no "$g: un [FAULT] sulla seriale"
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
