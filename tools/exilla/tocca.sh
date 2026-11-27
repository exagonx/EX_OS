#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/tocca.sh — prima di cambiare un file di firefox-main, se ne
# tiene l'originale (@EXILLA-PATCH, 30 settembre 2026)
#
#     tools/exilla/tocca.sh firefox-main/percorso/file [...]
#
# ! L'ALBERO DI FIREFOX NON HA UN .git (e' fuori dalla cronologia, 4,9 GB), quindi
# le nostre modifiche non le ricorda nessuno. Questo script copia l'originale,
# la prima volta e solo quella, in cross_build/<macchina>/firefox-originali/;
# tools/exilla/patch-crea.sh ne ricava le patch di tools/exilla/patch/, che
# sono cio' che sta nel repository e cio' che la MPL chiede di pubblicare.
# Un file NUOVO si segna lo stesso: l'originale e' «non c'era».
# (27/11/2026) Anche thunderbird-main/...: gli originali in
# cross_build/<macchina>/thunderbird-originali (voce 103).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
for f in "$@"; do
    f="${f#./}"
    case "$f" in
        firefox-main/*)     ORIG="${EXOS_FUORI}/cross_build/$MACCHINA/firefox-originali" ;;
        thunderbird-main/*) ORIG="${EXOS_FUORI}/cross_build/$MACCHINA/thunderbird-originali" ;;
        *) echo "tocca: $f non e' in firefox-main/ ne' in thunderbird-main/" >&2; exit 1 ;;
    esac
    r="${f#*-main/}"
    [ -e "$ORIG/$r" ] || [ -e "$ORIG/$r.nuovo" ] && continue
    mkdir -p "$(dirname "$ORIG/$r")"
    if [ -f "$f" ]; then cp -p "$f" "$ORIG/$r"; else : > "$ORIG/$r.nuovo"; fi
done
