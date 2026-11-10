#!/bin/bash
# =============================================================================
# tools/font-cjk/prepara.sh — Chinese, Japanese and Korean fonts for Exilla
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/font-cjk/prepara.sh          download (if missing), check, extract
#
# Noto Sans CJK 2.004 and Noto Serif CJK 2.003 (Google and Adobe, SIL Open
# Font License 1.1, LICENSE next to the fonts), from the releases of
# https://github.com/notofonts/noto-cjk. The fonts end up in
# cross_build/terze-parti/noto-cjk/scelti, where tools/mknetinst.sh takes them
# for the netupdate package [font-cjk] (files in /exwin/font).
#
# ! FOUR FILES, NOT FOURTEEN: Regular and Bold of Sans and of Serif. Each
# .ttc is a collection with Japanese, Korean, Simplified Chinese, Traditional
# Chinese and Hong Kong faces (and the Sans files also the Mono ones), so the
# four cover all the CJK text of the web. Gecko makes the other weights
# (Light, Medium, Black...) from these two; the seven weights would be 330 MB
# instead of 93, and Gecko reads the name table of every face at its first
# start.
#
# ! THE DIGESTS ARE OURS: GitHub gives none for these releases. They were
# taken on 9 November 2026 from the archives as GitHub served them (sizes as
# the release API reports). A different digest means a different file: the
# script stops.
# =============================================================================
set -e
cd "$(dirname "$0")/../.."
TP=cross_build/terze-parti/noto-cjk
FUORI=$TP/scelti
mkdir -p "$TP"

URL_SANS=https://github.com/notofonts/noto-cjk/releases/download/Sans2.004/03_NotoSansCJK-OTC.zip
URL_SERIF=https://github.com/notofonts/noto-cjk/releases/download/Serif2.003/04_NotoSerifCJKOTC.zip

prendi() {          # prendi URL SHA256
    local f="$TP/${1##*/}"
    if [ ! -f "$f" ]; then
        curl -fL --retry 3 -o "$f.part" "$1"
        mv "$f.part" "$f"
    fi
    echo "$2  $f" | sha256sum -c --quiet - || { echo "font-cjk: $f non e' quello atteso" >&2; exit 1; }
}
prendi "$URL_SANS"  528f4e1b25ff3badb0321b38d015d954c4c0de926c7830ef50e4a1948f6a3eed
prendi "$URL_SERIF" c76ed8fd491ce98182ec6430238b90a1eabea8857c7db64e9037efa38e198a31

rm -rf "$FUORI"; mkdir -p "$FUORI"
unzip -o -j -q "$TP/03_NotoSansCJK-OTC.zip" NotoSansCJK-Regular.ttc NotoSansCJK-Bold.ttc LICENSE -d "$FUORI"
unzip -o -j -q "$TP/04_NotoSerifCJKOTC.zip" OTC/NotoSerifCJK-Regular.ttc OTC/NotoSerifCJK-Bold.ttc -d "$FUORI"
mv "$FUORI/LICENSE" "$FUORI/NotoCJK-LICENSE.txt"
( cd "$FUORI" && sha256sum -c --quiet "$OLDPWD/tools/font-cjk/IMPRONTE" )
ls -l "$FUORI"
