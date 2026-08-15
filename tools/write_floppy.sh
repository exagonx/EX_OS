#!/bin/bash
# =============================================================================
# tools/write_floppy.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Scrive dist/floppy.img su un floppy fisico, da dentro WSL.
#
#   ./tools/write_floppy.sh              # dist/floppy.img su A:
#   ./tools/write_floppy.sh -d B:        # altra unita'
#   ./tools/write_floppy.sh -i altra.img
#   ./tools/write_floppy.sh -y           # senza chiedere conferma
#
# Lo stesso lavoro si puo' fare direttamente da Windows:
#   PowerShell : .\tools\write_floppy.ps1
#   cmd.exe    : tools\write-floppy.cmd
#
# PERCHE' NON BASTA dd
#
# WSL2 gira in una VM leggera e non vede i dischi fisici di Windows: non
# esiste /dev/fd0 ne' un device per il floppy USB, e `wsl --mount` non
# gestisce i floppy USB. L'unica strada e' passare per Windows.
#
# Questo script si limita a convertire il percorso e a invocare
# write_floppy.ps1, che pensa da solo all'elevazione (UAC).
# =============================================================================

set -u

IMG="dist/floppy.img"
DRIVE="A:"
EXTRA=""

uso() {
    cat <<'EOF'
Uso: tools/write_floppy.sh [opzioni]

  -i <file>   immagine da scrivere        (default: dist/floppy.img)
  -d <X:>     unita' di destinazione      (default: A:)
  -y          non chiedere conferma
  -f          accetta unita' non rimovibili (PERICOLOSO)
  -n          salta la verifica per rilettura
  -h          questo messaggio

Comparira' la richiesta UAC: aprire il volume grezzo richiede i privilegi
di Amministratore.

Equivalenti lato Windows:
  PowerShell : .\tools\write_floppy.ps1 -Drive A:
  cmd.exe    : tools\write-floppy.cmd A:
EOF
}

while getopts "i:d:yfnh" opt; do
    case "$opt" in
        i) IMG="$OPTARG" ;;
        d) DRIVE="$OPTARG" ;;
        y) EXTRA="$EXTRA -Yes" ;;
        f) EXTRA="$EXTRA -Force" ;;
        n) EXTRA="$EXTRA -NoVerify" ;;
        h) uso; exit 0 ;;
        *) uso; exit 1 ;;
    esac
done

if [ ! -f "$IMG" ]; then
    echo "[ERRORE] immagine non trovata: $IMG" >&2
    echo "         compilala prima con 'make all'" >&2
    exit 1
fi

if ! command -v powershell.exe >/dev/null 2>&1; then
    echo "[ERRORE] powershell.exe non raggiungibile: l'interop con Windows e' disattivato." >&2
    echo "         Verifica /etc/wsl.conf ([interop] enabled=true) e riavvia WSL." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PS1_WIN="$(wslpath -w "$SCRIPT_DIR/write_floppy.ps1")"
IMG_WIN="$(wslpath -w "$(readlink -f "$IMG")")"

# Un percorso UNC (\\wsl.localhost\...) significa che il progetto vive dentro
# il filesystem di WSL: il processo elevato di Windows spesso non riesce a
# leggerlo, perche' l'elevazione non porta con se' le connessioni di rete.
case "$IMG_WIN" in
    '\\'*)
        echo "[AVVISO] l'immagine si trova nel filesystem di WSL ($IMG_WIN)."
        echo "         Il processo elevato potrebbe non riuscire a leggerla."
        echo "         Se fallisce, copiala prima sotto C:, per esempio:"
        echo "           cp $IMG /mnt/c/Temp/floppy.img"
        echo "           ./tools/write_floppy.sh -i /mnt/c/Temp/floppy.img"
        ;;
esac

echo "[..] Immagine : $IMG"
echo "[..]            $IMG_WIN"
echo "[..] Unita'   : $DRIVE"
echo "[..] Avvio PowerShell — comparira' la richiesta UAC in una nuova finestra"
echo

powershell.exe -NoProfile -ExecutionPolicy Bypass \
    -File "$PS1_WIN" -Image "$IMG_WIN" -Drive "$DRIVE" $EXTRA
RC=$?

if [ "$RC" -eq 0 ]; then
    echo "[OK] floppy scritto e verificato."
else
    echo "[ERRORE] scrittura non riuscita (codice $RC)." >&2
    echo "         Cause tipiche: disco non inserito, linguetta di protezione," >&2
    echo "         UAC rifiutato, o Esplora risorse che tiene aperto il volume." >&2
fi

exit $RC
