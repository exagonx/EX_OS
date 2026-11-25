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
#      left as before: /root 0755 and /tmp 0777 put right by itself (exit 2);
#   5  the network card as uid 1000 (tools/prove/reteprova.c, kernel 0.356):
#      INFO and CONTATORI yes, INVIA, RICEVI and REFERTO no, like Linux; and
#      then root still downloads a file from the host (EXOS_NIC, e1000 by
#      default: ne2k_pci, pcnet, rtl8139 are the others QEMU has);
#   6  the IPC of 0.356 (tools/prove/codaprova.c): the sender's uid stamped
#      by the kernel, the mailbox that grows, more than 128 names;
#   7  shm_open(SHM_ANON) from 8 threads on 4 processors, the zones passing
#      512 (tools/prove/shmfili.c): fstat always says the size asked. Firefox
#      closed on unsplash.com when it did not (the libc's table had no lock);
#   8  a program that falls after five calls with 6 KB frames
#      (tools/prove/pilaprova.c): the "PF:   sulla pila" lines of kernel 0.357
#      name its four callers and main with addr2line (giro5 is the EIP itself).
#   9  socketpair(AF_UNIX) of kernel 0.358 (tools/prove/unixprova.c): bytes
#      both ways, fork, dup, poll, MSG_DONTWAIT, end of data, EPIPE (voce 115)
#  10  kernel 0.359, voce 111 step 4b (tools/prove/consoleprova.c): a mailbox
#      for the console is closed to who does not sit there, open once root
#      (login) says so; one for the owner is closed to others; kbd and fb_map
#      refused to a user nobody logged in; ports below 1024 root's (ip.drv)
#  11  kernel 0.367, debug/bug_SU001 (tools/prove/suprova.c): SYS_SU brakes
#      per uid (1 s per failure, EAGAIN after three, a fork does not escape
#      it, another uid is not touched), four at once with the right password
#      all become root
#  12  kernel 0.367, debug/bug_PTY001 (tools/prove/ptyperm.c): /dev/pts/N
#      has an owner, 0620 as devpts; another user cannot open it, fchown and
#      fchmod give it away, an O_WRONLY slave does not read
#  13  kernel 0.367, debug/bug_SHM001 (tools/prove/shmperm.c): named shared
#      zones have an owner and a mode (0600 unless said), SHM_ESCL creates
#      or fails, root attaches to all
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
"$GCC" -O2 -Wall -o "$D/reteprova" tools/prove/reteprova.c || { echo "reteprova non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/codaprova" tools/prove/codaprova.c || { echo "codaprova non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/shmfili" tools/prove/shmfili.c -lpthread || { echo "shmfili non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/pilaprova" tools/prove/pilaprova.c || { echo "pilaprova non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/unixprova" tools/prove/unixprova.c -lpthread || { echo "unixprova non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/consoleprova" tools/prove/consoleprova.c || { echo "consoleprova non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/suprova" tools/prove/suprova.c || { echo "suprova non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/ptyperm" tools/prove/ptyperm.c || { echo "ptyperm non compila" >&2; exit 1; }
"$GCC" -O2 -Wall -o "$D/shmperm" tools/prove/shmperm.c || { echo "shmperm non compila" >&2; exit 1; }
NIC="${EXOS_NIC:-e1000}"

# a file for root to download at step 5, from a server on the host
mkdir -p "$D/www"; echo "rete-di-root-ok" > "$D/www/ok.txt"
PORTA=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
python3 -m http.server "$PORTA" --bind 127.0.0.1 --directory "$D/www" > "$D/www.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT

echo "=== 1. disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
# ! THE CARD OF THE INSTALL IS THE CARD OF THE TEST: install writes the
# driver of the card it finds, and mkhd's QEMU has its default one (e1000);
# with ne2k_pci at step 5 there was no rete0 at all.
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g; s|EXOS_QEMU_EXTRA=\"-drive file=\$IMG,format=raw,if=ide\"|EXOS_QEMU_EXTRA=\"-drive file=\$IMG,format=raw,if=ide -netdev user,id=n1 -device $NIC,netdev=n1\"|" tools/mkhd.sh > "$D/mkhd.sh"
grep -q "device $NIC" "$D/mkhd.sh" || { echo "mkhd.sh: non trovo dove dare la scheda" >&2; exit 1; }
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=pm$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 256 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }
si "installato"

# the two files on the installed partition (it starts at sector 2048)
for f in "$D/permprova" "$D/reteprova" "$D/codaprova" "$D/shmfili" "$D/pilaprova" "$D/unixprova" "$D/consoleprova" "$D/suprova" "$D/ptyperm" "$D/shmperm" tools/netinst/verifiche/sistema.sh; do
    "$DEBUGFS" -w -R "write $f $(basename "$f")" "$IMG?offset=1048576" > /dev/null 2>&1
done

echo "=== 2-4. dal disco ==="
A=("root@2" "root@6" "keymap us@3"
   "echo MODI; ls -l /; ls -l /home; echo FINE-MODI@4"
   "chmod 755 /permprova /reteprova /codaprova /shmfili /pilaprova /unixprova /consoleprova /suprova /ptyperm /shmperm@2"
   "echo PROVA; /permprova; echo FINE-PROVA \$?@10"
   "rm -r /root/privata /permprova.p /tmp/permprova.d@3"
   "chmod 755 /root@2" "chmod 777 /tmp@2"
   "echo VERIFICA; sh /sistema.sh; echo FINE-VERIFICA \$?@8"
   "echo DOPO; ls -l /; echo FINE-DOPO@4"
   "echo RETE; /reteprova; echo FINE-RETE \$?@15"
   "echo CODA; /codaprova; echo FINE-CODA \$?@20"
   "echo SHM; /shmfili 8 600 64; /shmfili 8 600 64; /shmfili 8 600 64; echo FINE-SHM \$?@120"
   "echo PILA; /pilaprova; dmesg | grep -a 'sulla pila'; echo FINE-PILA@10"
   "echo UNIX; /unixprova; echo FINE-UNIX \$?@20"
   "echo CONSOLE; /consoleprova; echo FINE-CONSOLE \$?@25"
   "echo SU; /suprova; echo FINE-SU \$?@25"
   "echo PTY; /ptyperm; echo FINE-PTY \$?@10"
   "echo SHMP; /shmperm; echo FINE-SHMP \$?@10"
   "echo SCARICA; scarica http://10.0.2.2:$PORTA/ok.txt /tmp/ok.txt; cat /tmp/ok.txt; echo FINE-SCARICA@20"
   "sync@5")
EXOS_QEMU=$Q EXOS_ISTANZA=pm$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$KVM $EXTRA -smp 4 -drive file=$IMG,format=raw,if=ide -netdev user,id=n1 -device $NIC,netdev=n1" \
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

R=$(echo "$L" | sed -n '/^RETE/,/^FINE-RETE/p')
echo "$R" | grep -a "\[SI\]\|\[NO\]" | sed 's/^/      /'
echo "$R" | grep -aq "^FINE-RETE 0" && si "la scheda ($NIC) come uid 1000: 0 falle" \
    || no "la scheda ($NIC) come uid 1000: $(echo "$R" | grep -ao 'reteprova: [0-9]* falle' || echo 'non arrivata in fondo')"
echo "$L" | sed -n '/^SCARICA/,/^FINE-SCARICA/p' | grep -aq "^rete-di-root-ok" \
    && si "root scarica ancora dall'host" || no "root non scarica piu' dall'host (vedi $D/qemu.log)"

C=$(echo "$L" | sed -n '/^CODA/,/^FINE-CODA/p')
echo "$C" | grep -a "\[OK\]\|\[NO\]" | sed 's/^/      /'
echo "$C" | grep -aq "^FINE-CODA 0" && si "IPC: timbro, casella che cresce, nomi oltre 128" \
    || no "IPC: $(echo "$C" | grep -ao 'codaprova: [0-9]* no' || echo 'non arrivata in fondo')"

M=$(echo "$L" | sed -n '/^SHM/,/^FINE-SHM/p')
echo "$M" | grep -a "shmfili:\|filo" | sed 's/^/      /'
[ "$(echo "$M" | grep -ac 'shmfili: .* 0 fstat sbagliati, 0 memorie altrui, 0 copie sbagliate, 0 falliti')" = 3 ] \
    && si "shm da 8 fili su 4 processori: fstat sempre giusto" \
    || no "shm da 8 fili: $(echo "$M" | grep -a 'shmfili:' | grep -v ' 0 fstat sbagliati, 0 memorie altrui, 0 copie sbagliate, 0 falliti' | head -1)"

P=$(echo "$L" | sed -n '/^PILA/,/^FINE-PILA/p' | grep -a 'sulla pila')
CHI=$(echo "$P" | grep -o '0x[0-9a-f]*' | addr2line -f -e "$D/pilaprova" 2>/dev/null | grep -o '^giro[1-4]\|^main' | sort -u | tr '\n' ' ')
echo "      chiamanti: $CHI"
[ "$CHI" = "giro1 giro2 giro3 giro4 main " ] \
    && si "al fault il kernel scrive tutta la catena delle chiamate" \
    || no "catena delle chiamate incompleta: $CHI ($(echo "$P" | wc -l) righe)"

U=$(echo "$L" | sed -n '/^UNIX/,/^FINE-UNIX/p')
echo "$U" | grep -a "NO:" | sed 's/^/      /'
echo "$U" | grep -aq "^FINE-UNIX 0" && si "socket unix a coppie: $(echo "$U" | grep -ao 'unixprova: [0-9]* prove')" \
    || no "socket unix: $(echo "$U" | grep -ao 'unixprova: .*' || echo 'non arrivata in fondo')"

C=$(echo "$L" | sed -n '/^CONSOLE/,/^FINE-CONSOLE/p')
echo "$C" | grep -a "\[NO\]" | sed 's/^/      /'
echo "$C" | grep -aq "^FINE-CONSOLE 0" && si "console, wserver e porte basse: $(echo "$C" | grep -ao 'consoleprova: [0-9]* prove')" \
    || no "console e porte: $(echo "$C" | grep -ao 'consoleprova: .*' || echo 'non arrivata in fondo')"

S=$(echo "$L" | sed -n '/^SU$/,/^FINE-SU/p')
echo "$S" | grep -a "\[NO\]" | sed 's/^/      /'
echo "$S" | grep -aq "^FINE-SU 0" && si "il freno di su: $(echo "$S" | grep -ac '\[OK\]') prove" \
    || no "il freno di su: $(echo "$S" | grep -ao 'suprova: .*' || echo 'non arrivata in fondo')"

T=$(echo "$L" | sed -n '/^PTY$/,/^FINE-PTY/p')
echo "$T" | grep -a "\[NO\]" | sed 's/^/      /'
echo "$T" | grep -aq "^FINE-PTY 0" && si "/dev/pts/N ha un proprietario: $(echo "$T" | grep -ac '\[OK\]') prove" \
    || no "i terminali: $(echo "$T" | grep -ao 'ptyperm: .*' || echo 'non arrivata in fondo')"

T=$(echo "$L" | sed -n '/^SHMP$/,/^FINE-SHMP/p')
echo "$T" | grep -a "\[NO\]" | sed 's/^/      /'
echo "$T" | grep -aq "^FINE-SHMP 0" && si "le memorie per nome hanno un proprietario: $(echo "$T" | grep -ac '\[OK\]') prove" \
    || no "le memorie per nome: $(echo "$T" | grep -ao 'shmperm: .*' || echo 'non arrivata in fondo')"

echo "=== $ok riusciti, $no no ($ARCH, $NIC) ==="
[ $no -eq 0 ]
