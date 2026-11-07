#!/bin/bash
# =============================================================================
# tools/prova_menu_netupdate.sh - a new published menu keeps the user's entries
# (voce 80, netupdate 0.046, menu_ricupera), in QEMU
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_menu_netupdate.sh                  64 bit
#     EXOS_ARCH=i386 tools/prova_menu_netupdate.sh   32 bit
#
# What happens on a machine installed before the menu in groups:
#   A  a disk installed from the CD; netupdate -check -yes from the
#      repository as it is (dist/netinst<_64>): the register is written;
#   -  on the disk, the menu of before (the everyday programs at the top,
#      outside the groups) with what a user adds: "Mio orologio" (a program
#      that is on the disk), "Sparito" (one that is not), "@avvio";
#   B  netupdate -check -yes from a copy of the repository whose published
#      menu is newer (one line more, so another hash: what the server
#      publishes the day the menu changes).
# Then the menu on the disk must have:
#   1. the groups ("Applicazioni/File manager"), and no "File manager |"
#      outside them;
#   2. "Mio orologio" again, and "nel menu di avvio, come prima" said;
#   3. no "Sparito" (its program is not on the disk);
#   4. "@avvio /exwin/bin/orologio" again;
#   5. the check of the system (verifiche/sistema.sh): "sistema  a posto".
# Served at 10.0.2.100 by tools/prova_netupdate_http.py, as in
# tools/prova_verifiche.sh: A at /netinst, B at /nuovo/netinst.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$PWD
ARCH="${EXOS_ARCH:-x86_64}"
D="${1:-/tmp/exos-prove/menu-netupdate-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; REPO=dist/netinst_64
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA="";            REPO=dist/netinst
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
cmp -s "$REPO/file/exwin/lib/applicazioni.txt" exwin/lib/applicazioni.txt ||
    { echo "$REPO non ha il menu nuovo: make netinst / make ARCH=x86_64 netinst64" >&2; exit 1; }
[ -w /dev/kvm ] && EXTRA="$EXTRA -enable-kvm"

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

echo "=== disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=mn$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 512 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

# --- the two servers ------------------------------------------------------------
NOME=$(basename "$REPO")
mkdir -p "$D/srv/nuovo"
# ! LINKS, AND SYMBOLIC: /tmp is another filesystem, hard links fail there,
# and a failed cp -al leaves half a directory that cp -a then fills one
# level down (as tools/prova_verifiche.sh: rm, then cp -as)
for dove in "$D/srv/$NOME" "$D/srv/nuovo/$NOME"; do
    rm -rf "$dove"; cp -as "$RADICE/$REPO" "$dove" || exit 1
done
# B: the published menu one line longer, its line in elenco.txt and the hash
# of elenco.txt in versione.txt rewritten (new files, not the hard links)
python3 -I - "$D/srv/nuovo/$NOME" <<'PY' || exit 1
import hashlib, os, sys
r = sys.argv[1]
def nuovo(p, dati):
    os.remove(p); open(p, "wb").write(dati)
m = os.path.join(r, "file/exwin/lib/applicazioni.txt")
dati = open(m, "rb").read() + b"# (giro B della prova: il menu pubblicato e' cambiato)\n"
nuovo(m, dati)
sha = hashlib.sha256(dati).hexdigest()
e = os.path.join(r, "elenco.txt"); righe = []
for l in open(e, "rb").read().split(b"\n"):
    c = l.split(b"\t")
    if c[0] == b"exwin/lib/applicazioni.txt":
        c[1] = str(len(dati)).encode(); c[2] = sha.encode(); l = b"\t".join(c)
    righe.append(l)
el = b"\n".join(righe); nuovo(e, el)
v = os.path.join(r, "versione.txt"); out = []
for l in open(v, "rb").read().split(b"\n"):
    if l.startswith(b"elenco "): l = b"elenco    = " + hashlib.sha256(el).hexdigest().encode()
    out.append(l)
nuovo(v, b"\n".join(out))
PY
# the menu of before, with a user's entries (what the disk had on 6/11/2026)
{
    grep -v "^#" "$RADICE/exwin/lib/applicazioni.txt" | grep -v "^$" | grep -v "^Applicazioni/\|^Multimedia/"
    echo "File manager           | /exwin/bin/filemgr   | /exwin/icon/baseapp/filemgr_64.ico"
    echo "Archivi                | /exwin/bin/archivi   | /exwin/icon/baseapp/archivi_64.ico"
    echo "Calctor                | /exwin/bin/calctor   | /exwin/icon/baseapp/calctor_64.ico"
    echo "Lettore audio          | /exwin/bin/explayer  | /exwin/icon/baseapp/explayer_64.ico"
    echo "Volume                 | /exwin/bin/exvolume  | /exwin/icon/baseapp/explayer_64.ico"
    echo "Mio orologio | /exwin/bin/orologio"
    echo "Sparito | /exwin/bin/nonesiste"
    echo "@avvio /exwin/bin/orologio"
} > "$D/srv/vecchio.txt"
printf 'exec python3 %s/tools/prova_netupdate_http.py %s\n' "$RADICE" "$D/srv" > "$D/http.sh"
chmod +x "$D/http.sh"

EXOS_QEMU=$Q EXOS_ISTANZA=mn$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http.sh" \
    timeout 3600 python3 tools/qemu_drive.py "root@2" "root@6" "keymap us@3" \
    "echo tipo = HTTP > /boot/netupdate.cnf@2" "echo url = 10.0.2.100/netinst >> /boot/netupdate.cnf@2" \
    "netupdate -check -yes -noverifica@~1500" \
    "scarica http://10.0.2.100/vecchio.txt /exwin/lib/applicazioni.txt@~30" \
    "echo tipo = HTTP > /boot/netupdate.cnf@2" "echo url = 10.0.2.100/nuovo/netinst >> /boot/netupdate.cnf@2" \
    "netupdate -check -yes@~600" \
    "cat /exwin/lib/applicazioni.txt@5" \
    "echo FINE-MENU@2" "sync@5" > "$D/giro.log" 2>&1
tr -d '\r' < "$D/giro.log" | sed 's/\x1b\[[0-9;]*m//g' | sed -n '/^=== seriale dal prompt/,$p' > "$D/giro.txt"
T="$D/giro.txt"

sed -n '/url = 10.0.2.100\/nuovo/,/> cat \/exwin/p' "$T" > "$D/b.txt"
sed -n '/> cat \/exwin\/lib\/applicazioni.txt/,/> echo FINE-MENU/p' "$T" > "$D/menu.txt"
grep -a "applicazioni.txt\|come prima" "$D/b.txt" | sed 's/^/      /' | head -8
if grep -aq "^Applicazioni/File manager" "$D/menu.txt" && ! grep -aq "^File manager  *|" "$D/menu.txt"; then
    si "1. il menu ha i gruppi, e File manager non e' piu' fuori"
else no "1. il menu nuovo non c'e' ($D/menu.txt)"; fi
grep -aq "^Mio orologio | /exwin/bin/orologio" "$D/menu.txt" && grep -aq "come prima: Mio orologio" "$D/b.txt" \
    && si "2. la voce dell'utente e' tornata, e netupdate lo dice" || no "2. la voce dell'utente manca ($D/menu.txt, $D/b.txt)"
grep -aq "^Sparito" "$D/menu.txt" && no "3. e' tornata la voce di un programma che non c'e'" \
    || si "3. la voce di un programma che non c'e' non torna"
grep -aq "^@avvio /exwin/bin/orologio" "$D/menu.txt" \
    && si "4. @avvio e' rimasto" || no "4. @avvio perso ($D/menu.txt)"
grep -aq "^  sistema *a posto" "$D/b.txt" \
    && si "5. la verifica del sistema: a posto" || no "5. la verifica del sistema non dice a posto ($D/b.txt)"
grep -a "\[FAULT\]\|KERNEL PANIC" /tmp/exos/serialmn$ARCH.txt > /dev/null 2>&1 && no "un [FAULT] sulla seriale"
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
