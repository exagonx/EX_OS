#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/psl-scarica.sh - the Public Suffix List for EXBrowser's cookies
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     bash tools/psl-scarica.sh          fetches it if missing, checks it if there
#     bash tools/psl-scarica.sh -nuova   fetches today's list over the old one
#
# ! THE TABLE IS NOT IN THE REPOSITORY (25 November 2026, the user): it has
# domains with names the repository keeps out, so the Makefile makes
# build/generati/psl_tabella.h from the list (tools/psl-tabella.py) at
# every build where the list is newer. This script gets the list from the
# one address its authors allow ("Please pull this list from, and only from
# https://publicsuffix.org/list/public_suffix_list.dat"), MPL-2.0.
#
# ! THERE IS NO SIGNATURE to check: the list changes every day. What is
# checked is that it IS the list (the VERSION line and both sections), and
# the .sha256 written beside it catches a copy damaged later.
# =============================================================================
D="$EXOS_FUORI/cross_build/terze-parti/publicsuffix"
F="$D/public_suffix_list.dat"
URL="https://publicsuffix.org/list/public_suffix_list.dat"
mkdir -p "$D"

if [ -f "$F" ] && [ "$1" != "-nuova" ]; then
    if [ -f "$F.sha256" ] && ! (cd "$D" && sha256sum -c --quiet public_suffix_list.dat.sha256); then
        echo "! $F non corrisponde a $F.sha256: copia rovinata." >&2
        echo "  Toglila e rilancia, o: bash tools/psl-scarica.sh -nuova" >&2
        exit 1
    fi
    exit 0
fi

T="$F.scarico"
echo "=== Public Suffix List da $URL ==="
curl -fsSL --retry 3 -o "$T" "$URL" || { rm -f "$T"; echo "! non riesco a scaricare $URL" >&2; exit 1; }
VER=$(sed -n 's|^// VERSION: ||p' "$T" | head -1)
if [ -z "$VER" ] || ! grep -q '===BEGIN ICANN DOMAINS===' "$T" || ! grep -q '===BEGIN PRIVATE DOMAINS===' "$T"; then
    rm -f "$T"; echo "! quello scaricato non e' la Public Suffix List" >&2; exit 1
fi
mv "$T" "$F"
(cd "$D" && sha256sum public_suffix_list.dat > public_suffix_list.dat.sha256)
echo "Prelevato il $(date +%F) da $URL (MPL-2.0) per i cookie di EXBrowser, versione $VER." > "$D/PROVENIENZA.txt"
echo "[OK] $F (versione $VER)"
