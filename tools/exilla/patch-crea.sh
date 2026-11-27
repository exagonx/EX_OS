#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/patch-crea.sh — le patch di Exilla, da originali e file di adesso
#
#     tools/exilla/patch-crea.sh
#     tools/exilla/patch-crea.sh thunderbird   thunderbird-main ->
#                                              tools/thunderbird-exos/patch
#
# Per ogni file segnato da tools/exilla/tocca.sh scrive in tools/exilla/patch/
# una patch unificata (percorsi relativi a firefox-main, livello -p1), con il
# nome del file e «/» diventati «_». Le patch vecchie si rifanno tutte: sono un
# prodotto. Per rimetterle su un albero pulito:
#
#     cd firefox-main && for p in ../tools/exilla/patch/*.patch; do patch -p1 < "$p"; done
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
# ! THUNDERBIRD'S SET IS WHOLE: every change against its own tarball, the
# ones taken from Exilla too (its base is another Gecko version)
if [ "${1:-}" = thunderbird ]; then
    A=thunderbird-main; ORIG="${EXOS_FUORI}/cross_build/$MACCHINA/thunderbird-originali"
    P="$PWD/tools/thunderbird-exos/patch"
else
    A=firefox-main; ORIG="${EXOS_FUORI}/cross_build/$MACCHINA/firefox-originali"
    P="$PWD/tools/exilla/patch"
fi
mkdir -p "$P"; rm -f "$P"/*.patch
[ -d "$ORIG" ] || { echo "nessun file toccato"; exit 0; }
n=0
( cd "$ORIG" && find . -type f | sed 's#^\./##' ) | sort | while read -r o; do
    r="${o%.nuovo}"
    nome=$(echo "$r" | tr '/' '_').patch
    if [ "$o" != "$r" ]; then
        diff -u --label "a/$r" --label "b/$r" /dev/null "$A/$r" > "$P/$nome"
    else
        diff -u --label "a/$r" --label "b/$r" "$ORIG/$r" "$A/$r" > "$P/$nome"
    fi
    [ -s "$P/$nome" ] || rm -f "$P/$nome"
done
echo "patch: $(ls "$P"/*.patch 2>/dev/null | wc -l) in ${P#$PWD/}/"
