#!/bin/bash
# =============================================================================
# tools/prova_sorgenti.sh — netupdate -install:sourcecode (netupdate 0.032),
# in QEMU, on a disk installed from the CD
#
#     tools/prova_sorgenti.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_sorgenti.sh [dir]    64 bit
#
# tools/mknetinstsrc.sh makes two source repositories: one whole archive and
# one in 4 MB pieces. tools/prova_netupdate_http.py serves them through
# QEMU's guestfwd at 10.0.2.100/netinst_src (netupdate.cnf says
# url = 10.0.2.100/netinst, and netupdate puts "_src" in place of the
# architecture's tail by itself).
# Run A: -install:sourcecode puts the tree in /src/exos-<version>; a second
#        call says it is there already; -remove:sourcecode says how.
#        Outside: the tree is git archive's byte for byte, scripts 0755,
#        nothing left in /src but the tree, e2fsck clean.
# Run B: the tree removed, the archive in pieces: put back the same.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$(pwd)
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/sorgenti-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
pulito() { tr -d '\r' < "$1"; }
partizione() { dd if="$1" of="$2" bs=512 skip=2048 status=none; }

# --- the two repositories ------------------------------------------------------
echo "=== 0. i repository dei sorgenti ==="
SRC_FUORI="$D/srv-a/netinst_src" tools/mknetinstsrc.sh > "$D/mknetinstsrc-a.log" || exit 1
SRC_FUORI="$D/srv-b/netinst_src" SRC_PEZZO=4194304 tools/mknetinstsrc.sh > "$D/mknetinstsrc-b.log" || exit 1
VER=$(sed -n 's/^versione *= *//p' "$D/srv-a/netinst_src/versione.txt")
NOME="exos-$VER"
N_PEZZI=$(ls "$D/srv-b/netinst_src/file/src/" | grep -c '\.p[0-9][0-9][0-9]$')
[ "$N_PEZZI" -ge 2 ] && si "l'albero B e' a pezzi: $N_PEZZI" || no "l'albero B non e' a pezzi"
mkdir -p "$D/atteso"
tar -xzf "$D/srv-a/netinst_src/file/src/$NOME.tar.gz" -C "$D/atteso"

# --- the disk ------------------------------------------------------------------
echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|^IMG=dischi/hd.img|IMG=$IMG|; s|^MB=\"\${1:-512}\"|MB=\"\${1:-256}\"|; \
     s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=src$ARCH EXOS_SUPPORTO=cd bash "$D/mkhd.sh" > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

# The installed disk has the Italian keyboard: qemu_drive types US keys.
L=("root@2" "root@6" "keymap us@3")
gira() {    # $1 = run, $2 = served directory, then the commands
    local giro=$1 srv=$2; shift 2
    printf 'exec python3 %s/tools/prova_netupdate_http.py %s\n' "$RADICE" "$srv" > "$D/http-$giro.sh"
    chmod +x "$D/http-$giro.sh"
    EXOS_QEMU=$Q EXOS_ISTANZA=src$ARCH$giro EXOS_NO_FLOPPY=1 EXOS_RAM=256M EXOS_MARCA="utente:" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http-$giro.sh" \
        timeout 1800 python3 tools/qemu_drive.py "${L[@]}" "$@" > "$D/$giro.log" 2>&1
}
# Outside: the tree on the disk against git archive's.
confronta() {   # $1 = run
    partizione "$IMG" "$D/p1-$1.img"
    rm -rf "$D/fuori-$1"; mkdir -p "$D/fuori-$1"
    $SBIN/debugfs -R "rdump /src/$NOME $D/fuori-$1" "$D/p1-$1.img" > /dev/null 2>&1
    diff -r "$D/atteso/$NOME" "$D/fuori-$1/$NOME" > "$D/diff-$1.txt" 2>&1 \
        && si "$1: /src/$NOME e' l'albero di git archive, byte per byte ($(find "$D/fuori-$1/$NOME" -type f | wc -l) file)" \
        || no "$1: l'albero sul disco e' diverso (vedi $D/diff-$1.txt)"
    $SBIN/debugfs -R "stat /src/$NOME/tools/mkiso64.sh" "$D/p1-$1.img" 2>/dev/null | grep -q "Mode:  0755" \
        && si "$1: gli script sono eseguibili (tools/mkiso64.sh 0755)" \
        || no "$1: tools/mkiso64.sh non e' 0755"
    $SBIN/debugfs -R "stat /src/$NOME/Makefile" "$D/p1-$1.img" 2>/dev/null | grep -q "Mode:  0644" \
        && si "$1: gli altri restano 0644 (Makefile)" || no "$1: Makefile non e' 0644"
    [ "$($SBIN/debugfs -R "ls /src" "$D/p1-$1.img" 2>/dev/null | tr -s ' \n' '\n\n' | grep -v '^$' \
          | grep -vE '^\(|^\.$|^\.\.$|^[0-9]+$' | sort -u)" = "$NOME" ] \
        && si "$1: in /src c'e' solo $NOME (niente archivio, niente appoggio)" \
        || no "$1: in /src e' rimasto altro: $($SBIN/debugfs -R "ls /src" "$D/p1-$1.img" 2>/dev/null | tr '\n' ' ')"
    $SBIN/e2fsck -fn "$D/p1-$1.img" > "$D/e2fsck-$1.log" 2>&1 \
        && si "$1: e2fsck pulito" || no "$1: e2fsck (vedi $D/e2fsck-$1.log)"
}

echo "=== 2. A: l'archivio intero ==="
gira a "$D/srv-a" \
    "echo tipo = HTTP > /boot/netupdate.cnf@2" \
    "echo url = 10.0.2.100/netinst >> /boot/netupdate.cnf@2" \
    "netupdate -install:sourcecode@15" "si@~900" \
    "netupdate -install:sourcecode@15" \
    "netupdate -remove:sourcecode@3" "sync@5"
pulito "$D/a.log" | grep -q "I sorgenti di EX-OS $VER sono in /src/$NOME/" \
    && si "A: netupdate dice che i sorgenti sono in /src/$NOME" \
    || no "A: -install:sourcecode non e' arrivato in fondo (vedi $D/a.log)"
pulito "$D/a.log" | grep -q "Guardo http://10.0.2.100/netinst_src" \
    && si "A: l'indirizzo dei sorgenti e' .../netinst_src" || no "A: l'indirizzo non e' .../netinst_src"
pulito "$D/a.log" | grep -q "Ci sono gia': /src/$NOME" \
    && si "A: la seconda volta dice che ci sono gia'" || no "A: la seconda volta non dice «ci sono gia'»"
pulito "$D/a.log" | grep -q "i sorgenti non sono nel registro" \
    && si "A: -remove:sourcecode dice come toglierli" || no "A: -remove:sourcecode non risponde"
confronta A

echo "=== 3. B: tolti, e ripresi a pezzi da 4 MB ==="
gira b "$D/srv-b" \
    "rm -r /src/$NOME@~300" "ls /src@3" \
    "netupdate -install:sourcecode@15" "si@~900" "sync@5"
pulito "$D/b.log" | grep -q "pezzo $N_PEZZI di $N_PEZZI" \
    && si "B: arrivati $N_PEZZI pezzi" || no "B: i pezzi non sono arrivati tutti (vedi $D/b.log)"
pulito "$D/b.log" | grep -q "I sorgenti di EX-OS $VER sono in /src/$NOME/" \
    && si "B: netupdate dice che i sorgenti sono in /src/$NOME" \
    || no "B: -install:sourcecode non e' arrivato in fondo (vedi $D/b.log)"
confronta B

echo "=== $ok riusciti, $no no ==="
[ "$no" -eq 0 ]
