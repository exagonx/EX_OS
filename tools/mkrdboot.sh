#!/bin/bash
# =============================================================================
# tools/mkrdboot.sh — the base system as one archive: rdboot.img
#
#     tools/mkrdboot.sh i386   [uscita]     default build/rdboot.img
#     tools/mkrdboot.sh x86_64 [uscita]     default build-64/rdboot.img
#
# Accanto all'.img fa il .lz4 (tools/lz4exos.py): e' quello che va in /boot.
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ! IL SISTEMA DI BASE IN UN FILE SOLO (@RDBOOT, 22 ottobre 2026, voce 22 di
# @CODA-20-10). Stage 2 lo carica in RAM insieme al kernel e il kernel lo monta
# su /rdboot: li' dentro c'e' quel che serve per arrivare ai dischi - oggi e'
# il contenuto del floppy - e niente altro. Il resto sta sul disco di sistema.
#
#     /rdboot/bin    i programmi del floppy (sh, login, mount, fdisk, ...)
#     /rdboot/dev    i driver per arrivare ai dischi e allo schermo, e i
#                    moduli del kernel (ext4.mod)
#     /rdboot/lib    libc.so (solo a 32 bit: a 64 la libc e' statica)
#     /rdboot/etc    la configurazione della base (vuota per ora)
#     /rdboot/tmp    vuota: il posto per scrivere quando il disco non c'e'
#
# ! E' UN VOLUME ext2 (l'utente, 22 ottobre 2026), non FAT come nella prima
# prova: i nomi restano in minuscolo come sul disco, e ci sono proprietari,
# permessi e bit di esecuzione - quando al passo 2 questo volume sara' la
# radice, sara' una radice vera e non un floppy. Il kernel monta ext2 da
# qualunque dispositivo a blocchi, anche quello in RAM, e lo legge prima che
# ext4.mod (che sta qui dentro) sia caricato. Lo fa mke2fs dall'albero
# preparato sotto fakeroot: tutto di root, programmi 0755.
#
# ! LA SOMMA 'EXRD' come la chiavetta (tools/mkchiave.sh): a 0x1F0 del primo
# settore la firma, a 0x1F4 la somma delle parole di 32 bit del volume con
# quel campo a zero. Il kernel la ricalcola (rd_verifica) e dice se il BIOS
# ha letto bene. Su ext2 i primi 1024 byte del volume sono l'area d'avvio,
# che nessuno usa.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${1:-i386}"
if [ "$ARCH" = x86_64 ]; then
    B=build-64; OUT="${2:-build-64/rdboot.img}"
else
    B=build; OUT="${2:-build/rdboot.img}"
fi
MB=4

# I driver: tastiera, PCI, schermo, mouse seriale e l'USB (dove stanno anche le
# chiavette e i lettori). IDE e AHCI sono nel kernel.
DRIVER="kbd pci svga mouseser uhci ohci ehci xhci"

# I programmi: quelli del floppy, cioe' tutto build/bin a 32 bit. A 64 bit
# build-64/bin ha anche quelli del CD: si prendono gli stessi nomi del 32.
programmi() {
    local p
    for p in build/bin/*; do
        case "$(basename "$p")" in *.o|*.d|*.a|*.so) continue ;; esac
        [ "$(head -c 4 "$p" | od -An -c | tr -d ' ')" = '177ELF' ] || continue
        case " whoami chown poweroff reboot halt " in *" $(basename "$p") "*) continue ;; esac
        basename "$p"
    done
}

T=$(mktemp -d "${TMPDIR:-/tmp}/mkrdboot.XXXXXX")
trap 'rm -rf "$T"' EXIT
IMG="$T/rdboot.img"
A="$T/albero"
mkdir -p "$A/rdboot/bin" "$A/rdboot/dev" "$A/rdboot/lib" "$A/rdboot/etc" "$A/rdboot/tmp"

mancano=""
for n in $(programmi); do
    if [ -f "$B/bin/$n" ]; then
        strip -o "$A/rdboot/bin/$n" "$B/bin/$n" 2>/dev/null || cp "$B/bin/$n" "$A/rdboot/bin/$n"
    else
        mancano="$mancano $n"
    fi
done
# umount e' mount (guarda argv[0]), come sul floppy.
[ -f "$A/rdboot/bin/mount" ] && cp "$A/rdboot/bin/mount" "$A/rdboot/bin/umount"

for n in $DRIVER; do
    # A 32 bit i driver che non stanno sul floppy sono in build/drivers-cd.
    f="$B/drivers/$n.drv"; [ -f "$f" ] || f="$B/drivers-cd/$n.drv"
    if [ -f "$f" ]; then cp "$f" "$A/rdboot/dev/$n.drv"; else mancano="$mancano $n.drv"; fi
done
for m in "$B"/moduli/*.mod; do [ -f "$m" ] && cp "$m" "$A/rdboot/dev/"; done
[ "$ARCH" = i386 ] && for l in build/lib/*.so; do [ -f "$l" ] && cp "$l" "$A/rdboot/lib/"; done

fakeroot sh -c "
    chown -R 0:0 '$A'
    find '$A' -type d -exec chmod 755 {} +
    find '$A' -type f -exec chmod 644 {} +
    chmod 755 '$A'/rdboot/bin/* '$A'/rdboot/dev/*.drv
    chmod 1777 '$A/rdboot/tmp'
    /usr/sbin/mke2fs -q -F -t ext2 -b 1024 -I 128 -m 0 -L rdboot -d '$A' '$IMG' $((MB * 1024)) > '$T/mke2fs.log' 2>&1 || { cat '$T/mke2fs.log'; exit 1; }
" || exit 1

python3 - "$IMG" <<'EOF' || exit 1
import struct, sys
p = sys.argv[1]
b = bytearray(open(p, 'rb').read())
b[0x1F0:0x1F4] = b'EXRD'
b[0x1F4:0x1F8] = b'\0\0\0\0'
s = sum(struct.unpack('<%dI' % (len(b) // 4), b)) & 0xFFFFFFFF
b[0x1F4:0x1F8] = struct.pack('<I', s)
open(p, 'wb').write(b)
EOF

mkdir -p "$(dirname "$OUT")"
cp "$IMG" "$OUT"
# ! L'ELENCO DI CIO' CHE C'E' DENTRO, accanto: e' il «sistema minimale» che
# `install` copia dal CD (boot/minimale.txt, nel Makefile). Prima lo si
# leggeva dal dischetto, che adesso porta solo l'avvio.
(cd "$A/rdboot" && find . -type f | sed 's|^\./||' | sort) > "${OUT%.img}.lista"
# ! E LA COPIA COMPRESSA accanto (@RDBOOT 1b): e' quella che va su /boot. La
# compressione si verifica da se' (lz4exos.py decomprime e confronta).
python3 tools/lz4exos.py "$OUT" "${OUT%.img}.lz4" || exit 1
liberi=$(/usr/sbin/dumpe2fs -h "$OUT" 2>/dev/null | sed -n 's/^Free blocks: *//p')
echo "[OK] $OUT ($ARCH, $MB MB, blocchi liberi ${liberi:-?} da 1 KB)"
[ -n "$mancano" ] && echo "  ! mancano in $B:$mancano"
exit 0
