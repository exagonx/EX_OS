#!/bin/bash
# =============================================================================
# tools/exilla/pubblica-github.sh — aggiorna il repository github.com/exagonx/exilla
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/pubblica-github.sh [--senza-invio]
#
# Il kit del porting di Firefox («Exilla») in un repository suo: le patch, i
# file nuovi, gli script che rigenerano le parti meccaniche (GN, crate Rust),
# il bersaglio Rust, la toolchain e le prove. Gli stessi percorsi che hanno
# dentro EX-OS (tools/exilla, tools/rust-exos, tools/gcc-exos), cosi' gli
# script valgono identici nei due posti.
#
# La copia di lavoro sta in cross_build/<macchina>/exilla-github (fuori da
# MEGA). Il messaggio del commit si scrive in tools/exilla/github/messaggio.txt
# come messaggio-commit.txt (titolo, riga vuota, corpo); senza, se ne fa uno
# con la data. --senza-invio prepara e committa ma non fa git push.
#
# ! IL REPOSITORY SU GITHUB VA CREATO VUOTO (senza README), NON COME FORK di
# Firefox: un fork si porterebbe dietro i GB di Mozilla.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
DEST="$PWD/cross_build/$MACCHINA/exilla-github"
REMOTO="${EXILLA_REMOTO:-https://github.com/exagonx/exilla.git}"
INVIA=1; [ "${1:-}" = "--senza-invio" ] && INVIA=0

if [ ! -d "$DEST/.git" ]; then
    if ! git clone "$REMOTO" "$DEST" 2>/dev/null; then
        mkdir -p "$DEST"
        git -C "$DEST" init -q -b main
        git -C "$DEST" remote add origin "$REMOTO"
    fi
fi

ESCLUDI=(--exclude __pycache__ --exclude target --exclude '*.o' --exclude '*.ppm'
         --exclude 'prova/target' --exclude github/)
mkdir -p "$DEST/tools"
for d in exilla rust-exos gcc-exos; do
    rsync -a --delete "${ESCLUDI[@]}" "tools/$d/" "$DEST/tools/$d/"
done

# Le pagine del repository: README in due lingue, licenza, la base di Firefox.
cp tools/exilla/github/README.md tools/exilla/github/README.en.md "$DEST/"
cp gpl-2.0.txt "$DEST/LICENSE"
cp tools/exilla/github/LICENZE.md "$DEST/"
{
    echo "Firefox $(cat firefox-main/browser/config/version.txt)"
    echo "istantanea di mozilla-firefox/firefox, ramo main, del 25 settembre 2026"
    echo "srcdir di mach: $(ls cross_build/"$MACCHINA"/costruzione-mozbuild/srcdirs/ 2>/dev/null | head -1)"
} > "$DEST/BASE.txt"

cd "$DEST"
git add -A
if git diff --cached --quiet; then
    echo "exilla: niente di nuovo da pubblicare"
    exit 0
fi
M="$OLDPWD/tools/exilla/github/messaggio.txt"
if [ -s "$M" ]; then
    git commit -q --cleanup=strip -F "$M"
    mv "$M" "$DEST/.git/ultimo-messaggio.txt"
else
    git commit -q -m "Exilla $(date '+%Y-%m-%d %H:%M')"
fi
git log --oneline -1
if [ "$INVIA" = 1 ]; then
    git push -u origin main
else
    echo "exilla: committato in $DEST, non inviato (--senza-invio)"
fi
