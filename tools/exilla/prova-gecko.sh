#!/bin/bash
# =============================================================================
# tools/exilla/prova-gecko.sh — Gecko senza finestre dentro EX-OS (tappa 6)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/prova-gecko.sh            (dopo tools/exilla/gecko-costruisci.sh build)
#
# Impacchetta il browser (mach package: omni.ja e niente collegamenti
# simbolici, che EX-OS non ha), lo mette su un disco ext2 insieme a una pagina
# di prova, avvia EX-OS e gli fa aprire la pagina senza finestre, in un
# processo solo, salvando quello che ha disegnato:
#
#     firefox --headless --screenshot /disk/schermata.png file:///disk/prova.html
#
# Poi riprende schermata.png dal disco e la lascia in costruzione-prova. La
# prova e' riuscita se l'immagine c'e' e non e' vuota: guardarla e' il resto.
#
# ! LA PARTIZIONE SI FA SULL'HOST, GIA' PIENA (mke2fs -d): debugfs scrive un
# file alla volta e non crea le cartelle di un pacchetto di centinaia di file.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
O="$PWD/cross_build/exilla-obj/gecko"
D="$B/costruzione-prova"
P="$O/dist/firefox"
mkdir -p "$D" /tmp/exos

if [ ! -x "$P/firefox" ] || [ "$O/dist/bin/firefox" -nt "$P/firefox" ]; then
    tools/exilla/gecko-costruisci.sh package > "$D/gecko-package.log" 2>&1 || {
        echo "mach package fallito: $D/gecko-package.log" >&2; exit 1; }
fi
[ -x "$P/firefox" ] || { echo "manca $P/firefox" >&2; exit 1; }

# --- il contenuto del disco -----------------------------------------------------
S="$D/gecko-disco"
rm -rf "${S:?}"; mkdir -p "$S/profilo" "$S/casa" "$S/font"
cp -a "$P" "$S/firefox"
# I caratteri di ExWin stanno sulla sua ISO, non sul CD di base: qui sul disco.
cp exwin/font/*.ttf "$S/font/"
cat > "$S/prova.html" <<'EOF'
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Exilla</title>
<style>
  body { font-family: sans-serif; background: #f4f1e8; margin: 24px; }
  h1 { color: #1d4e89; }
  .riquadro { display: inline-block; width: 120px; height: 60px; margin: 6px; }
</style></head>
<body>
  <h1>Gecko dentro EX-OS</h1>
  <p>Testo in <b>grassetto</b>, <i>corsivo</i> e <code>monospazio</code>.</p>
  <div class="riquadro" style="background:#c0392b"></div>
  <div class="riquadro" style="background:#27ae60"></div>
  <div class="riquadro" style="background:#2980b9"></div>
  <p id="js">JavaScript non e' partito.</p>
  <script>
    document.getElementById("js").textContent =
      "JavaScript: 6 x 7 = " + (6 * 7) + ", " + navigator.userAgent;
  </script>
</body></html>
EOF

# --- il disco: una partizione ext2 fatta qui, messa a 1 MB ------------------------
MB=$(( $(du -sm "$S" | cut -f1) * 13 / 10 + 128 ))
IMG="$D/gecko-hd.img"; PART="$D/gecko-part.img"
rm -f "$IMG" "$PART"
"$(command -v mke2fs || echo /sbin/mke2fs)" -q -t ext2 -b 4096 -L gecko -d "$S" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none
rm -f "$PART"
echo "disco: $MB MB, firefox $(du -sh "$S/firefox/firefox" | cut -f1)"

# --- EX-OS ---------------------------------------------------------------------------
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
CD="$B/costruzione-sistema/albero/dist/exos.iso"; [ -f "$CD" ] || CD="$PWD/dist/exos.iso"
export EXOS_ISTANZA=gecko EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-1536M}"
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
rm -f /tmp/exos/serialgecko.txt
# AMBIENTE="VAR=valore ..." aggiunge variabili (EXOS_MALLOC_CONTROLLA=1, ...).
ESPORTA=()
for v in $AMBIENTE; do ESPORTA+=("export $v@1"); done
timeout "${ATTESA:-1800}" python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "${ESPORTA[@]}" \
    "export MOZ_FORCE_DISABLE_E10S=1@1" "export MOZ_HEADLESS=1@1" \
    "export HOME=/disk/casa@1" "export LANG=it_IT.UTF-8@1" "export EXILLA_FONTS=/disk/font@1" \
    "/disk/firefox/firefox --headless -no-remote -profile /disk/profilo --window-size 800,600 --screenshot /disk/schermata.png file:///disk/prova.html@~${SECONDI:-900}" \
    "ls /disk@3" "echo FINE-GECKO@2" > "$D/gecko-1.log" 2>&1

tr -d '\r' < /tmp/exos/serialgecko.txt | sed 's/\x1b\[[0-9;]*m//g' | grep -v ENTROPIA \
    | sed -n '/firefox --headless/,/FINE-GECKO/p' | tail -40

rm -f "$D/schermata.png"
"$(command -v debugfs || echo /sbin/debugfs)" -R "dump /schermata.png $D/schermata.png" \
    "$IMG?offset=1048576" > /dev/null 2>&1
if [ -s "$D/schermata.png" ]; then
    echo "=== [OK] Gecko ha disegnato la pagina dentro EX-OS: $D/schermata.png ==="
    exit 0
fi
echo "=== [NO] nessuna schermata: vedi $D/gecko-1.log e /tmp/exos/serialgecko.txt ==="
exit 1
