#!/bin/bash
# =============================================================================
# tools/prova_video.sh — the video modes and the trial (@PANNELLO-VIDEO),
# on a disk installed from the 32-bit CD, in QEMU (std VGA)
#
#     tools/prova_video.sh [dir]
#
# 1. disk installed from dist/exos.iso (tools/mkhd.sh), if not there yet;
# 2. graphics on (svga.drv 800x600), reboot; svga.drv -lista;
#    svga.drv -prova 1280x1024, reboot: the screen at 1280x1024; nobody
#    confirms: the guard of avvio.sh puts 800x600 back and reboots;
# 3. svga.drv -prova 1024x768 -16, reboot; the desktop asks (photo); Enter
#    on "Tieni questo modo"; svga.drv says no trial is pending.
# Screenshots in DIR/*.png, serial logs in DIR.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/claude-1004/video}"
mkdir -p "$D"
IMG="$D/hd-video.img"
export EXOS_ISTANZA=vid
if [ ! -f "$IMG" ]; then
    echo "=== 1. disco installato dal CD ($IMG) ==="
    sed "s|^IMG=dischi/hd.img|IMG=$IMG|; s|^MB=\"\${1:-512}\"|MB=\"\${1:-256}\"|; \
         s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
    EXOS_SUPPORTO=cd bash "$D/mkhd.sh" > "$D/1-disco.log" 2>&1 || {
        echo "NON RIUSCITO: il disco non si e' installato (vedi $D/1-disco.log)"; exit 1; }
fi
export EXOS_NO_FLOPPY=1 EXOS_RAM=128M EXOS_MARCA="utente:"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide -vga std"

# ! QEMU ENDS AT EVERY REBOOT (qemu_drive starts it so): one run per boot.
corsa() {   # $1 = name, then the steps
    local n="$1"; shift
    timeout 600 python3 tools/qemu_drive.py "$@" > "$D/$n.log" 2>&1
    cp "/tmp/exos/serial${EXOS_ISTANZA}.txt" "$D/$n-seriale.txt" 2>/dev/null
}
L=("root@2" "root@6" "keymap us@3")
echo "=== 2. grafica, lista, prova senza conferma ==="
corsa 2a "${L[@]}" "/dev/svga.drv 800x600@6" "reboot@10"
corsa 2b "${L[@]}" "foto:$D/a1-800.ppm@1" "/dev/svga.drv -lista@4" "foto:$D/a2-lista.ppm@1" \
    "/dev/svga.drv -prova 1280x1024@6" "grep guardia /boot/avvio.sh@3" "reboot@10"
corsa 2c "foto:$D/a3-1280.ppm@170"
corsa 2d "${L[@]}" "/dev/svga.drv@4" "foto:$D/a4-stato.ppm@1"
echo "=== 3. prova confermata dalla scrivania ==="
corsa 3a "${L[@]}" "/dev/svga.drv -prova 1024x768 -16@6" "reboot@10"
corsa 3b "${L[@]}" "exwin@25" "foto:$D/b1-domanda.ppm@1" \
    "key:ret@4" "foto:$D/b2-tenuto.ppm@1" "key:alt-f1@2" "/dev/svga.drv@4" "foto:$D/b3-stato.ppm@1"

for f in "$D"/*.ppm; do convert "$f" "${f%.ppm}.png" 2>/dev/null; done
for f in "$D"/*.png; do printf '%s  ' "$(basename "$f")"; identify -format '%wx%h\n' "$f" 2>/dev/null; done
echo "registri: $D/2.log $D/3.log $D/*-seriale.txt"
