#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/qt5-exos/prova.sh - a Qt Widgets program in ExWin, inside QEMU
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/qt5-exos/prova.sh [32|64]      64 by default
#
# Run after tools/qt5-exos/costruisci.sh. tools/qt5-exos/ciaoqt.cpp is built
# with the cross compiler against cross_build/qt5-exos<bit>, and goes on an
# ext2 disk with Qt (libraries, the exwin plugin, the fonts) and every .so it
# needs from the sysroot. In QEMU, from the CD: ExWin starts, then ciaoqt
# with the platform plugin "exwin". The pointer clicks where ciaoqt's
# widgets are (it puts its window at 100,100), the keyboard types.
# What is [OK], from ciaoqt's own lines on the serial line and from
# screenshots:
#   - QT-PRONTO: Qt started, the plugin made a window of the server;
#   - the screenshot after it differs from the empty desktop (the window
#     is painted: backing store -> zone -> WIN_MSG_AGGIORNA);
#   - QT-TESTO ciao: a click gave the line edit the focus, the presses and
#     releases (WIN_MSG_TASTI) became text, Enter came through;
#   - QT-CLIC: a click on the button (press and release);
#   - QT-MENU and a screenshot with the menu open (a WIN_ST_COMPARSA window),
#     then QT-ESCI: the click on the menu item reached the popup;
#   - QT-PNG: a PNG written and read back in memory;
#   - QT-NATIVO 1: a child widget's flush has a window to go to;
#   - QT-ANIMA 1: a property animation reached its end (VLC's cone);
#   - QT-IPC: QSystemSemaphore and QSharedMemory work (sem_open, shm_open;
#     kernel 0.373; the ftok key file in TMPDIR=/disk/tmp); QT-GUARDA:
#     QFileSystemWatcher sees a new file in $HOME less than 200 ms after it
#     was written (inotify; Qt's fallback looks once a second);
#     QT-RETE: QNetworkInterface lists lo/1 and eth0/2 (getifaddrs);
#   - no [FAULT] or PANIC.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
BIT=${1:-64}
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
TC="${EXOS_FUORI}/cross_build"
case $BIT in
    64) T=x86_64-exos; CROSS=$TC/$MACCHINA/exos-cross64; CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"
        export EXOS_QEMU=qemu-system-x86_64 ;;
    32) T=i386-exos;   CROSS=$TC/$MACCHINA/exos-cross;   CD="$PWD/dist/exos.iso";   CPU="-cpu pentium3" ;;
    *)  echo "uso: $0 [32|64]" >&2; exit 2 ;;
esac
Q="$TC/qt5-exos$BIT/exos/qt5"
[ -f "$Q/lib/libQt5Widgets.so.5" ] || { echo "manca $Q: prima tools/qt5-exos/costruisci.sh $BIT" >&2; exit 1; }
D="$TC/$MACCHINA/prova-qt$BIT"
S="$D/disco"
rm -rf "${D:?}"; mkdir -p "$S/casa" "$S/tmp" "$S/lib" "$D/foto"
# an RGBA PNG made elsewhere, for QT-PNGFILE (VLC's cone is one)
cp tools/exilla/tappa6-schermata.png "$S/casa/prova.png"

# --- the program --------------------------------------------------------------
"$CROSS/bin/$T-g++" -O2 -fPIC -std=c++17 -D__unix__ -DEXOS_SOLO_POSIX \
    -I"$Q/include" -I"$Q/include/QtCore" -I"$Q/include/QtGui" -I"$Q/include/QtWidgets" \
    -I"$Q/include/QtNetwork" \
    tools/qt5-exos/ciaoqt.cpp -o "$S/ciaoqt" \
    -L"$Q/lib" -lQt5Widgets -lQt5Gui -lQt5Network -lQt5Core -Wl,-rpath-link,"$Q/lib" -Wl,-rpath-link,"$CROSS/$T/lib" \
    || { echo "  [NO] ciaoqt non si compila"; exit 1; }

# --- Qt and what it needs -----------------------------------------------------
cp -a "$Q" "$S/qt5"
rm -rf "$S/qt5/include" "$S/qt5/mkspecs" "$S/qt5/bin" "$S/qt5/doc" "$S/qt5/lib/cmake" "$S/qt5/lib/pkgconfig"
python3 - "$CROSS/bin/$T-readelf" "$S" "$S/qt5/lib" "$CROSS/$T/lib" "$S/lib" <<'PY'
# every DT_NEEDED, from the program and the plugins down: the sysroot's ones
# are copied to /disk/lib
import glob, os, re, shutil, subprocess, sys
readelf, disco, qtlib, sysroot, dest = sys.argv[1:6]
da_fare = [disco + "/ciaoqt"] + glob.glob(disco + "/qt5/plugins/*/*.so")
visti, mancano = set(), set()
while da_fare:
    f = da_fare.pop()
    d = subprocess.run([readelf, "-d", f], capture_output=True, text=True).stdout
    for n in re.findall(r"\(NEEDED\).*\[(.*)\]", d):
        if n in visti: continue
        visti.add(n)
        if os.path.exists(os.path.join(qtlib, n)):
            da_fare.append(os.path.join(qtlib, n)); continue
        p = os.path.join(sysroot, n)
        if os.path.exists(p):
            shutil.copy(os.path.realpath(p), os.path.join(dest, n)); da_fare.append(p)
        else:
            mancano.add(n)
print("  librerie: %s" % " ".join(sorted(visti)))
if mancano:
    print("  [NO] non trovate: %s" % " ".join(sorted(mancano))); sys.exit(1)
PY
[ $? = 0 ] || exit 1

MB=$(( $(du -sm "$S" | cut -f1) * 13 / 10 + 32 ))
IMG="$D/hd.img"; PART="$D/part.img"
/sbin/mke2fs -q -t ext2 -b 4096 -L qt -d "$S" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=qt$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
# a card on QEMU's user network: QT-RETE wants eth0 beside lo
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15 -device ne2k_pci,netdev=n1"
SER=/tmp/exos/serialqt$BIT.txt; rm -f "$SER"
F=/tmp/exos/foto-qt$BIT; rm -rf "$F"; mkdir -p "$F"
{
    echo "mount hd0p1 /disk@5"
    echo "echo attendo la fine dei lavori di avvio@25"
    echo "export HOME=/disk/casa@1"
    # the CD has no /tmp to write in: the temporary files on the disk, as
    # lib/libc.c (tmp_componi) says - QSystemSemaphore's ftok key file goes there
    echo "export TMPDIR=/disk/tmp@1"
    echo "export LD_LIBRARY_PATH=/disk/lib:/disk/qt5/lib@1"
    echo "export QT_PLUGIN_PATH=/disk/qt5/plugins@1"
    echo "export QT_QPA_FONTDIR=/disk/qt5/lib/fonts@1"
    echo "export QT_EXWIN_DEBUG=${QT_EXWIN_DEBUG:-0}@1"   # 1: the events, on the serial line
    echo "exwin@14"
    echo "key:alt-f5@4"
    echo "foto:$F/fondo.ppm@2"
    echo "key:alt-f1@2"
    echo "/disk/ciaoqt &@1"
    echo "key:alt-f5@${PAUSA_AVVIO:-25}"
    echo "foto:$F/finestra.ppm@2"
    # the line edit: a click, then the keys
    echo "vai:220,158@1"
    echo "mon:mouse_button 1;mouse_button 0@2"
    echo "key:c,i,a,o,ret@3"
    # the button
    echo "vai:180,220@1"
    echo "mon:mouse_button 1;mouse_button 0@3"
    # the menu, open; then its item.
    # ! NOT vai:115,112 - Qt lights "File" up when the pointer comes over
    # it, and vai() took that change for the pointer's tip and "corrected"
    # the aim off the menu bar. vai() to an empty part of the bar, which does
    # not change, then steps of ten (QEMU moves those 1:1).
    echo "vai:300,112@1"
    for i in $(seq 1 18); do echo "mon:mouse_move -10 0@0"; done
    echo "mon:mouse_move -5 0@1"
    echo "mon:mouse_button 1;mouse_button 0@3"
    echo "foto:$F/menu.ppm@2"
    echo "mon:mouse_move 10 10@0"
    echo "mon:mouse_move 5 16@1"
    echo "mon:mouse_button 1;mouse_button 0@4"
    echo "foto:$F/dopo.ppm@2"
    echo "key:alt-f1@2"
    echo "echo FINE-QT@2"
} > "$D/passi.txt"
mapfile -t A < "$D/passi.txt"
timeout "${ATTESA:-900}" python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
diversi() {     # percentage of the screen that differs between two pictures
    python3 - "$1" "$2" <<'PY'
import sys
def pixel(n):
    return open(n, 'rb').read().split(maxsplit=4)[4]
try:
    a, b = pixel(sys.argv[1]), pixel(sys.argv[2])
except (OSError, IndexError):
    print(0); sys.exit()
n = min(len(a), len(b)) // 3
print(sum(1 for i in range(0, n, 5) if a[3 * i:3 * i + 3] != b[3 * i:3 * i + 3]) * 5 * 100 // max(n, 1))
PY
}
for f in "$F"/*.ppm; do python3 tools/ppm2png.py "$f" "$D/foto/$(basename "${f%.ppm}").png" > /dev/null 2>&1; done
grep -aq "QT-PRONTO" "$D/seriale.txt" && si "Qt parte, il plugin exwin fa la finestra: $(grep -a -m1 QT-PRONTO "$D/seriale.txt")" \
    || no "QT-PRONTO non c'e' ($D/seriale.txt)"
c=$(diversi "$F/fondo.ppm" "$F/finestra.ppm")
[ "${c:-0}" -ge 3 ] && si "la finestra e' disegnata: $c% dello schermo cambiato ($D/foto/finestra.png)" \
    || no "la finestra non si vede: ${c:-0}% cambiato ($D/foto/finestra.png)"
L=$(grep -a -m1 "QT-ANIMA" "$D/seriale.txt" | tr -d ' ')
[ "$L" = "QT-ANIMA1" ] && si "un'animazione arriva in fondo (QT-ANIMA 1)" || no "l'animazione: '$L' invece di QT-ANIMA 1"
grep -aq "QT-PNG scritto 1 letto 1 16x8" "$D/seriale.txt" && si "PNG scritto e riletto" \
    || no "PNG: '$(grep -a -m1 QT-PNG "$D/seriale.txt")'"
grep -aq "QT-PNGFILE 800x600" "$D/seriale.txt" && si "un PNG RGBA letto dal disco" \
    || no "PNG RGBA dal disco: '$(grep -a -m1 QT-PNGFILE "$D/seriale.txt")'"
grep -aq "QT-NATIVO 1 winId 1" "$D/seriale.txt" && si "un widget figlio trova la sua finestra (QT-NATIVO)" \
    || no "widget figlio senza finestra nativa: '$(grep -a -m1 QT-NATIVO "$D/seriale.txt")' (winId 0?)"
# (kernel 0.373, voce 85) semaphores and shared memory, inotify, interfaces
grep -aq "QT-IPC sem 11 shm 11 ciao" "$D/seriale.txt" && si "QSystemSemaphore e QSharedMemory (sem_open, shm_open)" \
    || no "IPC di Qt: '$(grep -a -m1 QT-IPC "$D/seriale.txt")'"
# the signal after the file was written: inotify comes at once, Qt's
# fallback looks once a second (the write itself is the disk's time)
G=$(grep -a -m1 "QT-GUARDA" "$D/seriale.txt" | awk '{print $2}')
GS=$(grep -a -m1 "QT-GUARDA" "$D/seriale.txt" | awk '{print $5}')
[ -n "$G" ] && [ -n "$GS" ] && [ $((G - GS)) -lt 200 ] \
    && si "QFileSystemWatcher vede un file nuovo $((G - GS)) ms dopo la scrittura (inotify; scrittura $GS ms)" \
    || no "QFileSystemWatcher: '$(grep -a -m1 QT-GUARDA "$D/seriale.txt")' (a intervalli guarda ogni secondo)"
grep -a -m1 "QT-RETE" "$D/seriale.txt" | grep -q "lo/1" && grep -a -m1 "QT-RETE" "$D/seriale.txt" | grep -q "eth0/2" \
    && si "QNetworkInterface: $(grep -a -m1 QT-RETE "$D/seriale.txt")" \
    || no "QNetworkInterface: '$(grep -a -m1 QT-RETE "$D/seriale.txt")'"
grep -aq "QT-TESTO ciao" "$D/seriale.txt" && si "la tastiera scrive nella riga (QT-TESTO ciao)" || no "QT-TESTO ciao non c'e'"
grep -aq "QT-CLIC" "$D/seriale.txt" && si "il clic sul bottone (QT-CLIC)" || no "QT-CLIC non c'e'"
c=$(diversi "$F/finestra.ppm" "$F/menu.ppm")
grep -aq "QT-MENU" "$D/seriale.txt" && [ "${c:-0}" -ge 1 ] && si "il menu si apre ($c% cambiato, $D/foto/menu.png)" \
    || no "il menu non si apre (QT-MENU $(grep -ac QT-MENU "$D/seriale.txt"), ${c:-0}% cambiato)"
grep -aq "QT-ESCI" "$D/seriale.txt" && si "la voce Esci del menu chiude il programma (QT-ESCI)" || no "QT-ESCI non c'e'"
grep -aq "\[FAULT\]\|PANIC" "$SER" && no "[FAULT] o PANIC sulla seriale" || si "nessun FAULT"
grep -a -i "qt\|exwin:\|ciaoqt" "$D/seriale.txt" | grep -av "^/disk\|QT_\|export" | head -20
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
